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
#include "trace.h"

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
 * Check if the position is a draw by insufficient material.
 *
 * @param pos The board structure.
 * @return Returns true if the position is a draw.
 */
bool eval_is_material_draw(struct position *pos);

#ifdef TRACE
/*
 * Generate a trace for the evaluation function.
 *
 * @param pos The position.
 * @param trace The evaluation trace.
 */
void eval_generate_trace(struct position *pos, struct eval_trace *trace);
#endif

#endif
