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
#include <assert.h>
#include <stdio.h>

#include "timectl.h"
#include "utils.h"
#include "debug.h"

/*
 * When using sudden death or fischer time controls this constant is used. An
 * assumption is made that there is always MOVES_TO_TIME_CONTROL moves left
 * to the time control.
 */
#define MOVES_TO_TIME_CONTROL 30

/* Safety margin to avoid loosing on time (in ms) */
#define SAFETY_MARGIN 50

/* The time control type to use for the search */
static enum timectl_type time_control_type = TC_INFINITE;

/* The time increment (in milliseconds) */
static int time_control_increment = 0;

/* The number of moves to the next time control */
static int time_control_movestogo = 0;

/* The time when the current search was started */
static time_t search_start = 0;

/* The number of milliseconds left on the clock */
static int search_time_left = 0;

/*
 * Limit on how long the engine is allowed to search. In
 * some special circumstances it can be ok to exceed
 * this limit.
 */
static time_t soft_time_limit = 0;

/* A hard time limit that may not be exceeded */
static time_t hard_time_limit = 0;

/* Keeps track if the clock is running or not */
static bool clock_is_running = false;

void tc_configure_time_control(enum timectl_type type, int time, int inc,
                               int movestogo)
{
    time_control_type = type;
    search_time_left = time;
    time_control_increment = inc;
    time_control_movestogo = movestogo;
    soft_time_limit = 0;
    hard_time_limit = 0;
}

bool tc_is_flexible(void)
{
    switch (time_control_type) {
    case TC_SUDDEN_DEATH:
    case TC_FISCHER:
    case TC_TOURNAMENT:
        return true;
    default:
        return false;
    }
}

bool tc_is_infinite(void)
{
    return time_control_type == TC_INFINITE;
}

void tc_start_clock(void)
{
    search_start = get_current_time();
    clock_is_running = true;
}

void tc_stop_clock(void)
{
    clock_is_running = false;
}

bool tc_is_clock_running(void)
{
    return clock_is_running;
}

void tc_allocate_time(void)
{
    time_t allocated = 0;
    time_t time_with_safety;
    time_t time_left;

    if (search_time_left > SAFETY_MARGIN) {
        time_with_safety = search_time_left - SAFETY_MARGIN;
    } else {
        time_with_safety = 0;
    }

    switch (time_control_type) {
    case TC_SUDDEN_DEATH:
        /*
         * An assumption is made that there are always a constant
         * number of moves left to time control. This makes the
         * engine spend more time in the beginning and play faster
         * and faster as the game progresses.
         */
        allocated = MIN(search_time_left/MOVES_TO_TIME_CONTROL,
                        time_with_safety);
        break;
    case TC_FISCHER:
        /*
         * Based on the same principle as for sudden death time
         * control, except that the full increment is always added
         * to the allocated time. Since the increment is gained after
         * making a move the increment for the previous move have already
         * been added to the clock it has to be subtracted first.
         */
        assert(search_time_left >= time_control_increment);
        time_left = search_time_left - time_control_increment;
        allocated =
                CLAMP(time_left/MOVES_TO_TIME_CONTROL+time_control_increment, 0,
                      time_with_safety);
        break;
    case TC_TOURNAMENT:
        allocated = MIN(search_time_left/time_control_movestogo,
                        time_with_safety);
        break;
    case TC_FIXED_TIME:
        /* No reason not to use all the time */
        allocated = time_with_safety;
        break;
    case TC_INFINITE:
        allocated = 0;
        break;
    default:
        assert(false);
        break;
    }
    assert(allocated >= 0);

    soft_time_limit = search_start + allocated;
    if (tc_is_flexible()) {
        /*
         * For flexible time controls we also set a hard time limit
         * that can be used to allocate extra time when needed. Specifically
         * we allow five times the normally allocated time up to at
         * most 80% of the remaining time to be used.
         */
        allocated = MIN(5*allocated, search_time_left*0.8);
        allocated = CLAMP(allocated, 0, time_with_safety);
        hard_time_limit = MAX(search_start+allocated, soft_time_limit);
    } else {
        hard_time_limit = soft_time_limit;
    }
}

time_t tc_elapsed_time(void)
{
    return get_current_time() - search_start;
}

void tc_update_time(int time)
{
    search_time_left = time;
}

bool tc_check_time(struct search_worker *worker)
{
    assert(worker != NULL);

    if (worker->state->pondering || (time_control_type == TC_INFINITE)) {
        return true;
    }

    /*
     * When resolving a fail-low we allow the search to exceed the soft
     * limit in the hope that the iteration can be finished.
     */
    if (worker->resolving_root_fail) {
        return get_current_time() < hard_time_limit;
    } else {
        return get_current_time() < soft_time_limit;
    }
}

bool tc_new_iteration(struct search_worker *worker)
{
    return worker->state->pondering || (time_control_type == TC_INFINITE) ||
           (get_current_time() < soft_time_limit);
}
