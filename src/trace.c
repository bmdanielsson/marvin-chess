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

void trace_track_pawn_shield(struct eval_trace *trace, int side, int file,
                             int tp)
{
    if (trace == NULL) {
        return;
    }

    trace->pawn_shield_trace[side][file] = tp;
}

void trace_pawn_shield(struct eval_trace *trace, int side, int castle_side)
{
    int file;
    int idx;
    int start;
    int stop;
    int tp;

    if (trace == NULL) {
        return;
    }

    start = (castle_side == QUEENSIDE)?0:FILE_F;
    stop = (castle_side == QUEENSIDE)?FILE_C:FILE_H;
    for (file=start;file<=stop;file++) {
        tp = trace->pawn_shield_trace[side][file];
        if (tp != 0) {
            idx = tuning_param_index(tp);
            trace->params[idx].multiplier[MIDDLEGAME][side] += 1;
        }
    }
}

void trace_material(struct eval_trace *trace, int side, int piece, bool endgame,
                    int count)
{
    int idx;
    int tp;

    if (trace == NULL) {
        return;
    }

    switch (piece) {
    case WHITE_PAWN:
    case BLACK_PAWN:
        trace->base[endgame][side] += count*PAWN_BASE_VALUE;
        break;
    case WHITE_KNIGHT:
    case BLACK_KNIGHT:
        tp = endgame?TP_KNIGHT_MATERIAL_VALUE_EG:TP_KNIGHT_MATERIAL_VALUE_MG;
        idx = tuning_param_index(tp);
        trace->params[idx].multiplier[endgame][side] += count;
        break;
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        tp = endgame?TP_BISHOP_MATERIAL_VALUE_EG:TP_BISHOP_MATERIAL_VALUE_MG;
        idx = tuning_param_index(tp);
        trace->params[idx].multiplier[endgame][side] += count;
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        tp = endgame?TP_ROOK_MATERIAL_VALUE_EG:TP_ROOK_MATERIAL_VALUE_MG;
        idx = tuning_param_index(tp);
        trace->params[idx].multiplier[endgame][side] += count;
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        tp = endgame?TP_QUEEN_MATERIAL_VALUE_EG:TP_QUEEN_MATERIAL_VALUE_MG;
        idx = tuning_param_index(tp);
        trace->params[idx].multiplier[endgame][side] += count;
        break;
    default:
        break;
    }
}

void trace_psq(struct eval_trace *trace, int side, int piece, int sq,
               bool endgame)
{
    int idx;
    int tp;

    if (trace == NULL) {
        return;
    }

    switch (piece) {
    case WHITE_PAWN:
    case BLACK_PAWN:
        tp = endgame?TP_PSQ_TABLE_PAWN_EG:TP_PSQ_TABLE_PAWN_MG;
        idx = tuning_param_index(tp) + sq;
        trace->params[idx].multiplier[endgame][side] += 1;
        break;
    case WHITE_KNIGHT:
    case BLACK_KNIGHT:
        tp = endgame?TP_PSQ_TABLE_KNIGHT_EG:TP_PSQ_TABLE_KNIGHT_MG;
        idx = tuning_param_index(tp) + sq;
        trace->params[idx].multiplier[endgame][side] += 1;
        break;
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        tp = endgame?TP_PSQ_TABLE_BISHOP_EG:TP_PSQ_TABLE_BISHOP_MG;
        idx = tuning_param_index(tp) + sq;
        trace->params[idx].multiplier[endgame][side] += 1;
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        tp = endgame?TP_PSQ_TABLE_ROOK_EG:TP_PSQ_TABLE_ROOK_MG;
        idx = tuning_param_index(tp) + sq;
        trace->params[idx].multiplier[endgame][side] += 1;
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        tp = endgame?TP_PSQ_TABLE_QUEEN_EG:TP_PSQ_TABLE_QUEEN_MG;
        idx = tuning_param_index(tp) + sq;
        trace->params[idx].multiplier[endgame][side] += 1;
        break;
    case WHITE_KING:
    case BLACK_KING:
        tp = endgame?TP_PSQ_TABLE_KING_EG:TP_PSQ_TABLE_KING_MG;
        idx = tuning_param_index(tp) + sq;
        trace->params[idx].multiplier[endgame][side] += 1;
        break;
    default:
        break;
    }
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
        trace->params[idx].multiplier[MIDDLEGAME][side] += multiplier;
        trace->params[idx].divisor[MIDDLEGAME][side] += divisor;
    }
    if (tp2 != -1) {
        idx = tuning_param_index(tp2) + offset;
        trace->params[idx].multiplier[ENDGAME][side] += multiplier;
        trace->params[idx].divisor[ENDGAME][side] += divisor;
    }
}
