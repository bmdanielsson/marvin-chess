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

from multiprocessing import Process, Lock, Value
from datetime import datetime

BATCH_SIZE = 1000
PROGRESS_INTERVAL = 10
MAX_TIME = 60

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
        if board.is_game_over(claim_draw=True):
            break


def play_game(fh, pos_left, args):
    # Setup a new board
    board = chess.Board()

    # Play a random opening
    play_random_moves(board, args.random_plies)
    if board.is_game_over(claim_draw=True):
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
    while not board.is_game_over(claim_draw=True):
        # Search the position to the required depth
        result = engine.play(board,
                            chess.engine.Limit(depth=args.depth, time=MAX_TIME),
                            info=chess.engine.Info.SCORE)

        # If no score was received then skip this move
        if 'score' not in result.info:
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

        # Check draw adjudication
        if ply > MIN_DRAW_PLY:
            if abs(result.info['score'].relative.score()) <= DRAW_SCORE:
                draw_count = draw_count + 1;
            else:
                draw_count = 0
            if draw_count >= DRAW_COUNT:
                result_val = 0
                break;

        # Apply move
        board.push(result.move)

    engine.quit()

    if board.is_game_over(claim_draw=True):
        result_str = board.result(claim_draw=True)
        if result_str == '1-0':
            result_val = 1
        elif result_str == '0-1':
            result_val = -1
        elif result_str == '1/2-1/2':
            result_val = 0

    for sfen in positions:
        write_position(fh, sfen, result_val)
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
    fh = open(training_file, 'w')

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

    remaining_work = Value('i', args.npositions)
    finished_work = Value('i', 0)
    position_lock = Lock()

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

    while True:
        time.sleep(PROGRESS_INTERVAL)

        position_lock.acquire()
        pos_generated = finished_work.value
        position_lock.release()
        print(f'\r{pos_generated}/{args.npositions}', end='')

        if pos_generated == args.npositions:
            break;
    print('\n');

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
