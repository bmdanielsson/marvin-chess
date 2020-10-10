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
#ifndef NNUEIF_H
#define NNUEIF_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "chess.h"
#include "utils.h"

/* Maccros mapping Cfish to Marvin */
#define Position struct position
#define SQ_NONE NO_SQUARE
#define square_of(c,p) (bitscan_forward(pos->bb_pieces[c+p]))
#define pieces() (pos->bb_all)
#define pieces_p(p) (pos->bb_pieces[WHITE+p]|pos->bb_pieces[BLACK+p])
#define piece_on(s) (pos->pieces[s])
#define type_of_p(p) ((p)&(~BLACK))
#define make_piece(c,p) ((p)+(c))
#define stm() pos->stm
#define pop_lsb pop_bit

/*
 * Initialize NNUE with a network.
 *
 * @param eval_file The network file.
 * @return Returns true on success, false otherwise.
 */
bool nnueif_init(char *eval_file);

/*
 * Reset the NNUE part of a position.
 *
 * @param pos The position.
 */
void nnueif_reset_pos(struct position *pos);

/*
 * Make a new move.
 *
 * @param pos The position.
 * @param move The move.
 */
void nnueif_make_move(struct position *pos, uint32_t move);

/*
 * Undo the latest move.
 *
 * @param pos The position.
 */
void nnueif_unmake_move(struct position *pos);

/*
 * Make a new null move.
 *
 * @param pos The position.
 */
void nnueif_make_null_move(struct position *pos);

/*
 * Undo a null move.
 *
 * @param pos The position.
 */
void nnueif_unmake_null_move(struct position *pos);

/*
 * Evaluate a position using NNUE.
 *
 * @param pos The position.
 * @return Returns the score assigned to the position from the side
 *         to move point of view.
 */
int nnueif_evaluate(struct position *pos);

#endif
