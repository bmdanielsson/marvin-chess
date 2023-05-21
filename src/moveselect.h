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

#include "types.h"

/*
 * Initialize the move selector.
 *
 * @param ms The moveselector.
 * @param worker The worker.
 * @param captures_only Flags indicating if only tactical moves i.e., captures
 *                      and promotoions, should be considered.
 * @param in_check Is the side to move in check.
 * @param ttmove Transposition table move for this position.
 * @param qchecks Flag indicating if checks should be included in the quiscence
 *                search.
 * @param recap_sq If the last move was a capture then this is set to the
 *                 destination square of the move, otherwise it is set to
 *                 NO_SQUARE.
 * @param depth The current search depth. For quiscence search the depth will
 *              be negative.
 */
void select_init_node(struct moveselector *ms, struct search_worker *worker,
                      bool tactical_only, bool in_check, uint32_t ttmove,
                      bool qchecks, int recap_sq, int depth);

/*
 * Get the next move to search.
 *
 * @param ms The moveselector.
 * @param worker The worker.
 * @param move Location to store the move at.
 * @return Returns true if a move was available, false otherwise.
 */
bool select_get_move(struct moveselector *ms, struct search_worker *worker,
                     uint32_t *move);

/*
 * Check if the current phase is the bad capture phase.
 *
 * @param ms The moveselector.
 * @return Returns the true if it is the bad capture phase.
 */
bool select_is_bad_capture_phase(struct moveselector *ms);

#endif

