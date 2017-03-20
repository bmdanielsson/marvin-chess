#!/usr/bin/env python3
import sys
import random
import chess
import chess.pgn

position_set = set()

def select_positions(epdlist):
    random.seed()
    randlist = []
    for k in range(20):
        npos = len(epdlist)
        if npos > 0:
            idx = random.randint(0, npos-1)
            posstr = epdlist[idx]
            epdlist.remove(posstr)
            randlist.append(posstr)

    return randlist

def iterate_game(game):
    plycount = 0
    node = game
    epdlist = []
    while len(node.variations) > 0:
        # Get the next game node
        node = node.variations[0]
        plycount = plycount + 1

        # Skip the first 10 moves (20 plies)
        if plycount < 20:
            continue

        # Skip book moves
        if node.comment.find('book') != -1:
            continue

        # Skip the rest of the game if a forced mate is found
        if node.comment.find('M') != -1:
            break

        # Create an EPD string for this position
        epdstr = node.board().epd(result=game.headers['Result'])

        # Skip duplicates
        if epdstr in position_set:
            continue
        position_set.add(epdstr)

        # Add position to list
        epdlist.append(epdstr)

    return epdlist

if len(sys.argv) != 3:
    print('Missing arguments')
    print('gentrainingset.py <input> <output>')
    sys.exit(0)

# Iterate over all games
gamefile = sys.argv[1]
outputfile = sys.argv[2]
pgn = open(gamefile)
output = open(outputfile, "w")
game = chess.pgn.read_game(pgn)
count = 0
while game:
    count = count + 1
    str = "\r" + repr(count)
    print(str, end="")

    # Get the game result
    result = game.headers['Result']

    # Skip games without a proper result
    if result == '*':
        game = chess.pgn.read_game(pgn)
        continue

    # Skip games that were lost on time
    last = game.end()
    comment = last.comment
    if comment.find('loses on time') != -1:
        game = chess.pgn.read_game(pgn)
        continue

    # Iterate over the game
    epdlist = iterate_game(game)

    # Select a set of position to include
    randlist = select_positions(epdlist)

    # Print all positions
    for epd in randlist:
        output.write(epd)
        output.write('\n')

    # Next game
    game = chess.pgn.read_game(pgn)

print("")
