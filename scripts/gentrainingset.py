#!/usr/bin/env python3
import sys
import random
import argparse
import chess
import chess.pgn
import chess.pgn
import subprocess

# Number of plies to skip at the beginning of the game
EARLY_SKIP = 4

# Number of plies to skip at the end of the game
LATE_SKIP = 10

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

def select_positions(epdlist, samples_to_get):
    random.seed()
    nsamples = 0
    randlist = []
    while nsamples < samples_to_get:
        npos = len(epdlist)
        if npos > 0:
            idx = random.randint(0, npos-1)
            posstr = epdlist[idx]
            epdlist.remove(posstr)
            randlist.append(posstr)
            nsamples += 1
        else:
            break

    return randlist

def count_pieces(board):
    npieces = 0
    for sq in range(0, 64):
        if board.piece_at(sq):
            npieces += 1
    return npieces

def count_moves(game):
    movecount = 0
    node = game
    while len(node.variations) > 0:
        node = node.variations[0]
        movecount += 1
    return movecount

def iterate_game(game):
    # Skip games shorter than 40 plies
    movecount = count_moves(game)
    if movecount < 40:
        return None

    # Iterate over all moves
    plycount = 0
    node = game
    epdlist = []
    while len(node.variations) > 0:
        # Get the next game node
        node = node.variations[0]
        plycount = plycount + 1

        # Skip the first few plies since these are normally
        # played from opening book so the exact evaluation is
        # not that interesting.
        if plycount < EARLY_SKIP:
            continue

        # Skip the last few plies of the game since the game
        # are likely to be decided already.
        if plycount > (movecount-LATE_SKIP):
            break

        # Skip positions with less than 5 pieces since these
        # are normally handled by tablesbases
        if count_pieces(node.board()) <= 5:
            break

        # Create an EPD string for this position
        epdstr = node.board().epd(c9=game.headers['Result'])

        # Add position to list
        epdlist.append(epdstr)

    return epdlist

# Parse command line arguments
parser = argparse.ArgumentParser()
parser.add_argument('-s', '--samples', type=int, default='20',
                    help='the number of samples to get from each game')
parser.add_argument('-n', '--npositions', type=int, default='1000000',
                    help='the number of positions to generate')
parser.add_argument('-m', '--marvin', default='./marvin',
                    help='the path to Marvin')
parser.add_argument('pgn', type=argparse.FileType('r'),
                    help='the input pgn file')
parser.add_argument('epd', type=argparse.FileType('w'),
                    help='the output epd file')
args = parser.parse_args()

# Start and initialize engine
process = subprocess.Popen(args.marvin, stdout=subprocess.PIPE,
                            stdin=subprocess.PIPE, bufsize=0)
send_command(process, 'uci\n')
recv_until(process, 'uciok')

# Iterate over all games
game = chess.pgn.read_game(args.pgn)
npositions = 0
while game:
    # Get the game result
    result = game.headers['Result']

    # Skip games without a proper result
    if result != '1-0' and result != '1/2-1/2' and result != '0-1':
        game = chess.pgn.read_game(args.pgn)
        continue

    # Iterate over the game
    epdlist = iterate_game(game)

    # Select a set of position to include
    randlist = []
    if epdlist:
        samples_to_get = args.samples
        if samples_to_get > (args.npositions-npositions):
            samples_to_get = args.npositions - npositions
        randlist = select_positions(epdlist, samples_to_get)
    for epdstr in randlist:
        epdstr = convert_epd_position(process, epdstr)
        args.epd.write(epdstr)
        args.epd.write('\n')
    npositions += len(randlist)
    if npositions >= args.npositions:
        break

    # Display progress
    str = "\r" + repr(npositions)
    print(str, end="")

    # Next game
    game = chess.pgn.read_game(args.pgn)

print("")

# Close Marvin
send_command(process, 'quit\n')

# Close files
args.pgn.close()
args.epd.close()
