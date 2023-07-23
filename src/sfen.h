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
#ifndef SFEN_H
#define SFEN_H

/*
 * Generate an sfen file in bin format.
 *
 * @param argc The number of command line arguments.
 * @param argv The command line arguments.
 * @return Return 0 on success or a positive value in case of error.
 */
int sfen_generate(int argc, char *argv[]);

#endif
