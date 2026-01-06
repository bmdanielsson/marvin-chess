#!/usr/bin/env python3
import sys
import random
import argparse
import random
import os
import shutil
import time
import subprocess

from multiprocessing import Process, Lock, Value
from datetime import datetime

BATCH_SIZE = 1000
PROGRESS_INTERVAL = 10
MAX_INT_32 = 2147483647

def generate_batch(outputfile, size, args):
    # Generate a random seed
    seed = random.randint(0, MAX_INT_32)

    # Setup command
    command = []

    command.append(args.engine)
    command.append('--selfplay')

    command.append('--output')
    command.append(outputfile)

    command.append('--depth')
    command.append(str(args.depth))

    command.append('--npositions')
    command.append(str(size))

    command.append('--seed')
    command.append(str(seed))

    command.append('--format')
    command.append(args.format)

    if args.frc_prob:
        command.append('--frc-prob')
        command.append(str(args.frc_prob))

    # Execute command
    subprocess.call(command)


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

    # Keep generating positions until the requested number is reached
    work_todo = 0
    while True:
        work_todo = request_work(work_todo, remaining_work, finished_work,
                                position_lock)
        if work_todo == 0:
            break

        generate_batch(training_file, work_todo, args)


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
        progress = (pos_generated/args.npositions)*100
        print(f'\r{pos_generated}/{args.npositions} ({int(progress)}%)', end='')

        if pos_generated == args.npositions:
            break;
    print('\n');

    # Wait for all threads to exit
    for p in processes:
        p.join()

    # Merge data from all threads into one file
    with open(args.output, 'ab') as wfd:
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
    parser.add_argument('-e', '--engine', type=str, required=True,
            help='the path to the engine')
    parser.add_argument('-d', '--depth', type=int, default=8,
            help='the depth to search each position to')
    parser.add_argument('-t', '--nthreads', type=int, default='1',
            help='the number of threads to use (default 1)')
    parser.add_argument('-n', '--npositions', type=int, default='100000000',
            help='the number of positions to generate (default 100000000)')
    parser.add_argument('-o', '--output', type=str,
            help='the name of the output file', required=True)
    parser.add_argument('--seed', type=int,
            help='seed to use for random number generator')
    parser.add_argument('--frc-prob', type=float,
            help="Probability of using a FRC starting position (default 0.0)")
    parser.add_argument('--format', type=str, default="sfen",
            help="Output format to use, sfen (default) or bullet")

    args = parser.parse_args()

    print(f'Number of positions: {args.npositions}')
    print(f'Depth: {args.depth}')
    print(f'FRC probabliity: {args.frc_prob}')
    print(f'Format: {args.format}')
    print('')

    main(args)
