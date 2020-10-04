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

#include <stdbool.h>

#include "chess.h"

/* Different time control flags */
#define TC_INFINITE_TIME 0x00000001
#define TC_FIXED_TIME    0x00000002
#define TC_TIME_LIMIT    0x00000004
#define TC_DEPTH_LIMIT   0x00000008
#define TC_NODE_LIMIT    0x00000010
#define TC_REGULAR       0x00000020

/*
 * Configure the time control to use for the next search.
 *
 * @param time The number of milliseconds left on the clock for the engine.
 * @param inc The time increment.
 * @param movestogo The number of moves left to the next time control.
 * @param flags Time control flags.
 */
void tc_configure_time_control(int time, int inc, int movestogo, int flags);

/*
 * Get the currently configured time control flags.
 *
 * @return Returns the time control flags.
 */
int tc_get_flags(void);

/*
 * Check if the infinite time control has been configured.
 *
 * @return Return true if the time control is infinite.
 */
bool tc_is_infinite(void);

/*
 * Start the clock.
 */
void tc_start_clock(void);

/*
 * Stop the clock.
 */
void tc_stop_clock(void);

/*
 * Check if the clock is running.
 *
 * @return Returns TRUE if the clock is running.
 */
bool tc_is_clock_running(void);

/*
 * Allocate time for the current search.
 */
void tc_allocate_time(void);

/*
 * Update the remaining time.
 *
 * @param time The remaining time.
 */
void tc_update_time(int time);

/*
 * Get the time since the search was started.
 *
 * @return Returns the number of elapsed milli seconds since the search was
 *         started.
 */
time_t tc_elapsed_time(void);

/*
 * Check if there is still time left.
 *
 * @param worker The worker.
 * @return Returns true if there is more time left.
 */
bool tc_check_time(struct search_worker *worker);

/*
 * Check if there is enough time left to start a new search iteration.
 *
 * @param worker The worker.
 * @return Returns true if there is enough time left.
 */
bool tc_new_iteration(struct search_worker *worker);

#endif
