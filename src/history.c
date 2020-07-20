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
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "history.h"
#include "utils.h"
#include "validation.h"

/* The maximum allowed history score */
#define MAX_HISTORY_SCORE (INT_MAX/3)

/* The maximum depth used for history scores */
#define MAX_HISTORY_DEPTH 20

/* Constants for scaling history scores */
#define UP   32
#define DOWN 512

void history_clear_tables(struct search_worker *worker)
{
    memset(worker->history_table, 0, sizeof(int)*NPIECES*NSQUARES);
    memset(worker->counter_history, 0,
           sizeof(int)*NPIECES*NSQUARES*NPIECES*NSQUARES);
    memset(worker->follow_history, 0,
           sizeof(int)*NPIECES*NSQUARES*NPIECES*NSQUARES);
}

void history_update_tables(struct search_worker *worker, struct movelist *list,
                           int depth)
{
    uint32_t        best_move;
    uint32_t        move;
    int             to;
    int             piece;
    int             k;
    int             delta;
    int             score;
    uint32_t        move_c;
    uint32_t        move_f;
    int             prev_to;
    int             prev_piece;
    struct position *pos;

    assert(list != NULL);
    assert(list->size > 0);
    assert(depth > 0);

    pos = &worker->pos;

    /* Limit the depth used for bonus/penalty calculations */
    depth = MIN(depth, MAX_HISTORY_DEPTH);

    /* Get the opponents previous move */
    move_c = ((pos->ply >= 1) &&
             !ISNULLMOVE(pos->history[pos->ply-1].move))?
             pos->history[pos->ply-1].move:NOMOVE;

    /* Get our previous move */
    move_f = ((pos->ply >= 2) &&
             !ISNULLMOVE(pos->history[pos->ply-1].move) &&
             !ISNULLMOVE(pos->history[pos->ply-2].move))?
             pos->history[pos->ply-2].move:NOMOVE;

    /* Update history table */
    best_move = list->moves[list->size-1];
    for (k=0;k<list->size;k++) {
        move = list->moves[k];
        piece = pos->pieces[FROM(move)];
        to = TO(move);

        /* Calculate the bonus to apply */
        delta = (move != best_move)?-(depth*depth):depth*depth;

        /* Update history table */
        score = worker->history_table[piece][to];
        score = UP*delta - score*abs(delta)/DOWN;
        worker->history_table[piece][to] = MIN(score, MAX_HISTORY_SCORE);

        /* Update counter history table */
        if (move_c != NOMOVE) {
            prev_to = TO(move_c);
            prev_piece = pos->history[pos->ply-1].piece;
            score = worker->counter_history[prev_piece][prev_to][piece][to];
            score += UP*delta - score*abs(delta)/DOWN;
            worker->counter_history[prev_piece][prev_to][piece][to] =
                                                MIN(score, MAX_HISTORY_SCORE);
        }

        /* Update follow up history table */
        if (move_f != NOMOVE) {
            prev_to = TO(move_f);
            prev_piece = pos->history[pos->ply-2].piece;
            score = worker->follow_history[prev_piece][prev_to][piece][to];
            score += UP*delta - score*abs(delta)/DOWN;
            worker->follow_history[prev_piece][prev_to][piece][to] =
                                                MIN(score, MAX_HISTORY_SCORE);
        }
    }
}

int history_get_score(struct search_worker *worker, uint32_t move)
{
    struct position *pos;
    int             score;
    int             to;
    int             piece;
    int             prev_move;
    int             prev_to;
    int             prev_piece;

    assert(valid_move(move));

    pos = &worker->pos;
    piece = pos->pieces[FROM(move)];
    to = TO(move);

    /* Add score from history table */
    score = worker->history_table[piece][to];

    /* Add score from counter history table */
    prev_move = ((pos->ply >= 1) &&
                !ISNULLMOVE(pos->history[pos->ply-1].move))?
                pos->history[pos->ply-1].move:NOMOVE;
    if (prev_move != NOMOVE) {
        prev_to = TO(prev_move);
        prev_piece = pos->history[pos->ply-1].piece;
        score += worker->counter_history[prev_piece][prev_to][piece][to];
    }

    /* Add score from follow up history table */
    prev_move = ((pos->ply >= 2) &&
                !ISNULLMOVE(pos->history[pos->ply-1].move) &&
                !ISNULLMOVE(pos->history[pos->ply-2].move))?
                pos->history[pos->ply-2].move:NOMOVE;
    if (prev_move != NOMOVE) {
        prev_to = TO(prev_move);
        prev_piece = pos->history[pos->ply-2].piece;
        score += worker->follow_history[prev_piece][prev_to][piece][to];
    }

    return score;
}

void history_get_scores(struct search_worker *worker, uint32_t move,
                        int *hist, int *chist, int *fhist)
{
    struct position *pos;
    int             to;
    int             piece;
    int             prev_move;
    int             prev_to;
    int             prev_piece;

    assert(valid_move(move));

    pos = &worker->pos;
    piece = pos->pieces[FROM(move)];
    to = TO(move);

    /* Initialize history values */
    *hist = 0;
    *chist = 0;
    *fhist = 0;

    /* Add score from history table */
    *hist = worker->history_table[piece][to];

    /* Add score from counter history table */
    prev_move = ((pos->ply >= 1) &&
                !ISNULLMOVE(pos->history[pos->ply-1].move))?
                pos->history[pos->ply-1].move:NOMOVE;
    if (prev_move != NOMOVE) {
        prev_to = TO(prev_move);
        prev_piece = pos->history[pos->ply-1].piece;
        *chist = worker->counter_history[prev_piece][prev_to][piece][to];
    }

    /* Add score from follow up history table */
    prev_move = ((pos->ply >= 2) &&
                !ISNULLMOVE(pos->history[pos->ply-1].move) &&
                !ISNULLMOVE(pos->history[pos->ply-2].move))?
                pos->history[pos->ply-2].move:NOMOVE;
    if (prev_move != NOMOVE) {
        prev_to = TO(prev_move);
        prev_piece = pos->history[pos->ply-2].piece;
        *fhist = worker->follow_history[prev_piece][prev_to][piece][to];
    }
}

void killer_clear_table(struct search_worker *worker)
{
    int k;

    for (k=0;k<MAX_PLY;k++) {
        worker->killer_table[k] = NOMOVE;
    }
}

void killer_add_move(struct search_worker *worker, uint32_t move)
{
    struct position *pos = &worker->pos;

    worker->killer_table[pos->sply] = move;
}

uint32_t killer_get_move(struct search_worker *worker)
{
    assert(worker != NULL);

    return worker->killer_table[worker->pos.sply];
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

uint32_t counter_get_move(struct search_worker *worker)
{
    struct position *pos;
    uint32_t        prev_move;
    int             prev_to;

    assert(worker != NULL);

    pos = &worker->pos;
    if (pos->ply == 0) {
        return NOMOVE;
    }

    prev_move = pos->history[pos->ply-1].move;
    if (prev_move == NOMOVE || ISNULLMOVE(prev_move)) {
        return NOMOVE;
    }

    prev_to = TO(prev_move);
    return worker->countermove_table[pos->pieces[prev_to]][prev_to];
}
