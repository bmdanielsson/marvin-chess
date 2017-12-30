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
#ifndef SEE_H
#define SEE_H

#include "chess.h"

/* Material values for see calculations. Separate material values are
 * are used to make sure that knight and knights are treated equally. If
 * not then the quiscence search will prune some knight/bishop trades.
 */
extern int see_material[NPIECES];

/*
 * Evaluate a capture, including potential recaptures, using
 * Static Exchange Evaluation (SEE). Note that en passant captures
 * are not handled.
 *
 * @param pos The chess position.
 * @param move The move to evaluate.
 * @return The calculated score.
 */
int see_calculate_score(struct position *pos, uint32_t move);

#endif

