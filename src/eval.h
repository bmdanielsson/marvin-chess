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
#ifndef EVAL_H
#define EVAL_H

#include "chess.h"

/* Reset the evaluation module */
void eval_reset(void);

/*
 * Evaluate the position and assign a static score to it.
 *
 * @param pos The board structure.
 * @return Returns the score assigned to the position from the side
 *         to move point of view.
 */
int eval_evaluate(struct gamestate *pos);

/*
 * Print evaluation details about the position.
 *
 * @param pos The board structure.
 */
void eval_display(struct gamestate *pos);

/*
 * Calculate the material for a specific side.
 *
 * @param pos The board structure.
 * @param side The side to calculate material for.
 * @param endgame If the score should be or the endgame.
 * @return Returns the material score.
 */
int eval_material(struct gamestate *pos, int side, bool endgame);

/*
 * Incrementally update the material score for a piece.
 *
 * @param add Indicates if the piece was added or removed.
 * @param pos The board structure.
 * @param piece The piece.
 */
void eval_update_material_score(struct gamestate *pos, int add, int piece);

/*
 * Calculate the piece/square table score for a specific side.
 *
 * @param pos The board structure.
 * @param side The side to calculate the score for.
 * @param endgame If the score should be or the endgame.
 * @return Returns the piece/square table score.
 */
int eval_psq(struct gamestate *pos, int side, bool endgame);

/*
 * Incrementally update the piece/square table score for a piece
 * on a specific square.
 *
 * @param add Indicates if the piece was added or removed.
 * @param pos The board structure.
 * @param piece The piece.
 * @param sq The square.
 */
void eval_update_psq_score(struct gamestate *pos, int add, int piece, int sq);

/*
 * Check if the position is a draw by insufficient material.
 *
 * @param pos The board structure.
 * @return Returns true if the position is a draw.
 */
bool eval_is_material_draw(struct gamestate *pos);

#endif
