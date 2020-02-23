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

/* Material values for see calculations */
extern int see_material[NPIECES];

/*
 * Check if the Static Exchange Evaluation (SEE) score of a capturer
 * is equal to or above a certain threshold.
 *
 * @param pos The chess position.
 * @param move The move to evaluate.
 * @param threshold The threshold.
 * @return Returns true if the score is greater than the threahold.
 */
bool see_ge(struct position *pos, uint32_t move, int threshold);

#endif

