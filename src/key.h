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
#ifndef KEY_H
#define KEY_H

#include <stdint.h>

#include "chess.h"

/*
 * Generate a unique key for a chess position.
 *
 * @param pos A chess position.
 * @return Returns a unique key.
 */
uint64_t key_generate(struct position *pos);

/*
 * Generate a unique key for a the pawns of a chess position.
 *
 * @param pos A chess position.
 * @return Returns a unique key for the pawns.
 */
uint64_t key_generate_pawnkey(struct position *pos);

/*
 * Update a piece in the key.
 *
 * @param key The key to update.
 * @param piece The piece to add/remove.
 * @param sq The square.
 * @return Returns the updated key.
 */
uint64_t key_update_piece(uint64_t key, int piece, int sq);

/*
 * Update the en passant square in the key.
 *
 * @param key The key to update.
 * @param old_sq The old en-passant square.
  * @param new_sq The new en-passant square.
 * @return Returns the updated key.
 */
uint64_t key_update_ep_square(uint64_t key, int old_sq, int new_sq);

/*
 * Update the side to move in the key.
 *
 * @param key The key to update.
 * @param new_color The new color.
 * @return Returns the updated key.
 */
uint64_t key_update_side(uint64_t key, int new_color);

/*
 * Update the castling availability status in the key.
 *
 * @param key The key to update.
 * @param old_castle The old castling availability.
 * @param new_castle The new castling availability.
 * @return Returns the updated key.
 */
uint64_t key_update_castling(uint64_t key, int old_castle, int new_castle);

#endif
