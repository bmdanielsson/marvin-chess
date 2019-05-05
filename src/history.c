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
#include <assert.h>
#include <stdlib.h>

#include "history.h"

void history_clear_table(struct search_worker *worker)
{
    int k;
    int l;

    for (k=0;k<NPIECES;k++) {
        for (l=0;l<NSQUARES;l++) {
            worker->history_table[k][l] = 0;
        }
    }
}

void history_update_table(struct search_worker *worker, struct movelist *list,
                          int depth)
{
    uint32_t        best_move;
    uint32_t        move;
    int             to;
    int             piece;
    int             k;
    bool            rescale;
    int             delta;
    struct position *pos;

    assert(list != NULL);
    assert(list->nmoves > 0);
    assert(depth > 0);

    /* Update history table */
    best_move = list->moves[list->nmoves-1];
    rescale = false;
    pos = &worker->pos;
    for (k=0;k<list->nmoves;k++) {
        move = list->moves[k];
        piece = pos->pieces[FROM(move)];
        to = TO(move);

        delta = (move != best_move)?-(depth*depth):depth*depth;
        worker->history_table[piece][to] += delta;
        if (abs(worker->history_table[piece][to]) > MAX_HISTORY_SCORE) {
            rescale = true;
        }
    }

    /* Rescale entries if necessary */
    if (rescale) {
        for (piece=0;piece<NPIECES;piece++) {
            for (to=0;to<NSQUARES;to++) {
                worker->history_table[piece][to] /= 2;
            }
        }
    }
}

void killer_clear_table(struct search_worker *worker)
{
    int k;

    for (k=0;k<MAX_PLY;k++) {
        worker->killer_table[k][0] = NOMOVE;
        worker->killer_table[k][1] = NOMOVE;
    }
}

void killer_add_move(struct search_worker *worker, uint32_t move)
{
    struct position *pos = &worker->pos;

    if (move == worker->killer_table[pos->sply][0]) {
        return;
    }

    worker->killer_table[pos->sply][1] = worker->killer_table[pos->sply][0];
    worker->killer_table[pos->sply][0] = move;
}

void counter_clear_table(struct search_worker *worker)
{
    int k;
    int l;

    for (k=0;k<NPIECES;k++) {
        for (l=0;l<NSQUARES;l++) {
            worker->countermove_table[k][l] = NOMOVE;
        }
    }
}

void counter_add_move(struct search_worker *worker, uint32_t move)
{
    struct position *pos;
    uint32_t prev_move;

    assert(worker->pos.sply > 0);

    pos = &worker->pos;
    prev_move = pos->history[pos->ply-1].move;
    if (ISNULLMOVE(prev_move)) {
        return;
    }

    worker->countermove_table[pos->pieces[TO(prev_move)]][TO(prev_move)] = move;
}
