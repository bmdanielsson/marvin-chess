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

void tc_configure_time_control(struct gamestate *pos, enum timectl_type type,
                               int time, int inc, int movestogo)
{
    assert(pos != NULL);

    pos->tc_type = type;
    pos->time_left = time;
    pos->increment = inc;
    pos->movestogo = movestogo;
    pos->soft_time_limit = 0;
    pos->hard_time_limit = 0;
}

bool tc_is_flexible(struct gamestate *pos)
{
    switch (pos->tc_type) {
    case TC_SUDDEN_DEATH:
    case TC_FISCHER:
    case TC_TOURNAMENT:
        return true;
    default:
        return false;
    }
}

void tc_start_clock(struct gamestate *pos)
{
    assert(pos != NULL);

    pos->search_start = get_current_time();
}

void tc_allocate_time(struct gamestate *pos)
{
    time_t allocated = 0;
    time_t time_with_safety;
    time_t time_left;

    if (pos->time_left > SAFETY_MARGIN) {
        time_with_safety = pos->time_left - SAFETY_MARGIN;
    } else {
        time_with_safety = 0;
    }

    switch (pos->tc_type) {
    case TC_SUDDEN_DEATH:
        /*
         * An assumption is made that there are always a constant
         * number of moves left to time control. This makes the
         * engine spend more time in the beginning and play faster
         * and faster as the game progresses.
         */
        allocated = MIN(pos->time_left/MOVES_TO_TIME_CONTROL, time_with_safety);
        break;
    case TC_FISCHER:
        /*
         * Based on the same principle as for sudden death time
         * control, except that the full increment is always added
         * to the allocated time. Since the increment is gained after
         * making a move the increment for the previous move have already
         * been added to the clock it has to be subtracted first.
         */
        assert(pos->time_left >= pos->increment);
        time_left = pos->time_left - pos->increment;
        allocated = CLAMP(time_left/MOVES_TO_TIME_CONTROL+pos->increment, 0,
                          time_with_safety);
        break;
    case TC_TOURNAMENT:
        allocated = MIN(pos->time_left/pos->movestogo, time_with_safety);
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

    pos->soft_time_limit = pos->search_start + allocated;
    if (tc_is_flexible(pos)) {
        /*
         * For flexible time controls we also set a hard time limit
         * that can be used to allocate extra time when needed. Specifically
         * we allow five times the normally allocated time up to at
         * most 80% of the remaining time to be used.
         */
        allocated = MIN(5*allocated, pos->time_left*0.8);
        allocated = CLAMP(allocated, 0, time_with_safety);
        pos->hard_time_limit = MAX(pos->search_start+allocated,
                                   pos->soft_time_limit);
    } else {
        pos->hard_time_limit = pos->soft_time_limit;
    }
}

bool tc_check_time(struct gamestate *pos)
{
    assert(pos != NULL);

    if (pos->pondering || (pos->tc_type == TC_INFINITE)) {
        return true;
    }

    /*
     * When resolving a fail-low we allow the search to exceed the soft
     * limit in the hope that the iteration can be finished.
     */
    if (pos->resolving_root_fail) {
        return get_current_time() < pos->hard_time_limit;
    } else {
        return get_current_time() < pos->soft_time_limit;
    }
}

bool tc_new_iteration(struct gamestate *pos)
{
    return pos->pondering || (pos->tc_type == TC_INFINITE) ||
           (get_current_time() < pos->soft_time_limit);
}
