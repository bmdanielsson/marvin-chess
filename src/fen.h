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
#ifndef FEN_H
#define FEN_H

#include <stdbool.h>

#include "chess.h"

/* The chess starting position in FEN format */
#define FEN_STARTPOS "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* The maximum possible length of a FEN string */
#define FEN_MAX_LENGTH 256

/*
 * Setup a chess position baed on a FEN representation.
 *
 * @param pos The chess position.
 * @param fenstr The FEN string to parse. The string is assumed to be
 *               zero terminated.
 * @return Returns true if the position was sucessfully setup, false otherwise.
 */
bool fen_setup_board(struct position *pos, char *fenstr);

/*
 * Builds a FEN string for the current position.
 *
 * @param pos The board structure.
 * @param fenstr Location to store the generated string at. It must have room
 *               for at least FEN_MAX_LENGTH characters.
 */
void fen_build_string(struct position *pos, char *fenstr);

#endif
