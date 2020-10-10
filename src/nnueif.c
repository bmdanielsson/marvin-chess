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
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nnueif.h"
#include "nnue.h"

bool nnueif_init(char *eval_file)
{
    return nnue_init(eval_file);
}

void nnueif_reset_pos(struct position *pos)
{
    memset(pos->stack, 0, sizeof(pos->stack));
    pos->st = &pos->stack[0];
}

void nnueif_make_move(struct position *pos, uint32_t move)
{
    int        from = FROM(move);
    int        to = TO(move);
    int        promotion = PROMOTION(move);
    int        capture = pos->pieces[to];
    int        piece = pos->pieces[from];
    Stack      *st;
    DirtyPiece *dp;

    assert(pos != NULL);

    st = ++pos->st;
    st->accumulator.computedAccumulation = false;
    dp = &(st->dirtyPiece);
    dp->dirtyNum = 1;

    if (ISKINGSIDECASTLE(move)) {
        dp->dirtyNum = 2;

        dp->pc[0] = KING + pos->stm;
        dp->from[0] = from;
        dp->to[0] = to;

        dp->pc[1] = ROOK + pos->stm;
        dp->from[1] = to + 1;
        dp->to[1] = to - 1;
    } else if (ISQUEENSIDECASTLE(move)) {
        dp->dirtyNum = 2;

        dp->pc[0] = KING + pos->stm;
        dp->from[0] = from;
        dp->to[0] = to;

        dp->pc[1] = ROOK + pos->stm;
        dp->from[1] = to - 2;
        dp->to[1] = to + 1;
    } else if (ISENPASSANT(move)) {
        dp->dirtyNum = 2;

        dp->pc[0] = piece;
        dp->from[0] = from;
        dp->to[0] = to;

        dp->pc[1] = PAWN + FLIP_COLOR(pos->stm);
        dp->from[1] = (pos->stm == WHITE)?to-8:to+8;
        dp->to[1] = NO_SQUARE;
    } else {
        dp->pc[0] = piece;
        dp->from[0] = from;
        dp->to[0] = to;

        if (ISCAPTURE(move)) {
            dp->dirtyNum = 2;
            dp->pc[1] = capture;
            dp->from[1] = to;
            dp->to[1] = NO_SQUARE;
        }
        if (ISPROMOTION(move)) {
            dp->to[0] = SQ_NONE;
            dp->pc[dp->dirtyNum] = promotion;
            dp->from[dp->dirtyNum] = NO_SQUARE;
            dp->to[dp->dirtyNum] = to;
            dp->dirtyNum++;
        }
    }
}

void nnueif_unmake_move(struct position *pos)
{
    pos->st--;
}

void nnueif_make_null_move(struct position *pos)
{
    Stack *st = ++pos->st;
    if ((st-1)->accumulator.computedAccumulation) {
        st->accumulator = (st-1)->accumulator;
    } else {
        st->accumulator.computedAccumulation = false;
    }
}

void nnueif_unmake_null_move(struct position *pos)
{
    pos->st--;
}

int nnueif_evaluate(struct position *pos)
{
    return (int)nnue_evaluate(pos);
}
