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
#ifndef NNUE_H
#define NNUE_H

#include <stdbool.h>

#include "chess.h"

/* Initialize the NNUE component */
void nnue_init(void);

/*
 * Load a NNUE net.
 *
 * @param path The path of the net to load.
 * @return Returns true if the new was succesfully loaded.
 */
bool nnue_load_net(char *path);

/*
 * Evaluate a position.
 *
 * @param pos The position.
 * @return Returns the score assigned to the position from the side
 *         to move point of view.
 */
int16_t nnue_evaluate(struct position *pos); 

#endif
