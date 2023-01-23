/*
 * Marvin - an UCI/XBoard compatible chess engine
 * Copyright (C) 2023 Martin Danielsson
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
#ifndef EGTB_H
#define EGTB_H

#include <stdbool.h>

#include "types.h"

/* Scores assigned to tablebase wins/losses */
#define TABLEBASE_WIN   19000
#define TABLEBASE_LOSS  -19000

void egtb_init(char *path);

bool egtb_should_probe(struct position *pos);

bool egtb_probe_dtz_tables(struct position *pos, uint32_t *move, int *score);

bool egtb_probe_wdl_tables(struct position *pos, int *score);

#endif
