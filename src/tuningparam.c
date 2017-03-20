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
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "tuningparam.h"
#include "evalparams.h"
#include "eval.h"
#include "chess.h"

/* Identifiers for all tunable parameters */
enum {
    TP_DOUBLE_PAWNS,
    TP_ISOLATED_PAWN,
    TP_ROOK_OPEN_FILE,
    TP_ROOK_HALF_OPEN_FILE,
    TP_QUEEN_OPEN_FILE,
    TP_QUEEN_HALF_OPEN_FILE,
    TP_ROOK_ON_7TH_MG,
    TP_ROOK_ON_7TH_EG,
    TP_BISHOP_PAIR,
    TP_PAWN_SHIELD_RANK1,
    TP_PAWN_SHIELD_RANK2,
    TP_PAWN_SHIELD_HOLE,
    TP_PASSED_PAWN_RANK2,
    TP_PASSED_PAWN_RANK3,
    TP_PASSED_PAWN_RANK4,
    TP_PASSED_PAWN_RANK5,
    TP_PASSED_PAWN_RANK6,
    TP_PASSED_PAWN_RANK7,
    TP_KNIGHT_MOBILITY,
    TP_BISHOP_MOBILITY,
    TP_ROOK_MOBILITY,
    TP_QUEEN_MOBILITY,
    TP_PSQ_TABLE_PAWN,
    TP_PSQ_TABLE_KNIGHT,
    TP_PSQ_TABLE_BISHOP,
    TP_PSQ_TABLE_ROOK,
    TP_PSQ_TABLE_KING_MG,
    TP_PSQ_TABLE_KING_EG,
    NUM_PARAM_DECLARATIONS
};

/* Definitions for all tunable parameters */
struct param_decl parameter_declarations[NUM_PARAM_DECLARATIONS] = {
    {"double_pawns", 0, 0, -150, 0},
    {"isolated_pawn", 1, 1, -150, 0},
    {"rook_open_file", 2, 2, 0, 150},
    {"rook_half_open_file", 3, 3, 0, 150},
    {"queen_open_file", 4, 4, 0, 150},
    {"queen_half_open_file", 5, 5, 0, 150},
    {"rook_on_7th_mg", 6, 6, 0, 150},
    {"rook_on_7th_eg", 7, 7, 0, 150},
    {"bishop_pair", 8, 8, 0, 200},
    {"pawn_shield_rank1", 9, 9, 0, 100},
    {"pawn_shield_rank2", 10, 10, 0, 100},
    {"pawn_shield_hole", 11, 11, -100, 0},
    {"passed_pawn_rank2", 12, 12, 0, 200},
    {"passed_pawn_rank3", 13, 13, 0, 200},
    {"passed_pawn_rank4", 14, 14, 0, 200},
    {"passed_pawn_rank5", 15, 15, 0, 200},
    {"passed_pawn_rank6", 16, 16, 0, 200},
    {"passed_pawn_rank7", 17, 17, 0, 200},
    {"knight_mobility", 18, 18, 0, 15},
    {"bishop_mobility", 19, 19, 0, 15},
    {"rook_mobility", 20, 20, 0, 15},
    {"queen_mobility", 21, 21, 0, 15},
    {"psq_table_pawn", 22, 85, -200, 200},
    {"psq_table_knight", 86, 149, -200, 200},
    {"psq_table_bishop", 150, 213, -200, 200},
    {"psq_table_rook", 214, 277, -200, 200},
    {"psq_table_king_mg", 278, 341, -200, 200},
    {"psq_table_king_eg", 342, 405, -200, 200}
};

void tuning_param_assign_current(struct tuning_param *params)
{
    int start;
    int stop;
    int k;

    start = parameter_declarations[TP_DOUBLE_PAWNS].start;
    DOUBLE_PAWNS = params[start].current;

    start = parameter_declarations[TP_ISOLATED_PAWN].start;
    ISOLATED_PAWN = params[start].current;

    start = parameter_declarations[TP_ROOK_OPEN_FILE].start;
    ROOK_OPEN_FILE = params[start].current;

    start = parameter_declarations[TP_ROOK_HALF_OPEN_FILE].start;
    ROOK_HALF_OPEN_FILE = params[start].current;

    start = parameter_declarations[TP_QUEEN_OPEN_FILE].start;
    QUEEN_OPEN_FILE = params[start].current;

    start = parameter_declarations[TP_QUEEN_HALF_OPEN_FILE].start;
    QUEEN_HALF_OPEN_FILE = params[start].current;

    start = parameter_declarations[TP_ROOK_ON_7TH_MG].start;
    ROOK_ON_7TH_MG = params[start].current;

    start = parameter_declarations[TP_ROOK_ON_7TH_EG].start;
    ROOK_ON_7TH_EG = params[start].current;

    start = parameter_declarations[TP_BISHOP_PAIR].start;
    BISHOP_PAIR = params[start].current;

    start = parameter_declarations[TP_PAWN_SHIELD_RANK1].start;
    PAWN_SHIELD_RANK1 = params[start].current;

    start = parameter_declarations[TP_PAWN_SHIELD_RANK2].start;
    PAWN_SHIELD_RANK2 = params[start].current;

    start = parameter_declarations[TP_PAWN_SHIELD_HOLE].start;
    PAWN_SHIELD_HOLE = params[start].current;

    start = parameter_declarations[TP_PASSED_PAWN_RANK2].start;
    PASSED_PAWN_RANK2 = params[start].current;

    start = parameter_declarations[TP_PASSED_PAWN_RANK3].start;
    PASSED_PAWN_RANK3 = params[start].current;

    start = parameter_declarations[TP_PASSED_PAWN_RANK4].start;
    PASSED_PAWN_RANK4 = params[start].current;

    start = parameter_declarations[TP_PASSED_PAWN_RANK5].start;
    PASSED_PAWN_RANK5 = params[start].current;

    start = parameter_declarations[TP_PASSED_PAWN_RANK6].start;
    PASSED_PAWN_RANK6 = params[start].current;

    start = parameter_declarations[TP_PASSED_PAWN_RANK7].start;
    PASSED_PAWN_RANK7 = params[start].current;

    start = parameter_declarations[TP_KNIGHT_MOBILITY].start;
    KNIGHT_MOBILITY = params[start].current;

    start = parameter_declarations[TP_BISHOP_MOBILITY].start;
    BISHOP_MOBILITY = params[start].current;

    start = parameter_declarations[TP_ROOK_MOBILITY].start;
    ROOK_MOBILITY = params[start].current;

    start = parameter_declarations[TP_QUEEN_MOBILITY].start;
    QUEEN_MOBILITY = params[start].current;

    start = parameter_declarations[TP_PSQ_TABLE_PAWN].start;
    stop = parameter_declarations[TP_PSQ_TABLE_PAWN].stop;
    for (k=start;k<=stop;k++) {
        PSQ_TABLE_PAWN[k-start] = params[k].current;
    }

    start = parameter_declarations[TP_PSQ_TABLE_KNIGHT].start;
    stop = parameter_declarations[TP_PSQ_TABLE_KNIGHT].stop;
    for (k=start;k<=stop;k++) {
        PSQ_TABLE_KNIGHT[k-start] = params[k].current;
    }

    start = parameter_declarations[TP_PSQ_TABLE_BISHOP].start;
    stop = parameter_declarations[TP_PSQ_TABLE_BISHOP].stop;
    for (k=start;k<=stop;k++) {
        PSQ_TABLE_BISHOP[k-start] = params[k].current;
    }

    start = parameter_declarations[TP_PSQ_TABLE_ROOK].start;
    stop = parameter_declarations[TP_PSQ_TABLE_ROOK].stop;
    for (k=start;k<=stop;k++) {
        PSQ_TABLE_ROOK[k-start] = params[k].current;
    }

    start = parameter_declarations[TP_PSQ_TABLE_KING_MG].start;
    stop = parameter_declarations[TP_PSQ_TABLE_KING_MG].stop;
    for (k=start;k<=stop;k++) {
        PSQ_TABLE_KING_MG[k-start] = params[k].current;
    }

    start = parameter_declarations[TP_PSQ_TABLE_KING_EG].start;
    stop = parameter_declarations[TP_PSQ_TABLE_KING_EG].stop;
    for (k=start;k<=stop;k++) {
        PSQ_TABLE_KING_EG[k-start] = params[k].current;
    }

    eval_reset();
}

struct tuning_param* tuning_param_create_list(void)
{
    struct tuning_param *params;
    int                 start;
    int                 stop;
    int                 k;

    params = malloc(sizeof(struct tuning_param)*NUM_TUNING_PARAMS);

    start = parameter_declarations[TP_DOUBLE_PAWNS].start;
    params[start].min = parameter_declarations[TP_DOUBLE_PAWNS].min;
    params[start].max = parameter_declarations[TP_DOUBLE_PAWNS].max;
    params[start].current = DOUBLE_PAWNS;
    params[start].active = false;

    start = parameter_declarations[TP_ISOLATED_PAWN].start;
    params[start].min = parameter_declarations[TP_ISOLATED_PAWN].min;
    params[start].max = parameter_declarations[TP_ISOLATED_PAWN].max;
    params[start].current = ISOLATED_PAWN;
    params[start].active = false;

    start = parameter_declarations[TP_ROOK_OPEN_FILE].start;
    params[start].min = parameter_declarations[TP_ROOK_OPEN_FILE].min;
    params[start].max = parameter_declarations[TP_ROOK_OPEN_FILE].max;
    params[start].current = ROOK_OPEN_FILE;
    params[start].active = false;

    start = parameter_declarations[TP_ROOK_HALF_OPEN_FILE].start;
    params[start].min = parameter_declarations[TP_ROOK_HALF_OPEN_FILE].min;
    params[start].max = parameter_declarations[TP_ROOK_HALF_OPEN_FILE].max;
    params[start].current = ROOK_HALF_OPEN_FILE;
    params[start].active = false;

    start = parameter_declarations[TP_QUEEN_OPEN_FILE].start;
    params[start].min = parameter_declarations[TP_QUEEN_OPEN_FILE].min;
    params[start].max = parameter_declarations[TP_QUEEN_OPEN_FILE].max;
    params[start].current = QUEEN_OPEN_FILE;
    params[start].active = false;

    start = parameter_declarations[TP_QUEEN_HALF_OPEN_FILE].start;
    params[start].min = parameter_declarations[TP_QUEEN_HALF_OPEN_FILE].min;
    params[start].max = parameter_declarations[TP_QUEEN_HALF_OPEN_FILE].max;
    params[start].current = QUEEN_HALF_OPEN_FILE;
    params[start].active = false;

    start = parameter_declarations[TP_ROOK_ON_7TH_MG].start;
    params[start].min = parameter_declarations[TP_ROOK_ON_7TH_MG].min;
    params[start].max = parameter_declarations[TP_ROOK_ON_7TH_MG].max;
    params[start].current = ROOK_ON_7TH_MG;
    params[start].active = false;

    start = parameter_declarations[TP_ROOK_ON_7TH_EG].start;
    params[start].min = parameter_declarations[TP_ROOK_ON_7TH_EG].min;
    params[start].max = parameter_declarations[TP_ROOK_ON_7TH_EG].max;
    params[start].current = ROOK_ON_7TH_EG;
    params[start].active = false;

    start = parameter_declarations[TP_BISHOP_PAIR].start;
    params[start].min = parameter_declarations[TP_BISHOP_PAIR].min;
    params[start].max = parameter_declarations[TP_BISHOP_PAIR].max;
    params[start].current = BISHOP_PAIR;
    params[start].active = false;

    start = parameter_declarations[TP_PAWN_SHIELD_RANK1].start;
    params[start].min = parameter_declarations[TP_PAWN_SHIELD_RANK1].min;
    params[start].max = parameter_declarations[TP_PAWN_SHIELD_RANK1].max;
    params[start].current = PAWN_SHIELD_RANK1;
    params[start].active = false;

    start = parameter_declarations[TP_PAWN_SHIELD_RANK2].start;
    params[start].min = parameter_declarations[TP_PAWN_SHIELD_RANK2].min;
    params[start].max = parameter_declarations[TP_PAWN_SHIELD_RANK2].max;
    params[start].current = PAWN_SHIELD_RANK2;
    params[start].active = false;

    start = parameter_declarations[TP_PAWN_SHIELD_HOLE].start;
    params[start].min = parameter_declarations[TP_PAWN_SHIELD_HOLE].min;
    params[start].max = parameter_declarations[TP_PAWN_SHIELD_HOLE].max;
    params[start].current = PAWN_SHIELD_HOLE;
    params[start].active = false;

    start = parameter_declarations[TP_PASSED_PAWN_RANK2].start;
    params[start].min = parameter_declarations[TP_PASSED_PAWN_RANK2].min;
    params[start].max = parameter_declarations[TP_PASSED_PAWN_RANK2].max;
    params[start].current = PASSED_PAWN_RANK2;
    params[start].active = false;

    start = parameter_declarations[TP_PASSED_PAWN_RANK3].start;
    params[start].min = parameter_declarations[TP_PASSED_PAWN_RANK3].min;
    params[start].max = parameter_declarations[TP_PASSED_PAWN_RANK3].max;
    params[start].current = PASSED_PAWN_RANK3;
    params[start].active = false;

    start = parameter_declarations[TP_PASSED_PAWN_RANK4].start;
    params[start].min = parameter_declarations[TP_PASSED_PAWN_RANK4].min;
    params[start].max = parameter_declarations[TP_PASSED_PAWN_RANK4].max;
    params[start].current = PASSED_PAWN_RANK4;
    params[start].active = false;

    start = parameter_declarations[TP_PASSED_PAWN_RANK5].start;
    params[start].min = parameter_declarations[TP_PASSED_PAWN_RANK5].min;
    params[start].max = parameter_declarations[TP_PASSED_PAWN_RANK5].max;
    params[start].current = PASSED_PAWN_RANK5;
    params[start].active = false;

    start = parameter_declarations[TP_PASSED_PAWN_RANK6].start;
    params[start].min = parameter_declarations[TP_PASSED_PAWN_RANK6].min;
    params[start].max = parameter_declarations[TP_PASSED_PAWN_RANK6].max;
    params[start].current = PASSED_PAWN_RANK6;
    params[start].active = false;

    start = parameter_declarations[TP_PASSED_PAWN_RANK7].start;
    params[start].min = parameter_declarations[TP_PASSED_PAWN_RANK7].min;
    params[start].max = parameter_declarations[TP_PASSED_PAWN_RANK7].max;
    params[start].current = PASSED_PAWN_RANK7;
    params[start].active = false;

    start = parameter_declarations[TP_KNIGHT_MOBILITY].start;
    params[start].min = parameter_declarations[TP_KNIGHT_MOBILITY].min;
    params[start].max = parameter_declarations[TP_KNIGHT_MOBILITY].max;
    params[start].current = KNIGHT_MOBILITY;
    params[start].active = false;

    start = parameter_declarations[TP_BISHOP_MOBILITY].start;
    params[start].min = parameter_declarations[TP_BISHOP_MOBILITY].min;
    params[start].max = parameter_declarations[TP_BISHOP_MOBILITY].max;
    params[start].current = BISHOP_MOBILITY;
    params[start].active = false;

    start = parameter_declarations[TP_ROOK_MOBILITY].start;
    params[start].min = parameter_declarations[TP_ROOK_MOBILITY].min;
    params[start].max = parameter_declarations[TP_ROOK_MOBILITY].max;
    params[start].current = ROOK_MOBILITY;
    params[start].active = false;

    start = parameter_declarations[TP_QUEEN_MOBILITY].start;
    params[start].min = parameter_declarations[TP_QUEEN_MOBILITY].min;
    params[start].max = parameter_declarations[TP_QUEEN_MOBILITY].max;
    params[start].current = QUEEN_MOBILITY;
    params[start].active = false;

    start = parameter_declarations[TP_PSQ_TABLE_PAWN].start;
    stop = parameter_declarations[TP_PSQ_TABLE_PAWN].stop;
    for (k=start;k<=stop;k++) {
        params[k].min = parameter_declarations[TP_PSQ_TABLE_PAWN].min;
        params[k].max = parameter_declarations[TP_PSQ_TABLE_PAWN].max;
        params[k].current = PSQ_TABLE_PAWN[k-start];
        params[k].active = false;
    }

    start = parameter_declarations[TP_PSQ_TABLE_KNIGHT].start;
    stop = parameter_declarations[TP_PSQ_TABLE_KNIGHT].stop;
    for (k=start;k<=stop;k++) {
        params[k].min = parameter_declarations[TP_PSQ_TABLE_KNIGHT].min;
        params[k].max = parameter_declarations[TP_PSQ_TABLE_KNIGHT].max;
        params[k].current = PSQ_TABLE_KNIGHT[k-start];
        params[k].active = false;
    }

    start = parameter_declarations[TP_PSQ_TABLE_BISHOP].start;
    stop = parameter_declarations[TP_PSQ_TABLE_BISHOP].stop;
    for (k=start;k<=stop;k++) {
        params[k].min = parameter_declarations[TP_PSQ_TABLE_BISHOP].min;
        params[k].max = parameter_declarations[TP_PSQ_TABLE_BISHOP].max;
        params[k].current = PSQ_TABLE_BISHOP[k-start];
        params[k].active = false;
    }

    start = parameter_declarations[TP_PSQ_TABLE_ROOK].start;
    stop = parameter_declarations[TP_PSQ_TABLE_ROOK].stop;
    for (k=start;k<=stop;k++) {
        params[k].min = parameter_declarations[TP_PSQ_TABLE_ROOK].min;
        params[k].max = parameter_declarations[TP_PSQ_TABLE_ROOK].max;
        params[k].current = PSQ_TABLE_ROOK[k-start];
        params[k].active = false;
    }

    start = parameter_declarations[TP_PSQ_TABLE_KING_MG].start;
    stop = parameter_declarations[TP_PSQ_TABLE_KING_MG].stop;
    for (k=start;k<=stop;k++) {
        params[k].min = parameter_declarations[TP_PSQ_TABLE_KING_MG].min;
        params[k].max = parameter_declarations[TP_PSQ_TABLE_KING_MG].max;
        params[k].current = PSQ_TABLE_KING_MG[k-start];
        params[k].active = false;
    }

    start = parameter_declarations[TP_PSQ_TABLE_KING_EG].start;
    stop = parameter_declarations[TP_PSQ_TABLE_KING_EG].stop;
    for (k=start;k<=stop;k++) {
        params[k].min = parameter_declarations[TP_PSQ_TABLE_KING_EG].min;
        params[k].max = parameter_declarations[TP_PSQ_TABLE_KING_EG].max;
        params[k].current = PSQ_TABLE_KING_EG[k-start];
        params[k].active = false;
    }

    return params;
}

void tuning_param_destroy_list(struct tuning_param *params)
{
    assert(params != NULL);

    free(params);
}

struct param_decl* tuning_param_lookup(char *name)
{
    int k;

    for (k=0;k<NUM_PARAM_DECLARATIONS;k++) {
        if (!strcmp(name, parameter_declarations[k].name)) {
            return &parameter_declarations[k];
        }
    }

    return NULL;
}

void tuning_param_write_parameters(FILE *fp, struct tuning_param *params,
                                   bool active_only)
{
    int k;
    int l;
    int start;
    int stop;

    for (k=0;k<NUM_PARAM_DECLARATIONS;k++) {
        start = parameter_declarations[k].start;
        stop = parameter_declarations[k].stop;
        if (active_only && !params[start].active) {
            continue;
        }

        if (start == stop) {
            fprintf(fp, "%s %d\n", parameter_declarations[k].name,
                    params[start].current);
        } else {
            fprintf(fp, "%s {", parameter_declarations[k].name);
            for (l=start;l<=stop;l++) {
                fprintf(fp, "%d", params[l].current);
                if (l < stop) {
                    fprintf(fp, ", ");
                }
            }
            fprintf(fp, "}\n");
        }
    }
}
