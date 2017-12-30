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
#ifndef VALIDATION_H
#define VALIDATION_H

#include <stdbool.h>

#include "chess.h"

/*
 * Check if the board position is valid.
 *
 * @param pos The chess position.
 * @return Returns true if the position is valid.
 */
bool valid_position(struct position *pos);

/*
 * Check if the square is valid.
 *
 * @param sq The square.
 * @return Returns true if the square is valid.
 */
bool valid_square(int sq);

/*
 * Check if the side is valid.
 *
 * @param side The side.
 * @return Returns true if the side is valid.
 */
bool valid_side(int side);

/*
 * Check if the piece is valid.
 *
 * @param piece The chess piece.
 * @return Returns true if the piece is valid.
 */
bool valid_piece(int piece);

/*
 * Check if the move is valid.
 *
 * @param move The chess move.
 * @return Returns true if the move is valid.
 */
bool valid_move(uint32_t move);

/*
 * Check if correct number of quiscence moves were generated.
 *
 * @param pos The chess position.
 * @param checks Indicates if checking moves  were included or not.
 * @param list List of generated moves.
 * @return Returns true if the correct number of captures were generated.
 */
bool valid_gen_quiscenece_moves(struct position *pos, bool checks,
                                struct movelist *list);

/*
 * Check if incrementally updated score are correct.
 *
 * @param pos The chess position.
 * @return Returns true if the scores are correct.
 */
bool valid_scores(struct position *pos);

#endif
