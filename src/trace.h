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
#ifndef TRACE_H
#define TRACE_H

#ifndef TRACE

#define TRACE_MATERIAL(p, e, a)
#define TRACE_PSQ(p, s, e)
#define TRACE_M(tm, te, m)
#define TRACE_M_M(tm, m)
#define TRACE_M_E(te, m)
#define TRACE_MD(tm, te, m, d)
#define TRACE_MD_M(tm, m, d)
#define TRACE_MD_E(te, m, d)
#define TRACE_OM(tm, te, o, m)
#define TRACE_OM_M(tm, o, m)
#define TRACE_OM_E(te, o, m)

#else

#include "chess.h"
#include "tuningparam.h"

/* Parameter of an evaluation trace */
struct trace_param {
    int multiplier[NPHASES][NSIDES];
    int divisor[NPHASES][NSIDES];
};

/* Evaluation trace */
struct eval_trace {
    int                 phase;
    int                 base[NPHASES][NSIDES];
    struct trace_param  params[NUM_TUNING_PARAMS];
};

#define TRACE_MATERIAL(p, e, c) \
                    trace_material(eval->trace, side, (p), (e), (c));
#define TRACE_PSQ(p, s, e) trace_psq(eval->trace, side, (p), (s), (e))

#define TRACE_M(tm, te, m) \
            trace_param(eval->trace, side, (TP_ ## tm), (TP_ ## te), 0, (m), 0)
#define TRACE_M_M(tm, m) \
            trace_param(eval->trace, side, (TP_ ## tm), -1, 0, (m), 0)
#define TRACE_M_E(te, m) \
            trace_param(eval->trace, side, -1, (TP_ ## te), 0, (m), 0)

#define TRACE_MD(tm, te, m, d) \
        trace_param(eval->trace, side, (TP_ ## tm), (TP_ ## te), 0, (m), (d))
#define TRACE_MD_M(tm, m, d) \
        trace_param(eval->trace, side, (TP_ ## tm), 0, (m), (d))
#define TRACE_MD_E(te, m, d) \
        trace_param(eval->trace, side, (TP_ ## te), 0, (m), (d))

#define TRACE_OM(tm, te, o, m) \
        trace_param(eval->trace, side, (TP_ ## tm), (TP_ ## te), (o), (m), 0)
#define TRACE_OM_M(tm, o, m) \
        trace_param(eval->trace, side, (TP_ ## tm), -1, (o), (m), 0)
#define TRACE_OM_E(te, o, m) \
        trace_param(eval->trace, side, -1, (TP_ ## te), (o), (m), 0)

/*
 * Add a material trace for a piece.
 *
 * @param trace The evaluation trace.
 * @param side The side.
 * @param piece The piece.
 * @param endgame Indicates if this is for the endgame.
 * @param count The number of pieces of this type.
 */
void trace_material(struct eval_trace *trace, int side, int piece, bool endgame,
                    int count);
/*
 * Add a piece/square table trace for a piece.
 *
 * @param trace The evaluation trace.
 * @param side The side.
 * @param piece The piece.
 * @param square The square.
 * @param endgame Indicates if this is for the endgame.
 */
void trace_psq(struct eval_trace *trace, int side, int piece, int sq,
               bool endgame);

/*
 * Add a trace for a specific parameter.
 *
 * @param trace The evaluation trace.
 * @param side The side.
 * @param tp1 The middlegame tuning parameter, or -1 if there is none.
 * @param tp2 The endgame tuning parameter, or -1 if there is none.
 * @param offset Offset to apply to the tuning parameters.
 * @param multiplier The multiplier.
 * @param divisor The divisor.
 */
void trace_param(struct eval_trace *trace, int side, int tp1, int tp2,
                 int offset, int multiplier, int divisor);

#endif

#endif
