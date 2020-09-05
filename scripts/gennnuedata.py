#!/usr/bin/env python3
import sys
import random
import argparse
import random
import os
import shutil
import chess
import chess.engine

from multiprocessing import Process
from datetime import datetime

MAX_PLY = 400
MIN_DRAW_PLY = 80
DRAW_SCORE = 0
DRAW_COUNT = 8

def write_position(fh, sfen, result):
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
        if board.is_game_over():
            break


def play_game(fh, pos_left, args):
    # Setup a new board
    board = chess.Board()

    # Play a random opening
    play_random_moves(board, args.random_plies)
    if board.is_game_over():
        return pos_left

    # Start engine
    options = {}
    options['Hash'] = args.hash
    options['Threads'] = 1
    if args.syzygy_path:
        options['SyzygyPath'] = args.syzygy_path
    if args.eval_file:
        options['EvalFile'] = args.eval_file
    engine = chess.engine.SimpleEngine.popen_uci(args.engine)
    engine.configure(options)

    # Let the engine play against itself and a record all positions
    draw_count = 0 
    count = 0
    positions = []
    result_val = 0
    while not board.is_game_over():
        # Search the position to the required depth
        info = engine.analyse(board, chess.engine.Limit(depth=args.depth))

        # Check eval limit
        if abs(info['score'].relative.score()) > args.eval_limit:
            if info['score'].white().score() > 0:
                result_val = 1
            else:
                result_val = -1
            break

        # Extract and store information
        move = info['pv'][0]
        score = info['score']
        fen_str = board.fen()
        if board.turn == chess.WHITE:
            ply = board.fullmove_number*2
        else: 
            ply = board.fullmove_number*2 + 1
        sfen = {'fen':fen_str, 'move':move, 'score':score, 'ply':ply}
        positions.append(sfen)

        # Check ply limit
        if ply > MAX_PLY:
            result_val = 0
            break

        # Check draw adjudication
        if ply > MIN_DRAW_PLY:
            if abs(score.relative.score()) <= DRAW_SCORE:
                draw_count = draw_count + 1;
            else:
                draw_count = 0
            if draw_count >= DRAW_COUNT:
                result_val = 0
                break;

        # Apply move        
        board.push(info['pv'][0])

    engine.quit()

    if board.is_game_over():
        result = board.result(claim_draw=True)
        if result == '1-0':
            result_val = 1
        elif result == '0-1':
            result_val = -1
        elif result == '1/2-1/2':
            result_val = 0

    for sfen in positions:
        write_position(fh, sfen, result_val)
        pos_left = pos_left - 1
        if pos_left == 0:
            break

    return pos_left


def process_func(pid, npos, training_file, args):
    pos_left = npos

    # Generate positions
    fh = open(training_file, 'w')
    while pos_left > 0:
        pos_left = play_game(fh, pos_left, args)
    fh.close()


def main(args):
    before = datetime.now();

    parts = os.path.splitext(args.output)
    pos_left = args.npositions
    processes = []
    training_files = []
    validation_files = []
    for pid in range(0, args.nthreads):
        training_file = parts[0] + '_' + str(pid) + parts[1]
        training_files.append(training_file)

        if pid != (args.nthreads-1):
            npos = int(args.npositions/args.nthreads)
            pos_left = pos_left - npos
        else:
            npos = pos_left
            pos_left = 0
        process_args = (pid, npos, training_file, args)
        processes.append(Process(target=process_func, args=process_args))
        processes[pid].start()

    for p in processes:
        p.join()

    with open(args.output,'w') as wfd:
        for f in training_files:
            with open(f,'r') as fd:
                shutil.copyfileobj(fd, wfd)
    for f in training_files:
        os.remove(f)

    after = datetime.now();
    print(f'Time: {after-before}')


if __name__ == "__main__":
    # Parse command line arguments
    parser = argparse.ArgumentParser()
    parser.add_argument('-d', '--depth', type=int, default='8',
                    help='the depth to search each position to')
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
    parser.add_argument('-l', '--eval_limit', type=int, default='20000',
                    help='the highest evaluation that is accepted')
    parser.add_argument('-a', '--hash', type=int, default='128',
                    help='the amount of hash the engine should use (in MB)')
    parser.add_argument('-s', '--syzygy_path', type=str,
                    help='the path to syzygy tablebases')
    parser.add_argument('-f', '--eval_file', type=str,
                    help='the path to the NNUE eval file')

    args = parser.parse_args()

    print(f'Engine: {args.engine}')
    print(f'Number of positions: {args.npositions}')
    print(f'Depth: {args.depth}')
    print(f'Number of random plies: {args.random_plies}')
    print(f'Eval limit: {args.eval_limit}')
    print(f'Hash: {args.hash} MB')
    print(f'Syzygy path: {args.syzygy_path}')
    print(f'NNUE eval file path: {args.eval_file}')
    print('')

    main(args)
