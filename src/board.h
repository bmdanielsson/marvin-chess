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
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>

#include "chess.h"

/*
 * Reset a chess board structure.
 *
 * @param pos The chess position.
 */
void board_reset(struct position *pos);

/*
 * Initialize a board structure to the chess starting posuition.
 *
 * @param pos The chess position.
 */
void board_start_position(struct position *pos);

/*
 * Initialize a board structure to the chess position in FEN notation.
 *
 * @param pos The chess position.
 * @param fenstr Chess position in FEN notation.
 * @return Returns true if the position was sucessfully setup, false otherwise.
 */
bool board_setup_from_fen(struct position *pos, char *fenstr);

/*
 * Tests if a player is in check.
 *
 * @param pos The chess board.
 * @param side The player to test.
 * @return Returns true if the player is in check.
 */
bool board_in_check(struct position *pos, int side);

/*
 * Make a move.
 *
 * @param pos The chess board.
 * @param move The move to make.
 * @param Returns false if the move was illegal (left the king in check),
 *        true otherwise. If the move was illegal the it is automatically
 *        undone.
 */
bool board_make_move(struct position *pos, uint32_t move);

/*
 * Undo the last move.
 *
 * @param pos The chess board.
 */
void board_unmake_move(struct position *pos);

/*
 * Make a null-move.
 *
 * @param pos The chess board.
 */
void board_make_null_move(struct position *pos);

/*
 * Undo a null move.
 *
 * @param pos The chess board.
 */
void board_unmake_null_move(struct position *pos);

/*
 * Check if the current board position is a repeat of a previous position.
 *
 * @param pos The chess board.
 * @return Returns true if this position is a repeat.
 */
bool board_is_repetition(struct position *pos);

/*
 * Check if a specific player has a non-pawn, non-king piece.
 *
 * @param pos The chess board.
 * @param side The side to check.
 */
bool board_has_non_pawn(struct position *pos, int side);

/*
 * Check if a move is at least pseudo-legal in a given position. Pseudo-legal
 * means that the move is ok but it might leave the own king in check.
 *
 * @param pos The chess board.
 * @param move The move to check.
 */
bool board_is_move_pseudo_legal(struct position *pos, uint32_t move);

/*
 * Check if a move is a checking move.
 *
 * @param pos The chess board.
 * @param move The move to check.
 */
bool board_move_gives_check(struct position *pos, uint32_t move);

#endif
