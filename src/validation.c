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
#include <stdlib.h>

#include "validation.h"
#include "bitboard.h"
#include "key.h"
#include "movegen.h"
#include "board.h"
#include "eval.h"
#include "debug.h"

bool valid_position(struct position *pos)
{
    uint64_t white;
    uint64_t black;
    uint64_t all;
    int      pieces[NSQUARES];
    int      sq;
    int      k;

    if (pos == NULL) {
        return false;
    }

    /* Calculate a piece array based on the bitboards */
    for (sq=0;sq<NSQUARES;sq++) {
        if (ISBITSET(pos->bb_pieces[WHITE_PAWN], sq)) {
            pieces[sq] = WHITE_PAWN;
        } else if (ISBITSET(pos->bb_pieces[BLACK_PAWN], sq)) {
            pieces[sq] = BLACK_PAWN;
        } else if (ISBITSET(pos->bb_pieces[WHITE_KNIGHT], sq)) {
            pieces[sq] = WHITE_KNIGHT;
        } else if (ISBITSET(pos->bb_pieces[BLACK_KNIGHT], sq)) {
            pieces[sq] = BLACK_KNIGHT;
        } else if (ISBITSET(pos->bb_pieces[WHITE_BISHOP], sq)) {
            pieces[sq] = WHITE_BISHOP;
        } else if (ISBITSET(pos->bb_pieces[BLACK_BISHOP], sq)) {
            pieces[sq] = BLACK_BISHOP;
        } else if (ISBITSET(pos->bb_pieces[WHITE_ROOK], sq)) {
            pieces[sq] = WHITE_ROOK;
        } else if (ISBITSET(pos->bb_pieces[BLACK_ROOK], sq)) {
            pieces[sq] = BLACK_ROOK;
        } else if (ISBITSET(pos->bb_pieces[WHITE_QUEEN], sq)) {
            pieces[sq] = WHITE_QUEEN;
        } else if (ISBITSET(pos->bb_pieces[BLACK_QUEEN], sq)) {
            pieces[sq] = BLACK_QUEEN;
        } else if (ISBITSET(pos->bb_pieces[WHITE_KING], sq)) {
            pieces[sq] = WHITE_KING;
        } else if (ISBITSET(pos->bb_pieces[BLACK_KING], sq)) {
            pieces[sq] = BLACK_KING;
        } else {
            pieces[sq] = NO_PIECE;
        }
    }

    /* Validate piece array */
    for (sq=0;sq<NSQUARES;sq++) {
        if (pieces[sq] != pos->pieces[sq]) {
            return false;
        }
    }

    /* Calculate color bitboards based on piece bitboards */
    black = 0ULL;
    white = 0ULL;
    for (k=0;k<NPIECES;k++) {
        if (COLOR(k) == BLACK) {
            black |= pos->bb_pieces[k];
        } else {
            white |= pos->bb_pieces[k];
        }
    }
    all = white|black;

    /* Validate bitboards */
    if (white != pos->bb_sides[WHITE]) {
        return false;
    }
    if (black != pos->bb_sides[BLACK]) {
        return false;
    }
    if ((white&black) != 0) {
        return false;
    }
    if (all != pos->bb_all) {
        return false;
    }

    /* Validate en-passant target square */
    if ((pos->ep_sq < A1) || (pos->ep_sq > NO_SQUARE)) {
        return false;
    }

    /* Validate side to move */
    if ((pos->stm != WHITE) && (pos->stm != BLACK)) {
        return false;
    }

    /* Validate castling availability */
    if ((pos->castle < 0) || (pos->castle > 15)) {
        return false;
    }

    /* Validate ply counter */
    if ((pos->ply < 0) || (pos->ply > MAX_MOVES*2)) {
        return false;
    }

    /* Validate fifty-move-draw counter */
    if (pos->fifty < 0) {
        return false;
    }

    return true;
}

bool valid_square(int sq)
{
    return ((sq >= 0) && (sq < NSQUARES));
}

bool valid_side(int side)
{
    return ((side == WHITE) || (side == BLACK));
}

bool valid_piece(int piece)
{
    return ((piece >= WHITE_PAWN) && (piece < NO_PIECE));
}

bool valid_move(uint32_t move)
{
    int from;
    int to;
    int promotion;
    int type;

    from = FROM(move);
    to = TO(move);
    promotion = PROMOTION(move);
    type = TYPE(move);

    if (!valid_square(from)) {
        return false;
    }
    if (!valid_square(to)) {
        return false;
    }
    if (!valid_piece(promotion) && (promotion != NO_PIECE)) {
        return false;
    }
    if ((type < 0) || (type > 63)) {
        return false;
    }

    return true;
}

bool valid_scores(struct position *pos)
{
    return
        pos->material[MIDDLEGAME][WHITE] == eval_material(pos, WHITE, false) &&
        pos->material[MIDDLEGAME][BLACK] == eval_material(pos, BLACK, false) &&
        pos->material[ENDGAME][WHITE] == eval_material(pos, WHITE, true) &&
        pos->material[ENDGAME][BLACK] == eval_material(pos, BLACK, true) &&
        pos->psq[MIDDLEGAME][WHITE] == eval_psq(pos, WHITE, false) &&
        pos->psq[MIDDLEGAME][BLACK] == eval_psq(pos, BLACK, false) &&
        pos->psq[ENDGAME][WHITE] == eval_psq(pos, WHITE, true) &&
        pos->psq[ENDGAME][BLACK] == eval_psq(pos, BLACK, true);
}
