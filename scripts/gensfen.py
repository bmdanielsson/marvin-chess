#!/usr/bin/env python3
import sys
import random
import argparse
import random
import os
import shutil
import time
import chess
import chess.engine

import numpy as np

from multiprocessing import Process, Lock, Value
from datetime import datetime

BATCH_SIZE = 1000
PROGRESS_INTERVAL = 10
MAX_TIME = 60

MAX_PLY = 400
MIN_DRAW_PLY = 80
DRAW_SCORE = 10
DRAW_COUNT = 10
RESIGN_SCORE = 10000
RESIGN_COUNT = 10

HUFFMAN_TABLE = [np.uint8(0),    # No piece (1 bit)
                 np.uint8(1),    # Pawn (4 bits)
                 np.uint8(3),    # Knight (4 bits)
                 np.uint8(5),    # Bishop (4 bits)
                 np.uint8(7),    # Rook (4 bits)
                 np.uint8(9)]    # Queen (4 bits)


# Encode a single-bit value according to Stockfish bitstream format
def encode_bit(data, pos, value):
    if np.uint16(value) != np.uint16(0):
        data[int(pos/8)] = data[int(pos/8)] | (1 << (pos & np.uint16(7)))

    return pos + 1


# Encode a multi-bit value according to Stockfish bitstream format
def encode_bits(data, pos, value, nbits):
    for i in range(0, nbits):
        pos = encode_bit(data, pos, np.uint16(value & np.uint16(1 << i)))

    return pos


def encode_piece_at(data, pos, board, sq):
    piece_type = board.piece_type_at(sq)
    piece_color = board.color_at(sq)

    if piece_type == None:
        pos = encode_bits(data, pos, np.uint16(HUFFMAN_TABLE[0]), 1)
        return pos

    pos = encode_bits(data, pos, np.uint16(HUFFMAN_TABLE[piece_type]), 4)
    pos = encode_bit(data, pos, np.uint16(not piece_color))

    return pos


# Side to move (White = 0, Black = 1) (1bit)
# White King Position (6 bits)
# Black King Position (6 bits)
# Huffman Encoding of the board
# Castling availability (1 bit x 4)
# En passant square (1 or 1 + 6 bits)
# 50-move counter (6 bits)
# Full move counter (8 bits)
# Full move counter high bits (8 bits)
# 50-move counter high bit (1 bit)
def encode_position(board):
    data = np.zeros(32, dtype='uint8')
    pos = np.uint16(0)

    # Encode side to move
    pos = encode_bit(data, pos, np.uint16(not board.turn))

    # Encode king positions
    pos = encode_bits(data, pos, np.uint16(board.king(chess.WHITE)), 6)
    pos = encode_bits(data, pos, np.uint16(board.king(chess.BLACK)), 6)

    # Encode piece positions
    for r in reversed(range(0, 8)):
        for f in range(0, 8):
            sq = r*8 + f
            pc = board.piece_at(sq)
            if pc:
                if pc.piece_type == chess.KING or pc.piece_type == chess.KING:
                    continue
            pos = encode_piece_at(data, pos, board, sq)

    # Encode castling availability
    pos = encode_bit(data, pos,
                    np.uint16(board.has_kingside_castling_rights(chess.WHITE)))
    pos = encode_bit(data, pos,
                    np.uint16(board.has_queenside_castling_rights(chess.WHITE)))
    pos = encode_bit(data, pos,
                    np.uint16(board.has_kingside_castling_rights(chess.BLACK)))
    pos = encode_bit(data, pos,
                    np.uint16(board.has_queenside_castling_rights(chess.BLACK)))

    # Encode en-passant square
    if not board.ep_square:
        pos = encode_bit(data, pos, np.uint16(0))
    else:
        pos = encode_bit(data, pos, np.uint16(1))
        pos = encode_bits(data, pos, np.uint16(board.ep_square), 6)

    # Encode 50-move counter. Use 7 bits for counter unless Stockfish
    # compatibility is requested. In that case the last bit is stored
    # at the end instead in order to be backwards compoatible with
    # older parsers.
    pos = encode_bits(data, pos, np.uint16(board.halfmove_clock), 6)

    # Encode move counter
    pos = encode_bits(data, pos, np.uint16(board.fullmove_number), 8)

    # Backwards compatible fix for 50-move counter only being stored
    # with 6 bits. It's done this way to keep compatibility with
    # Stockfish trainer.
    high_bit = (board.halfmove_clock >> 6) & 1
    pos = encode_bits(data, pos, np.uint16(board.fullmove_number>>8), 8)
    pos = encode_bit(data, pos, np.uint16(high_bit))

    return data


# bit  0- 5: destination square (from 0 to 63)
# bit  6-11: origin square (from 0 to 63)
# bit 12-13: promotion piece type - 2 (from KNIGHT-2 to QUEEN-2)
# bit 14-15: special move flag: promotion (1), en passant (2), castling (3)
def encode_move(board, move):
    data = np.uint16(0)

    to_sq = move.to_square
    from_sq = move.from_square
    if board.turn == chess.WHITE and board.is_kingside_castling(move):
        to_sq = 7
    elif board.turn == chess.WHITE and board.is_queenside_castling(move):
        to_sq = 0
    elif board.turn == chess.BLACK and board.is_kingside_castling(move):
        to_sq = 63
    elif board.turn == chess.BLACK and board.is_queenside_castling(move):
        to_sq = 56

    data = data | np.uint16(to_sq)
    data = data | np.uint16(from_sq << 6)
    if move.promotion:
        data = data | np.uint16((move.promotion-2) << 12)
        data = data | np.uint16(1 << 14)
    if board.is_en_passant(move):
        data = data | np.uint16(2 << 14)
    elif board.is_castling(move):
        data = data | np.uint16(3 << 14)

    return data


# position (256 bits)
# score (16 bits)
# move (16 bits)
# ply (16 bits)
# result (8 bits)
# padding (8 bits)
def write_sfen_bin(fh, sfen, result):
    board = chess.Board(fen=sfen['fen'])
    pov_result = result
    if sfen['score'].turn == chess.BLACK:
        pov_result = -1*pov_result
    pov_score = sfen['score'].pov(sfen['score'].turn).score()

    pos_data = encode_position(board)
    pos_data.tofile(fh)
    np.int16(pov_score).tofile(fh)
    move_data = encode_move(board, sfen['move'])
    move_data.tofile(fh)
    np.uint16(sfen['ply']).tofile(fh)
    np.int8(pov_result).tofile(fh)
    np.uint8(0xFF).tofile(fh)


def write_sfen_plain(fh, sfen, result):
    pov_result = result
    if sfen['score'].turn == chess.BLACK:
        pov_result = -1*pov_result
    pov_score = sfen['score'].pov(sfen['score'].turn).score()

    fh.write('fen ' + sfen['fen'] + '\n')
    fh.write('move ' + sfen['move'].uci() + '\n')
    fh.write('score ' + str(pov_score) + '\n')
    fh.write('ply ' + str(sfen['ply']) + '\n')
    fh.write('result ' + str(pov_result) + '\n')
    fh.write('e\n')


def play_random_moves(board, nmoves):
    for k in range(0, nmoves):
        legal_moves = [move for move in board.legal_moves]
        if len(legal_moves) > 1:
            idx = random.randint(0, len(legal_moves)-1)
        else:
            idx = 0
        move = legal_moves[idx]
        board.push(move)
        if board.is_game_over(claim_draw=True):
            break


def setup_board(args):
    if args.use_frc and random.random() < args.frc_prob:
        frc_board = chess.Board.from_chess960_pos(random.randint(0, 959))
        frc_board.set_castling_fen('-')
        board = chess.Board(fen=frc_board.fen())
    else:
        board = chess.Board()

    return board


def is_quiet(board, move):
    if board.is_check():
        return False
    return not board.is_capture(move)


def play_game(fh, pos_left, args):
    # Setup a new board
    board = setup_board(args)

    # Play a random opening
    play_random_moves(board, args.random_plies)
    if board.is_game_over(claim_draw=True):
        return pos_left

    # Start engine
    engine = chess.engine.SimpleEngine.popen_uci(args.engine)
    options = {}
    options['Hash'] = args.hash
    options['Threads'] = 1
    if 'UseNNUE' in engine.options:
        options['UseNNUE'] = args.use_nnue
    if args.syzygy_path and 'SyzygyPath' in engine.options:
        options['SyzygyPath'] = args.syzygy_path
    engine.configure(options)

    # Setup search limits
    limit = chess.engine.Limit(time=MAX_TIME)
    if args.depth:
        limit.depth = args.depth
    if args.nodes:
        limit.nodes = args.nodes

    # Let the engine play against itself and a record all positions
    resign_count = 0
    draw_count = 0 
    count = 0
    positions = []
    result_val = 0
    while not board.is_game_over(claim_draw=True):
        # Search the position to the required depth
        result = engine.play(board, limit, info=chess.engine.Info.SCORE)

        # If no score was received then skip this move
        if 'score' not in result.info:
            board.push(result.move)
            continue

        # Skip non-quiet moves
        if not is_quiet(board, result.move):
            board.push(result.move)
            continue

        # Check eval limit
        if abs(result.info['score'].relative.score()) > args.eval_limit:
            if result.info['score'].white().score() > 0:
                result_val = 1
            else:
                result_val = -1
            break

        # Extract and store information
        if board.turn == chess.WHITE:
            ply = board.fullmove_number*2
        else: 
            ply = board.fullmove_number*2 + 1
        sfen = {'fen':board.fen(), 'move':result.move,
                'score':result.info['score'], 'ply':ply}
        positions.append(sfen)

        # Check ply limit
        if ply > MAX_PLY:
            result_val = 0
            break

        # Check resign condition
        if abs(result.info['score'].relative.score()) >= RESIGN_SCORE:
            resign_count += 1;
        else:
            resign_count = 0
        if resign_count >= RESIGN_COUNT:
            if result.info['score'].relative.score() > 0:
                result_val = 1
            else:
                result_val = -1
            break;

        # Check draw adjudication
        if ply > MIN_DRAW_PLY:
            if abs(result.info['score'].relative.score()) <= DRAW_SCORE:
                draw_count += 1;
            else:
                draw_count = 0
            if draw_count >= DRAW_COUNT:
                result_val = 0
                break;

        # Apply move
        board.push(result.move)

    engine.quit()

    # Convert result to sfen representation
    if board.is_game_over(claim_draw=True):
        result_str = board.result(claim_draw=True)
        if result_str == '1-0':
            result_val = 1
        elif result_str == '0-1':
            result_val = -1
        elif result_str == '1/2-1/2':
            result_val = 0

    # Write positions to file
    for sfen in positions:
        if args.format == 'plain':
            write_sfen_plain(fh, sfen, result_val)
        else:
            write_sfen_bin(fh, sfen, result_val)
        pos_left = pos_left - 1
        if pos_left == 0:
            break
    fh.flush()

    return pos_left


def request_work(finished, remaining_work, finished_work, position_lock):
    position_lock.acquire()
    finished_work.value = finished_work.value + finished
    if remaining_work.value == 0:
        npos = 0
    elif remaining_work.value < BATCH_SIZE:
        npos = remaining_work.value
    else:
        npos = BATCH_SIZE
    remaining_work.value = remaining_work.value - npos
    position_lock.release()
    return npos


def process_func(pid, training_file, remaining_work, finished_work,
                position_lock, args):
    # Set seed for random number generation
    if (args.seed):
        random.seed(a=args.seed+pid*10)
    else:
        random.seed()

    # Open output file
    if args.format == 'plain':
        fh = open(training_file, 'w')
    else:
        fh = open(training_file, 'wb')

    # Keep generating positions until the requested number is reached
    work_todo = 0
    while True:
        work_todo = request_work(work_todo, remaining_work, finished_work,
                                position_lock)
        if work_todo == 0:
            break
        pos_left = work_todo
        while pos_left > 0:
            pos_left = play_game(fh, pos_left, args)

    fh.close()


def main(args):
    before = datetime.now();

    # Initialize
    remaining_work = Value('i', args.npositions)
    finished_work = Value('i', 0)
    position_lock = Lock()

    # Start generating data with all threads
    training_files = []
    processes = []
    parts = os.path.splitext(args.output)
    for pid in range(0, args.nthreads):
        training_file = parts[0] + '_' + str(pid) + parts[1]
        training_files.append(training_file)

        process_args = (pid, training_file, remaining_work, finished_work,
                        position_lock, args)
        processes.append(Process(target=process_func, args=process_args))
        processes[pid].start()

    # Handle progress output
    while True:
        time.sleep(PROGRESS_INTERVAL)

        position_lock.acquire()
        pos_generated = finished_work.value
        position_lock.release()
        print(f'\r{pos_generated}/{args.npositions}', end='')

        if pos_generated == args.npositions:
            break;
    print('\n');

    # Wait for all threads to exit
    for p in processes:
        p.join()

    # Merge data from all threads into one file
    if args.format == 'plain':
        with open(args.output, 'w') as wfd:
            for f in training_files:
                with open(f, 'r') as fd:
                    shutil.copyfileobj(fd, wfd)
    else:
        with open(args.output, 'wb') as wfd:
            for f in training_files:
                with open(f, 'rb') as fd:
                    shutil.copyfileobj(fd, wfd)
    for f in training_files:
        os.remove(f)

    after = datetime.now();
    print(f'Time: {after-before}')


if __name__ == "__main__":
    # Parse command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', '--depth', type=int,
                    help='the depth to search each position to')
    parser.add_argument('--nodes', type=int,
                    help='the number of nodes to search')
    parser.add_argument('-e', '--engine', help='the path to the engine',
                    required=True)
    parser.add_argument('-t', '--nthreads', type=int, default='1',
                    help='the number of threads to use')
    parser.add_argument('-n', '--npositions', type=int, default='30000000',
                    help='the number of positions to generate')
    parser.add_argument('-o', '--output', type=str,
                    help='the name of the output file', required=True)
    parser.add_argument('-r', '--random_plies', type=int, default='10',
                    help='the number of random plies at the beginning')
    parser.add_argument('-l', '--eval_limit', type=int, default='19000',
                    help='the highest evaluation that is accepted')
    parser.add_argument('-a', '--hash', type=int, default='128',
                    help='the amount of hash the engine should use (in MB)')
    parser.add_argument('-s', '--syzygy_path', type=str,
                    help='the path to syzygy tablebases')
    parser.add_argument('--use_nnue', action='store_true',
                    help='flag indicating if NNUE evaluation should be used')
    parser.add_argument('--format', choices=['plain', 'bin'], default='bin',
                    help='the output format')
    parser.add_argument('--seed', type=int,
                    help='seed to use for random number generator')
    parser.add_argument('--use_frc', action='store_true',
                    help="Include Chess960 starting positions")
    parser.add_argument('--frc_prob', type=float, default=0.2,
                    help="Probability of using a Chess960 starting position")

    args = parser.parse_args()

    print(f'Engine: {args.engine}')
    print(f'Output format: {args.format}')
    print(f'Number of positions: {args.npositions}')
    print(f'Depth: {args.depth}')
    print(f'Number of random plies: {args.random_plies}')
    print(f'Eval limit: {args.eval_limit}')
    print(f'Hash: {args.hash} MB')
    print(f'Use NNUE: {args.use_nnue}')
    print(f'Syzygy path: {args.syzygy_path}')
    print('')

    main(args)
