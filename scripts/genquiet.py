#!/usr/bin/env python3
import sys
import random
import chess
import chess.pgn
import subprocess

num_converted = 0

def send_command(proc, cmd):
    proc.stdin.write(cmd.encode('utf-8'))

def recv_command(proc):
    return proc.stdout.readline().decode('utf-8')

def recv_until(proc, stopcmd):
    while True:
        msg = recv_command(proc)
        if msg.strip() == stopcmd:
            break

def epd2fen(epd):
    parts = epd.split()
    fen = parts[0] + ' ' + parts[1] + ' ' + parts[2] + ' ' + parts[3] + ' 0 1'
    return fen

def convert_epd_position(process, epdstr):
    global num_different

    # Get a board position from the EPD string
    board = chess.Board()
    epdops = board.set_epd(epdstr)

    # Perform a quiescence search to find a sequence
    # of moves that leads to a quiet position.
    send_command(process, 'position fen %s\n' % board.fen())
    send_command(process, 'quiet\n')
    msg = recv_command(process)

    # If there is a PV then apply it to the board position
    pv = []
    msgparts = msg.split()
    if len(msgparts) > 1:
        for idx in range(1, len(msgparts)):
            board.push(chess.Move.from_uci(msgparts[idx]))

    # Get an EPD string from the board
    quietepd = board.epd() 

    # Append any operations from the original EPD string
    epdparts = epdstr.split()
    if len(epdparts) > 4:
        idx = 4
        while idx < len(epdparts):
            quietepd = quietepd + ' ' + epdparts[idx]
            idx = idx + 1

    return quietepd

# Make sure all required command line arguments are specified
if len(sys.argv) != 4:
    print('Missing arguments')
    print('genquiet.py <path to marvin> <input> <output>')
    sys.exit(0)

# Start and initialize engine
process = subprocess.Popen(sys.argv[1], stdout=subprocess.PIPE,
                            stdin=subprocess.PIPE, bufsize=0)
send_command(process, 'uci\n')
recv_until(process, 'uciok')

# Open output EPD file
outputepdfile = open(sys.argv[3], 'w')

# Iterate over all EPD positions
epdfile = open(sys.argv[2], 'r')
for line in epdfile:
    line = line.strip()
    quietepd = convert_epd_position(process, line)
    num_converted += 1
    outputepdfile.write('%s\n' % quietepd)
    if num_converted%1000 == 0:
        print('\r%d' % num_converted, end='')
print('\r%d' % num_converted)

# Clean up
epdfile.close()
outputepdfile.close()

