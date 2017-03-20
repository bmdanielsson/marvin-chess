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
#ifndef TIMECTL_H
#define TIMECTL_H

#include "chess.h"

/*
 * Configure the time control to use for the next search.
 *
 * @param pos The board structure.
 * @param type Time control type.
 * @param time The number of milliseconds left on the clock for the engine.
 *             TC_FIXED: the time to search for
 * @param inc The time increment.
 * @param movestogo The number of moves left to the next time control.
 */
void tc_configure_time_control(struct gamestate *pos, enum timectl_type type,
                               int time, int inc, int movestogo);

/*
 * Check if the the time control is flexible or if any part of it is fixed.
 *
 * @param pos The board structure.
 * @return Return true if the time control is flexible.
 */
bool tc_is_flexible(struct gamestate *pos);

/*
 * Start the clock.
 *
 * @param pos The board structure.
 */
void tc_start_clock(struct gamestate *pos);

/*
 * Allocate time for the current search.
 *
 * @param pos The board structure.
 */
void tc_allocate_time(struct gamestate *pos);

/*
 * Check if there is still time left.
 *
 * @param pos The board structure.
 * @return Returns true if there is more time left.
 */
bool tc_check_time(struct gamestate *pos);

/*
 * Check if there is enough time left to start a new search iteration.
 *
 * @param pos The board structure.
 * @return Returns true if there is enough time left.
 */
bool tc_new_iteration(struct gamestate *pos);

#endif
