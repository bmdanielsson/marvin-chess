/*
 * Marvin - an UCI/XBoard compatible chess engine
 * Copyright (C) 2015 Martin Danielsson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "fen.h"
#include "key.h"
#include "bitboard.h"
#include "validation.h"
#include "eval.h"
#include "position.h"
#include "data.h"

/* Returns true if c is a digit between '0' and '8'. */
#define IS_DIGIT_08(c)  ((c=='0')||(c=='1')||(c=='2')||(c=='3')|| \
                         (c=='4')||(c=='5')||(c=='6')||(c=='7')|| \
                         (c=='8')) \
                        ?true:false

/* Returns true if c is a digit between '0' and '9'. */
#define IS_DIGIT_09(c)  ((c=='0')||(c=='1')||(c=='2')||(c=='3')|| \
                         (c=='4')||(c=='5')||(c=='6')||(c=='7')|| \
                         (c=='8')||(c=='9')) \
                        ?true:false

/* Returns true if c is a piece charachter. */
#define IS_PIECE(c) ((c=='K')||(c=='Q')||(c=='R')||(c=='B')||(c=='N')||  \
                     (c=='P')|| (c=='k')||(c=='q')||(c=='r')||(c=='b')|| \
                     (c=='n')||(c=='p')) \
                    ?true:false

/* FEN string for all FRC starting positions */
static char *frc_str[960] = {
    "qrbknbnr/pppppppp/8/8/8/8/PPPPPPPP/QRBKNBNR w KQkq - 0 1",
    "rbqnbkrn/pppppppp/8/8/8/8/PPPPPPPP/RBQNBKRN w KQkq - 0 1",
    "nrbknbrq/pppppppp/8/8/8/8/PPPPPPPP/NRBKNBRQ w KQkq - 0 1",
    "qrbknbnr/pppppppp/8/8/8/8/PPPPPPPP/QRBKNBNR w KQkq - 0 1",
    "rnbkqnrb/pppppppp/8/8/8/8/PPPPPPPP/RNBKQNRB w KQkq - 0 1",
    "rkbnrqnb/pppppppp/8/8/8/8/PPPPPPPP/RKBNRQNB w KQkq - 0 1",
    "rnnkqbbr/pppppppp/8/8/8/8/PPPPPPPP/RNNKQBBR w KQkq - 0 1",
    "nbbrqnkr/pppppppp/8/8/8/8/PPPPPPPP/NBBRQNKR w KQkq - 0 1",
    "nrkbqnbr/pppppppp/8/8/8/8/PPPPPPPP/NRKBQNBR w KQkq - 0 1",
    "rnkbbnqr/pppppppp/8/8/8/8/PPPPPPPP/RNKBBNQR w KQkq - 0 1",
    "rnqnbbkr/pppppppp/8/8/8/8/PPPPPPPP/RNQNBBKR w KQkq - 0 1",
    "rqkrnnbb/pppppppp/8/8/8/8/PPPPPPPP/RQKRNNBB w KQkq - 0 1",
    "nrnbbkqr/pppppppp/8/8/8/8/PPPPPPPP/NRNBBKQR w KQkq - 0 1",
    "nrbkqbrn/pppppppp/8/8/8/8/PPPPPPPP/NRBKQBRN w KQkq - 0 1",
    "nrbqnbkr/pppppppp/8/8/8/8/PPPPPPPP/NRBQNBKR w KQkq - 0 1",
    "rnkrbnqb/pppppppp/8/8/8/8/PPPPPPPP/RNKRBNQB w KQkq - 0 1",
    "rbbknqrn/pppppppp/8/8/8/8/PPPPPPPP/RBBKNQRN w KQkq - 0 1",
    "rkqnrnbb/pppppppp/8/8/8/8/PPPPPPPP/RKQNRNBB w KQkq - 0 1",
    "nqnrbbkr/pppppppp/8/8/8/8/PPPPPPPP/NQNRBBKR w KQkq - 0 1",
    "rbqnbknr/pppppppp/8/8/8/8/PPPPPPPP/RBQNBKNR w KQkq - 0 1",
    "rknqnbbr/pppppppp/8/8/8/8/PPPPPPPP/RKNQNBBR w KQkq - 0 1",
    "rnqkbnrb/pppppppp/8/8/8/8/PPPPPPPP/RNQKBNRB w KQkq - 0 1",
    "bnrnkrqb/pppppppp/8/8/8/8/PPPPPPPP/BNRNKRQB w KQkq - 0 1",
    "nrkrbqnb/pppppppp/8/8/8/8/PPPPPPPP/NRKRBQNB w KQkq - 0 1",
    "nrbkqbrn/pppppppp/8/8/8/8/PPPPPPPP/NRBKQBRN w KQkq - 0 1",
    "rkbnnbqr/pppppppp/8/8/8/8/PPPPPPPP/RKBNNBQR w KQkq - 0 1",
    "rknrbqnb/pppppppp/8/8/8/8/PPPPPPPP/RKNRBQNB w KQkq - 0 1",
    "rnnbkqbr/pppppppp/8/8/8/8/PPPPPPPP/RNNBKQBR w KQkq - 0 1",
    "nbrqbknr/pppppppp/8/8/8/8/PPPPPPPP/NBRQBKNR w KQkq - 0 1",
    "brkqrbnn/pppppppp/8/8/8/8/PPPPPPPP/BRKQRBNN w KQkq - 0 1",
    "nrkqnbbr/pppppppp/8/8/8/8/PPPPPPPP/NRKQNBBR w KQkq - 0 1",
    "rnqbnkbr/pppppppp/8/8/8/8/PPPPPPPP/RNQBNKBR w KQkq - 0 1",
    "nrbknbrq/pppppppp/8/8/8/8/PPPPPPPP/NRBKNBRQ w KQkq - 0 1",
    "rbqnkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBQNKRBN w KQkq - 0 1",
    "brqknnrb/pppppppp/8/8/8/8/PPPPPPPP/BRQKNNRB w KQkq - 0 1",
    "rqnknrbb/pppppppp/8/8/8/8/PPPPPPPP/RQNKNRBB w KQkq - 0 1",
    "bqrnnbkr/pppppppp/8/8/8/8/PPPPPPPP/BQRNNBKR w KQkq - 0 1",
    "bbnrkrqn/pppppppp/8/8/8/8/PPPPPPPP/BBNRKRQN w KQkq - 0 1",
    "rkqrnbbn/pppppppp/8/8/8/8/PPPPPPPP/RKQRNBBN w KQkq - 0 1",
    "nbrnbkqr/pppppppp/8/8/8/8/PPPPPPPP/NBRNBKQR w KQkq - 0 1",
    "rqkbnrbn/pppppppp/8/8/8/8/PPPPPPPP/RQKBNRBN w KQkq - 0 1",
    "qrnkbrnb/pppppppp/8/8/8/8/PPPPPPPP/QRNKBRNB w KQkq - 0 1",
    "rnbbqnkr/pppppppp/8/8/8/8/PPPPPPPP/RNBBQNKR w KQkq - 0 1",
    "rbqnbknr/pppppppp/8/8/8/8/PPPPPPPP/RBQNBKNR w KQkq - 0 1",
    "nrbbknrq/pppppppp/8/8/8/8/PPPPPPPP/NRBBKNRQ w KQkq - 0 1",
    "rbbkrnqn/pppppppp/8/8/8/8/PPPPPPPP/RBBKRNQN w KQkq - 0 1",
    "bbqrnknr/pppppppp/8/8/8/8/PPPPPPPP/BBQRNKNR w KQkq - 0 1",
    "brnknqrb/pppppppp/8/8/8/8/PPPPPPPP/BRNKNQRB w KQkq - 0 1",
    "rqkbbnrn/pppppppp/8/8/8/8/PPPPPPPP/RQKBBNRN w KQkq - 0 1",
    "rqbknnrb/pppppppp/8/8/8/8/PPPPPPPP/RQBKNNRB w KQkq - 0 1",
    "nqrnbkrb/pppppppp/8/8/8/8/PPPPPPPP/NQRNBKRB w KQkq - 0 1",
    "rkqbbnrn/pppppppp/8/8/8/8/PPPPPPPP/RKQBBNRN w KQkq - 0 1",
    "qrbnkbrn/pppppppp/8/8/8/8/PPPPPPPP/QRBNKBRN w KQkq - 0 1",
    "bbrkrnqn/pppppppp/8/8/8/8/PPPPPPPP/BBRKRNQN w KQkq - 0 1",
    "bbnrnkrq/pppppppp/8/8/8/8/PPPPPPPP/BBNRNKRQ w KQkq - 0 1",
    "bnrkqrnb/pppppppp/8/8/8/8/PPPPPPPP/BNRKQRNB w KQkq - 0 1",
    "rnkbbqnr/pppppppp/8/8/8/8/PPPPPPPP/RNKBBQNR w KQkq - 0 1",
    "nrknbqrb/pppppppp/8/8/8/8/PPPPPPPP/NRKNBQRB w KQkq - 0 1",
    "brkrnqnb/pppppppp/8/8/8/8/PPPPPPPP/BRKRNQNB w KQkq - 0 1",
    "bqnrkbrn/pppppppp/8/8/8/8/PPPPPPPP/BQNRKBRN w KQkq - 0 1",
    "rqkbnrbn/pppppppp/8/8/8/8/PPPPPPPP/RQKBNRBN w KQkq - 0 1",
    "rnnbbqkr/pppppppp/8/8/8/8/PPPPPPPP/RNNBBQKR w KQkq - 0 1",
    "brkrnqnb/pppppppp/8/8/8/8/PPPPPPPP/BRKRNQNB w KQkq - 0 1",
    "nbbqnrkr/pppppppp/8/8/8/8/PPPPPPPP/NBBQNRKR w KQkq - 0 1",
    "nbbrknqr/pppppppp/8/8/8/8/PPPPPPPP/NBBRKNQR w KQkq - 0 1",
    "qnnrbbkr/pppppppp/8/8/8/8/PPPPPPPP/QNNRBBKR w KQkq - 0 1",
    "nrbkrqnb/pppppppp/8/8/8/8/PPPPPPPP/NRBKRQNB w KQkq - 0 1",
    "nrbnqbkr/pppppppp/8/8/8/8/PPPPPPPP/NRBNQBKR w KQkq - 0 1",
    "rknnqbbr/pppppppp/8/8/8/8/PPPPPPPP/RKNNQBBR w KQkq - 0 1",
    "rqbknrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBKNRNB w KQkq - 0 1",
    "rnkrbbnq/pppppppp/8/8/8/8/PPPPPPPP/RNKRBBNQ w KQkq - 0 1",
    "nrbkrnqb/pppppppp/8/8/8/8/PPPPPPPP/NRBKRNQB w KQkq - 0 1",
    "qbrkbnnr/pppppppp/8/8/8/8/PPPPPPPP/QBRKBNNR w KQkq - 0 1",
    "rbnqbnkr/pppppppp/8/8/8/8/PPPPPPPP/RBNQBNKR w KQkq - 0 1",
    "qrnknrbb/pppppppp/8/8/8/8/PPPPPPPP/QRNKNRBB w KQkq - 0 1",
    "nbqnbrkr/pppppppp/8/8/8/8/PPPPPPPP/NBQNBRKR w KQkq - 0 1",
    "bbqnrkrn/pppppppp/8/8/8/8/PPPPPPPP/BBQNRKRN w KQkq - 0 1",
    "rnqkrbbn/pppppppp/8/8/8/8/PPPPPPPP/RNQKRBBN w KQkq - 0 1",
    "nrkbbrqn/pppppppp/8/8/8/8/PPPPPPPP/NRKBBRQN w KQkq - 0 1",
    "rnbkrbqn/pppppppp/8/8/8/8/PPPPPPPP/RNBKRBQN w KQkq - 0 1",
    "qbrkbrnn/pppppppp/8/8/8/8/PPPPPPPP/QBRKBRNN w KQkq - 0 1",
    "brqbnnkr/pppppppp/8/8/8/8/PPPPPPPP/BRQBNNKR w KQkq - 0 1",
    "bnrnkbrq/pppppppp/8/8/8/8/PPPPPPPP/BNRNKBRQ w KQkq - 0 1",
    "nnqbbrkr/pppppppp/8/8/8/8/PPPPPPPP/NNQBBRKR w KQkq - 0 1",
    "bnrkqrnb/pppppppp/8/8/8/8/PPPPPPPP/BNRKQRNB w KQkq - 0 1",
    "rnkqbbrn/pppppppp/8/8/8/8/PPPPPPPP/RNKQBBRN w KQkq - 0 1",
    "rnkrnbbq/pppppppp/8/8/8/8/PPPPPPPP/RNKRNBBQ w KQkq - 0 1",
    "qrnnbkrb/pppppppp/8/8/8/8/PPPPPPPP/QRNNBKRB w KQkq - 0 1",
    "bqnrnbkr/pppppppp/8/8/8/8/PPPPPPPP/BQNRNBKR w KQkq - 0 1",
    "nrkqbbnr/pppppppp/8/8/8/8/PPPPPPPP/NRKQBBNR w KQkq - 0 1",
    "nqbnrkrb/pppppppp/8/8/8/8/PPPPPPPP/NQBNRKRB w KQkq - 0 1",
    "qnrbnkbr/pppppppp/8/8/8/8/PPPPPPPP/QNRBNKBR w KQkq - 0 1",
    "rnkqbbnr/pppppppp/8/8/8/8/PPPPPPPP/RNKQBBNR w KQkq - 0 1",
    "brnbknqr/pppppppp/8/8/8/8/PPPPPPPP/BRNBKNQR w KQkq - 0 1",
    "qrknbbrn/pppppppp/8/8/8/8/PPPPPPPP/QRKNBBRN w KQkq - 0 1",
    "brnbkrnq/pppppppp/8/8/8/8/PPPPPPPP/BRNBKRNQ w KQkq - 0 1",
    "rkqbnrbn/pppppppp/8/8/8/8/PPPPPPPP/RKQBNRBN w KQkq - 0 1",
    "nrnbbkqr/pppppppp/8/8/8/8/PPPPPPPP/NRNBBKQR w KQkq - 0 1",
    "rbqknnbr/pppppppp/8/8/8/8/PPPPPPPP/RBQKNNBR w KQkq - 0 1",
    "rkbrqnnb/pppppppp/8/8/8/8/PPPPPPPP/RKBRQNNB w KQkq - 0 1",
    "rnqbbkrn/pppppppp/8/8/8/8/PPPPPPPP/RNQBBKRN w KQkq - 0 1",
    "rnbnkrqb/pppppppp/8/8/8/8/PPPPPPPP/RNBNKRQB w KQkq - 0 1",
    "rkrnnbbq/pppppppp/8/8/8/8/PPPPPPPP/RKRNNBBQ w KQkq - 0 1",
    "nrbbkqnr/pppppppp/8/8/8/8/PPPPPPPP/NRBBKQNR w KQkq - 0 1",
    "rknnrbbq/pppppppp/8/8/8/8/PPPPPPPP/RKNNRBBQ w KQkq - 0 1",
    "bqrknbnr/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBNR w KQkq - 0 1",
    "nrbbknqr/pppppppp/8/8/8/8/PPPPPPPP/NRBBKNQR w KQkq - 0 1",
    "bnnrkrqb/pppppppp/8/8/8/8/PPPPPPPP/BNNRKRQB w KQkq - 0 1",
    "bnrkrbqn/pppppppp/8/8/8/8/PPPPPPPP/BNRKRBQN w KQkq - 0 1",
    "bqnbrnkr/pppppppp/8/8/8/8/PPPPPPPP/BQNBRNKR w KQkq - 0 1",
    "nrknbbrq/pppppppp/8/8/8/8/PPPPPPPP/NRKNBBRQ w KQkq - 0 1",
    "rnbnkqrb/pppppppp/8/8/8/8/PPPPPPPP/RNBNKQRB w KQkq - 0 1",
    "brqknnrb/pppppppp/8/8/8/8/PPPPPPPP/BRQKNNRB w KQkq - 0 1",
    "brnnqkrb/pppppppp/8/8/8/8/PPPPPPPP/BRNNQKRB w KQkq - 0 1",
    "bnrkrnqb/pppppppp/8/8/8/8/PPPPPPPP/BNRKRNQB w KQkq - 0 1",
    "rbqnkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBQNKRBN w KQkq - 0 1",
    "bnrbnkqr/pppppppp/8/8/8/8/PPPPPPPP/BNRBNKQR w KQkq - 0 1",
    "rbbknnrq/pppppppp/8/8/8/8/PPPPPPPP/RBBKNNRQ w KQkq - 0 1",
    "qnnbbrkr/pppppppp/8/8/8/8/PPPPPPPP/QNNBBRKR w KQkq - 0 1",
    "qrnkbrnb/pppppppp/8/8/8/8/PPPPPPPP/QRNKBRNB w KQkq - 0 1",
    "brnqkbnr/pppppppp/8/8/8/8/PPPPPPPP/BRNQKBNR w KQkq - 0 1",
    "rnqkrbbn/pppppppp/8/8/8/8/PPPPPPPP/RNQKRBBN w KQkq - 0 1",
    "rnbkqrnb/pppppppp/8/8/8/8/PPPPPPPP/RNBKQRNB w KQkq - 0 1",
    "nrkqrnbb/pppppppp/8/8/8/8/PPPPPPPP/NRKQRNBB w KQkq - 0 1",
    "nrbkqbnr/pppppppp/8/8/8/8/PPPPPPPP/NRBKQBNR w KQkq - 0 1",
    "bqrnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNNKRB w KQkq - 0 1",
    "rkqnbrnb/pppppppp/8/8/8/8/PPPPPPPP/RKQNBRNB w KQkq - 0 1",
    "brqnnbkr/pppppppp/8/8/8/8/PPPPPPPP/BRQNNBKR w KQkq - 0 1",
    "rbnkbnrq/pppppppp/8/8/8/8/PPPPPPPP/RBNKBNRQ w KQkq - 0 1",
    "qrbnkbrn/pppppppp/8/8/8/8/PPPPPPPP/QRBNKBRN w KQkq - 0 1",
    "brkbqnrn/pppppppp/8/8/8/8/PPPPPPPP/BRKBQNRN w KQkq - 0 1",
    "qrbnkrnb/pppppppp/8/8/8/8/PPPPPPPP/QRBNKRNB w KQkq - 0 1",
    "rnbbqknr/pppppppp/8/8/8/8/PPPPPPPP/RNBBQKNR w KQkq - 0 1",
    "nrnbkqbr/pppppppp/8/8/8/8/PPPPPPPP/NRNBKQBR w KQkq - 0 1",
    "rbbnkrnq/pppppppp/8/8/8/8/PPPPPPPP/RBBNKRNQ w KQkq - 0 1",
    "brnnkbrq/pppppppp/8/8/8/8/PPPPPPPP/BRNNKBRQ w KQkq - 0 1",
    "rbkqbnrn/pppppppp/8/8/8/8/PPPPPPPP/RBKQBNRN w KQkq - 0 1",
    "rnbknbrq/pppppppp/8/8/8/8/PPPPPPPP/RNBKNBRQ w KQkq - 0 1",
    "bbqrnnkr/pppppppp/8/8/8/8/PPPPPPPP/BBQRNNKR w KQkq - 0 1",
    "brnnkrqb/pppppppp/8/8/8/8/PPPPPPPP/BRNNKRQB w KQkq - 0 1",
    "bnqbrnkr/pppppppp/8/8/8/8/PPPPPPPP/BNQBRNKR w KQkq - 0 1",
    "nrknbbrq/pppppppp/8/8/8/8/PPPPPPPP/NRKNBBRQ w KQkq - 0 1",
    "rbkqnnbr/pppppppp/8/8/8/8/PPPPPPPP/RBKQNNBR w KQkq - 0 1",
    "rknrqbbn/pppppppp/8/8/8/8/PPPPPPPP/RKNRQBBN w KQkq - 0 1",
    "rknqnbbr/pppppppp/8/8/8/8/PPPPPPPP/RKNQNBBR w KQkq - 0 1",
    "brnbkrqn/pppppppp/8/8/8/8/PPPPPPPP/BRNBKRQN w KQkq - 0 1",
    "rqnkbbrn/pppppppp/8/8/8/8/PPPPPPPP/RQNKBBRN w KQkq - 0 1",
    "nrnbbqkr/pppppppp/8/8/8/8/PPPPPPPP/NRNBBQKR w KQkq - 0 1",
    "nbrnbkqr/pppppppp/8/8/8/8/PPPPPPPP/NBRNBKQR w KQkq - 0 1",
    "bqrknbrn/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBRN w KQkq - 0 1",
    "rnqbnkbr/pppppppp/8/8/8/8/PPPPPPPP/RNQBNKBR w KQkq - 0 1",
    "qnrnbbkr/pppppppp/8/8/8/8/PPPPPPPP/QNRNBBKR w KQkq - 0 1",
    "nnqrkbbr/pppppppp/8/8/8/8/PPPPPPPP/NNQRKBBR w KQkq - 0 1",
    "brknqnrb/pppppppp/8/8/8/8/PPPPPPPP/BRKNQNRB w KQkq - 0 1",
    "brkqnbrn/pppppppp/8/8/8/8/PPPPPPPP/BRKQNBRN w KQkq - 0 1",
    "nrqkbbrn/pppppppp/8/8/8/8/PPPPPPPP/NRQKBBRN w KQkq - 0 1",
    "rkrbbnqn/pppppppp/8/8/8/8/PPPPPPPP/RKRBBNQN w KQkq - 0 1",
    "bnrkqrnb/pppppppp/8/8/8/8/PPPPPPPP/BNRKQRNB w KQkq - 0 1",
    "rknqrnbb/pppppppp/8/8/8/8/PPPPPPPP/RKNQRNBB w KQkq - 0 1",
    "rqbbnkrn/pppppppp/8/8/8/8/PPPPPPPP/RQBBNKRN w KQkq - 0 1",
    "qbrnbknr/pppppppp/8/8/8/8/PPPPPPPP/QBRNBKNR w KQkq - 0 1",
    "rkqbbnrn/pppppppp/8/8/8/8/PPPPPPPP/RKQBBNRN w KQkq - 0 1",
    "nbnrkrbq/pppppppp/8/8/8/8/PPPPPPPP/NBNRKRBQ w KQkq - 0 1",
    "rqbknbrn/pppppppp/8/8/8/8/PPPPPPPP/RQBKNBRN w KQkq - 0 1",
    "rbbnqkrn/pppppppp/8/8/8/8/PPPPPPPP/RBBNQKRN w KQkq - 0 1",
    "rbqnbknr/pppppppp/8/8/8/8/PPPPPPPP/RBQNBKNR w KQkq - 0 1",
    "rnqknbbr/pppppppp/8/8/8/8/PPPPPPPP/RNQKNBBR w KQkq - 0 1",
    "bbrknqnr/pppppppp/8/8/8/8/PPPPPPPP/BBRKNQNR w KQkq - 0 1",
    "bbqnrnkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNRNKR w KQkq - 0 1",
    "qnrbnkbr/pppppppp/8/8/8/8/PPPPPPPP/QNRBNKBR w KQkq - 0 1",
    "qnrnkrbb/pppppppp/8/8/8/8/PPPPPPPP/QNRNKRBB w KQkq - 0 1",
    "qrkbbnrn/pppppppp/8/8/8/8/PPPPPPPP/QRKBBNRN w KQkq - 0 1",
    "qnrbkrbn/pppppppp/8/8/8/8/PPPPPPPP/QNRBKRBN w KQkq - 0 1",
    "nbrqbknr/pppppppp/8/8/8/8/PPPPPPPP/NBRQBKNR w KQkq - 0 1",
    "bbrnknqr/pppppppp/8/8/8/8/PPPPPPPP/BBRNKNQR w KQkq - 0 1",
    "rnqnbkrb/pppppppp/8/8/8/8/PPPPPPPP/RNQNBKRB w KQkq - 0 1",
    "nbbrknqr/pppppppp/8/8/8/8/PPPPPPPP/NBBRKNQR w KQkq - 0 1",
    "bnqrkbrn/pppppppp/8/8/8/8/PPPPPPPP/BNQRKBRN w KQkq - 0 1",
    "bbnrknrq/pppppppp/8/8/8/8/PPPPPPPP/BBNRKNRQ w KQkq - 0 1",
    "brnqkbnr/pppppppp/8/8/8/8/PPPPPPPP/BRNQKBNR w KQkq - 0 1",
    "rbknbqrn/pppppppp/8/8/8/8/PPPPPPPP/RBKNBQRN w KQkq - 0 1",
    "qbbnrknr/pppppppp/8/8/8/8/PPPPPPPP/QBBNRKNR w KQkq - 0 1",
    "qnnbbrkr/pppppppp/8/8/8/8/PPPPPPPP/QNNBBRKR w KQkq - 0 1",
    "qbrnknbr/pppppppp/8/8/8/8/PPPPPPPP/QBRNKNBR w KQkq - 0 1",
    "rknrqnbb/pppppppp/8/8/8/8/PPPPPPPP/RKNRQNBB w KQkq - 0 1",
    "brkbrqnn/pppppppp/8/8/8/8/PPPPPPPP/BRKBRQNN w KQkq - 0 1",
    "rbbqknnr/pppppppp/8/8/8/8/PPPPPPPP/RBBQKNNR w KQkq - 0 1",
    "rnknrbbq/pppppppp/8/8/8/8/PPPPPPPP/RNKNRBBQ w KQkq - 0 1",
    "bqrknrnb/pppppppp/8/8/8/8/PPPPPPPP/BQRKNRNB w KQkq - 0 1",
    "rkqnbbnr/pppppppp/8/8/8/8/PPPPPPPP/RKQNBBNR w KQkq - 0 1",
    "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w KQkq - 0 1",
    "rnqnbkrb/pppppppp/8/8/8/8/PPPPPPPP/RNQNBKRB w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rknrqnbb/pppppppp/8/8/8/8/PPPPPPPP/RKNRQNBB w KQkq - 0 1",
    "qrknrnbb/pppppppp/8/8/8/8/PPPPPPPP/QRKNRNBB w KQkq - 0 1",
    "bnqbnrkr/pppppppp/8/8/8/8/PPPPPPPP/BNQBNRKR w KQkq - 0 1",
    "nbqrbnkr/pppppppp/8/8/8/8/PPPPPPPP/NBQRBNKR w KQkq - 0 1",
    "rnkbbqrn/pppppppp/8/8/8/8/PPPPPPPP/RNKBBQRN w KQkq - 0 1",
    "bnrbqnkr/pppppppp/8/8/8/8/PPPPPPPP/BNRBQNKR w KQkq - 0 1",
    "nqbnrkrb/pppppppp/8/8/8/8/PPPPPPPP/NQBNRKRB w KQkq - 0 1",
    "bqrknbnr/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBNR w KQkq - 0 1",
    "rqnnkrbb/pppppppp/8/8/8/8/PPPPPPPP/RQNNKRBB w KQkq - 0 1",
    "qrnnbbkr/pppppppp/8/8/8/8/PPPPPPPP/QRNNBBKR w KQkq - 0 1",
    "bnnrkrqb/pppppppp/8/8/8/8/PPPPPPPP/BNNRKRQB w KQkq - 0 1",
    "nrqbknbr/pppppppp/8/8/8/8/PPPPPPPP/NRQBKNBR w KQkq - 0 1",
    "rkbbnnrq/pppppppp/8/8/8/8/PPPPPPPP/RKBBNNRQ w KQkq - 0 1",
    "brqnkbrn/pppppppp/8/8/8/8/PPPPPPPP/BRQNKBRN w KQkq - 0 1",
    "qrnkbbrn/pppppppp/8/8/8/8/PPPPPPPP/QRNKBBRN w KQkq - 0 1",
    "bbrknqrn/pppppppp/8/8/8/8/PPPPPPPP/BBRKNQRN w KQkq - 0 1",
    "rnkbrnbq/pppppppp/8/8/8/8/PPPPPPPP/RNKBRNBQ w KQkq - 0 1",
    "bbqrknnr/pppppppp/8/8/8/8/PPPPPPPP/BBQRKNNR w KQkq - 0 1",
    "qnbrkrnb/pppppppp/8/8/8/8/PPPPPPPP/QNBRKRNB w KQkq - 0 1",
    "brqbnknr/pppppppp/8/8/8/8/PPPPPPPP/BRQBNKNR w KQkq - 0 1",
    "bbrnkrqn/pppppppp/8/8/8/8/PPPPPPPP/BBRNKRQN w KQkq - 0 1",
    "rbnqbkrn/pppppppp/8/8/8/8/PPPPPPPP/RBNQBKRN w KQkq - 0 1",
    "nrqbbkrn/pppppppp/8/8/8/8/PPPPPPPP/NRQBBKRN w KQkq - 0 1",
    "bqrknbnr/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBNR w KQkq - 0 1",
    "rqbknrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBKNRNB w KQkq - 0 1",
    "rnbkrqnb/pppppppp/8/8/8/8/PPPPPPPP/RNBKRQNB w KQkq - 0 1",
    "brqnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BRQNNKRB w KQkq - 0 1",
    "rqkbnrbn/pppppppp/8/8/8/8/PPPPPPPP/RQKBNRBN w KQkq - 0 1",
    "qrnbknbr/pppppppp/8/8/8/8/PPPPPPPP/QRNBKNBR w KQkq - 0 1",
    "nbbqrknr/pppppppp/8/8/8/8/PPPPPPPP/NBBQRKNR w KQkq - 0 1",
    "rkrnqnbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNQNBB w KQkq - 0 1",
    "bnnbrqkr/pppppppp/8/8/8/8/PPPPPPPP/BNNBRQKR w KQkq - 0 1",
    "nrbbnqkr/pppppppp/8/8/8/8/PPPPPPPP/NRBBNQKR w KQkq - 0 1",
    "brkbqnnr/pppppppp/8/8/8/8/PPPPPPPP/BRKBQNNR w KQkq - 0 1",
    "rqbnknrb/pppppppp/8/8/8/8/PPPPPPPP/RQBNKNRB w KQkq - 0 1",
    "qrknnrbb/pppppppp/8/8/8/8/PPPPPPPP/QRKNNRBB w KQkq - 0 1",
    "bbrkqnrn/pppppppp/8/8/8/8/PPPPPPPP/BBRKQNRN w KQkq - 0 1",
    "nrbbnkqr/pppppppp/8/8/8/8/PPPPPPPP/NRBBNKQR w KQkq - 0 1",
    "nrnbkqbr/pppppppp/8/8/8/8/PPPPPPPP/NRNBKQBR w KQkq - 0 1",
    "bqnnrbkr/pppppppp/8/8/8/8/PPPPPPPP/BQNNRBKR w KQkq - 0 1",
    "brqkrnnb/pppppppp/8/8/8/8/PPPPPPPP/BRQKRNNB w KQkq - 0 1",
    "nbbrkqrn/pppppppp/8/8/8/8/PPPPPPPP/NBBRKQRN w KQkq - 0 1",
    "nrbqkrnb/pppppppp/8/8/8/8/PPPPPPPP/NRBQKRNB w KQkq - 0 1",
    "nrkbbrqn/pppppppp/8/8/8/8/PPPPPPPP/NRKBBRQN w KQkq - 0 1",
    "rkbrqbnn/pppppppp/8/8/8/8/PPPPPPPP/RKBRQBNN w KQkq - 0 1",
    "nnbrqbkr/pppppppp/8/8/8/8/PPPPPPPP/NNBRQBKR w KQkq - 0 1",
    "brqbnknr/pppppppp/8/8/8/8/PPPPPPPP/BRQBNKNR w KQkq - 0 1",
    "nbnrkrbq/pppppppp/8/8/8/8/PPPPPPPP/NBNRKRBQ w KQkq - 0 1",
    "rnkqbbrn/pppppppp/8/8/8/8/PPPPPPPP/RNKQBBRN w KQkq - 0 1",
    "bqrnkbrn/pppppppp/8/8/8/8/PPPPPPPP/BQRNKBRN w KQkq - 0 1",
    "rkbbnrqn/pppppppp/8/8/8/8/PPPPPPPP/RKBBNRQN w KQkq - 0 1",
    "bqrnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNNKRB w KQkq - 0 1",
    "qbrnbknr/pppppppp/8/8/8/8/PPPPPPPP/QBRNBKNR w KQkq - 0 1",
    "rknbrqbn/pppppppp/8/8/8/8/PPPPPPPP/RKNBRQBN w KQkq - 0 1",
    "rbqkbrnn/pppppppp/8/8/8/8/PPPPPPPP/RBQKBRNN w KQkq - 0 1",
    "bbnnrqkr/pppppppp/8/8/8/8/PPPPPPPP/BBNNRQKR w KQkq - 0 1",
    "rnqbbnkr/pppppppp/8/8/8/8/PPPPPPPP/RNQBBNKR w KQkq - 0 1",
    "brkrnbnq/pppppppp/8/8/8/8/PPPPPPPP/BRKRNBNQ w KQkq - 0 1",
    "rbqnkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBQNKRBN w KQkq - 0 1",
    "nnqrbbkr/pppppppp/8/8/8/8/PPPPPPPP/NNQRBBKR w KQkq - 0 1",
    "bbrkrqnn/pppppppp/8/8/8/8/PPPPPPPP/BBRKRQNN w KQkq - 0 1",
    "bbnrknqr/pppppppp/8/8/8/8/PPPPPPPP/BBNRKNQR w KQkq - 0 1",
    "qrbnnbkr/pppppppp/8/8/8/8/PPPPPPPP/QRBNNBKR w KQkq - 0 1",
    "nbbnrkqr/pppppppp/8/8/8/8/PPPPPPPP/NBBNRKQR w KQkq - 0 1",
    "qnnbbrkr/pppppppp/8/8/8/8/PPPPPPPP/QNNBBRKR w KQkq - 0 1",
    "brkrnbqn/pppppppp/8/8/8/8/PPPPPPPP/BRKRNBQN w KQkq - 0 1",
    "nnrbkrbq/pppppppp/8/8/8/8/PPPPPPPP/NNRBKRBQ w KQkq - 0 1",
    "nrqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/NRQKBRNB w KQkq - 0 1",
    "nbnrbkqr/pppppppp/8/8/8/8/PPPPPPPP/NBNRBKQR w KQkq - 0 1",
    "nbrqnkbr/pppppppp/8/8/8/8/PPPPPPPP/NBRQNKBR w KQkq - 0 1",
    "bbnrqknr/pppppppp/8/8/8/8/PPPPPPPP/BBNRQKNR w KQkq - 0 1",
    "bbnqrnkr/pppppppp/8/8/8/8/PPPPPPPP/BBNQRNKR w KQkq - 0 1",
    "nrnbqkbr/pppppppp/8/8/8/8/PPPPPPPP/NRNBQKBR w KQkq - 0 1",
    "qnbnrkrb/pppppppp/8/8/8/8/PPPPPPPP/QNBNRKRB w KQkq - 0 1",
    "rqnnbkrb/pppppppp/8/8/8/8/PPPPPPPP/RQNNBKRB w KQkq - 0 1",
    "qnbrkbrn/pppppppp/8/8/8/8/PPPPPPPP/QNBRKBRN w KQkq - 0 1",
    "nrbknbrq/pppppppp/8/8/8/8/PPPPPPPP/NRBKNBRQ w KQkq - 0 1",
    "nbrkbqrn/pppppppp/8/8/8/8/PPPPPPPP/NBRKBQRN w KQkq - 0 1",
    "rnnkqrbb/pppppppp/8/8/8/8/PPPPPPPP/RNNKQRBB w KQkq - 0 1",
    "brknnrqb/pppppppp/8/8/8/8/PPPPPPPP/BRKNNRQB w KQkq - 0 1",
    "qrbbknnr/pppppppp/8/8/8/8/PPPPPPPP/QRBBKNNR w KQkq - 0 1",
    "rqnnkrbb/pppppppp/8/8/8/8/PPPPPPPP/RQNNKRBB w KQkq - 0 1",
    "nbbqnrkr/pppppppp/8/8/8/8/PPPPPPPP/NBBQNRKR w KQkq - 0 1",
    "qnrknrbb/pppppppp/8/8/8/8/PPPPPPPP/QNRKNRBB w KQkq - 0 1",
    "nrbkqrnb/pppppppp/8/8/8/8/PPPPPPPP/NRBKQRNB w KQkq - 0 1",
    "bqrknbnr/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBNR w KQkq - 0 1",
    "rbbnqknr/pppppppp/8/8/8/8/PPPPPPPP/RBBNQKNR w KQkq - 0 1",
    "rnbkrqnb/pppppppp/8/8/8/8/PPPPPPPP/RNBKRQNB w KQkq - 0 1",
    "rbqkbnnr/pppppppp/8/8/8/8/PPPPPPPP/RBQKBNNR w KQkq - 0 1",
    "bqnrnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQNRNKRB w KQkq - 0 1",
    "nbrkbnrq/pppppppp/8/8/8/8/PPPPPPPP/NBRKBNRQ w KQkq - 0 1",
    "rbnnqkbr/pppppppp/8/8/8/8/PPPPPPPP/RBNNQKBR w KQkq - 0 1",
    "rbnkqrbn/pppppppp/8/8/8/8/PPPPPPPP/RBNKQRBN w KQkq - 0 1",
    "rqnknrbb/pppppppp/8/8/8/8/PPPPPPPP/RQNKNRBB w KQkq - 0 1",
    "rnkqrnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKQRNBB w KQkq - 0 1",
    "brkqnrnb/pppppppp/8/8/8/8/PPPPPPPP/BRKQNRNB w KQkq - 0 1",
    "rnknbqrb/pppppppp/8/8/8/8/PPPPPPPP/RNKNBQRB w KQkq - 0 1",
    "qrknbbrn/pppppppp/8/8/8/8/PPPPPPPP/QRKNBBRN w KQkq - 0 1",
    "rnnqkrbb/pppppppp/8/8/8/8/PPPPPPPP/RNNQKRBB w KQkq - 0 1",
    "bbqrnknr/pppppppp/8/8/8/8/PPPPPPPP/BBQRNKNR w KQkq - 0 1",
    "rnkbnrbq/pppppppp/8/8/8/8/PPPPPPPP/RNKBNRBQ w KQkq - 0 1",
    "bnrqkbrn/pppppppp/8/8/8/8/PPPPPPPP/BNRQKBRN w KQkq - 0 1",
    "bqrbknrn/pppppppp/8/8/8/8/PPPPPPPP/BQRBKNRN w KQkq - 0 1",
    "brknqnrb/pppppppp/8/8/8/8/PPPPPPPP/BRKNQNRB w KQkq - 0 1",
    "rknnbqrb/pppppppp/8/8/8/8/PPPPPPPP/RKNNBQRB w KQkq - 0 1",
    "rqbbknrn/pppppppp/8/8/8/8/PPPPPPPP/RQBBKNRN w KQkq - 0 1",
    "qrbnkbrn/pppppppp/8/8/8/8/PPPPPPPP/QRBNKBRN w KQkq - 0 1",
    "rkbrnbnq/pppppppp/8/8/8/8/PPPPPPPP/RKBRNBNQ w KQkq - 0 1",
    "brkqrnnb/pppppppp/8/8/8/8/PPPPPPPP/BRKQRNNB w KQkq - 0 1",
    "rbkrqnbn/pppppppp/8/8/8/8/PPPPPPPP/RBKRQNBN w KQkq - 0 1",
    "brkqnnrb/pppppppp/8/8/8/8/PPPPPPPP/BRKQNNRB w KQkq - 0 1",
    "nrbnkbqr/pppppppp/8/8/8/8/PPPPPPPP/NRBNKBQR w KQkq - 0 1",
    "qrnkbnrb/pppppppp/8/8/8/8/PPPPPPPP/QRNKBNRB w KQkq - 0 1",
    "rqnnbbkr/pppppppp/8/8/8/8/PPPPPPPP/RQNNBBKR w KQkq - 0 1",
    "bbnqrnkr/pppppppp/8/8/8/8/PPPPPPPP/BBNQRNKR w KQkq - 0 1",
    "rnnkqrbb/pppppppp/8/8/8/8/PPPPPPPP/RNNKQRBB w KQkq - 0 1",
    "rqknbbrn/pppppppp/8/8/8/8/PPPPPPPP/RQKNBBRN w KQkq - 0 1",
    "rkqnbbrn/pppppppp/8/8/8/8/PPPPPPPP/RKQNBBRN w KQkq - 0 1",
    "rnbkqbrn/pppppppp/8/8/8/8/PPPPPPPP/RNBKQBRN w KQkq - 0 1",
    "nnbrkbqr/pppppppp/8/8/8/8/PPPPPPPP/NNBRKBQR w KQkq - 0 1",
    "brknqnrb/pppppppp/8/8/8/8/PPPPPPPP/BRKNQNRB w KQkq - 0 1",
    "rnkqrnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKQRNBB w KQkq - 0 1",
    "nrkqrbbn/pppppppp/8/8/8/8/PPPPPPPP/NRKQRBBN w KQkq - 0 1",
    "rbqnknbr/pppppppp/8/8/8/8/PPPPPPPP/RBQNKNBR w KQkq - 0 1",
    "rnkrnbbq/pppppppp/8/8/8/8/PPPPPPPP/RNKRNBBQ w KQkq - 0 1",
    "bbqnrkrn/pppppppp/8/8/8/8/PPPPPPPP/BBQNRKRN w KQkq - 0 1",
    "bbrknqrn/pppppppp/8/8/8/8/PPPPPPPP/BBRKNQRN w KQkq - 0 1",
    "rbbqnkrn/pppppppp/8/8/8/8/PPPPPPPP/RBBQNKRN w KQkq - 0 1",
    "nrkbbnrq/pppppppp/8/8/8/8/PPPPPPPP/NRKBBNRQ w KQkq - 0 1",
    "rknnbbrq/pppppppp/8/8/8/8/PPPPPPPP/RKNNBBRQ w KQkq - 0 1",
    "rkbrnqnb/pppppppp/8/8/8/8/PPPPPPPP/RKBRNQNB w KQkq - 0 1",
    "qbrnnkbr/pppppppp/8/8/8/8/PPPPPPPP/QBRNNKBR w KQkq - 0 1",
    "qrnkbbnr/pppppppp/8/8/8/8/PPPPPPPP/QRNKBBNR w KQkq - 0 1",
    "nrkbbnrq/pppppppp/8/8/8/8/PPPPPPPP/NRKBBNRQ w KQkq - 0 1",
    "nbqnbrkr/pppppppp/8/8/8/8/PPPPPPPP/NBQNBRKR w KQkq - 0 1",
    "rbnnqkbr/pppppppp/8/8/8/8/PPPPPPPP/RBNNQKBR w KQkq - 0 1",
    "rkbnrbnq/pppppppp/8/8/8/8/PPPPPPPP/RKBNRBNQ w KQkq - 0 1",
    "rkrbbnnq/pppppppp/8/8/8/8/PPPPPPPP/RKRBBNNQ w KQkq - 0 1",
    "nqnbbrkr/pppppppp/8/8/8/8/PPPPPPPP/NQNBBRKR w KQkq - 0 1",
    "rkrqnbbn/pppppppp/8/8/8/8/PPPPPPPP/RKRQNBBN w KQkq - 0 1",
    "nbbqrknr/pppppppp/8/8/8/8/PPPPPPPP/NBBQRKNR w KQkq - 0 1",
    "rbknnrbq/pppppppp/8/8/8/8/PPPPPPPP/RBKNNRBQ w KQkq - 0 1",
    "nrbknrqb/pppppppp/8/8/8/8/PPPPPPPP/NRBKNRQB w KQkq - 0 1",
    "bnrnqbkr/pppppppp/8/8/8/8/PPPPPPPP/BNRNQBKR w KQkq - 0 1",
    "brkrnbnq/pppppppp/8/8/8/8/PPPPPPPP/BRKRNBNQ w KQkq - 0 1",
    "nbrkqrbn/pppppppp/8/8/8/8/PPPPPPPP/NBRKQRBN w KQkq - 0 1",
    "rqbnkbnr/pppppppp/8/8/8/8/PPPPPPPP/RQBNKBNR w KQkq - 0 1",
    "rnbkqbrn/pppppppp/8/8/8/8/PPPPPPPP/RNBKQBRN w KQkq - 0 1",
    "rqkbnnbr/pppppppp/8/8/8/8/PPPPPPPP/RQKBNNBR w KQkq - 0 1",
    "nnrkbbrq/pppppppp/8/8/8/8/PPPPPPPP/NNRKBBRQ w KQkq - 0 1",
    "brknqbrn/pppppppp/8/8/8/8/PPPPPPPP/BRKNQBRN w KQkq - 0 1",
    "brnbkqrn/pppppppp/8/8/8/8/PPPPPPPP/BRNBKQRN w KQkq - 0 1",
    "rbqkbnrn/pppppppp/8/8/8/8/PPPPPPPP/RBQKBNRN w KQkq - 0 1",
    "bnrqkbrn/pppppppp/8/8/8/8/PPPPPPPP/BNRQKBRN w KQkq - 0 1",
    "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w KQkq - 0 1",
    "rkbqnbrn/pppppppp/8/8/8/8/PPPPPPPP/RKBQNBRN w KQkq - 0 1",
    "nrbbknqr/pppppppp/8/8/8/8/PPPPPPPP/NRBBKNQR w KQkq - 0 1",
    "rknbrnbq/pppppppp/8/8/8/8/PPPPPPPP/RKNBRNBQ w KQkq - 0 1",
    "rqknbbrn/pppppppp/8/8/8/8/PPPPPPPP/RQKNBBRN w KQkq - 0 1",
    "brnbqkrn/pppppppp/8/8/8/8/PPPPPPPP/BRNBQKRN w KQkq - 0 1",
    "nnqrbbkr/pppppppp/8/8/8/8/PPPPPPPP/NNQRBBKR w KQkq - 0 1",
    "rnnbqkbr/pppppppp/8/8/8/8/PPPPPPPP/RNNBQKBR w KQkq - 0 1",
    "brnbkrqn/pppppppp/8/8/8/8/PPPPPPPP/BRNBKRQN w KQkq - 0 1",
    "brkbrnqn/pppppppp/8/8/8/8/PPPPPPPP/BRKBRNQN w KQkq - 0 1",
    "nqnrkbbr/pppppppp/8/8/8/8/PPPPPPPP/NQNRKBBR w KQkq - 0 1",
    "rkqbnnbr/pppppppp/8/8/8/8/PPPPPPPP/RKQBNNBR w KQkq - 0 1",
    "bnnqrkrb/pppppppp/8/8/8/8/PPPPPPPP/BNNQRKRB w KQkq - 0 1",
    "bbrkqrnn/pppppppp/8/8/8/8/PPPPPPPP/BBRKQRNN w KQkq - 0 1",
    "rnkbnrbq/pppppppp/8/8/8/8/PPPPPPPP/RNKBNRBQ w KQkq - 0 1",
    "bbqrknnr/pppppppp/8/8/8/8/PPPPPPPP/BBQRKNNR w KQkq - 0 1",
    "brnknrqb/pppppppp/8/8/8/8/PPPPPPPP/BRNKNRQB w KQkq - 0 1",
    "nbbrkrqn/pppppppp/8/8/8/8/PPPPPPPP/NBBRKRQN w KQkq - 0 1",
    "qbbnrknr/pppppppp/8/8/8/8/PPPPPPPP/QBBNRKNR w KQkq - 0 1",
    "bbrqnnkr/pppppppp/8/8/8/8/PPPPPPPP/BBRQNNKR w KQkq - 0 1",
    "bbrnnkqr/pppppppp/8/8/8/8/PPPPPPPP/BBRNNKQR w KQkq - 0 1",
    "qnrbknbr/pppppppp/8/8/8/8/PPPPPPPP/QNRBKNBR w KQkq - 0 1",
    "qrknbnrb/pppppppp/8/8/8/8/PPPPPPPP/QRKNBNRB w KQkq - 0 1",
    "nbrkbqrn/pppppppp/8/8/8/8/PPPPPPPP/NBRKBQRN w KQkq - 0 1",
    "brnnqbkr/pppppppp/8/8/8/8/PPPPPPPP/BRNNQBKR w KQkq - 0 1",
    "nrqbknbr/pppppppp/8/8/8/8/PPPPPPPP/NRQBKNBR w KQkq - 0 1",
    "rbnkqnbr/pppppppp/8/8/8/8/PPPPPPPP/RBNKQNBR w KQkq - 0 1",
    "bbrnkrnq/pppppppp/8/8/8/8/PPPPPPPP/BBRNKRNQ w KQkq - 0 1",
    "rknqbrnb/pppppppp/8/8/8/8/PPPPPPPP/RKNQBRNB w KQkq - 0 1",
    "rkbnnbqr/pppppppp/8/8/8/8/PPPPPPPP/RKBNNBQR w KQkq - 0 1",
    "rqbbkrnn/pppppppp/8/8/8/8/PPPPPPPP/RQBBKRNN w KQkq - 0 1",
    "rnknbqrb/pppppppp/8/8/8/8/PPPPPPPP/RNKNBQRB w KQkq - 0 1",
    "rnbbnkrq/pppppppp/8/8/8/8/PPPPPPPP/RNBBNKRQ w KQkq - 0 1",
    "rnkqbbnr/pppppppp/8/8/8/8/PPPPPPPP/RNKQBBNR w KQkq - 0 1",
    "bbqrknrn/pppppppp/8/8/8/8/PPPPPPPP/BBQRKNRN w KQkq - 0 1",
    "brnnqkrb/pppppppp/8/8/8/8/PPPPPPPP/BRNNQKRB w KQkq - 0 1",
    "qbnrnkbr/pppppppp/8/8/8/8/PPPPPPPP/QBNRNKBR w KQkq - 0 1",
    "rnqnbkrb/pppppppp/8/8/8/8/PPPPPPPP/RNQNBKRB w KQkq - 0 1",
    "bnrbnkrq/pppppppp/8/8/8/8/PPPPPPPP/BNRBNKRQ w KQkq - 0 1",
    "rqknbbrn/pppppppp/8/8/8/8/PPPPPPPP/RQKNBBRN w KQkq - 0 1",
    "brnbkqrn/pppppppp/8/8/8/8/PPPPPPPP/BRNBKQRN w KQkq - 0 1",
    "brnknrqb/pppppppp/8/8/8/8/PPPPPPPP/BRNKNRQB w KQkq - 0 1",
    "rknrbnqb/pppppppp/8/8/8/8/PPPPPPPP/RKNRBNQB w KQkq - 0 1",
    "bbnrnqkr/pppppppp/8/8/8/8/PPPPPPPP/BBNRNQKR w KQkq - 0 1",
    "bqnrkrnb/pppppppp/8/8/8/8/PPPPPPPP/BQNRKRNB w KQkq - 0 1",
    "rkbbrqnn/pppppppp/8/8/8/8/PPPPPPPP/RKBBRQNN w KQkq - 0 1",
    "rbbnnqkr/pppppppp/8/8/8/8/PPPPPPPP/RBBNNQKR w KQkq - 0 1",
    "rnqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/RNQKBRNB w KQkq - 0 1",
    "brnkrnqb/pppppppp/8/8/8/8/PPPPPPPP/BRNKRNQB w KQkq - 0 1",
    "nrbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/NRBQKBNR w KQkq - 0 1",
    "qrkbbnnr/pppppppp/8/8/8/8/PPPPPPPP/QRKBBNNR w KQkq - 0 1",
    "rknbqrbn/pppppppp/8/8/8/8/PPPPPPPP/RKNBQRBN w KQkq - 0 1",
    "qrbbknrn/pppppppp/8/8/8/8/PPPPPPPP/QRBBKNRN w KQkq - 0 1",
    "brnbqkrn/pppppppp/8/8/8/8/PPPPPPPP/BRNBQKRN w KQkq - 0 1",
    "bqrbnnkr/pppppppp/8/8/8/8/PPPPPPPP/BQRBNNKR w KQkq - 0 1",
    "bnrknbqr/pppppppp/8/8/8/8/PPPPPPPP/BNRKNBQR w KQkq - 0 1",
    "bnnbrqkr/pppppppp/8/8/8/8/PPPPPPPP/BNNBRQKR w KQkq - 0 1",
    "nbrnqkbr/pppppppp/8/8/8/8/PPPPPPPP/NBRNQKBR w KQkq - 0 1",
    "brkbrnqn/pppppppp/8/8/8/8/PPPPPPPP/BRKBRNQN w KQkq - 0 1",
    "rqbnkrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBNKRNB w KQkq - 0 1",
    "nrqnbkrb/pppppppp/8/8/8/8/PPPPPPPP/NRQNBKRB w KQkq - 0 1",
    "brnnkqrb/pppppppp/8/8/8/8/PPPPPPPP/BRNNKQRB w KQkq - 0 1",
    "rkrnnqbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNNQBB w KQkq - 0 1",
    "rbnnqkbr/pppppppp/8/8/8/8/PPPPPPPP/RBNNQKBR w KQkq - 0 1",
    "rkbbnrnq/pppppppp/8/8/8/8/PPPPPPPP/RKBBNRNQ w KQkq - 0 1",
    "nrqkrnbb/pppppppp/8/8/8/8/PPPPPPPP/NRQKRNBB w KQkq - 0 1",
    "brqnknrb/pppppppp/8/8/8/8/PPPPPPPP/BRQNKNRB w KQkq - 0 1",
    "rqbbnkrn/pppppppp/8/8/8/8/PPPPPPPP/RQBBNKRN w KQkq - 0 1",
    "bbnrkrnq/pppppppp/8/8/8/8/PPPPPPPP/BBNRKRNQ w KQkq - 0 1",
    "rbbknrqn/pppppppp/8/8/8/8/PPPPPPPP/RBBKNRQN w KQkq - 0 1",
    "rknbnrbq/pppppppp/8/8/8/8/PPPPPPPP/RKNBNRBQ w KQkq - 0 1",
    "rbqnbknr/pppppppp/8/8/8/8/PPPPPPPP/RBQNBKNR w KQkq - 0 1",
    "rnknbrqb/pppppppp/8/8/8/8/PPPPPPPP/RNKNBRQB w KQkq - 0 1",
    "nnbqrbkr/pppppppp/8/8/8/8/PPPPPPPP/NNBQRBKR w KQkq - 0 1",
    "nrbknbrq/pppppppp/8/8/8/8/PPPPPPPP/NRBKNBRQ w KQkq - 0 1",
    "rnkrqbbn/pppppppp/8/8/8/8/PPPPPPPP/RNKRQBBN w KQkq - 0 1",
    "rqknbbnr/pppppppp/8/8/8/8/PPPPPPPP/RQKNBBNR w KQkq - 0 1",
    "rnqbbknr/pppppppp/8/8/8/8/PPPPPPPP/RNQBBKNR w KQkq - 0 1",
    "bqnrnbkr/pppppppp/8/8/8/8/PPPPPPPP/BQNRNBKR w KQkq - 0 1",
    "nnqbrkbr/pppppppp/8/8/8/8/PPPPPPPP/NNQBRKBR w KQkq - 0 1",
    "rkqrbnnb/pppppppp/8/8/8/8/PPPPPPPP/RKQRBNNB w KQkq - 0 1",
    "qrknnrbb/pppppppp/8/8/8/8/PPPPPPPP/QRKNNRBB w KQkq - 0 1",
    "nbqrbknr/pppppppp/8/8/8/8/PPPPPPPP/NBQRBKNR w KQkq - 0 1",
    "bnrknbrq/pppppppp/8/8/8/8/PPPPPPPP/BNRKNBRQ w KQkq - 0 1",
    "bbqrkrnn/pppppppp/8/8/8/8/PPPPPPPP/BBQRKRNN w KQkq - 0 1",
    "nqbbrnkr/pppppppp/8/8/8/8/PPPPPPPP/NQBBRNKR w KQkq - 0 1",
    "rknqrnbb/pppppppp/8/8/8/8/PPPPPPPP/RKNQRNBB w KQkq - 0 1",
    "nqnrkbbr/pppppppp/8/8/8/8/PPPPPPPP/NQNRKBBR w KQkq - 0 1",
    "rkqbbnrn/pppppppp/8/8/8/8/PPPPPPPP/RKQBBNRN w KQkq - 0 1",
    "nnrbbqkr/pppppppp/8/8/8/8/PPPPPPPP/NNRBBQKR w KQkq - 0 1",
    "brkqnrnb/pppppppp/8/8/8/8/PPPPPPPP/BRKQNRNB w KQkq - 0 1",
    "brnkqbnr/pppppppp/8/8/8/8/PPPPPPPP/BRNKQBNR w KQkq - 0 1",
    "rknbbnrq/pppppppp/8/8/8/8/PPPPPPPP/RKNBBNRQ w KQkq - 0 1",
    "rqnnbkrb/pppppppp/8/8/8/8/PPPPPPPP/RQNNBKRB w KQkq - 0 1",
    "bnrqkbnr/pppppppp/8/8/8/8/PPPPPPPP/BNRQKBNR w KQkq - 0 1",
    "bnqbrnkr/pppppppp/8/8/8/8/PPPPPPPP/BNQBRNKR w KQkq - 0 1",
    "bnnrkqrb/pppppppp/8/8/8/8/PPPPPPPP/BNNRKQRB w KQkq - 0 1",
    "bbrkqnrn/pppppppp/8/8/8/8/PPPPPPPP/BBRKQNRN w KQkq - 0 1",
    "bnqnrkrb/pppppppp/8/8/8/8/PPPPPPPP/BNQNRKRB w KQkq - 0 1",
    "nrbqkbrn/pppppppp/8/8/8/8/PPPPPPPP/NRBQKBRN w KQkq - 0 1",
    "bqnrkbnr/pppppppp/8/8/8/8/PPPPPPPP/BQNRKBNR w KQkq - 0 1",
    "nqrknrbb/pppppppp/8/8/8/8/PPPPPPPP/NQRKNRBB w KQkq - 0 1",
    "nrkrqnbb/pppppppp/8/8/8/8/PPPPPPPP/NRKRQNBB w KQkq - 0 1",
    "rbkqbnnr/pppppppp/8/8/8/8/PPPPPPPP/RBKQBNNR w KQkq - 0 1",
    "rkbrnbqn/pppppppp/8/8/8/8/PPPPPPPP/RKBRNBQN w KQkq - 0 1",
    "rnbqkrnb/pppppppp/8/8/8/8/PPPPPPPP/RNBQKRNB w KQkq - 0 1",
    "rqbnkrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBNKRNB w KQkq - 0 1",
    "bbrnqnkr/pppppppp/8/8/8/8/PPPPPPPP/BBRNQNKR w KQkq - 0 1",
    "bqrbknnr/pppppppp/8/8/8/8/PPPPPPPP/BQRBKNNR w KQkq - 0 1",
    "bqrbnnkr/pppppppp/8/8/8/8/PPPPPPPP/BQRBNNKR w KQkq - 0 1",
    "nqbbnrkr/pppppppp/8/8/8/8/PPPPPPPP/NQBBNRKR w KQkq - 0 1",
    "brknnrqb/pppppppp/8/8/8/8/PPPPPPPP/BRKNNRQB w KQkq - 0 1",
    "rbbqkrnn/pppppppp/8/8/8/8/PPPPPPPP/RBBQKRNN w KQkq - 0 1",
    "rkqrbbnn/pppppppp/8/8/8/8/PPPPPPPP/RKQRBBNN w KQkq - 0 1",
    "rbknrnbq/pppppppp/8/8/8/8/PPPPPPPP/RBKNRNBQ w KQkq - 0 1",
    "bqrknbrn/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBRN w KQkq - 0 1",
    "rbkqnnbr/pppppppp/8/8/8/8/PPPPPPPP/RBKQNNBR w KQkq - 0 1",
    "rkbnnqrb/pppppppp/8/8/8/8/PPPPPPPP/RKBNNQRB w KQkq - 0 1",
    "rknrbqnb/pppppppp/8/8/8/8/PPPPPPPP/RKNRBQNB w KQkq - 0 1",
    "rnnkbrqb/pppppppp/8/8/8/8/PPPPPPPP/RNNKBRQB w KQkq - 0 1",
    "bbnrqkrn/pppppppp/8/8/8/8/PPPPPPPP/BBNRQKRN w KQkq - 0 1",
    "qbnnbrkr/pppppppp/8/8/8/8/PPPPPPPP/QBNNBRKR w KQkq - 0 1",
    "rknnbbqr/pppppppp/8/8/8/8/PPPPPPPP/RKNNBBQR w KQkq - 0 1",
    "rbqnkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBQNKRBN w KQkq - 0 1",
    "rqknbrnb/pppppppp/8/8/8/8/PPPPPPPP/RQKNBRNB w KQkq - 0 1",
    "rnkrqnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKRQNBB w KQkq - 0 1",
    "rbkqbnrn/pppppppp/8/8/8/8/PPPPPPPP/RBKQBNRN w KQkq - 0 1",
    "nrkrbnqb/pppppppp/8/8/8/8/PPPPPPPP/NRKRBNQB w KQkq - 0 1",
    "rkbbrnnq/pppppppp/8/8/8/8/PPPPPPPP/RKBBRNNQ w KQkq - 0 1",
    "rkbrnbqn/pppppppp/8/8/8/8/PPPPPPPP/RKBRNBQN w KQkq - 0 1",
    "bnrkrbnq/pppppppp/8/8/8/8/PPPPPPPP/BNRKRBNQ w KQkq - 0 1",
    "qrnnkbbr/pppppppp/8/8/8/8/PPPPPPPP/QRNNKBBR w KQkq - 0 1",
    "rkbqnnrb/pppppppp/8/8/8/8/PPPPPPPP/RKBQNNRB w KQkq - 0 1",
    "qrkbnrbn/pppppppp/8/8/8/8/PPPPPPPP/QRKBNRBN w KQkq - 0 1",
    "nqnbbrkr/pppppppp/8/8/8/8/PPPPPPPP/NQNBBRKR w KQkq - 0 1",
    "bqrbkrnn/pppppppp/8/8/8/8/PPPPPPPP/BQRBKRNN w KQkq - 0 1",
    "bqrnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNNKRB w KQkq - 0 1",
    "nqbbnrkr/pppppppp/8/8/8/8/PPPPPPPP/NQBBNRKR w KQkq - 0 1",
    "nrbnkbrq/pppppppp/8/8/8/8/PPPPPPPP/NRBNKBRQ w KQkq - 0 1",
    "rbqnkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBQNKRBN w KQkq - 0 1",
    "rqbnnbkr/pppppppp/8/8/8/8/PPPPPPPP/RQBNNBKR w KQkq - 0 1",
    "bnqbrknr/pppppppp/8/8/8/8/PPPPPPPP/BNQBRKNR w KQkq - 0 1",
    "brqnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BRQNNKRB w KQkq - 0 1",
    "qrbbnknr/pppppppp/8/8/8/8/PPPPPPPP/QRBBNKNR w KQkq - 0 1",
    "qrknbbrn/pppppppp/8/8/8/8/PPPPPPPP/QRKNBBRN w KQkq - 0 1",
    "bbrknqrn/pppppppp/8/8/8/8/PPPPPPPP/BBRKNQRN w KQkq - 0 1",
    "brqnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BRQNNKRB w KQkq - 0 1",
    "rkqbnrbn/pppppppp/8/8/8/8/PPPPPPPP/RKQBNRBN w KQkq - 0 1",
    "nbrkbrqn/pppppppp/8/8/8/8/PPPPPPPP/NBRKBRQN w KQkq - 0 1",
    "bbnrqnkr/pppppppp/8/8/8/8/PPPPPPPP/BBNRQNKR w KQkq - 0 1",
    "rqbnkrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBNKRNB w KQkq - 0 1",
    "rbbqnnkr/pppppppp/8/8/8/8/PPPPPPPP/RBBQNNKR w KQkq - 0 1",
    "nrkrbqnb/pppppppp/8/8/8/8/PPPPPPPP/NRKRBQNB w KQkq - 0 1",
    "qbbrkrnn/pppppppp/8/8/8/8/PPPPPPPP/QBBRKRNN w KQkq - 0 1",
    "qrknnbbr/pppppppp/8/8/8/8/PPPPPPPP/QRKNNBBR w KQkq - 0 1",
    "rqnbkrbn/pppppppp/8/8/8/8/PPPPPPPP/RQNBKRBN w KQkq - 0 1",
    "brqnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BRQNNKRB w KQkq - 0 1",
    "rnknbqrb/pppppppp/8/8/8/8/PPPPPPPP/RNKNBQRB w KQkq - 0 1",
    "nbrqkrbn/pppppppp/8/8/8/8/PPPPPPPP/NBRQKRBN w KQkq - 0 1",
    "rbqkbrnn/pppppppp/8/8/8/8/PPPPPPPP/RBQKBRNN w KQkq - 0 1",
    "nbrkbqnr/pppppppp/8/8/8/8/PPPPPPPP/NBRKBQNR w KQkq - 0 1",
    "rkqnbrnb/pppppppp/8/8/8/8/PPPPPPPP/RKQNBRNB w KQkq - 0 1",
    "qnbrknrb/pppppppp/8/8/8/8/PPPPPPPP/QNBRKNRB w KQkq - 0 1",
    "bnrbkrnq/pppppppp/8/8/8/8/PPPPPPPP/BNRBKRNQ w KQkq - 0 1",
    "rkrbqnbn/pppppppp/8/8/8/8/PPPPPPPP/RKRBQNBN w KQkq - 0 1",
    "rknbbnqr/pppppppp/8/8/8/8/PPPPPPPP/RKNBBNQR w KQkq - 0 1",
    "rqbkrbnn/pppppppp/8/8/8/8/PPPPPPPP/RQBKRBNN w KQkq - 0 1",
    "rnnbqkbr/pppppppp/8/8/8/8/PPPPPPPP/RNNBQKBR w KQkq - 0 1",
    "qrnnbkrb/pppppppp/8/8/8/8/PPPPPPPP/QRNNBKRB w KQkq - 0 1",
    "nqrkrnbb/pppppppp/8/8/8/8/PPPPPPPP/NQRKRNBB w KQkq - 0 1",
    "bbnrkqrn/pppppppp/8/8/8/8/PPPPPPPP/BBNRKQRN w KQkq - 0 1",
    "rbqnkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBQNKRBN w KQkq - 0 1",
    "qrnbkrbn/pppppppp/8/8/8/8/PPPPPPPP/QRNBKRBN w KQkq - 0 1",
    "brqknnrb/pppppppp/8/8/8/8/PPPPPPPP/BRQKNNRB w KQkq - 0 1",
    "nrnbbkrq/pppppppp/8/8/8/8/PPPPPPPP/NRNBBKRQ w KQkq - 0 1",
    "bbnrknqr/pppppppp/8/8/8/8/PPPPPPPP/BBNRKNQR w KQkq - 0 1",
    "bnrkrbqn/pppppppp/8/8/8/8/PPPPPPPP/BNRKRBQN w KQkq - 0 1",
    "qnbnrkrb/pppppppp/8/8/8/8/PPPPPPPP/QNBNRKRB w KQkq - 0 1",
    "rkbnnrqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNNRQB w KQkq - 0 1",
    "bqrbnkrn/pppppppp/8/8/8/8/PPPPPPPP/BQRBNKRN w KQkq - 0 1",
    "qrkbbnnr/pppppppp/8/8/8/8/PPPPPPPP/QRKBBNNR w KQkq - 0 1",
    "qrnknrbb/pppppppp/8/8/8/8/PPPPPPPP/QRNKNRBB w KQkq - 0 1",
    "nrqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/NRQKBRNB w KQkq - 0 1",
    "nrbbkrnq/pppppppp/8/8/8/8/PPPPPPPP/NRBBKRNQ w KQkq - 0 1",
    "brkbrnnq/pppppppp/8/8/8/8/PPPPPPPP/BRKBRNNQ w KQkq - 0 1",
    "nbqrknbr/pppppppp/8/8/8/8/PPPPPPPP/NBQRKNBR w KQkq - 0 1",
    "rbbnqnkr/pppppppp/8/8/8/8/PPPPPPPP/RBBNQNKR w KQkq - 0 1",
    "nrkbbrnq/pppppppp/8/8/8/8/PPPPPPPP/NRKBBRNQ w KQkq - 0 1",
    "qbnrkrbn/pppppppp/8/8/8/8/PPPPPPPP/QBNRKRBN w KQkq - 0 1",
    "nqrkbbrn/pppppppp/8/8/8/8/PPPPPPPP/NQRKBBRN w KQkq - 0 1",
    "rnbqknrb/pppppppp/8/8/8/8/PPPPPPPP/RNBQKNRB w KQkq - 0 1",
    "rnkbbqrn/pppppppp/8/8/8/8/PPPPPPPP/RNKBBQRN w KQkq - 0 1",
    "bqrbknrn/pppppppp/8/8/8/8/PPPPPPPP/BQRBKNRN w KQkq - 0 1",
    "bqrbnnkr/pppppppp/8/8/8/8/PPPPPPPP/BQRBNNKR w KQkq - 0 1",
    "nqbrkbnr/pppppppp/8/8/8/8/PPPPPPPP/NQBRKBNR w KQkq - 0 1",
    "rnkqnrbb/pppppppp/8/8/8/8/PPPPPPPP/RNKQNRBB w KQkq - 0 1",
    "brkqnbrn/pppppppp/8/8/8/8/PPPPPPPP/BRKQNBRN w KQkq - 0 1",
    "qrnbknbr/pppppppp/8/8/8/8/PPPPPPPP/QRNBKNBR w KQkq - 0 1",
    "nrqbbkrn/pppppppp/8/8/8/8/PPPPPPPP/NRQBBKRN w KQkq - 0 1",
    "bnnrkbqr/pppppppp/8/8/8/8/PPPPPPPP/BNNRKBQR w KQkq - 0 1",
    "nnqbbrkr/pppppppp/8/8/8/8/PPPPPPPP/NNQBBRKR w KQkq - 0 1",
    "bqnrnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQNRNKRB w KQkq - 0 1",
    "bbrknqnr/pppppppp/8/8/8/8/PPPPPPPP/BBRKNQNR w KQkq - 0 1",
    "brnkrbqn/pppppppp/8/8/8/8/PPPPPPPP/BRNKRBQN w KQkq - 0 1",
    "qrknbnrb/pppppppp/8/8/8/8/PPPPPPPP/QRKNBNRB w KQkq - 0 1",
    "rnkqrbbn/pppppppp/8/8/8/8/PPPPPPPP/RNKQRBBN w KQkq - 0 1",
    "rkbbqnnr/pppppppp/8/8/8/8/PPPPPPPP/RKBBQNNR w KQkq - 0 1",
    "qrnbnkbr/pppppppp/8/8/8/8/PPPPPPPP/QRNBNKBR w KQkq - 0 1",
    "rnqnkrbb/pppppppp/8/8/8/8/PPPPPPPP/RNQNKRBB w KQkq - 0 1",
    "nqbbnrkr/pppppppp/8/8/8/8/PPPPPPPP/NQBBNRKR w KQkq - 0 1",
    "rbbkrqnn/pppppppp/8/8/8/8/PPPPPPPP/RBBKRQNN w KQkq - 0 1",
    "rbbnqkrn/pppppppp/8/8/8/8/PPPPPPPP/RBBNQKRN w KQkq - 0 1",
    "brqbkrnn/pppppppp/8/8/8/8/PPPPPPPP/BRQBKRNN w KQkq - 0 1",
    "rbknrqbn/pppppppp/8/8/8/8/PPPPPPPP/RBKNRQBN w KQkq - 0 1",
    "rqbnknrb/pppppppp/8/8/8/8/PPPPPPPP/RQBNKNRB w KQkq - 0 1",
    "rkqbnnbr/pppppppp/8/8/8/8/PPPPPPPP/RKQBNNBR w KQkq - 0 1",
    "bqnrnbkr/pppppppp/8/8/8/8/PPPPPPPP/BQNRNBKR w KQkq - 0 1",
    "brqknbrn/pppppppp/8/8/8/8/PPPPPPPP/BRQKNBRN w KQkq - 0 1",
    "rbnqknbr/pppppppp/8/8/8/8/PPPPPPPP/RBNQKNBR w KQkq - 0 1",
    "rnnkbbrq/pppppppp/8/8/8/8/PPPPPPPP/RNNKBBRQ w KQkq - 0 1",
    "rbnkrnbq/pppppppp/8/8/8/8/PPPPPPPP/RBNKRNBQ w KQkq - 0 1",
    "bbqnrknr/pppppppp/8/8/8/8/PPPPPPPP/BBQNRKNR w KQkq - 0 1",
    "rbkrqnbn/pppppppp/8/8/8/8/PPPPPPPP/RBKRQNBN w KQkq - 0 1",
    "brnqkbnr/pppppppp/8/8/8/8/PPPPPPPP/BRNQKBNR w KQkq - 0 1",
    "rnbnkqrb/pppppppp/8/8/8/8/PPPPPPPP/RNBNKQRB w KQkq - 0 1",
    "qrbnkbrn/pppppppp/8/8/8/8/PPPPPPPP/QRBNKBRN w KQkq - 0 1",
    "nrqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/NRQKBRNB w KQkq - 0 1",
    "rbnnbkqr/pppppppp/8/8/8/8/PPPPPPPP/RBNNBKQR w KQkq - 0 1",
    "nbrkqnbr/pppppppp/8/8/8/8/PPPPPPPP/NBRKQNBR w KQkq - 0 1",
    "rbqknnbr/pppppppp/8/8/8/8/PPPPPPPP/RBQKNNBR w KQkq - 0 1",
    "bnrbkrqn/pppppppp/8/8/8/8/PPPPPPPP/BNRBKRQN w KQkq - 0 1",
    "brkbrqnn/pppppppp/8/8/8/8/PPPPPPPP/BRKBRQNN w KQkq - 0 1",
    "brnbqnkr/pppppppp/8/8/8/8/PPPPPPPP/BRNBQNKR w KQkq - 0 1",
    "qbnrnkbr/pppppppp/8/8/8/8/PPPPPPPP/QBNRNKBR w KQkq - 0 1",
    "bnrbqkrn/pppppppp/8/8/8/8/PPPPPPPP/BNRBQKRN w KQkq - 0 1",
    "nbrkbrqn/pppppppp/8/8/8/8/PPPPPPPP/NBRKBRQN w KQkq - 0 1",
    "rnbknqrb/pppppppp/8/8/8/8/PPPPPPPP/RNBKNQRB w KQkq - 0 1",
    "qbbnrkrn/pppppppp/8/8/8/8/PPPPPPPP/QBBNRKRN w KQkq - 0 1",
    "qbrnbkrn/pppppppp/8/8/8/8/PPPPPPPP/QBRNBKRN w KQkq - 0 1",
    "rkbbnrnq/pppppppp/8/8/8/8/PPPPPPPP/RKBBNRNQ w KQkq - 0 1",
    "nrnkqbbr/pppppppp/8/8/8/8/PPPPPPPP/NRNKQBBR w KQkq - 0 1",
    "nqbrkbnr/pppppppp/8/8/8/8/PPPPPPPP/NQBRKBNR w KQkq - 0 1",
    "brnbqkrn/pppppppp/8/8/8/8/PPPPPPPP/BRNBQKRN w KQkq - 0 1",
    "rnkqrnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKQRNBB w KQkq - 0 1",
    "rkbnrbnq/pppppppp/8/8/8/8/PPPPPPPP/RKBNRBNQ w KQkq - 0 1",
    "rbkqbrnn/pppppppp/8/8/8/8/PPPPPPPP/RBKQBRNN w KQkq - 0 1",
    "brqnkrnb/pppppppp/8/8/8/8/PPPPPPPP/BRQNKRNB w KQkq - 0 1",
    "bqrnkbrn/pppppppp/8/8/8/8/PPPPPPPP/BQRNKBRN w KQkq - 0 1",
    "rqbnkrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBNKRNB w KQkq - 0 1",
    "rknbbnrq/pppppppp/8/8/8/8/PPPPPPPP/RKNBBNRQ w KQkq - 0 1",
    "qrknbrnb/pppppppp/8/8/8/8/PPPPPPPP/QRKNBRNB w KQkq - 0 1",
    "qbrknrbn/pppppppp/8/8/8/8/PPPPPPPP/QBRKNRBN w KQkq - 0 1",
    "nrqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/NRQKBRNB w KQkq - 0 1",
    "bqrnkbrn/pppppppp/8/8/8/8/PPPPPPPP/BQRNKBRN w KQkq - 0 1",
    "rkqbbnrn/pppppppp/8/8/8/8/PPPPPPPP/RKQBBNRN w KQkq - 0 1",
    "nrqbknbr/pppppppp/8/8/8/8/PPPPPPPP/NRQBKNBR w KQkq - 0 1",
    "rnnbbkrq/pppppppp/8/8/8/8/PPPPPPPP/RNNBBKRQ w KQkq - 0 1",
    "rkqrnbbn/pppppppp/8/8/8/8/PPPPPPPP/RKQRNBBN w KQkq - 0 1",
    "qrknbrnb/pppppppp/8/8/8/8/PPPPPPPP/QRKNBRNB w KQkq - 0 1",
    "nrbbkrqn/pppppppp/8/8/8/8/PPPPPPPP/NRBBKRQN w KQkq - 0 1",
    "rknbbrnq/pppppppp/8/8/8/8/PPPPPPPP/RKNBBRNQ w KQkq - 0 1",
    "rnqkrnbb/pppppppp/8/8/8/8/PPPPPPPP/RNQKRNBB w KQkq - 0 1",
    "rbnkbnrq/pppppppp/8/8/8/8/PPPPPPPP/RBNKBNRQ w KQkq - 0 1",
    "nrbknrqb/pppppppp/8/8/8/8/PPPPPPPP/NRBKNRQB w KQkq - 0 1",
    "qrnnbkrb/pppppppp/8/8/8/8/PPPPPPPP/QRNNBKRB w KQkq - 0 1",
    "nrkbbqnr/pppppppp/8/8/8/8/PPPPPPPP/NRKBBQNR w KQkq - 0 1",
    "rnkrqnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKRQNBB w KQkq - 0 1",
    "qnrnbkrb/pppppppp/8/8/8/8/PPPPPPPP/QNRNBKRB w KQkq - 0 1",
    "rqbkrnnb/pppppppp/8/8/8/8/PPPPPPPP/RQBKRNNB w KQkq - 0 1",
    "rnkbbqrn/pppppppp/8/8/8/8/PPPPPPPP/RNKBBQRN w KQkq - 0 1",
    "rkbnrbqn/pppppppp/8/8/8/8/PPPPPPPP/RKBNRBQN w KQkq - 0 1",
    "nnqrkbbr/pppppppp/8/8/8/8/PPPPPPPP/NNQRKBBR w KQkq - 0 1",
    "qbbnrnkr/pppppppp/8/8/8/8/PPPPPPPP/QBBNRNKR w KQkq - 0 1",
    "brnknqrb/pppppppp/8/8/8/8/PPPPPPPP/BRNKNQRB w KQkq - 0 1",
    "bbqnrnkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNRNKR w KQkq - 0 1",
    "rbqkbrnn/pppppppp/8/8/8/8/PPPPPPPP/RBQKBRNN w KQkq - 0 1",
    "rkbnqbrn/pppppppp/8/8/8/8/PPPPPPPP/RKBNQBRN w KQkq - 0 1",
    "bbnrkrqn/pppppppp/8/8/8/8/PPPPPPPP/BBNRKRQN w KQkq - 0 1",
    "rbnqbkrn/pppppppp/8/8/8/8/PPPPPPPP/RBNQBKRN w KQkq - 0 1",
    "brkrqnnb/pppppppp/8/8/8/8/PPPPPPPP/BRKRQNNB w KQkq - 0 1",
    "bnqbrkrn/pppppppp/8/8/8/8/PPPPPPPP/BNQBRKRN w KQkq - 0 1",
    "rknnqbbr/pppppppp/8/8/8/8/PPPPPPPP/RKNNQBBR w KQkq - 0 1",
    "nrkqbbnr/pppppppp/8/8/8/8/PPPPPPPP/NRKQBBNR w KQkq - 0 1",
    "bqrnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNNKRB w KQkq - 0 1",
    "qbbrknrn/pppppppp/8/8/8/8/PPPPPPPP/QBBRKNRN w KQkq - 0 1",
    "nqbrnbkr/pppppppp/8/8/8/8/PPPPPPPP/NQBRNBKR w KQkq - 0 1",
    "nbrnbkrq/pppppppp/8/8/8/8/PPPPPPPP/NBRNBKRQ w KQkq - 0 1",
    "nnbrkqrb/pppppppp/8/8/8/8/PPPPPPPP/NNBRKQRB w KQkq - 0 1",
    "brqnnbkr/pppppppp/8/8/8/8/PPPPPPPP/BRQNNBKR w KQkq - 0 1",
    "rbkrbnqn/pppppppp/8/8/8/8/PPPPPPPP/RBKRBNQN w KQkq - 0 1",
    "rbnkrqbn/pppppppp/8/8/8/8/PPPPPPPP/RBNKRQBN w KQkq - 0 1",
    "rbbknrnq/pppppppp/8/8/8/8/PPPPPPPP/RBBKNRNQ w KQkq - 0 1",
    "qbbnrkrn/pppppppp/8/8/8/8/PPPPPPPP/QBBNRKRN w KQkq - 0 1",
    "rbbnnqkr/pppppppp/8/8/8/8/PPPPPPPP/RBBNNQKR w KQkq - 0 1",
    "brkbnnrq/pppppppp/8/8/8/8/PPPPPPPP/BRKBNNRQ w KQkq - 0 1",
    "rqknbrnb/pppppppp/8/8/8/8/PPPPPPPP/RQKNBRNB w KQkq - 0 1",
    "nbqrkrbn/pppppppp/8/8/8/8/PPPPPPPP/NBQRKRBN w KQkq - 0 1",
    "qbrkbnrn/pppppppp/8/8/8/8/PPPPPPPP/QBRKBNRN w KQkq - 0 1",
    "bnrknbqr/pppppppp/8/8/8/8/PPPPPPPP/BNRKNBQR w KQkq - 0 1",
    "rnqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/RNQKBRNB w KQkq - 0 1",
    "rkrqnnbb/pppppppp/8/8/8/8/PPPPPPPP/RKRQNNBB w KQkq - 0 1",
    "bqrkrnnb/pppppppp/8/8/8/8/PPPPPPPP/BQRKRNNB w KQkq - 0 1",
    "rknqnrbb/pppppppp/8/8/8/8/PPPPPPPP/RKNQNRBB w KQkq - 0 1",
    "rbqnknbr/pppppppp/8/8/8/8/PPPPPPPP/RBQNKNBR w KQkq - 0 1",
    "bnrbqkrn/pppppppp/8/8/8/8/PPPPPPPP/BNRBQKRN w KQkq - 0 1",
    "bbrqkrnn/pppppppp/8/8/8/8/PPPPPPPP/BBRQKRNN w KQkq - 0 1",
    "bbrqnknr/pppppppp/8/8/8/8/PPPPPPPP/BBRQNKNR w KQkq - 0 1",
    "bbrnnkrq/pppppppp/8/8/8/8/PPPPPPPP/BBRNNKRQ w KQkq - 0 1",
    "bqrnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNNKRB w KQkq - 0 1",
    "bqnbrnkr/pppppppp/8/8/8/8/PPPPPPPP/BQNBRNKR w KQkq - 0 1",
    "bbnqnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBNQNRKR w KQkq - 0 1",
    "bbrkqrnn/pppppppp/8/8/8/8/PPPPPPPP/BBRKQRNN w KQkq - 0 1",
    "nrkqbnrb/pppppppp/8/8/8/8/PPPPPPPP/NRKQBNRB w KQkq - 0 1",
    "nqrkbnrb/pppppppp/8/8/8/8/PPPPPPPP/NQRKBNRB w KQkq - 0 1",
    "bbrknqrn/pppppppp/8/8/8/8/PPPPPPPP/BBRKNQRN w KQkq - 0 1",
    "rkrnnbbq/pppppppp/8/8/8/8/PPPPPPPP/RKRNNBBQ w KQkq - 0 1",
    "rkbnqbrn/pppppppp/8/8/8/8/PPPPPPPP/RKBNQBRN w KQkq - 0 1",
    "nbqrbnkr/pppppppp/8/8/8/8/PPPPPPPP/NBQRBNKR w KQkq - 0 1",
    "rbknqnbr/pppppppp/8/8/8/8/PPPPPPPP/RBKNQNBR w KQkq - 0 1",
    "qrbknrnb/pppppppp/8/8/8/8/PPPPPPPP/QRBKNRNB w KQkq - 0 1",
    "qnrnbkrb/pppppppp/8/8/8/8/PPPPPPPP/QNRNBKRB w KQkq - 0 1",
    "bqrnnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNNKRB w KQkq - 0 1",
    "nrbnkqrb/pppppppp/8/8/8/8/PPPPPPPP/NRBNKQRB w KQkq - 0 1",
    "rnkbbqrn/pppppppp/8/8/8/8/PPPPPPPP/RNKBBQRN w KQkq - 0 1",
    "rnknrqbb/pppppppp/8/8/8/8/PPPPPPPP/RNKNRQBB w KQkq - 0 1",
    "rnbkqnrb/pppppppp/8/8/8/8/PPPPPPPP/RNBKQNRB w KQkq - 0 1",
    "rkbnrnqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNRNQB w KQkq - 0 1",
    "qnnrbbkr/pppppppp/8/8/8/8/PPPPPPPP/QNNRBBKR w KQkq - 0 1",
    "brnkrnqb/pppppppp/8/8/8/8/PPPPPPPP/BRNKRNQB w KQkq - 0 1",
    "rkrnnqbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNNQBB w KQkq - 0 1",
    "nrknrbbq/pppppppp/8/8/8/8/PPPPPPPP/NRKNRBBQ w KQkq - 0 1",
    "nrqbbknr/pppppppp/8/8/8/8/PPPPPPPP/NRQBBKNR w KQkq - 0 1",
    "nnrbbkrq/pppppppp/8/8/8/8/PPPPPPPP/NNRBBKRQ w KQkq - 0 1",
    "brkqnrnb/pppppppp/8/8/8/8/PPPPPPPP/BRKQNRNB w KQkq - 0 1",
    "nbbnqrkr/pppppppp/8/8/8/8/PPPPPPPP/NBBNQRKR w KQkq - 0 1",
    "nnrqkbbr/pppppppp/8/8/8/8/PPPPPPPP/NNRQKBBR w KQkq - 0 1",
    "rbbknrqn/pppppppp/8/8/8/8/PPPPPPPP/RBBKNRQN w KQkq - 0 1",
    "nrnkbbqr/pppppppp/8/8/8/8/PPPPPPPP/NRNKBBQR w KQkq - 0 1",
    "rnbbqkrn/pppppppp/8/8/8/8/PPPPPPPP/RNBBQKRN w KQkq - 0 1",
    "nqbbnrkr/pppppppp/8/8/8/8/PPPPPPPP/NQBBNRKR w KQkq - 0 1",
    "rbbknnrq/pppppppp/8/8/8/8/PPPPPPPP/RBBKNNRQ w KQkq - 0 1",
    "nbrkqrbn/pppppppp/8/8/8/8/PPPPPPPP/NBRKQRBN w KQkq - 0 1",
    "bnnqrkrb/pppppppp/8/8/8/8/PPPPPPPP/BNNQRKRB w KQkq - 0 1",
    "nbqrbnkr/pppppppp/8/8/8/8/PPPPPPPP/NBQRBNKR w KQkq - 0 1",
    "rkrbnnbq/pppppppp/8/8/8/8/PPPPPPPP/RKRBNNBQ w KQkq - 0 1",
    "nrqbnkbr/pppppppp/8/8/8/8/PPPPPPPP/NRQBNKBR w KQkq - 0 1",
    "rknqbnrb/pppppppp/8/8/8/8/PPPPPPPP/RKNQBNRB w KQkq - 0 1",
    "rkbnnrqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNNRQB w KQkq - 0 1",
    "rkbrnqnb/pppppppp/8/8/8/8/PPPPPPPP/RKBRNQNB w KQkq - 0 1",
    "bbrkrqnn/pppppppp/8/8/8/8/PPPPPPPP/BBRKRQNN w KQkq - 0 1",
    "qbbnnrkr/pppppppp/8/8/8/8/PPPPPPPP/QBBNNRKR w KQkq - 0 1",
    "qrbknrnb/pppppppp/8/8/8/8/PPPPPPPP/QRBKNRNB w KQkq - 0 1",
    "bbrknrqn/pppppppp/8/8/8/8/PPPPPPPP/BBRKNRQN w KQkq - 0 1",
    "rkrnqnbb/pppppppp/8/8/8/8/PPPPPPPP/RKRNQNBB w KQkq - 0 1",
    "brkrqbnn/pppppppp/8/8/8/8/PPPPPPPP/BRKRQBNN w KQkq - 0 1",
    "rkqnbbrn/pppppppp/8/8/8/8/PPPPPPPP/RKQNBBRN w KQkq - 0 1",
    "nrkbbnqr/pppppppp/8/8/8/8/PPPPPPPP/NRKBBNQR w KQkq - 0 1",
    "bnqrkrnb/pppppppp/8/8/8/8/PPPPPPPP/BNQRKRNB w KQkq - 0 1",
    "bqrbkrnn/pppppppp/8/8/8/8/PPPPPPPP/BQRBKRNN w KQkq - 0 1",
    "rknnrbbq/pppppppp/8/8/8/8/PPPPPPPP/RKNNRBBQ w KQkq - 0 1",
    "rbqkbrnn/pppppppp/8/8/8/8/PPPPPPPP/RBQKBRNN w KQkq - 0 1",
    "rbnqkrbn/pppppppp/8/8/8/8/PPPPPPPP/RBNQKRBN w KQkq - 0 1",
    "nrnkrbbq/pppppppp/8/8/8/8/PPPPPPPP/NRNKRBBQ w KQkq - 0 1",
    "rkbnnrqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNNRQB w KQkq - 0 1",
    "nrkqnrbb/pppppppp/8/8/8/8/PPPPPPPP/NRKQNRBB w KQkq - 0 1",
    "rqnbnkbr/pppppppp/8/8/8/8/PPPPPPPP/RQNBNKBR w KQkq - 0 1",
    "bnrbqknr/pppppppp/8/8/8/8/PPPPPPPP/BNRBQKNR w KQkq - 0 1",
    "bqrknbnr/pppppppp/8/8/8/8/PPPPPPPP/BQRKNBNR w KQkq - 0 1",
    "bnrkrqnb/pppppppp/8/8/8/8/PPPPPPPP/BNRKRQNB w KQkq - 0 1",
    "nbbqnrkr/pppppppp/8/8/8/8/PPPPPPPP/NBBQNRKR w KQkq - 0 1",
    "brkbnrqn/pppppppp/8/8/8/8/PPPPPPPP/BRKBNRQN w KQkq - 0 1",
    "nbrkbrnq/pppppppp/8/8/8/8/PPPPPPPP/NBRKBRNQ w KQkq - 0 1",
    "brnqkbnr/pppppppp/8/8/8/8/PPPPPPPP/BRNQKBNR w KQkq - 0 1",
    "bbrqknnr/pppppppp/8/8/8/8/PPPPPPPP/BBRQKNNR w KQkq - 0 1",
    "nnrkqrbb/pppppppp/8/8/8/8/PPPPPPPP/NNRKQRBB w KQkq - 0 1",
    "nrkbbqrn/pppppppp/8/8/8/8/PPPPPPPP/NRKBBQRN w KQkq - 0 1",
    "rqnbbknr/pppppppp/8/8/8/8/PPPPPPPP/RQNBBKNR w KQkq - 0 1",
    "rkbqnnrb/pppppppp/8/8/8/8/PPPPPPPP/RKBQNNRB w KQkq - 0 1",
    "bnrkrnqb/pppppppp/8/8/8/8/PPPPPPPP/BNRKRNQB w KQkq - 0 1",
    "nrnkbbqr/pppppppp/8/8/8/8/PPPPPPPP/NRNKBBQR w KQkq - 0 1",
    "qnrnbbkr/pppppppp/8/8/8/8/PPPPPPPP/QNRNBBKR w KQkq - 0 1",
    "brqkrbnn/pppppppp/8/8/8/8/PPPPPPPP/BRQKRBNN w KQkq - 0 1",
    "nbbrkqrn/pppppppp/8/8/8/8/PPPPPPPP/NBBRKQRN w KQkq - 0 1",
    "rqnbnkbr/pppppppp/8/8/8/8/PPPPPPPP/RQNBNKBR w KQkq - 0 1",
    "rqnkbrnb/pppppppp/8/8/8/8/PPPPPPPP/RQNKBRNB w KQkq - 0 1",
    "rknrbnqb/pppppppp/8/8/8/8/PPPPPPPP/RKNRBNQB w KQkq - 0 1",
    "qnrbbkrn/pppppppp/8/8/8/8/PPPPPPPP/QNRBBKRN w KQkq - 0 1",
    "nqbbnrkr/pppppppp/8/8/8/8/PPPPPPPP/NQBBNRKR w KQkq - 0 1",
    "bqnbnrkr/pppppppp/8/8/8/8/PPPPPPPP/BQNBNRKR w KQkq - 0 1",
    "nbrnkqbr/pppppppp/8/8/8/8/PPPPPPPP/NBRNKQBR w KQkq - 0 1",
    "rqnnkrbb/pppppppp/8/8/8/8/PPPPPPPP/RQNNKRBB w KQkq - 0 1",
    "nbbqrkrn/pppppppp/8/8/8/8/PPPPPPPP/NBBQRKRN w KQkq - 0 1",
    "nqrnkbbr/pppppppp/8/8/8/8/PPPPPPPP/NQRNKBBR w KQkq - 0 1",
    "rknnqbbr/pppppppp/8/8/8/8/PPPPPPPP/RKNNQBBR w KQkq - 0 1",
    "rbbqknnr/pppppppp/8/8/8/8/PPPPPPPP/RBBQKNNR w KQkq - 0 1",
    "rknbbqnr/pppppppp/8/8/8/8/PPPPPPPP/RKNBBQNR w KQkq - 0 1",
    "rqbnnkrb/pppppppp/8/8/8/8/PPPPPPPP/RQBNNKRB w KQkq - 0 1",
    "bbnrnqkr/pppppppp/8/8/8/8/PPPPPPPP/BBNRNQKR w KQkq - 0 1",
    "rbbnnkrq/pppppppp/8/8/8/8/PPPPPPPP/RBBNNKRQ w KQkq - 0 1",
    "nbqrknbr/pppppppp/8/8/8/8/PPPPPPPP/NBQRKNBR w KQkq - 0 1",
    "rbknbqnr/pppppppp/8/8/8/8/PPPPPPPP/RBKNBQNR w KQkq - 0 1",
    "rknbqnbr/pppppppp/8/8/8/8/PPPPPPPP/RKNBQNBR w KQkq - 0 1",
    "bbrknrqn/pppppppp/8/8/8/8/PPPPPPPP/BBRKNRQN w KQkq - 0 1",
    "rkqrnnbb/pppppppp/8/8/8/8/PPPPPPPP/RKQRNNBB w KQkq - 0 1",
    "rbbkrnqn/pppppppp/8/8/8/8/PPPPPPPP/RBBKRNQN w KQkq - 0 1",
    "brqnknrb/pppppppp/8/8/8/8/PPPPPPPP/BRQNKNRB w KQkq - 0 1",
    "bqnrnkrb/pppppppp/8/8/8/8/PPPPPPPP/BQNRNKRB w KQkq - 0 1",
    "brqknbrn/pppppppp/8/8/8/8/PPPPPPPP/BRQKNBRN w KQkq - 0 1",
    "bnrbkrqn/pppppppp/8/8/8/8/PPPPPPPP/BNRBKRQN w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "bnrbkrqn/pppppppp/8/8/8/8/PPPPPPPP/BNRBKRQN w KQkq - 0 1",
    "brkrnqnb/pppppppp/8/8/8/8/PPPPPPPP/BRKRNQNB w KQkq - 0 1",
    "rkrnbbqn/pppppppp/8/8/8/8/PPPPPPPP/RKRNBBQN w KQkq - 0 1",
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "bnqrknrb/pppppppp/8/8/8/8/PPPPPPPP/BNQRKNRB w KQkq - 0 1",
    "rnkrqnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKRQNBB w KQkq - 0 1",
    "brknnbrq/pppppppp/8/8/8/8/PPPPPPPP/BRKNNBRQ w KQkq - 0 1",
    "qnbrnbkr/pppppppp/8/8/8/8/PPPPPPPP/QNBRNBKR w KQkq - 0 1",
    "nrqbnkbr/pppppppp/8/8/8/8/PPPPPPPP/NRQBNKBR w KQkq - 0 1",
    "rnbnqkrb/pppppppp/8/8/8/8/PPPPPPPP/RNBNQKRB w KQkq - 0 1",
    "qrkrnnbb/pppppppp/8/8/8/8/PPPPPPPP/QRKRNNBB w KQkq - 0 1",
    "nbrnbkqr/pppppppp/8/8/8/8/PPPPPPPP/NBRNBKQR w KQkq - 0 1",
    "nrkbbqnr/pppppppp/8/8/8/8/PPPPPPPP/NRKBBQNR w KQkq - 0 1",
    "qrnkrnbb/pppppppp/8/8/8/8/PPPPPPPP/QRNKRNBB w KQkq - 0 1",
    "rnnbbkrq/pppppppp/8/8/8/8/PPPPPPPP/RNNBBKRQ w KQkq - 0 1",
    "rbbkqrnn/pppppppp/8/8/8/8/PPPPPPPP/RBBKQRNN w KQkq - 0 1",
    "rkrnbnqb/pppppppp/8/8/8/8/PPPPPPPP/RKRNBNQB w KQkq - 0 1",
    "rnnbkqbr/pppppppp/8/8/8/8/PPPPPPPP/RNNBKQBR w KQkq - 0 1",
    "nrqnkbbr/pppppppp/8/8/8/8/PPPPPPPP/NRQNKBBR w KQkq - 0 1",
    "brqnnbkr/pppppppp/8/8/8/8/PPPPPPPP/BRQNNBKR w KQkq - 0 1",
    "rqbbknnr/pppppppp/8/8/8/8/PPPPPPPP/RQBBKNNR w KQkq - 0 1",
    "rqkbrnbn/pppppppp/8/8/8/8/PPPPPPPP/RQKBRNBN w KQkq - 0 1",
    "brqkrbnn/pppppppp/8/8/8/8/PPPPPPPP/BRQKRBNN w KQkq - 0 1",
    "nbnrqkbr/pppppppp/8/8/8/8/PPPPPPPP/NBNRQKBR w KQkq - 0 1",
    "rknqbnrb/pppppppp/8/8/8/8/PPPPPPPP/RKNQBNRB w KQkq - 0 1",
    "bbnqrkrn/pppppppp/8/8/8/8/PPPPPPPP/BBNQRKRN w KQkq - 0 1",
    "bbrknnrq/pppppppp/8/8/8/8/PPPPPPPP/BBRKNNRQ w KQkq - 0 1",
    "rbqknnbr/pppppppp/8/8/8/8/PPPPPPPP/RBQKNNBR w KQkq - 0 1",
    "brknqbrn/pppppppp/8/8/8/8/PPPPPPPP/BRKNQBRN w KQkq - 0 1",
    "rbbqkrnn/pppppppp/8/8/8/8/PPPPPPPP/RBBQKRNN w KQkq - 0 1",
    "nrbkrbnq/pppppppp/8/8/8/8/PPPPPPPP/NRBKRBNQ w KQkq - 0 1",
    "rknbnqbr/pppppppp/8/8/8/8/PPPPPPPP/RKNBNQBR w KQkq - 0 1",
    "rkbbqrnn/pppppppp/8/8/8/8/PPPPPPPP/RKBBQRNN w KQkq - 0 1",
    "bnrbqknr/pppppppp/8/8/8/8/PPPPPPPP/BNRBQKNR w KQkq - 0 1",
    "bbnnrqkr/pppppppp/8/8/8/8/PPPPPPPP/BBNNRQKR w KQkq - 0 1",
    "brnbkrnq/pppppppp/8/8/8/8/PPPPPPPP/BRNBKRNQ w KQkq - 0 1",
    "rqbnnkrb/pppppppp/8/8/8/8/PPPPPPPP/RQBNNKRB w KQkq - 0 1",
    "nrbbkrqn/pppppppp/8/8/8/8/PPPPPPPP/NRBBKRQN w KQkq - 0 1",
    "qnrnbkrb/pppppppp/8/8/8/8/PPPPPPPP/QNRNBKRB w KQkq - 0 1",
    "brknqbnr/pppppppp/8/8/8/8/PPPPPPPP/BRKNQBNR w KQkq - 0 1",
    "rbkrnqbn/pppppppp/8/8/8/8/PPPPPPPP/RBKRNQBN w KQkq - 0 1",
    "qbnrbnkr/pppppppp/8/8/8/8/PPPPPPPP/QBNRBNKR w KQkq - 0 1",
    "brnkqbrn/pppppppp/8/8/8/8/PPPPPPPP/BRNKQBRN w KQkq - 0 1",
    "bbrnkrqn/pppppppp/8/8/8/8/PPPPPPPP/BBRNKRQN w KQkq - 0 1",
    "rknqbnrb/pppppppp/8/8/8/8/PPPPPPPP/RKNQBNRB w KQkq - 0 1",
    "brnkrbnq/pppppppp/8/8/8/8/PPPPPPPP/BRNKRBNQ w KQkq - 0 1",
    "bbrnknqr/pppppppp/8/8/8/8/PPPPPPPP/BBRNKNQR w KQkq - 0 1",
    "qrbnkrnb/pppppppp/8/8/8/8/PPPPPPPP/QRBNKRNB w KQkq - 0 1",
    "rqbbnnkr/pppppppp/8/8/8/8/PPPPPPPP/RQBBNNKR w KQkq - 0 1",
    "qrkbbnrn/pppppppp/8/8/8/8/PPPPPPPP/QRKBBNRN w KQkq - 0 1",
    "nnrkbqrb/pppppppp/8/8/8/8/PPPPPPPP/NNRKBQRB w KQkq - 0 1",
    "nrqbkrbn/pppppppp/8/8/8/8/PPPPPPPP/NRQBKRBN w KQkq - 0 1",
    "rkbnrqnb/pppppppp/8/8/8/8/PPPPPPPP/RKBNRQNB w KQkq - 0 1",
    "bbqrnknr/pppppppp/8/8/8/8/PPPPPPPP/BBQRNKNR w KQkq - 0 1",
    "rbknbqrn/pppppppp/8/8/8/8/PPPPPPPP/RBKNBQRN w KQkq - 0 1",
    "bnqbrkrn/pppppppp/8/8/8/8/PPPPPPPP/BNQBRKRN w KQkq - 0 1",
    "bnrknbqr/pppppppp/8/8/8/8/PPPPPPPP/BNRKNBQR w KQkq - 0 1",
    "nqbrknrb/pppppppp/8/8/8/8/PPPPPPPP/NQBRKNRB w KQkq - 0 1",
    "rbknrnbq/pppppppp/8/8/8/8/PPPPPPPP/RBKNRNBQ w KQkq - 0 1",
    "rkrbbnnq/pppppppp/8/8/8/8/PPPPPPPP/RKRBBNNQ w KQkq - 0 1",
    "rkbnrnqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNRNQB w KQkq - 0 1",
    "rkrnbnqb/pppppppp/8/8/8/8/PPPPPPPP/RKRNBNQB w KQkq - 0 1",
    "qnnrbkrb/pppppppp/8/8/8/8/PPPPPPPP/QNNRBKRB w KQkq - 0 1",
    "rbnqnkbr/pppppppp/8/8/8/8/PPPPPPPP/RBNQNKBR w KQkq - 0 1",
    "nrqknrbb/pppppppp/8/8/8/8/PPPPPPPP/NRQKNRBB w KQkq - 0 1",
    "nrbbkrnq/pppppppp/8/8/8/8/PPPPPPPP/NRBBKRNQ w KQkq - 0 1",
    "rbbkqrnn/pppppppp/8/8/8/8/PPPPPPPP/RBBKQRNN w KQkq - 0 1",
    "rbnkbrqn/pppppppp/8/8/8/8/PPPPPPPP/RBNKBRQN w KQkq - 0 1",
    "qrknnbbr/pppppppp/8/8/8/8/PPPPPPPP/QRKNNBBR w KQkq - 0 1",
    "qrnbbkrn/pppppppp/8/8/8/8/PPPPPPPP/QRNBBKRN w KQkq - 0 1",
    "bnrqkbrn/pppppppp/8/8/8/8/PPPPPPPP/BNRQKBRN w KQkq - 0 1",
    "rkbnrnqb/pppppppp/8/8/8/8/PPPPPPPP/RKBNRNQB w KQkq - 0 1",
    "brnknrqb/pppppppp/8/8/8/8/PPPPPPPP/BRNKNRQB w KQkq - 0 1",
    "rbqkrnbn/pppppppp/8/8/8/8/PPPPPPPP/RBQKRNBN w KQkq - 0 1",
    "qrnbknbr/pppppppp/8/8/8/8/PPPPPPPP/QRNBKNBR w KQkq - 0 1",
    "rnkrbbnq/pppppppp/8/8/8/8/PPPPPPPP/RNKRBBNQ w KQkq - 0 1",
    "rkqnrbbn/pppppppp/8/8/8/8/PPPPPPPP/RKQNRBBN w KQkq - 0 1",
    "qrnnbbkr/pppppppp/8/8/8/8/PPPPPPPP/QRNNBBKR w KQkq - 0 1",
    "brnqnkrb/pppppppp/8/8/8/8/PPPPPPPP/BRNQNKRB w KQkq - 0 1",
    "rbbkqrnn/pppppppp/8/8/8/8/PPPPPPPP/RBBKQRNN w KQkq - 0 1",
    "rbknbrnq/pppppppp/8/8/8/8/PPPPPPPP/RBKNBRNQ w KQkq - 0 1",
    "qnbbnrkr/pppppppp/8/8/8/8/PPPPPPPP/QNBBNRKR w KQkq - 0 1",
    "brqbknrn/pppppppp/8/8/8/8/PPPPPPPP/BRQBKNRN w KQkq - 0 1",
    "qbrnbkrn/pppppppp/8/8/8/8/PPPPPPPP/QBRNBKRN w KQkq - 0 1",
    "rknnrqbb/pppppppp/8/8/8/8/PPPPPPPP/RKNNRQBB w KQkq - 0 1",
    "rbknbrnq/pppppppp/8/8/8/8/PPPPPPPP/RBKNBRNQ w KQkq - 0 1",
    "nrbbkqnr/pppppppp/8/8/8/8/PPPPPPPP/NRBBKQNR w KQkq - 0 1",
    "rnbkqnrb/pppppppp/8/8/8/8/PPPPPPPP/RNBKQNRB w KQkq - 0 1",
    "nrkqbnrb/pppppppp/8/8/8/8/PPPPPPPP/NRKQBNRB w KQkq - 0 1",
    "qbbnrknr/pppppppp/8/8/8/8/PPPPPPPP/QBBNRKNR w KQkq - 0 1",
    "nnbqrkrb/pppppppp/8/8/8/8/PPPPPPPP/NNBQRKRB w KQkq - 0 1",
    "brnbkrnq/pppppppp/8/8/8/8/PPPPPPPP/BRNBKRNQ w KQkq - 0 1",
    "brnqknrb/pppppppp/8/8/8/8/PPPPPPPP/BRNQKNRB w KQkq - 0 1",
    "bnnrqbkr/pppppppp/8/8/8/8/PPPPPPPP/BNNRQBKR w KQkq - 0 1",
    "qnrnkbbr/pppppppp/8/8/8/8/PPPPPPPP/QNRNKBBR w KQkq - 0 1",
    "rknbbrqn/pppppppp/8/8/8/8/PPPPPPPP/RKNBBRQN w KQkq - 0 1",
    "rknbnqbr/pppppppp/8/8/8/8/PPPPPPPP/RKNBNQBR w KQkq - 0 1",
    "nrbknqrb/pppppppp/8/8/8/8/PPPPPPPP/NRBKNQRB w KQkq - 0 1",
    "nrkbbrnq/pppppppp/8/8/8/8/PPPPPPPP/NRKBBRNQ w KQkq - 0 1",
    "nnqbbrkr/pppppppp/8/8/8/8/PPPPPPPP/NNQBBRKR w KQkq - 0 1",
    "bqrbkrnn/pppppppp/8/8/8/8/PPPPPPPP/BQRBKRNN w KQkq - 0 1",
    "brnnkbrq/pppppppp/8/8/8/8/PPPPPPPP/BRNNKBRQ w KQkq - 0 1",
    "brqkrbnn/pppppppp/8/8/8/8/PPPPPPPP/BRQKRBNN w KQkq - 0 1",
    "bnrbnkqr/pppppppp/8/8/8/8/PPPPPPPP/BNRBNKQR w KQkq - 0 1",
    "rnkrqnbb/pppppppp/8/8/8/8/PPPPPPPP/RNKRQNBB w KQkq - 0 1",
    "rnbbkqrn/pppppppp/8/8/8/8/PPPPPPPP/RNBBKQRN w KQkq - 0 1",
    "rnbkrnqb/pppppppp/8/8/8/8/PPPPPPPP/RNBKRNQB w KQkq - 0 1",
    "nnbbrkrq/pppppppp/8/8/8/8/PPPPPPPP/NNBBRKRQ w KQkq - 0 1",
    "bnnbrkqr/pppppppp/8/8/8/8/PPPPPPPP/BNNBRKQR w KQkq - 0 1",
    "nrbqknrb/pppppppp/8/8/8/8/PPPPPPPP/NRBQKNRB w KQkq - 0 1",
    "nqrkbnrb/pppppppp/8/8/8/8/PPPPPPPP/NQRKBNRB w KQkq - 0 1",
    "rqnnbkrb/pppppppp/8/8/8/8/PPPPPPPP/RQNNBKRB w KQkq - 0 1",
    "rknqbbrn/pppppppp/8/8/8/8/PPPPPPPP/RKNQBBRN w KQkq - 0 1",
    "brkrnqnb/pppppppp/8/8/8/8/PPPPPPPP/BRKRNQNB w KQkq - 0 1",
    "rkbnnbrq/pppppppp/8/8/8/8/PPPPPPPP/RKBNNBRQ w KQkq - 0 1",
    "qnrbbkrn/pppppppp/8/8/8/8/PPPPPPPP/QNRBBKRN w KQkq - 0 1",
    "rbnkrqbn/pppppppp/8/8/8/8/PPPPPPPP/RBNKRQBN w KQkq - 0 1",
    "nrnbkqbr/pppppppp/8/8/8/8/PPPPPPPP/NRNBKQBR w KQkq - 0 1",
    "bbrknnrq/pppppppp/8/8/8/8/PPPPPPPP/BBRKNNRQ w KQkq - 0 1",
    "bbrnkqnr/pppppppp/8/8/8/8/PPPPPPPP/BBRNKQNR w KQkq - 0 1",
    "rkqbbrnn/pppppppp/8/8/8/8/PPPPPPPP/RKQBBRNN w KQkq - 0 1",
    "rnqkrbbn/pppppppp/8/8/8/8/PPPPPPPP/RNQKRBBN w KQkq - 0 1",
    "nnrbbkrq/pppppppp/8/8/8/8/PPPPPPPP/NNRBBKRQ w KQkq - 0 1",
    "nrnbbkrq/pppppppp/8/8/8/8/PPPPPPPP/NRNBBKRQ w KQkq - 0 1",
    "rkqbbrnn/pppppppp/8/8/8/8/PPPPPPPP/RKQBBRNN w KQkq - 0 1",
    "qnrkrnbb/pppppppp/8/8/8/8/PPPPPPPP/QNRKRNBB w KQkq - 0 1",
    "rbbnkrqn/pppppppp/8/8/8/8/PPPPPPPP/RBBNKRQN w KQkq - 0 1",
    "rqbknrnb/pppppppp/8/8/8/8/PPPPPPPP/RQBKNRNB w KQkq - 0 1",
    "nnbrkrqb/pppppppp/8/8/8/8/PPPPPPPP/NNBRKRQB w KQkq - 0 1",
    "qnrknrbb/pppppppp/8/8/8/8/PPPPPPPP/QNRKNRBB w KQkq - 0 1",
    "nrbbkqnr/pppppppp/8/8/8/8/PPPPPPPP/NRBBKQNR w KQkq - 0 1",
    "rnbnqbkr/pppppppp/8/8/8/8/PPPPPPPP/RNBNQBKR w KQkq - 0 1",
    "rbknbnrq/pppppppp/8/8/8/8/PPPPPPPP/RBKNBNRQ w KQkq - 0 1",
    "rbbknrqn/pppppppp/8/8/8/8/PPPPPPPP/RBBKNRQN w KQkq - 0 1",
    "bbqrknrn/pppppppp/8/8/8/8/PPPPPPPP/BBQRKNRN w KQkq - 0 1",
    "rnbkqrnb/pppppppp/8/8/8/8/PPPPPPPP/RNBKQRNB w KQkq - 0 1",
    "bnqrnkrb/pppppppp/8/8/8/8/PPPPPPPP/BNQRNKRB w KQkq - 0 1",
    "nqbbrkrn/pppppppp/8/8/8/8/PPPPPPPP/NQBBRKRN w KQkq - 0 1",
    "bnqnrkrb/pppppppp/8/8/8/8/PPPPPPPP/BNQNRKRB w KQkq - 0 1",
    "qbnnrkbr/pppppppp/8/8/8/8/PPPPPPPP/QBNNRKBR w KQkq - 0 1",
    "rqnkbnrb/pppppppp/8/8/8/8/PPPPPPPP/RQNKBNRB w KQkq - 0 1",
    "nqrnbkrb/pppppppp/8/8/8/8/PPPPPPPP/NQRNBKRB w KQkq - 0 1",
    "rbnqbknr/pppppppp/8/8/8/8/PPPPPPPP/RBNQBKNR w KQkq - 0 1",
    "nrbbkrqn/pppppppp/8/8/8/8/PPPPPPPP/NRBBKRQN w KQkq - 0 1",
    "rqbnkbrn/pppppppp/8/8/8/8/PPPPPPPP/RQBNKBRN w KQkq - 0 1",
    "qnrkbrnb/pppppppp/8/8/8/8/PPPPPPPP/QNRKBRNB w KQkq - 0 1",
    "nrnkqrbb/pppppppp/8/8/8/8/PPPPPPPP/NRNKQRBB w KQkq - 0 1",
    "rbnknqbr/pppppppp/8/8/8/8/PPPPPPPP/RBNKNQBR w KQkq - 0 1",
    "nrqkrnbb/pppppppp/8/8/8/8/PPPPPPPP/NRQKRNBB w KQkq - 0 1",
    "rqnkrnbb/pppppppp/8/8/8/8/PPPPPPPP/RQNKRNBB w KQkq - 0 1",
    "brkrnnqb/pppppppp/8/8/8/8/PPPPPPPP/BRKRNNQB w KQkq - 0 1",
    "nqrbnkbr/pppppppp/8/8/8/8/PPPPPPPP/NQRBNKBR w KQkq - 0 1",
    "qrnbbknr/pppppppp/8/8/8/8/PPPPPPPP/QRNBBKNR w KQkq - 0 1",
    "nqrbnkbr/pppppppp/8/8/8/8/PPPPPPPP/NQRBNKBR w KQkq - 0 1",
    "nqrnkrbb/pppppppp/8/8/8/8/PPPPPPPP/NQRNKRBB w KQkq - 0 1",
    "qbrnknbr/pppppppp/8/8/8/8/PPPPPPPP/QBRNKNBR w KQkq - 0 1",
    "rqkbnrbn/pppppppp/8/8/8/8/PPPPPPPP/RQKBNRBN w KQkq - 0 1",
    "rnbbnqkr/pppppppp/8/8/8/8/PPPPPPPP/RNBBNQKR w KQkq - 0 1",
    "rkbnqrnb/pppppppp/8/8/8/8/PPPPPPPP/RKBNQRNB w KQkq - 0 1",
    "qbrknnbr/pppppppp/8/8/8/8/PPPPPPPP/QBRKNNBR w KQkq - 0 1",
    "rkqnbnrb/pppppppp/8/8/8/8/PPPPPPPP/RKQNBNRB w KQkq - 0 1",
    "rkqrnbbn/pppppppp/8/8/8/8/PPPPPPPP/RKQRNBBN w KQkq - 0 1",
    "nrbnkrqb/pppppppp/8/8/8/8/PPPPPPPP/NRBNKRQB w KQkq - 0 1",
    "rkqnrnbb/pppppppp/8/8/8/8/PPPPPPPP/RKQNRNBB w KQkq - 0 1",
    "bqrnknrb/pppppppp/8/8/8/8/PPPPPPPP/BQRNKNRB w KQkq - 0 1",
    "brkbnqrn/pppppppp/8/8/8/8/PPPPPPPP/BRKBNQRN w KQkq - 0 1",
    "qrnkbbnr/pppppppp/8/8/8/8/PPPPPPPP/QRNKBBNR w KQkq - 0 1",
    "nrqnbbkr/pppppppp/8/8/8/8/PPPPPPPP/NRQNBBKR w KQkq - 0 1",
    "nrkrqbbn/pppppppp/8/8/8/8/PPPPPPPP/NRKRQBBN w KQkq - 0 1",
    "bnrbkqnr/pppppppp/8/8/8/8/PPPPPPPP/BNRBKQNR w KQkq - 0 1",
    "nrnkqbbr/pppppppp/8/8/8/8/PPPPPPPP/NRNKQBBR w KQkq - 0 1",
    "nrknbqrb/pppppppp/8/8/8/8/PPPPPPPP/NRKNBQRB w KQkq - 0 1",
    "nbqrkrbn/pppppppp/8/8/8/8/PPPPPPPP/NBQRKRBN w KQkq - 0 1",
    "qrnbnkbr/pppppppp/8/8/8/8/PPPPPPPP/QRNBNKBR w KQkq - 0 1",
    "rbbknqrn/pppppppp/8/8/8/8/PPPPPPPP/RBBKNQRN w KQkq - 0 1",
    "nqrbbknr/pppppppp/8/8/8/8/PPPPPPPP/NQRBBKNR w KQkq - 0 1",
    "nrknrbbq/pppppppp/8/8/8/8/PPPPPPPP/NRKNRBBQ w KQkq - 0 1",
    "bqnbrknr/pppppppp/8/8/8/8/PPPPPPPP/BQNBRKNR w KQkq - 0 1",
    "rnbbnkqr/pppppppp/8/8/8/8/PPPPPPPP/RNBBNKQR w KQkq - 0 1",
    "rknrqbbn/pppppppp/8/8/8/8/PPPPPPPP/RKNRQBBN w KQkq - 0 1",
    "rkrqbbnn/pppppppp/8/8/8/8/PPPPPPPP/RKRQBBNN w KQkq - 0 1",
    "nrkbrnbq/pppppppp/8/8/8/8/PPPPPPPP/NRKBRNBQ w KQkq - 0 1",
    "nbbrqknr/pppppppp/8/8/8/8/PPPPPPPP/NBBRQKNR w KQkq - 0 1",
    "qrnkbnrb/pppppppp/8/8/8/8/PPPPPPPP/QRNKBNRB w KQkq - 0 1",
    "brnnqbkr/pppppppp/8/8/8/8/PPPPPPPP/BRNNQBKR w KQkq - 0 1",
    "nrkbnrbq/pppppppp/8/8/8/8/PPPPPPPP/NRKBNRBQ w KQkq - 0 1",
    "rnkqbrnb/pppppppp/8/8/8/8/PPPPPPPP/RNKQBRNB w KQkq - 0 1",
    "rbnkbrnq/pppppppp/8/8/8/8/PPPPPPPP/RBNKBRNQ w KQkq - 0 1",
    "rbnkrqbn/pppppppp/8/8/8/8/PPPPPPPP/RBNKRQBN w KQkq - 0 1",
    "nbbnqrkr/pppppppp/8/8/8/8/PPPPPPPP/NBBNQRKR w KQkq - 0 1",
    "rkqbbnnr/pppppppp/8/8/8/8/PPPPPPPP/RKQBBNNR w KQkq - 0 1",
    "rkbbnrqn/pppppppp/8/8/8/8/PPPPPPPP/RKBBNRQN w KQkq - 0 1",
    "rkbbnqnr/pppppppp/8/8/8/8/PPPPPPPP/RKBBNQNR w KQkq - 0 1",
    "rnnqkrbb/pppppppp/8/8/8/8/PPPPPPPP/RNNQKRBB w KQkq - 0 1",
    "rknrnbbq/pppppppp/8/8/8/8/PPPPPPPP/RKNRNBBQ w KQkq - 0 1",
    "qbrnknbr/pppppppp/8/8/8/8/PPPPPPPP/QBRNKNBR w KQkq - 0 1",
    "rqnknrbb/pppppppp/8/8/8/8/PPPPPPPP/RQNKNRBB w KQkq - 0 1",
    "nqrbkrbn/pppppppp/8/8/8/8/PPPPPPPP/NQRBKRBN w KQkq - 0 1",
    "brknrbnq/pppppppp/8/8/8/8/PPPPPPPP/BRKNRBNQ w KQkq - 0 1",
    "qrnbnkbr/pppppppp/8/8/8/8/PPPPPPPP/QRNBNKBR w KQkq - 0 1",
    "rkrnbbnq/pppppppp/8/8/8/8/PPPPPPPP/RKRNBBNQ w KQkq - 0 1",
    "nrbqnkrb/pppppppp/8/8/8/8/PPPPPPPP/NRBQNKRB w KQkq - 0 1",
    "nrqkbrnb/pppppppp/8/8/8/8/PPPPPPPP/NRQKBRNB w KQkq - 0 1"
};

static uint8_t outer_rook(struct position *pos, int castle)
{
    uint8_t sq;
    uint8_t start;
    uint8_t stop;
    int     target;
    int     blocker;
    int     delta;

    if (castle == WHITE_KINGSIDE) {
        start = H1;
        stop = A1;
        delta = -1;
        target = WHITE_ROOK;
        blocker = WHITE_KING;
    } else if (castle == WHITE_QUEENSIDE) {
        start = A1;
        stop = H1;
        delta = 1;
        target = WHITE_ROOK;
        blocker = WHITE_KING;
    } else if (castle == BLACK_KINGSIDE) {
        start = H8;
        stop = A8;
        delta = -1;
        target = BLACK_ROOK;
        blocker = BLACK_KING;
    } else if (castle == BLACK_QUEENSIDE) {
        start = A8;
        stop = H8;
        delta = 1;
        target = BLACK_ROOK;
        blocker = BLACK_KING;
    } else {
        return NO_SQUARE;
    }

    for (sq=start;sq!=stop;sq+=delta) {
        if (pos->pieces[sq] == target) {
            return sq;
        } else if (pos->pieces[sq] == blocker) {
            return NO_SQUARE;
        }
    }

    return NO_SQUARE;
}

static void set_castle_from_file(struct position *pos, char file_char)
{
    int king_sq;
    int rook_sq;

    if ((file_char >= 'A') && (file_char <= 'H')) {
        rook_sq = SQUARE(file_char-'A', RANK_1);
        king_sq = LSB(pos->bb_pieces[WHITE_KING]);
        if (king_sq < rook_sq) {
            pos->castle |= WHITE_KINGSIDE;
            pos->castle_wk = rook_sq;
        } else if (king_sq > rook_sq) {
            pos->castle |= WHITE_QUEENSIDE;
            pos->castle_wq = rook_sq;
        }
    } else if ((file_char >= 'a') && (file_char <= 'h')) {
        rook_sq = SQUARE(file_char-'a', RANK_8);
        king_sq = LSB(pos->bb_pieces[BLACK_KING]);
        if (king_sq < rook_sq) {
            pos->castle |= BLACK_KINGSIDE;
            pos->castle_bk = rook_sq;
        } else if (king_sq > rook_sq) {
            pos->castle |= BLACK_QUEENSIDE;
            pos->castle_bq = rook_sq;
        }
    }
}

static int char2piece(char piece)
{
    switch (piece) {
    case 'K':
        return WHITE_KING;
    case 'Q':
        return WHITE_QUEEN;
    case 'R':
        return WHITE_ROOK;
    case 'B':
        return WHITE_BISHOP;
    case 'N':
        return WHITE_KNIGHT;
    case 'P':
        return WHITE_PAWN;
    case 'k':
        return BLACK_KING;
    case 'q':
        return BLACK_QUEEN;
    case 'r':
        return BLACK_ROOK;
    case 'b':
        return BLACK_BISHOP;
    case 'n':
        return BLACK_KNIGHT;
    case 'p':
        return BLACK_PAWN;
    default:
        return NO_PIECE;
    }
}

bool fen_setup_board(struct position *pos, char *fenstr)
{
    int  rank;
    int  file;
    char *iter;
    int  k;
    int  sq;

    assert(pos != NULL);
    assert(fenstr != NULL);

    /* Parse the piece placement field */
    iter = fenstr;
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        file = FILE_A;
        while (*iter != '/') {
            if (IS_DIGIT_08(*iter)) {
                /* Consequtive empty squares */
                file += *iter - '0';
            } else if (*iter == ' ') {
                /* End of piece placement field */
                break;
            } else if (IS_PIECE(*iter)) {
                /* Piece */
                pos->pieces[SQUARE(file, rank)] = char2piece(*iter);
                file++;
            }
            iter++;
        }
        iter++;
    }

    /* Update bitboards */
    for (sq=0;sq<NSQUARES;sq++) {
        if (pos->pieces[sq] != NO_PIECE) {
            SETBIT(pos->bb_pieces[pos->pieces[sq]], sq);
            SETBIT(pos->bb_sides[COLOR(pos->pieces[sq])], sq);
            SETBIT(pos->bb_all, sq);
        }
    }

    /* Active color field */
    switch (*iter) {
    case 'w':
        pos->stm = WHITE;
        break;
    case 'b':
        pos->stm = BLACK;
        break;
    default:
        return false;
    }
    iter++;
    if (*iter != ' ') {
        return false;
    }

    /* Castling availability field */
    iter++;
    pos->castle = 0;
    pos->castle_wk = NO_SQUARE;
    pos->castle_wq = NO_SQUARE;
    pos->castle_bk = NO_SQUARE;
    pos->castle_bq = NO_SQUARE;
    for (k=0;k<4;k++) {
        if (*iter == '-') {
            iter++;
            break;
        } else if (*iter == ' ') {
            break;
        }

        switch (*iter) {
        case 'K':
            pos->castle |= WHITE_KINGSIDE;
            pos->castle_wk = outer_rook(pos, WHITE_KINGSIDE);
            break;
        case 'Q':
            pos->castle |= WHITE_QUEENSIDE;
            pos->castle_wq = outer_rook(pos, WHITE_QUEENSIDE);
            break;
        case 'k':
            pos->castle |= BLACK_KINGSIDE;
            pos->castle_bk = outer_rook(pos, BLACK_KINGSIDE);
            break;
        case 'q':
            pos->castle |= BLACK_QUEENSIDE;
            pos->castle_bq = outer_rook(pos, BLACK_QUEENSIDE);
            break;
        /* FRC extension */
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
            set_castle_from_file(pos, *iter);
            break;
        default:
            return false;
        }
        iter++;
    }
    if (*iter != ' ') {
        return false;
    }
    if ((pos->castle < 0) || (pos->castle > 15)) {
        return false;
    }

    /* En-passant field */
    iter++;
    if (*iter == '-') {
        pos->ep_sq = NO_SQUARE;
        iter++;
    } else {
        file = *iter - 'a';
        rank = *(iter+1) - '1';
        pos->ep_sq = SQUARE(file, rank);
        iter += 2;
    }
    if ((*iter != ' ') && (*iter != '\0')) {
        return false;
    }
    if ((pos->castle < A1) || (pos->ep_sq > NO_SQUARE)) {
        return false;
    }

    /*
     * Allow the 'halfmove' and 'full move' fields to be
     * omitted in order to handle EPD strings.
     */
    iter = skip_whitespace(iter);
    if (*iter != '\0' && IS_DIGIT_09(*iter)) {
        /* Halfmove counter field */
        iter++;
        if (sscanf(iter, "%d", &pos->fifty) != 1) {
            return false;
        }
        while (IS_DIGIT_09(*iter)) {
            iter++;
        }
        if (*iter != ' ') {
            return false;
        }
        if (pos->fifty < 0) {
            return false;
        }

        /* Full move field */
        iter++;
        if (sscanf(iter, "%d", &pos->fullmove) != 1) {
            return false;
        }
        while (IS_DIGIT_09(*iter)) {
            iter++;
        }
        if (*iter != '\0') {
            return false;
        }
    } else {
        pos->fifty = 0;
        pos->fullmove = 1;
    }

    /* Generate a key for the position */
    pos->key = key_generate(pos);

    /* Initialize material counter */
    eval_init_material(pos);

    return true;
}

void fen_build_string(struct position *pos, char *fenstr)
{
    char *iter;
    int  empty_count;
    int  rank;
    int  file;
    int  sq;

    assert(valid_position(pos));

    /* Clear the string */
    memset(fenstr, 0, FEN_MAX_LENGTH);

    /* Piece placement */
    empty_count = 0;
    iter = fenstr;
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        for (file=FILE_A;file<=FILE_H;file++) {
            sq = SQUARE(file, rank);
            if (pos->pieces[sq] != NO_PIECE) {
                if (empty_count > 0) {
                    *(iter++) = '0' + empty_count;
                    empty_count = 0;
                }
                *(iter++) = piece2char[pos->pieces[sq]];
            } else {
                empty_count++;
            }
        }
        if (empty_count != 0) {
            *(iter++) = '0' + empty_count;
            empty_count = 0;
        }
        if (rank > 0) {
            *(iter++) = '/';
        }
    }
    *(iter++) = ' ';

    /* Active color */
    if (pos->stm == WHITE) {
        *(iter++) = 'w';
    } else {
        *(iter++) = 'b';
    }
    *(iter++) = ' ';

    /* Castling avliability */
    if (pos->castle == 0) {
        *(iter++) = '-';
    } else {
        if (pos->castle&WHITE_KINGSIDE) {
            *(iter++) = 'K';
        }
        if (pos->castle&WHITE_QUEENSIDE) {
            *(iter++) = 'Q';
        }
        if (pos->castle&BLACK_KINGSIDE) {
            *(iter++) = 'k';
        }
        if (pos->castle&BLACK_QUEENSIDE) {
            *(iter++) = 'q';
        }
    }
    *(iter++) = ' ';

    /* En passant target square */
    if (pos->ep_sq == NO_SQUARE) {
        *(iter++) = '-';
    } else {
        *(iter++) = 'a' + FILENR(pos->ep_sq);
        *(iter++) = '1' + RANKNR(pos->ep_sq);
    }
    *(iter++) = ' ';

    /* Halfmove clock */
    sprintf(iter, "%d", pos->fifty);
    iter += strlen(iter);
    *(iter++) = ' ';

    /* Fullmove number */
    sprintf(iter, "%d", pos->fullmove);
}

char* fen_get_frc_start_position(int id)
{
    assert(id >= 0);
    assert(id < 960);

    return frc_str[id];
}
