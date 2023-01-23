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
#include <stdlib.h>

#include "smp.h"
#include "hash.h"
#include "config.h"
#include "timectl.h"
#include "movegen.h"
#include "search.h"
#include "engine.h"
#include "validation.h"
#include "polybook.h"
#include "bitboard.h"
#include "board.h"
#include "history.h"
#include "nnue.h"

/* Worker actions */
#define ACTION_IDLE 0
#define ACTION_EXIT 1
#define ACTION_RUN 2

/* Lock for updating the state struct during search */
static mutex_t state_lock;

/* Variables used to signal to workers to stop searching */
static mutex_t stop_lock;
static atomic_bool should_stop = false;

/* Data for worker threads */
static int number_of_workers = 0;
static struct search_worker *workers = NULL;

static thread_retval_t worker_thread_func(void *data)
{
    struct search_worker *worker = data;

    search_find_best_move(worker);

    return (thread_retval_t)0;
}

static void prepare_worker(struct search_worker *worker,
                           struct gamestate *state)
{
    int mpvidx;

    /* Copy data from game state */
    worker->pos = state->pos;

    /* Clear tables */
    killer_clear_table(worker);
    counter_clear_table(worker);

    /* Clear statistics */
    worker->nodes = 0;
    worker->qnodes = 0;
    worker->tbhits = 0ULL;

    /* Clear best move information */
    for (mpvidx=0;mpvidx<state->multipv;mpvidx++) {
        worker->mpv_moves[mpvidx] = NOMOVE;
        worker->mpv_lines[mpvidx].score = -INFINITE_SCORE;
        worker->mpv_lines[mpvidx].pv.size = 0;
        worker->mpv_lines[mpvidx].depth = 0;
        worker->mpv_lines[mpvidx].seldepth = 0;
    }

    /* Clear multipv information */
    worker->multipv = state->multipv;

    /* Initialize helper variables */
    worker->resolving_root_fail = false;

    /* Setup parent pointers */
    worker->state = state;
    worker->pos.state = state;
    worker->pos.worker = worker;

    /* Worker is initiallly idle */
    worker->action = ACTION_IDLE;
}

static void reset_worker(struct search_worker *worker)
{
    worker->state = NULL;
    worker->pos.state = NULL;
    worker->pos.worker = NULL;
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
    workers = aligned_malloc(64,
                             number_of_workers*sizeof(struct search_worker));
    for (k=0;k<number_of_workers;k++) {
        memset(&workers[k], 0, sizeof(struct search_worker));
        hash_nnue_create_table(&workers[k], NNUE_CACHE_SIZE);
        workers[k].state = NULL;
        workers[k].id = k;
    }
}

void smp_destroy_workers(void)
{
    int k;

    for (k=0;k<number_of_workers;k++) {
        hash_nnue_destroy_table(&workers[k]);
    }
    aligned_free(workers);
    workers = NULL;
    number_of_workers = 0;
}

int smp_number_of_workers(void)
{
    return number_of_workers;
}

void smp_newgame(void)
{
    int k;

    for (k=0;k<number_of_workers;k++) {
        history_clear_tables(&workers[k]);
    }
}

uint32_t smp_search(struct gamestate *state, bool pondering, bool use_book,
                    uint32_t *ponder_move)
{
    int                  k;
    struct search_worker *worker;
    struct pvinfo        *best_pv;
    struct movelist      legal;
    bool                 send_pv;
    uint32_t             best_move;
    uint32_t             move;

    assert(valid_position(&state->pos));
    assert(number_of_workers > 0);
    assert(workers != NULL);

    /* Reset the best move information */
    best_move = NOMOVE;
    if (ponder_move != NULL) {
        *ponder_move = NOMOVE;
    }

    /* Try to find a move in the opening book */
    if (use_book) {
        best_move = polybook_probe(&state->pos);
        if (best_move != NOMOVE) {
            return best_move;
        }
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
    state->probe_wdl = true;
    state->root_in_tb = false;
    state->root_tb_score = 0;
    state->pondering = pondering;
    state->pos.height = 0;
    state->completed_depth = 0;

    /* Probe tablebases for the root position */
    if (egtb_should_probe(&state->pos) &&
        (state->move_filter.size == 0) &&
        (state->multipv == 1)) {
        state->root_in_tb = egtb_probe_dtz_tables(&state->pos, &move,
                                                  &state->root_tb_score);
        if (state->root_in_tb) {
            state->move_filter.moves[0] = move;
            state->move_filter.size = 1;
        }
        state->probe_wdl = !state->root_in_tb;
    }

    /*
     * Initialize the best move to the first legal root
     * move to make sure a legal move is always returned.
     */
    gen_legal_moves(&state->pos, &legal);
    if (legal.size == 0) {
        return NOMOVE;
    }
    if (state->move_filter.size == 0) {
        best_move = legal.moves[0];
    } else {
        best_move = state->move_filter.moves[0];
    }

    /* Check if the number of multipv lines have to be reduced */
    state->multipv = MIN(state->multipv, legal.size);
    if (state->move_filter.size > 0) {
        state->multipv = MIN(state->multipv, state->move_filter.size);
    }

    /*
     * If there is only one legal move then there is no
     * need to do a search. Instead save the time for later.
     */
    if ((legal.size == 1) &&
        !state->pondering &&
        ((tc_get_flags()&TC_TIME_LIMIT) != 0) &&
        ((tc_get_flags()&(TC_INFINITE_TIME|TC_FIXED_TIME)) == 0)) {
        return best_move;
    }

    /* Prepare workers for a new search */
    for (k=0;k<number_of_workers;k++) {
        prepare_worker(&workers[k], state);
    }

    /* Start helpers */
    should_stop = false;
    for (k=1;k<number_of_workers;k++) {
        thread_create(&workers[k].thread, (thread_func_t)worker_thread_func,
                      &workers[k]);
    }

    /* Start the master worker thread */
    search_find_best_move(&workers[0]);

    /* Wait for all helpers to finish */
    for (k=1;k<number_of_workers;k++) {
        thread_join(&workers[k].thread);
    }

    /* Find the worker with the best move */
    worker = &workers[0];
    best_pv = &worker->mpv_lines[0];
    send_pv = false;
    if (state->multipv == 1) {
        for (k=1;k<number_of_workers;k++) {
            worker = &workers[k];
            if (worker->mpv_lines[0].pv.size < 1) {
                continue;
            }
            if ((worker->mpv_lines[0].depth >= best_pv->depth) ||
                ((worker->mpv_lines[0].depth == best_pv->depth) &&
                 (worker->mpv_lines[0].score > best_pv->score))) {
                best_pv = &worker->mpv_lines[0];
                send_pv = true;
            }
        }
    }

    /*
     * If the best worker is not the first worker then send
     * an extra pv line to the GUI.
     */
    if (send_pv) {
        engine_send_pv_info(state, best_pv);
    }

    /* Get the best move and the ponder move */
    if (best_pv->pv.size >= 1) {
        best_move = best_pv->pv.moves[0];
        if (ponder_move != NULL) {
            *ponder_move = (best_pv->pv.size > 1)?best_pv->pv.moves[1]:NOMOVE;
        }
    }

    /* Reset move filter since it's not needed anymore */
    state->move_filter.size = 0;

    /* Reset workers */
    for (k=0;k<number_of_workers;k++) {
        reset_worker(&workers[k]);
    }

    return best_move;
}

uint64_t smp_nodes(void)
{
    uint64_t nodes;
    int      k;

    nodes = 0ULL;
    for (k=0;k<number_of_workers;k++) {
        nodes += workers[k].nodes;
    }
    return nodes;
}

uint64_t smp_tbhits(void)
{
    uint64_t tbhits;
    int      k;

    tbhits = 0ULL;
    for (k=0;k<number_of_workers;k++) {
        tbhits += workers[k].tbhits;
    }
    return tbhits;
}

void smp_stop_all(void)
{
    atomic_store_explicit(&should_stop, true, memory_order_relaxed);
}

bool smp_should_stop(void)
{
    return atomic_load_explicit(&should_stop, memory_order_relaxed);
}

int smp_complete_iteration(struct search_worker *worker)
{
    int new_depth;
    int count;
    int k;

    mutex_lock(&state_lock);

    /*
     * If this is the first time completing this depth then
     * update the completed_depth counter.
     */
    if ((worker->depth > worker->state->completed_depth) &&
        (worker->mpv_lines[0].pv.size >= 1)) {
        worker->state->completed_depth = worker->depth;
    }

    /*
     * Calculate the next depth for this worker to search. For the first worker
     * always search the next depth since it is responsible for search output.
     */
    new_depth = worker->depth;
    if (worker->id == 0) {
        new_depth++;
    } else {
        while (true) {
            new_depth++;
            count = 0;
            for (k=0;k<number_of_workers;k++) {
                if (workers[k].depth >= new_depth) {
                    count++;
                }
                if (((count+1)/2) >= (number_of_workers/2)) {
                    break;
                }
            }
            if (k == number_of_workers || (number_of_workers == 1)) {
                break;
            }
        }
    }

    mutex_unlock(&state_lock);

    return new_depth;
}
