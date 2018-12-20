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
#include <string.h>
#include <stdatomic.h>

#include "smp.h"
#include "hash.h"
#include "config.h"
#include "timectl.h"
#include "movegen.h"
#include "search.h"
#include "engine.h"
#include "validation.h"
#include "tbprobe.h"
#include "polybook.h"
#include "bitboard.h"
#include "board.h"

/* Worker actions */
#define ACTION_IDLE 0
#define ACTION_EXIT 1
#define ACTION_RUN 2

/* Constants used for stoping workers */
#define ALL_WORKERS 0xFFFFFFFFFFFFFFFFULL
#define ABORT       0xFFFFFFFFFFFFFFFFULL

/* Lock for updating the state struct during search */
static mutex_t state_lock;

/* Variables used to signal to workers to stop searching */
static mutex_t stop_lock;
static atomic_uint_fast64_t stop_mask = 0ULL;

/* Data for worker threads */
static int number_of_workers = 0;
static struct search_worker workers[MAX_WORKERS];

static bool probe_dtz_tables(struct gamestate *state, int *score)
{
    unsigned int    res;
    int             wdl;
    int             promotion;
    int             flags;
    int             from;
    int             to;
    struct position *pos;

    pos = &state->pos;
    res = tb_probe_root(pos->bb_sides[WHITE], pos->bb_sides[BLACK],
                    pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING],
                    pos->bb_pieces[WHITE_QUEEN]|pos->bb_pieces[BLACK_QUEEN],
                    pos->bb_pieces[WHITE_ROOK]|pos->bb_pieces[BLACK_ROOK],
                    pos->bb_pieces[WHITE_BISHOP]|pos->bb_pieces[BLACK_BISHOP],
                    pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT],
                    pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN],
                    pos->fifty, pos->castle,
                    pos->ep_sq != NO_SQUARE?pos->ep_sq:0,
                    pos->stm == WHITE, NULL);
    if (res == TB_RESULT_FAILED) {
        return false;
    }
    wdl = TB_GET_WDL(res);
    switch (wdl) {
    case TB_LOSS:
        *score = -TABLEBASE_WIN;
        break;
    case TB_WIN:
        *score = TABLEBASE_WIN;
        break;
    case TB_BLESSED_LOSS:
    case TB_CURSED_WIN:
    case TB_DRAW:
    default:
        *score = 0;
        break;
    }

    from = TB_GET_FROM(res);
    to = TB_GET_TO(res);
    flags = NORMAL;
    promotion = NO_PIECE;
    if (TB_GET_EP(res) != 0) {
        flags = EN_PASSANT;
    } else {
        if (pos->pieces[to] != NO_PIECE) {
            flags |= CAPTURE;
        }
        switch (TB_GET_PROMOTES(res)) {
        case TB_PROMOTES_QUEEN:
            flags |= PROMOTION;
            promotion = QUEEN + pos->stm;
            break;
        case TB_PROMOTES_ROOK:
            flags |= PROMOTION;
            promotion = ROOK + pos->stm;
            break;
        case TB_PROMOTES_BISHOP:
            flags |= PROMOTION;
            promotion = BISHOP + pos->stm;
            break;
        case TB_PROMOTES_KNIGHT:
            flags |= PROMOTION;
            promotion = KNIGHT + pos->stm;
            break;
        case TB_PROMOTES_NONE:
        default:
            break;
        }
    }
    state->root_moves.moves[0] = MOVE(from, to, promotion, flags);
    state->root_moves.nmoves++;

    assert(board_is_move_pseudo_legal(&state->pos, state->root_moves.moves[0]));

    return true;
}

static thread_retval_t worker_thread_func(void *data)
{
    struct search_worker *worker = data;

    /* Worker main loop */
    while (true) {
        /* Wait for signal to start a searching */
        event_wait(&worker->ev_start);
        if (worker->action == ACTION_EXIT) {
            break;
        }
		if (worker->action == ACTION_IDLE) {
			continue;
		}

        search_find_best_move(worker);

        /* Signal that the worker is done searching */
        worker->action = ACTION_IDLE;
        event_set(&worker->ev_done);
    }

    /* Clean up */
#ifndef WINDOWS
    event_destroy(&worker->ev_start);
    event_destroy(&worker->ev_done);
    hash_pawntt_destroy_table(worker);
#endif

    return (thread_retval_t)0;
}

static void prepare_worker(struct search_worker *worker,
                           struct gamestate *state)
{
    int k;
    int l;

    /* Copy data from game state */
    worker->pos = state->pos;
    worker->root_moves = state->root_moves;

    /* Clear killer table */
    for (k=0;k<MAX_PLY;k++) {
        worker->killer_table[k][0] = NOMOVE;
        worker->killer_table[k][1] = NOMOVE;
    }

    /* Clear history table */
    for (k=0;k<NPIECES;k++) {
        for (l=0;l<NSQUARES;l++) {
            worker->history_table[k][l] = 0;
            worker->countermove_table[k][l] = NOMOVE;
        }
    }

    /* Clear statistics */
    worker->nodes = 0;
    worker->qnodes = 0;
    worker->depth = 0;
    worker->seldepth = 0;
    worker->currmovenumber = 0;
    worker->currmove = NOMOVE;

    /* Clear best move information */
    worker->ponder_move = NOMOVE;
    worker->best_move = worker->root_moves.moves[0];

    /* Initialize helper variables */
    worker->resolving_root_fail = false;
    worker->ppms[0].last_idx = 0;

    /* Setup parent pointers */
    worker->state = state;
    worker->pos.state = state;
    worker->pos.worker = worker;

    /* Worker is initiallly idle */
    worker->action = ACTION_IDLE;
}

void smp_init(void)
{
    mutex_init(&state_lock);
    mutex_init(&stop_lock);
}

void smp_destroy(void)
{
    mutex_destroy(&state_lock);
    mutex_destroy(&stop_lock);
}

void smp_create_workers(int nthreads)
{
    int k;

    number_of_workers = nthreads;
    for (k=0;k<number_of_workers;k++) {
        memset(&workers[k], 0, sizeof(struct search_worker));
        event_init(&workers[k].ev_start);
        event_init(&workers[k].ev_done);
        thread_create(&workers[k].thread, (thread_func_t)worker_thread_func, &workers[k]);

        hash_pawntt_create_table(&workers[k], PAWN_HASH_SIZE);
        workers[k].state = NULL;
        workers[k].id = k;
    }
}

void smp_destroy_workers(void)
{
    int k;

    for (k=0;k<number_of_workers;k++) {
        workers[k].action = ACTION_EXIT;
        event_set(&workers[k].ev_start);
    }
    for (k=0;k<number_of_workers;k++) {
        thread_join(&workers[k].thread);
    }
#ifdef WINDOWS
	for (k=0;k<number_of_workers;k++) {
		event_destroy(&workers[k].ev_start);
		event_destroy(&workers[k].ev_done);
		hash_pawntt_destroy_table(&workers[k]);
	}
#endif
    number_of_workers = 0;
}

void smp_search(struct gamestate *state, bool pondering, bool use_book,
                bool use_tablebases)
{
    int  k;
    bool analysis;

    assert(valid_position(&state->pos));

    /* Reset the best move information */
    state->best_move = NOMOVE;
    state->ponder_move = NOMOVE;

	/*
	 * Try to guess if the search is part of a
	 * game or if it is for analysis.
	 */
	analysis = tc_is_infinite() || (state->root_moves.nmoves > 0);

    /* Try to find a move in the opening book */
    if (use_book && state->in_book) {
        state->best_move = polybook_probe(&state->pos);
        if (state->best_move != NOMOVE) {
            return;
        }
        state->in_book = false;
    }

	/*
	 * Allocate time for the search. In pondering mode time
     * is allocated when the engine stops pondering and
     * enters the normal search.
	 */
	if (!pondering) {
		tc_allocate_time();
	}

    /* Prepare for search */
    hash_tt_age_table();
    state->probe_wdl = use_tablebases;
    state->root_in_tb = false;
    state->root_tb_score = 0;
    state->pondering = pondering;
    state->pos.sply = 0;
    state->completed_depth = 0;
    state->best_move_depth = 0;
    state->best_move_score = 0;

    /* Probe tablebases for the root position */
    if (use_tablebases &&
        (BITCOUNT(state->pos.bb_all) <= (int)TB_LARGEST)) {
        state->root_in_tb = probe_dtz_tables(state, &state->root_tb_score);
        state->probe_wdl = !state->root_in_tb;
    }

    /* If no root moves are specified then search all moves */
    if (state->root_moves.nmoves == 0) {
        gen_legal_moves(&state->pos, &state->root_moves);
        assert(state->root_moves.nmoves > 0);
    }

    /*
     * Initialize the best move to the first legal root
     * move to make sure a legal move is always returned.
     */
    state->best_move = state->root_moves.moves[0];

    /*
     * If there is only one legal move then there is no
     * need to do a search. Instead save the time for later.
     */
    if ((state->root_moves.nmoves == 1) && !state->pondering && !analysis &&
        !state->root_in_tb) {
        return;
    }

    /* Prepare workers for a new search */
    for (k=0;k<number_of_workers;k++) {
        prepare_worker(&workers[k], state);
    }

    /* Start all workers */
    stop_mask = 0ULL;
    for (k=0;k<number_of_workers;k++) {
        workers[k].action = ACTION_RUN;
        event_set(&workers[k].ev_start);
    }

    /* Wait for workers to finish */
    for (k=0;k<number_of_workers;k++) {
        event_wait(&workers[k].ev_done);
    }
}

uint32_t smp_nodes(void)
{
    uint32_t nodes;
    int      k;

    nodes = 0;
    for (k=0;k<number_of_workers;k++) {
        nodes += workers[k].nodes;
    }
    return nodes;
}

void smp_stop_all(struct search_worker *worker, bool abort)
{
    uint64_t mask;

    mutex_lock(&stop_lock);
    mask = ALL_WORKERS;
    if (!abort) {
        /*
         * Clear the bit for the current worker to indicate that this
         * is a "soft stop". This is ok since the current worker
         * already knows that it should stop.
         */
        mask &= (~(1ULL<<worker->id));
    }
    stop_mask = mask;
    mutex_unlock(&stop_lock);
}

bool smp_should_stop(struct search_worker *worker, bool *abort)
{
    uint64_t mask;

    mask = atomic_load_explicit(&stop_mask, memory_order_relaxed);
    *abort = (mask == ABORT);
    return (mask&(1ULL << worker->id)) != 0ULL;
}

void smp_update(struct search_worker *worker, int score)
{
    mutex_lock(&state_lock);

    /*
     * Update the best move if the depth is larger than the
     * depth for the current best move, or if the depth is
     * the same but the score is higher.
     */
    if ((worker->depth > worker->state->best_move_depth) ||
        ((worker->depth == worker->state->best_move_depth) &&
         (score > worker->state->best_move_score))) {
        /* Update best move information */
        worker->state->best_move = worker->best_move;
        worker->state->ponder_move = worker->ponder_move;
        worker->state->best_move_depth = worker->depth;
        worker->state->best_move_score = score;

        /* Send pv information */
        engine_send_pv_info(worker, &worker->pv_table[0], worker->depth,
                            worker->seldepth, score, smp_nodes());
    }

    mutex_unlock(&state_lock);
}

int smp_complete_iteration(struct search_worker *worker)
{
    int highest_depth;
    int new_depth;
    int count;
    int k;

    mutex_lock(&state_lock);

    /* Check if this the first time completing this depth */
    if (worker->depth > worker->state->completed_depth) {
        worker->state->completed_depth = worker->depth;
        hash_tt_insert_pv(&worker->pos, &worker->pv_table[0]);
    }

    /* Calculate the next depth for this worker to search */
    highest_depth = 0;
    count = 0;
    for (k=0;k<number_of_workers;k++) {
        if (workers[k].depth > highest_depth) {
            highest_depth = workers[k].depth;
            count = 1;
        } else if (workers[k].depth == highest_depth) {
            count++;
        }
    }
    new_depth = MAX(worker->depth+1, highest_depth);
    if ((new_depth == highest_depth) && (count >= (number_of_workers/2))) {
        new_depth++;
    }

    mutex_unlock(&state_lock);

    return new_depth;
}
