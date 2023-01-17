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
#include "config.h"

/*
 * When using sudden death or fischer time controls this constant is used. An
 * assumption is made that there is always MOVES_TO_TIME_CONTROL moves left
 * to the time control.
 */
#define MOVES_TO_TIME_CONTROL 30

/* Flags indicating special time control modes */
static int tc_flags = 0;

/* The time increment (in milliseconds) */
static int tc_increment = 0;

/* The number of moves to the next time control */
static int tc_movestogo = 0;

/* The number of milliseconds left on the clock */
static int tc_time_left = 0;

/*
 * Limit on how long the engine is allowed to search. In
 * some special circumstances it can be ok to exceed
 * this limit.
 */
static time_t soft_time_limit = 0;

/* A hard time limit that may not be exceeded */
static time_t hard_time_limit = 0;

/* The time when the current search was started */
static time_t search_start = 0;

/* Keeps track if the clock is running or not */
static bool clock_is_running = false;

/* Safety margin to avoid loosing on time (in ms) */
static int safety_margin = DEFAULT_MOVE_OVERHEAD;

void tc_set_move_overhead(int overhead)
{
    safety_margin = overhead;
}

void tc_configure_time_control(int time, int inc, int movestogo, int flags)
{
    tc_time_left = time;
    tc_increment = inc;
    tc_movestogo = movestogo > 0?movestogo:MOVES_TO_TIME_CONTROL;
    tc_flags = flags;
    soft_time_limit = 0;
    hard_time_limit = 0;
}

int tc_get_flags(void)
{
    return tc_flags;
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

    /* Handle special cases first */
    if (tc_flags&TC_INFINITE_TIME) {
        soft_time_limit = 0;
        hard_time_limit = 0;
        return;
    } else if (tc_flags&TC_FIXED_TIME) {
        allocated = MAX(tc_time_left, 0);
        soft_time_limit = search_start + allocated;
        hard_time_limit = soft_time_limit;
        return;
    }

    /* Calculate how much time to allocate */
    allocated = tc_time_left/tc_movestogo + tc_increment;
    allocated = MIN(allocated, tc_time_left-safety_margin);

    /*
     * Setup time limits. The soft time limit is time the engine is
     * expected to spend and the hard limit is the amount of time it
     * is allowed to spend in case of panic.
     */
    soft_time_limit = search_start + allocated;
    allocated = MIN(5*allocated, tc_time_left*0.8);
    allocated = MIN(allocated, tc_time_left-safety_margin);
    hard_time_limit = search_start + allocated;
}

time_t tc_elapsed_time(void)
{
    return get_current_time() - search_start;
}

void tc_update_time(int time)
{
    tc_time_left = time;
}

bool tc_check_time(struct search_worker *worker)
{
    assert(worker != NULL);

    /*
     * Always search at least one ply in order to make sure
     * a sensible (not random) move is always played
     */
    if (worker->depth <= 1) {
        return true;
    }

    /*
     * When resolving a fail-low we allow the search to exceed the soft
     * limit in the hope that the iteration can be finished.
     */
    if (worker->resolving_root_fail &&
        (worker->depth > worker->state->completed_depth)) {
        return get_current_time() < hard_time_limit;
    } else {
        return get_current_time() < soft_time_limit;
    }
}

bool tc_new_iteration(struct search_worker *worker)
{
    return worker->state->pondering || ((tc_flags&TC_TIME_LIMIT) == 0) ||
           worker->depth <= 1 || (get_current_time() < soft_time_limit);
}
