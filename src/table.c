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

#include "table.h"

void tbl_clear_history_table(struct search_worker *worker)
{
    int k;
    int l;

    for (k=0;k<NPIECES;k++) {
        for (l=0;l<NSQUARES;l++) {
            worker->history_table[k][l] = 0;
        }
    }
}

void tbl_update_history_table(struct search_worker *worker, uint32_t move,
                              int depth)
{
    int             from;
    int             to;
    int             piece;
    struct position *pos;

    if (ISCAPTURE(move) || ISENPASSANT(move)) {
        return;
    }

    /* Update the history table and rescale entries if necessary */
    pos = &worker->pos;
    from = FROM(move);
    to = TO(move);
    worker->history_table[pos->pieces[from]][to] += depth*depth;
    if (worker->history_table[pos->pieces[from]][to] > MAX_HISTORY_SCORE) {
        for (piece=0;piece<NPIECES;piece++) {
            for (to=0;to<NSQUARES;to++) {
                worker->history_table[piece][to] /= 2;
            }
        }
    }
}

void tbl_clear_killermove_table(struct search_worker *worker)
{
    int k;

    for (k=0;k<MAX_PLY;k++) {
        worker->killer_table[k][0] = NOMOVE;
        worker->killer_table[k][1] = NOMOVE;
    }
}

void tbl_add_killer_move(struct search_worker *worker, uint32_t move)
{
    struct position *pos = &worker->pos;

    if (move == worker->killer_table[pos->sply][0]) {
        return;
    }

    worker->killer_table[pos->sply][1] = worker->killer_table[pos->sply][0];
    worker->killer_table[pos->sply][0] = move;
}

bool tbl_is_killer_move(struct search_worker *worker, uint32_t move)
{
    struct position *pos;

    pos = &worker->pos;
    return (worker->killer_table[pos->sply][0] == move) ||
            (worker->killer_table[pos->sply][1] == move);
}


void tbl_clear_countermove_table(struct search_worker *worker)
{
    int k;
    int l;

    for (k=0;k<NPIECES;k++) {
        for (l=0;l<NSQUARES;l++) {
            worker->countermove_table[k][l] = NOMOVE;
        }
    }
}

void tbl_add_counter_move(struct search_worker *worker, uint32_t move)
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
