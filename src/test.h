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
#ifndef TEST_H
#define TEST_H

#include "types.h"

/*
 * Run perft on a specific position. Perft results can be compared with the
 * engine ROCE.
 *
 * Perft info: http://www.rocechess.ch/perft.html
 * ROCE: http://www.rocechess.ch/rocee.html
 *
 * @param pos The position to run perft for.
 * @param depth The depth to run perft to.
 */
void test_run_perft(struct position *pos, int depth);

/*
 * Run divide on a specific position. Divide is a variant of perft that counts
 * the number of moves and the number of child moves. This can be used for
 * debugging perft problems. Divide results can be compared with the
 * engine ROCE.
 *
 * Divide info: http://www.rocechess.ch/perft.html
 * ROCE: http://www.rocechess.ch/rocee.html
 *
 * @param pos The position to run divide for.
 * @param depth The depth to run divide to.
 */
void test_run_divide(struct position *pos, int depth);

/* Run a benchmark to check evaluate the performance of the engine */
void test_run_benchmark(void);

#endif
