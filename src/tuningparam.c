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

/* Define a tuning parameter and the connection evaluation parameter */
#define DEFINE(tp, ep)                                      \
    start = parameter_declarations[(tp)].start;             \
    params[start].min = parameter_declarations[(tp)].min;   \
    params[start].max = parameter_declarations[(tp)].max;   \
    params[start].current = (ep);                           \
    params[start].active = false;

/*
 * Define a tuning parameter of array type and the connection
 * evaluation parameter.
 */
#define DEFINE_MULTIPLE(tp, ep)                             \
    start = parameter_declarations[(tp)].start;             \
    stop = parameter_declarations[(tp)].stop;               \
    for (k=start;k<=stop;k++) {                             \
        params[k].min = parameter_declarations[(tp)].min;   \
        params[k].max = parameter_declarations[(tp)].max;   \
        params[k].current = (ep)[k-start];                  \
        params[k].active = false;                           \
    }

/*
 * Assign the value of a tuning parameter to the corresponding
 * evaluation parameter.
 */
#define ASSIGN(tp, ep)                          \
    start = parameter_declarations[(tp)].start; \
    (ep) = params[start].current;

/*
 * Assign the value of a tuning parameter of array type to the corresponding
 * evaluation parameter.
 */
#define ASSIGN_MULTIPLE(tp, ep)                 \
    start = parameter_declarations[(tp)].start; \
    stop = parameter_declarations[(tp)].stop;   \
    for (k=start;k<=stop;k++) {                 \
        (ep)[k-start] = params[k].current;      \
    }

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
    TP_KNIGHT_MOBILITY_MG,
    TP_BISHOP_MOBILITY_MG,
    TP_ROOK_MOBILITY_MG,
    TP_QUEEN_MOBILITY_MG,
    TP_KNIGHT_MOBILITY_EG,
    TP_BISHOP_MOBILITY_EG,
    TP_ROOK_MOBILITY_EG,
    TP_QUEEN_MOBILITY_EG,
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
    {"knight_mobility_mg", 18, 18, 0, 15},
    {"bishop_mobility_mg", 19, 19, 0, 15},
    {"rook_mobility_mg", 20, 20, 0, 15},
    {"queen_mobility_mg", 21, 21, 0, 15},
    {"knight_mobility_eg", 22, 22, 0, 15},
    {"bishop_mobility_eg", 23, 23, 0, 15},
    {"rook_mobility_eg", 24, 24, 0, 15},
    {"queen_mobility_eg", 25, 25, 0, 15},
    {"psq_table_pawn", 26, 89, -200, 200},
    {"psq_table_knight", 90, 153, -200, 200},
    {"psq_table_bishop", 154, 217, -200, 200},
    {"psq_table_rook", 218, 281, -200, 200},
    {"psq_table_king_mg", 282, 345, -200, 200},
    {"psq_table_king_eg", 346, 409, -200, 200}
};

void tuning_param_assign_current(struct tuning_param *params)
{
    int start;
    int stop;
    int k;

    ASSIGN(TP_DOUBLE_PAWNS, DOUBLE_PAWNS)
    ASSIGN(TP_ISOLATED_PAWN, ISOLATED_PAWN)
    ASSIGN(TP_ROOK_OPEN_FILE, ROOK_OPEN_FILE)
    ASSIGN(TP_ROOK_HALF_OPEN_FILE, ROOK_HALF_OPEN_FILE)
    ASSIGN(TP_QUEEN_OPEN_FILE, QUEEN_OPEN_FILE)
    ASSIGN(TP_QUEEN_HALF_OPEN_FILE, QUEEN_HALF_OPEN_FILE)
    ASSIGN(TP_ROOK_ON_7TH_MG, ROOK_ON_7TH_MG)
    ASSIGN(TP_ROOK_ON_7TH_EG, ROOK_ON_7TH_EG)
    ASSIGN(TP_BISHOP_PAIR, BISHOP_PAIR)
    ASSIGN(TP_PAWN_SHIELD_RANK1, PAWN_SHIELD_RANK1)
    ASSIGN(TP_PAWN_SHIELD_RANK2, PAWN_SHIELD_RANK2)
    ASSIGN(TP_PAWN_SHIELD_HOLE, PAWN_SHIELD_HOLE)
    ASSIGN(TP_PASSED_PAWN_RANK2, PASSED_PAWN_RANK2)
    ASSIGN(TP_PASSED_PAWN_RANK3, PASSED_PAWN_RANK3)
    ASSIGN(TP_PASSED_PAWN_RANK4, PASSED_PAWN_RANK4)
    ASSIGN(TP_PASSED_PAWN_RANK5, PASSED_PAWN_RANK5)
    ASSIGN(TP_PASSED_PAWN_RANK6, PASSED_PAWN_RANK6)
    ASSIGN(TP_PASSED_PAWN_RANK7, PASSED_PAWN_RANK7)
    ASSIGN(TP_KNIGHT_MOBILITY_MG, KNIGHT_MOBILITY_MG)
    ASSIGN(TP_BISHOP_MOBILITY_MG, BISHOP_MOBILITY_MG)
    ASSIGN(TP_ROOK_MOBILITY_MG, ROOK_MOBILITY_MG)
    ASSIGN(TP_QUEEN_MOBILITY_MG, QUEEN_MOBILITY_MG)
    ASSIGN(TP_KNIGHT_MOBILITY_EG, KNIGHT_MOBILITY_EG)
    ASSIGN(TP_BISHOP_MOBILITY_EG, BISHOP_MOBILITY_EG)
    ASSIGN(TP_ROOK_MOBILITY_EG, ROOK_MOBILITY_EG)
    ASSIGN(TP_QUEEN_MOBILITY_EG, QUEEN_MOBILITY_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_PAWN, PSQ_TABLE_PAWN)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KNIGHT, PSQ_TABLE_KNIGHT)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_BISHOP, PSQ_TABLE_BISHOP)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_ROOK, PSQ_TABLE_ROOK)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KING_MG, PSQ_TABLE_KING_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KING_EG, PSQ_TABLE_KING_EG)

    eval_reset();
}

struct tuning_param* tuning_param_create_list(void)
{
    struct tuning_param *params;
    int                 start;
    int                 stop;
    int                 k;

    params = malloc(sizeof(struct tuning_param)*NUM_TUNING_PARAMS);

    DEFINE(TP_DOUBLE_PAWNS, DOUBLE_PAWNS)
    DEFINE(TP_ISOLATED_PAWN, ISOLATED_PAWN)
    DEFINE(TP_ROOK_OPEN_FILE, ROOK_OPEN_FILE)
    DEFINE(TP_ROOK_HALF_OPEN_FILE, ROOK_HALF_OPEN_FILE)
    DEFINE(TP_QUEEN_OPEN_FILE, QUEEN_OPEN_FILE)
    DEFINE(TP_QUEEN_HALF_OPEN_FILE, QUEEN_HALF_OPEN_FILE)
    DEFINE(TP_ROOK_ON_7TH_MG, ROOK_ON_7TH_MG)
    DEFINE(TP_ROOK_ON_7TH_EG, ROOK_ON_7TH_EG)
    DEFINE(TP_BISHOP_PAIR, BISHOP_PAIR)
    DEFINE(TP_PAWN_SHIELD_RANK1, PAWN_SHIELD_RANK1)
    DEFINE(TP_PAWN_SHIELD_RANK2, PAWN_SHIELD_RANK2)
    DEFINE(TP_PAWN_SHIELD_HOLE, PAWN_SHIELD_HOLE)
    DEFINE(TP_PASSED_PAWN_RANK2, PASSED_PAWN_RANK2)
    DEFINE(TP_PASSED_PAWN_RANK3, PASSED_PAWN_RANK3)
    DEFINE(TP_PASSED_PAWN_RANK4, PASSED_PAWN_RANK4)
    DEFINE(TP_PASSED_PAWN_RANK5, PASSED_PAWN_RANK5)
    DEFINE(TP_PASSED_PAWN_RANK6, PASSED_PAWN_RANK6)
    DEFINE(TP_PASSED_PAWN_RANK7, PASSED_PAWN_RANK7)
    DEFINE(TP_KNIGHT_MOBILITY_MG, KNIGHT_MOBILITY_MG)
    DEFINE(TP_BISHOP_MOBILITY_MG, BISHOP_MOBILITY_MG)
    DEFINE(TP_ROOK_MOBILITY_MG, ROOK_MOBILITY_MG)
    DEFINE(TP_QUEEN_MOBILITY_MG, QUEEN_MOBILITY_MG)
    DEFINE(TP_KNIGHT_MOBILITY_EG, KNIGHT_MOBILITY_EG)
    DEFINE(TP_BISHOP_MOBILITY_EG, BISHOP_MOBILITY_EG)
    DEFINE(TP_ROOK_MOBILITY_EG, ROOK_MOBILITY_EG)
    DEFINE(TP_QUEEN_MOBILITY_EG, QUEEN_MOBILITY_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_PAWN, PSQ_TABLE_PAWN)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KNIGHT, PSQ_TABLE_KNIGHT)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_BISHOP, PSQ_TABLE_BISHOP)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_ROOK, PSQ_TABLE_ROOK)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KING_MG, PSQ_TABLE_KING_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KING_EG, PSQ_TABLE_KING_EG)

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
