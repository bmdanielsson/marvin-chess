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
#include "tbprobe.h"
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
    state->move_filter.moves[0] = MOVE(from, to, promotion, flags);
    state->move_filter.size = 1;

    return true;
}

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
    worker->currmovenumber = 0;
    worker->currmove = NOMOVE;
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

void smp_search(struct gamestate *state, bool pondering, bool use_book,
                bool use_tablebases)
{
    int                  k;
    struct search_worker *worker;
    struct search_worker *best;
    struct movelist      legal;

    assert(valid_position(&state->pos));
    assert(number_of_workers > 0);
    assert(workers != NULL);

    /* Reset the best move information */
    state->best_move = NOMOVE;
    state->ponder_move = NOMOVE;

    /* Try to find a move in the opening book */
    if (use_book) {
        state->best_move = polybook_probe(&state->pos);
        if (state->best_move != NOMOVE) {
            return;
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
    state->probe_wdl = use_tablebases;
    state->root_in_tb = false;
    state->root_tb_score = 0;
    state->pondering = pondering;
    state->pos.sply = 0;
    state->completed_depth = 0;

    /* Probe tablebases for the root position */
    if (use_tablebases &&
        (state->move_filter.size == 0) &&
        (state->multipv == 1) &&
        (BITCOUNT(state->pos.bb_all) <= (int)TB_LARGEST)) {
        state->root_in_tb = probe_dtz_tables(state, &state->root_tb_score);
        state->probe_wdl = !state->root_in_tb;
    }

    /*
     * Initialize the best move to the first legal root
     * move to make sure a legal move is always returned.
     */
    gen_legal_moves(&state->pos, &legal);
    if (legal.size == 0) {
        state->best_move = NOMOVE;
        return;
    }
    if (state->move_filter.size == 0) {
        state->best_move = legal.moves[0];
    } else {
        state->best_move = state->move_filter.moves[0];
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
        (state->move_filter.size > 0) &&
        ((tc_get_flags()&TC_TIME_LIMIT) != 0) &&
        ((tc_get_flags()&(TC_INFINITE_TIME|TC_FIXED_TIME)) == 0)) {
        return;
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

    /* Send information to the GUI about which eval that is being used */
    engine_send_eval_info(&workers[0]);

    /* Start the master worker thread */
    search_find_best_move(&workers[0]);

    /* Wait for all helpers to finish */
    for (k=1;k<number_of_workers;k++) {
        thread_join(&workers[k].thread);
    }

    /* Find the worker with the best move */
    best = &workers[0];
    if (state->multipv == 1) {
        for (k=1;k<number_of_workers;k++) {
            worker = &workers[k];
            if ((worker->mpv_lines[0].depth > best->mpv_lines[0].depth) ||
                ((worker->mpv_lines[0].depth == best->mpv_lines[0].depth) &&
                (worker->mpv_lines[0].score > best->mpv_lines[0].score))) {
                best = worker;
            }
        }
    }

    /*
     * If the best worker is not the first worker then send
     * an extra pv line to the GUI.
     */
    if (best->id != 0) {
        engine_send_pv_info(best, best->mpv_lines[0].score);
    }

    /* Copy the best move to the state struct */
    if (best->mpv_moves[0] != NOMOVE) {
        best->state->best_move = best->mpv_moves[0];
        best->state->ponder_move = (best->mpv_lines[0].pv.size > 1)?
                                    best->mpv_lines[0].pv.moves[1]:NOMOVE;
    }

    /* Reset move filter since it's not needed anymore */
    state->move_filter.size = 0;

    /* Reset workers */
    for (k=0;k<number_of_workers;k++) {
        reset_worker(&workers[k]);
    }
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
    if (worker->depth > worker->state->completed_depth) {
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
