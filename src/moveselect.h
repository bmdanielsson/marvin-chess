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
#ifndef MOVESELECT_H
#define MOVESELECT_H

#include "chess.h"

/* Delta between two different base scores */
#define BASE_SCORE_DELTA 10000000

/* The maximum allowed history score */
#define MAX_HISTORY_SCORE BASE_SCORE_DELTA

/*
 * Initialize the move selector a node.
 *
 * @param worker The worker.
 * @param depth The current depth. For quiscenece nodes the depth is <= 0.
 * @param qnode Indicates if this is a quiscence node.
 * @param root Indicates if this is the root node.
 * @param in_check Indicates if the side to move is in check.
 */
void select_init_node(struct search_worker *worker, bool qnode, bool root,
                      bool in_check);

/*
 * Set a transposition table move for this position.
 *
 * @param worker The worker.
 * @param move The move.
 */
void select_set_tt_move(struct search_worker *worker, uint32_t move);

/*
 * Get the next root move to search. Should only be called
 * from the root node.
 *
 * @param worker The worker.
 * @param move Location to store the move at.
 * @return Returns true if a move was available, false otherwise.
 */
bool select_get_root_move(struct search_worker *worker, uint32_t *move);

/*
 * Get the next move to search. Should not be called
 * from the root node.
 *
 * @param worker The worker.
 * @param move Location to store the move at.
 * @return Returns true if a move was available, false otherwise.
 */
bool select_get_move(struct search_worker *worker, uint32_t *move);

/*
 * Get the next quiscence move to search.
 *
 * @param worker The worker.
 * @param move Location to store the move at.
 * @return Returns true if a move was available, false otherwise.
 */
bool select_get_quiscence_move(struct search_worker *worker, uint32_t *move);

/*
 * Updated the move ordering score for the root moves.
 *
 * @param worker The worker.
 */
void select_update_root_move_scores(struct search_worker *worker);

#endif

