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
#include <string.h>

#include "trace.h"
#include "eval.h"

void trace_const(struct eval_trace *trace, int side, int const_val)
{
    if (trace == NULL) {
        return;
    }

    trace->base[MIDDLEGAME][side] += const_val;
    trace->base[ENDGAME][side] += const_val;
}

void trace_param(struct eval_trace *trace, int side, int tp1, int tp2,
                 int offset, int multiplier, int divisor)
{
    int idx;

    if (trace == NULL) {
        return;
    }

    if (tp1 != -1) {
        idx = tuning_param_index(tp1) + offset;
        trace->params[idx].mul[MIDDLEGAME][side] += multiplier;
        trace->params[idx].div[MIDDLEGAME][side] += divisor;
    }
    if (tp2 != -1) {
        idx = tuning_param_index(tp2) + offset;
        trace->params[idx].mul[ENDGAME][side] += multiplier;
        trace->params[idx].div[ENDGAME][side] += divisor;
    }
}
