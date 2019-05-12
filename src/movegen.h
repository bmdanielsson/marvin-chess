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
#ifndef MOVEGEN_H
#define MOVEGEN_H

#include <stdio.h>

#include "chess.h"

/*
 * Generate all pseudo legal move for this position.
 *
 * @param pos The board structure.
 * @param list The list to store the moves in.
 */
void gen_moves(struct position *pos, struct movelist *list);

/*
 * Generate all legal move for this position.
 *
 * @param pos The board structure.
 * @param list The list to store the moves in.
 */
void gen_legal_moves(struct position *pos, struct movelist *list);

/*
 * Generate all check evasions.
 *
 * @param pos The board structure.
 * @param list The list to store the moves in. Moves are appended to the list
 *             so it must be initialized before first use.
 */
void gen_check_evasions(struct position *pos, struct movelist *list);

/*
 * Generate all normal moves. Normal moves are all moves except captures,
 * en-passant and promotions.
 *
 * @param pos The board structure.
 * @param list The list to store the moves in. Moves are appended to the list
 *             so it must be initialized before first use.
 */
void gen_quiet_moves(struct position *pos, struct movelist *list);

/*
 * Generate all capture moves (including en-passant). Promotions with
 * captures are also included.
 *
 * @param pos The board structure.
 * @param list The list to store the moves in. Moves are appended to the list
 *             so it must be initialized before first use.
 */
void gen_capture_moves(struct position *pos, struct movelist *list);

/*
 * Generate all promotion moves.
 *
 * @param pos The board structure.
 * @param list The list to store the moves in. Moves are appended to the list
 *             so it must be initialized before first use.
 * @param capture Flag indicating if captures should be included.
 * @param underpromote Flag indicating if under promotions should be included.
 */
void gen_promotion_moves(struct position *pos, struct movelist *list,
                         bool capture, bool underpromote);

#endif
