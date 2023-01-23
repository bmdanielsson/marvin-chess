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
#ifndef SEARCH_H
#define SEARCH_H

#include <stdint.h>

#include "types.h"
#include "egtb.h"

/*
 * Score indicating infinity. Value is used for
 * alpha-beta search bounds.
 */
#define INFINITE_SCORE 30000

/* Base score indicating checkmate */
#define CHECKMATE 20000

/*
 * Score indicating a forced mate. If the score is above this value then
 * we can be sure that we have found a forced mate.
 */
#define FORCED_MATE (CHECKMATE - MAX_SEARCH_DEPTH)

/* Score indicating a known win/loss */
#define KNOWN_WIN   (TABLEBASE_WIN - MAX_SEARCH_DEPTH)
#define KNOWN_LOSS  (-KNOWN_WIN)

/* Initialize the search component */
void search_init(void);

/*
 * Called to starta search for a specific worker.
 *
 * @param worker The worker.
 */
void search_find_best_move(struct search_worker *worker);

#endif
