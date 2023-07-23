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

#include "types.h"

/*
 * Evaluate the position and assign a static score to it.
 *
 * @param pos The position.
 * @param force_hce Force evaluation using HCE.
 * @return Returns the score assigned to the position from the side
 *         to move point of view.
 */
int eval_evaluate(struct position *pos, bool force_hce);

/*
 * Initialize the material counter.
 *
 * @param pos The board structure.
 */
void eval_init_material(struct position *pos);

/*
 * Incrementally update the material counter.
 *
 * @param pos The board structure.
 * @param piece The changed piece.
 * @param added Flag indicating if the piece was added or removed.
 */
void eval_update_material(struct position *pos, int piece, bool added);

#endif
