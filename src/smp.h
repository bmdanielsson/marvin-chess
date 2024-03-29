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
#ifndef SMP_H
#define SMP_H

#include "types.h"
#include "thread.h"

/* Initilaize the SMP component */
void smp_init(void);

/* Clean up the SMP component */
void smp_destroy(void);

/*
 * Create worker threads.
 *
 * @param nthreads The number of threads to create.
 */
void smp_create_workers(int nthreads);

/* Destroy all workers */
void smp_destroy_workers(void);

/*
 * Prepare all workers for a new search.
 *
 * @param engine The engine.
 */
void smp_prepare_workers(struct engine *engine);

/* Reset all workers */
void smp_reset_workers(void);

/*
 * Get a pointer to the worker at a given index.
 *
 * @param idx Index of the worker to get.
 * @return Returns the worker at the spcified index.
 */
struct search_worker* smp_get_worker(int idx);

/*
 * Get the number of workers being used.
 *
 * @return Returns the number of workers.
 */
int smp_number_of_workers(void);

/*
 * Start a worker.
 *
 * @param worker The worker to start.
 * @param func The search function.
 * */
void smp_start_worker(struct search_worker *worker, thread_func_t func);

/*
 * Wait for a worker to stop searching.
 *
 * @param worker The worker to wait for.
 * */
void smp_wait_for_worker(struct search_worker *worker);

/* Indicate the start of a new game */
void smp_newgame(void);

/*
 * The number of nodes searched.
 *
 * @return Returns the total number of nodes searched (by all workers).
 */
uint64_t smp_nodes(void);

/*
 * The number of tablebase hits during search.
 *
 * @return Returns the total number of tablebase hits.
 */
uint64_t smp_tbhits(void);

/*
 * Stop all workers.
 */
void smp_stop_all(void);

/*
 * Check if searching should stop.
 *
 * @return Returns true if searching should stop.
 */
bool smp_should_stop(void);

/*
 * Called by workers when they have finished a search iteration.
 *
 * @param worker The worker.
 * @return Returns the depth the worker should search to for the
 *         next iteration.
 */
int smp_complete_iteration(struct search_worker *worker);

#endif
