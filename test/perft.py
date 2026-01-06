#!/usr/bin/env python3
import sys
import random
import chess
import chess.pgn
import subprocess
import re

num_tests = 0
num_test_passed = 0
num_test_failed = 0

def send_command(proc, cmd):
    proc.stdin.write(cmd.encode('utf-8'))

def recv_command(proc):
    return proc.stdout.readline().decode('utf-8')

def recv_until(proc, stopcmd):
    while True:
        msg = recv_command(proc)
        if msg.strip() == stopcmd:
            break

def run_perft(process, fenstr, depth, expected):
    global num_tests
    global num_test_passed
    global num_test_failed

    send_command(process, 'position fen %s\n' % fenstr)
    send_command(process, 'perft %d\n' % depth)
    resp = recv_command(process)
    m = re.search('Nodes: ([0-9]+)', resp)
    nodes = int(m.group(1));
    if nodes == expected:
        num_test_passed += 1
        print('Depth %d: %d (%d) => PASS' % (depth, nodes, expected))
    else:
        num_test_failed += 1
        print('Depth %d: %d (%d) => FAIL' % (depth, nodes, expected))
    num_tests += 1

def run_test(process, epdstr):
    board = chess.Board(chess960=True)
    print(epdstr)
    board.set_epd(epdstr)
    parts = epdstr.split(';')
    for op in parts:
        m = re.search('D([0-9]+) ([0-9]+)', op)
        if m and int(m.group(1)) <= 4:
            run_perft(process, board.fen(), int(m.group(1)), int(m.group(2)))

# Make sure all required command line arguments are specified
if len(sys.argv) != 3:
    print('Missing arguments')
    print('perft.py <path to engine> <testsuite>')
    sys.exit(0)

# Start and initialize engine
process = subprocess.Popen(sys.argv[1], stdout=subprocess.PIPE,
                            stdin=subprocess.PIPE, bufsize=0)
send_command(process, 'uci\n')
recv_until(process, 'uciok')
send_command(process, 'setoption name UCI_Chess960 value true\n')

# Iterate over all EPD positions
npass = 0
nfail = 0
epdfile = open(sys.argv[2], 'r')
for line in epdfile:
    line = line.strip()
    run_test(process, line)

print()
print('Total number of tests: %d' % num_tests)
print('#Pass: %d' % num_test_passed)
print('#Fail: %d' % num_test_failed)

# Clean up
epdfile.close()
send_command(process, 'quit\n')
