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
#ifndef POSITION_H
#define POSITION_H

#include <stdbool.h>

#include "types.h"

/*
 * Reset a chess position structure.
 *
 * @param pos The chess position.
 */
void pos_reset(struct position *pos);

/*
 * Initialize a board structure to the chess starting posuition.
 *
 * @param pos The chess position.
 */
void pos_setup_start_position(struct position *pos);

/*
 * Initialize a board structure to the chess position in FEN notation.
 *
 * @param pos The chess position.
 * @param fenstr Chess position in FEN notation.
 * @return Returns true if the position was sucessfully setup, false otherwise.
 */
bool pos_setup_from_fen(struct position *pos, char *fenstr);

/*
 * Convert a move into a string representation.
 *
 * @param move The move.
 * @param str Pointer to store the string representation at.
 */
void pos_move2str(uint32_t move, char *str);

/*
 * Convert a move in algebraic notation to the internal move format.
 *
 * @param str A move in algebraic notation.
 * @param pos The current chess position.
 * @return Returns the move in our internal representation.
 */
uint32_t pos_str2move(char *str, struct position *pos);

/*
 * Tests if a player is in check.
 *
 * @param pos The chess board.
 * @param side The player to test.
 * @return Returns true if the player is in check.
 */
bool pos_in_check(struct position *pos, int side);

/*
 * Make a move.
 *
 * @param pos The chess board.
 * @param move The move to make.
 * @param Returns false if the move was illegal (left the king in check),
 *        true otherwise. If the move was illegal the it is automatically
 *        undone.
 */
bool pos_make_move(struct position *pos, uint32_t move);

/*
 * Undo the last move.
 *
 * @param pos The chess board.
 */
void pos_unmake_move(struct position *pos);

/*
 * Make a null-move.
 *
 * @param pos The chess board.
 */
void pos_make_null_move(struct position *pos);

/*
 * Undo a null move.
 *
 * @param pos The chess board.
 */
void pos_unmake_null_move(struct position *pos);

/*
 * Check if the current board position is a repeat of a previous position.
 *
 * @param pos The chess board.
 * @return Returns true if this position is a repeat.
 */
bool pos_is_repetition(struct position *pos);

/*
 * Check if a specific player has a non-pawn, non-king piece.
 *
 * @param pos The chess board.
 * @param side The side to check.
 */
bool pos_has_non_pawn(struct position *pos, int side);

/*
 * Check if a move is at least pseudo-legal in a given position. Pseudo-legal
 * means that the move is ok but it might leave the own king in check.
 *
 * @param pos The chess board.
 * @param move The move to check.
 */
bool pos_is_move_pseudo_legal(struct position *pos, uint32_t move);

/*
 * Check if a move is a checking move.
 *
 * @param pos The chess board.
 * @param move The move to check.
 */
bool pos_move_gives_check(struct position *pos, uint32_t move);

/*
 * Check if castling is allowed in a given position.
 *
 * @param pos The position.
 * @param type The type of castling to check.
 * @return Returns true if castling is allowed, false otherwise.
 */
bool pos_is_castling_allowed(struct position *pos, int type);

/*
 * Check if either side has mating material.
 *
 * @param pos The position.
 * @return Return true if at least one side has mating material,
 *         false otherwise.
 */
bool pos_has_mating_material(struct position *pos);

#endif
