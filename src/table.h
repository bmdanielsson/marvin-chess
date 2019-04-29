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
#ifndef TABLE_H
#define TABLE_H

#include <stdbool.h>

#include "chess.h"

/* The maximum allowed history score */
#define MAX_HISTORY_SCORE 10000000

/*
 * Clear history table.
 *
 * @param worker The worker to clear the history table for.
 */
void tbl_clear_history_table(struct search_worker *worker);

/*
 * Update the history table with a move.
 *
 * @param worker The worker.
 * @param list List of quiet moves tried for this position. The last move
 *             in the list is the move tha caused the beta cutoff.
 * @param depth The depth to which the move was searched.
 */
void tbl_update_history_table(struct search_worker *worker,
                              struct movelist *list, int depth);

/*
 * Clear the killer move table.
 *
 * @param worker The worker to clear the killer table for.
 */
void tbl_clear_killermove_table(struct search_worker *worker);

/*
 * Add a move to the killer move table.
 *
 * @param worker The worker.
 * @param move The move to add.
 */ 
void tbl_add_killer_move(struct search_worker *worker, uint32_t move);

/*
 * Check if a move is present in the killer move table.
 *
 * @param worker The worker.
 * @param move The move to check.
 * @return Returns true if the move is present.
 */
bool tbl_is_killer_move(struct search_worker *worker, uint32_t move);

/*
 * Clear the counter move table.
 *
 * @param worker The worker to clear the counter move table for.
 */
void tbl_clear_countermove_table(struct search_worker *worker);

/*
 * Add a move to the counter move table.
 *
 * @param worker The worker.
 * @param move The move to add.
 */
void tbl_add_counter_move(struct search_worker *worker, uint32_t move);

#endif
