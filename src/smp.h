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

#include "chess.h"

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
 * Start a new search.
 *
 * @param state The game state.
 * @param pondering If a pindering search should be started.
 * @param use_book If the opening book should be used.
 * @param use_tablebases If tablebases should be used.
 */
void smp_search(struct gamestate *state, bool pondering, bool use_book,
                bool use_tablebases);

/*
 * The number of nodes searched.
 *
 * @return Returns the total number of nodes searched (by all workers).
 */
uint32_t smp_nodes(void);

/* Stop all workers */
void smp_stop_all(void);

/*
 * Check if a worker should stop.
 *
 * @param worker The worker.
 * @return Returns true if the worker should stop.
 */
bool smp_should_stop(struct search_worker *worker);

/*
 * Update best move so far and send status information.
 *
 * @param worker The worker.
 * @param score The current search score.
 */
void smp_update(struct search_worker *worker, int score);

/*
 * Called by workers when they have finished a search iteration.
 *
 * @param worker The worker.
 * @return Returns the depth the worker should search to for the
 *         next iteration.
 */
int smp_complete_iteration(struct search_worker *worker);

#endif
