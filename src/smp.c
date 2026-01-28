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
#include "bitboard.h"
#include "position.h"
#include "history.h"
#include "nnue.h"

/* Lock for updating the engine struct during search */
static mutex_t engine_lock;

/* Variables used to signal to workers to stop searching */
static mutex_t stop_lock;
static atomic_bool should_stop = false;

/* Data for worker threads */
static int number_of_workers = 0;
static struct search_worker *workers = NULL;

void smp_init(void)
{
    mutex_init(&engine_lock);
    mutex_init(&stop_lock);
}

void smp_destroy(void)
{
    mutex_destroy(&engine_lock);
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
        workers[k].engine = NULL;
        workers[k].id = k;
    }
}

void smp_destroy_workers(void)
{
    aligned_free(workers);
    workers = NULL;
    number_of_workers = 0;
}

void smp_prepare_workers(struct engine *engine)
{
    struct search_worker *worker;
    int                  mpvidx;
    int                  k;

    should_stop = false;
    for (k=0;k<number_of_workers;k++) {
        worker = &workers[k];

        /* Copy data from engine */
        worker->pos = engine->pos;

        /* Clear tables */
        killer_clear_table(worker);
        counter_clear_table(worker);

        /* Clear statistics */
        worker->nodes = 0;
        worker->qnodes = 0;
        worker->tbhits = 0ULL;

        /* Clear best move information */
        for (mpvidx=0;mpvidx<engine->multipv;mpvidx++) {
            worker->mpv_moves[mpvidx] = NOMOVE;
            worker->mpv_lines[mpvidx].score = -INFINITE_SCORE;
            worker->mpv_lines[mpvidx].pv.size = 0;
            worker->mpv_lines[mpvidx].depth = 0;
            worker->mpv_lines[mpvidx].seldepth = 0;
        }

        /* Clear multipv information */
        worker->multipv = engine->multipv;

        /* Initialize helper variables */
        worker->resolving_root_fail = false;

        /* Setup parent pointers */
        worker->engine = engine;
        worker->pos.engine = engine;
        worker->pos.worker = worker;
    }
}

void smp_reset_workers(void)
{
    struct search_worker *worker;
    int                  k;

    for (k=0;k<number_of_workers;k++) {
        worker = &workers[k];

        worker->engine = NULL;
        worker->pos.engine = NULL;
        worker->pos.worker = NULL;
    }
}

struct search_worker* smp_get_worker(int idx)
{
    assert((idx >= 0) && (idx < number_of_workers));

    return &workers[idx];
}

int smp_number_of_workers(void)
{
    return number_of_workers;
}

void smp_start_worker(struct search_worker *worker, thread_func_t func)
{
    thread_create(&worker->thread, func, worker);
}

void smp_wait_for_worker(struct search_worker *worker)
{
    thread_join(&worker->thread);
}

void smp_newgame(void)
{
    int k;

    for (k=0;k<number_of_workers;k++) {
        history_clear_tables(&workers[k]);
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

    mutex_lock(&engine_lock);

    /*
     * If this is the first time completing this depth then
     * update the completed_depth counter.
     */
    if ((worker->depth > worker->engine->completed_depth) &&
        (worker->mpv_lines[0].pv.size >= 1)) {
        worker->engine->completed_depth = worker->depth;
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

    mutex_unlock(&engine_lock);

    return new_depth;
}
