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

void smp_init(void);

void smp_destroy(void);

void smp_create_workers(int nthreads);

void smp_destroy_workers(void);

void smp_search(struct gamestate *state, bool pondering, bool use_book,
                bool use_tablebases);

uint32_t smp_nodes(void);

void smp_stop_all(void);

bool smp_should_stop(struct search_worker *worker);

void smp_complete_iteration(struct search_worker *worker);

#endif
