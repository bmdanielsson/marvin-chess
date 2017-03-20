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

#include "chess.h"

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

/*
 * Reset the search data to prepare for a new search.
 *
 * @param pos The board structure.
 */
void search_reset_data(struct gamestate *pos);

/*
 * Searches the current position to find the best move.
 *
 * @param pos The chess board structure.
 * @param pondering Flag indicating if this is a ponder search
 *                  or a regular search.
 * @param ponder_move Location to store a potential ponder move at.
 * @return Returns the best move for this position.
 */
uint32_t search_find_best_move(struct gamestate *pos, bool pondering,
                               uint32_t *ponder_move);

/*
 * Get a quiscence score for the current position.
 *
 * @param pos The chess board structure.
 * @return Returns the score.
 */
int search_get_quiscence_score(struct gamestate *pos);

#endif
