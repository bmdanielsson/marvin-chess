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

/* Definitions for all tunable parameters */
struct param_decl parameter_declarations[NUM_PARAM_DECLARATIONS] = {
    {"double_pawns_mg", 0, 0, -150, 0},
    {"double_pawns_eg", 1, 1, -150, 0},
    {"isolated_pawn_mg", 2, 2, -150, 0},
    {"isolated_pawn_eg", 3, 3, -150, 0},
    {"rook_open_file_mg", 4, 4, 0, 150},
    {"rook_open_file_eg", 5, 5, 0, 150},
    {"rook_half_open_file_mg", 6, 6, 0, 150},
    {"rook_half_open_file_eg", 7, 7, 0, 150},
    {"queen_open_file_mg", 8, 8, 0, 150},
    {"queen_open_file_eg", 9, 9, 0, 150},
    {"queen_half_open_file_mg", 10, 10, 0, 150},
    {"queen_half_open_file_eg", 11, 11, 0, 150},
    {"rook_on_7th_mg", 12, 12, 0, 150},
    {"rook_on_7th_eg", 13, 13, 0, 150},
    {"bishop_pair_mg", 14, 14, 0, 200},
    {"bishop_pair_eg", 15, 15, 0, 200},
    {"pawn_shield_rank1", 16, 16, 0, 100},
    {"pawn_shield_rank2", 17, 17, 0, 100},
    {"pawn_shield_hole", 18, 18, -100, 0},
    {"passed_pawn_rank2_mg", 19, 19, 0, 200},
    {"passed_pawn_rank3_mg", 20, 20, 0, 200},
    {"passed_pawn_rank4_mg", 21, 21, 0, 200},
    {"passed_pawn_rank5_mg", 22, 22, 0, 200},
    {"passed_pawn_rank6_mg", 23, 23, 0, 200},
    {"passed_pawn_rank7_mg", 24, 24, 0, 200},
    {"passed_pawn_rank2_eg", 25, 25, 0, 200},
    {"passed_pawn_rank3_eg", 26, 26, 0, 200},
    {"passed_pawn_rank4_eg", 27, 27, 0, 200},
    {"passed_pawn_rank5_eg", 28, 28, 0, 200},
    {"passed_pawn_rank6_eg", 29, 29, 0, 200},
    {"passed_pawn_rank7_eg", 30, 30, 0, 200},
    {"knight_mobility_mg", 31, 31, 0, 15},
    {"bishop_mobility_mg", 32, 32, 0, 15},
    {"rook_mobility_mg", 33, 33, 0, 15},
    {"queen_mobility_mg", 34, 34, 0, 15},
    {"knight_mobility_eg", 35, 35, 0, 15},
    {"bishop_mobility_eg", 36, 36, 0, 15},
    {"rook_mobility_eg", 37, 37, 0, 15},
    {"queen_mobility_eg", 38, 38, 0, 15},
    {"psq_table_pawn_mg", 39, 102, -200, 200},
    {"psq_table_knight_mg", 103, 166, -200, 200},
    {"psq_table_bishop_mg", 167, 230, -200, 200},
    {"psq_table_rook_mg", 231, 294, -200, 200},
    {"psq_table_queen_mg", 295, 358, -200, 200},
    {"psq_table_king_mg", 359, 422, -200, 200},
    {"psq_table_pawn_eg", 423, 486, -200, 200},
    {"psq_table_knight_eg", 487, 550, -200, 200},
    {"psq_table_bishop_eg", 551, 614, -200, 200},
    {"psq_table_rook_eg", 615, 678, -200, 200},
    {"psq_table_queen_eg", 679, 742, -200, 200},
    {"psq_table_king_eg", 743, 806, -200, 200},
    {"knight_material_value_mg", 807, 807, 200, 600},
    {"bishop_material_value_mg", 808, 808, 200, 600},
    {"rook_material_value_mg", 809, 809, 400, 800},
    {"queen_material_value_mg", 810, 810, 700, 1600},
    {"knight_material_value_eg", 811, 811, 200, 600},
    {"bishop_material_value_eg", 812, 812, 200, 600},
    {"rook_material_value_eg", 813, 813, 400, 800},
    {"queen_material_value_eg", 814, 814, 700, 1600},
    {"king_attack_scale_mg", 815, 815, 0, 100},
    {"king_attack_scale_eg", 816, 816, 0, 100},
    {"knight_outpost", 817, 817, 0, 100},
    {"protected_knight_outpost", 818, 818, 0, 100},
    {"candidate_passed_pawn_rank2_mg", 819, 819, 0, 200},
    {"candidate_passed_pawn_rank3_mg", 820, 820, 0, 200},
    {"candidate_passed_pawn_rank4_mg", 821, 821, 0, 200},
    {"candidate_passed_pawn_rank5_mg", 822, 822, 0, 200},
    {"candidate_passed_pawn_rank6_mg", 823, 823, 0, 200},
    {"candidate_passed_pawn_rank2_eg", 824, 824, 0, 200},
    {"candidate_passed_pawn_rank3_eg", 825, 825, 0, 200},
    {"candidate_passed_pawn_rank4_eg", 826, 826, 0, 200},
    {"candidate_passed_pawn_rank5_eg", 827, 827, 0, 200},
    {"candidate_passed_pawn_rank6_eg", 828, 828, 0, 200},
};

void tuning_param_assign_current(struct tuning_param *params)
{
    int start;
    int stop;
    int k;

    ASSIGN(TP_DOUBLE_PAWNS_MG, DOUBLE_PAWNS_MG)
    ASSIGN(TP_DOUBLE_PAWNS_EG, DOUBLE_PAWNS_EG)
    ASSIGN(TP_ISOLATED_PAWN_MG, ISOLATED_PAWN_MG)
    ASSIGN(TP_ISOLATED_PAWN_EG, ISOLATED_PAWN_EG)
    ASSIGN(TP_ROOK_OPEN_FILE_MG, ROOK_OPEN_FILE_MG)
    ASSIGN(TP_ROOK_OPEN_FILE_EG, ROOK_OPEN_FILE_EG)
    ASSIGN(TP_ROOK_HALF_OPEN_FILE_MG, ROOK_HALF_OPEN_FILE_MG)
    ASSIGN(TP_ROOK_HALF_OPEN_FILE_EG, ROOK_HALF_OPEN_FILE_EG)
    ASSIGN(TP_QUEEN_OPEN_FILE_MG, QUEEN_OPEN_FILE_MG)
    ASSIGN(TP_QUEEN_OPEN_FILE_EG, QUEEN_OPEN_FILE_EG)
    ASSIGN(TP_QUEEN_HALF_OPEN_FILE_MG, QUEEN_HALF_OPEN_FILE_MG)
    ASSIGN(TP_QUEEN_HALF_OPEN_FILE_EG, QUEEN_HALF_OPEN_FILE_EG)
    ASSIGN(TP_ROOK_ON_7TH_MG, ROOK_ON_7TH_MG)
    ASSIGN(TP_ROOK_ON_7TH_EG, ROOK_ON_7TH_EG)
    ASSIGN(TP_BISHOP_PAIR_MG, BISHOP_PAIR_MG)
    ASSIGN(TP_BISHOP_PAIR_EG, BISHOP_PAIR_EG)
    ASSIGN(TP_PAWN_SHIELD_RANK1, PAWN_SHIELD_RANK1)
    ASSIGN(TP_PAWN_SHIELD_RANK2, PAWN_SHIELD_RANK2)
    ASSIGN(TP_PAWN_SHIELD_HOLE, PAWN_SHIELD_HOLE)
    ASSIGN(TP_PASSED_PAWN_RANK2_MG, PASSED_PAWN_RANK2_MG)
    ASSIGN(TP_PASSED_PAWN_RANK3_MG, PASSED_PAWN_RANK3_MG)
    ASSIGN(TP_PASSED_PAWN_RANK4_MG, PASSED_PAWN_RANK4_MG)
    ASSIGN(TP_PASSED_PAWN_RANK5_MG, PASSED_PAWN_RANK5_MG)
    ASSIGN(TP_PASSED_PAWN_RANK6_MG, PASSED_PAWN_RANK6_MG)
    ASSIGN(TP_PASSED_PAWN_RANK7_MG, PASSED_PAWN_RANK7_MG)
    ASSIGN(TP_PASSED_PAWN_RANK2_EG, PASSED_PAWN_RANK2_EG)
    ASSIGN(TP_PASSED_PAWN_RANK3_EG, PASSED_PAWN_RANK3_EG)
    ASSIGN(TP_PASSED_PAWN_RANK4_EG, PASSED_PAWN_RANK4_EG)
    ASSIGN(TP_PASSED_PAWN_RANK5_EG, PASSED_PAWN_RANK5_EG)
    ASSIGN(TP_PASSED_PAWN_RANK6_EG, PASSED_PAWN_RANK6_EG)
    ASSIGN(TP_PASSED_PAWN_RANK7_EG, PASSED_PAWN_RANK7_EG)
    ASSIGN(TP_KNIGHT_MOBILITY_MG, KNIGHT_MOBILITY_MG)
    ASSIGN(TP_BISHOP_MOBILITY_MG, BISHOP_MOBILITY_MG)
    ASSIGN(TP_ROOK_MOBILITY_MG, ROOK_MOBILITY_MG)
    ASSIGN(TP_QUEEN_MOBILITY_MG, QUEEN_MOBILITY_MG)
    ASSIGN(TP_KNIGHT_MOBILITY_EG, KNIGHT_MOBILITY_EG)
    ASSIGN(TP_BISHOP_MOBILITY_EG, BISHOP_MOBILITY_EG)
    ASSIGN(TP_ROOK_MOBILITY_EG, ROOK_MOBILITY_EG)
    ASSIGN(TP_QUEEN_MOBILITY_EG, QUEEN_MOBILITY_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_PAWN_MG, PSQ_TABLE_PAWN_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KNIGHT_MG, PSQ_TABLE_KNIGHT_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_BISHOP_MG, PSQ_TABLE_BISHOP_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_ROOK_MG, PSQ_TABLE_ROOK_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_QUEEN_MG, PSQ_TABLE_QUEEN_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KING_MG, PSQ_TABLE_KING_MG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_PAWN_EG, PSQ_TABLE_PAWN_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KNIGHT_EG, PSQ_TABLE_KNIGHT_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_BISHOP_EG, PSQ_TABLE_BISHOP_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_ROOK_EG, PSQ_TABLE_ROOK_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_QUEEN_EG, PSQ_TABLE_QUEEN_EG)
    ASSIGN_MULTIPLE(TP_PSQ_TABLE_KING_EG, PSQ_TABLE_KING_EG)
    ASSIGN(TP_KNIGHT_MATERIAL_VALUE_MG, KNIGHT_MATERIAL_VALUE_MG)
    ASSIGN(TP_BISHOP_MATERIAL_VALUE_MG, BISHOP_MATERIAL_VALUE_MG)
    ASSIGN(TP_ROOK_MATERIAL_VALUE_MG, ROOK_MATERIAL_VALUE_MG)
    ASSIGN(TP_QUEEN_MATERIAL_VALUE_MG, QUEEN_MATERIAL_VALUE_MG)
    ASSIGN(TP_KNIGHT_MATERIAL_VALUE_EG, KNIGHT_MATERIAL_VALUE_EG)
    ASSIGN(TP_BISHOP_MATERIAL_VALUE_EG, BISHOP_MATERIAL_VALUE_EG)
    ASSIGN(TP_ROOK_MATERIAL_VALUE_EG, ROOK_MATERIAL_VALUE_EG)
    ASSIGN(TP_QUEEN_MATERIAL_VALUE_EG, QUEEN_MATERIAL_VALUE_EG)
    ASSIGN(TP_KING_ATTACK_SCALE_MG, KING_ATTACK_SCALE_MG)
    ASSIGN(TP_KING_ATTACK_SCALE_EG, KING_ATTACK_SCALE_EG)
    ASSIGN(TP_KNIGHT_OUTPOST, KNIGHT_OUTPOST)
    ASSIGN(TP_PROTECTED_KNIGHT_OUTPOST, PROTECTED_KNIGHT_OUTPOST)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK2_MG, CANDIDATE_PASSED_PAWN_RANK2_MG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK3_MG, CANDIDATE_PASSED_PAWN_RANK3_MG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK4_MG, CANDIDATE_PASSED_PAWN_RANK4_MG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK5_MG, CANDIDATE_PASSED_PAWN_RANK5_MG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK6_MG, CANDIDATE_PASSED_PAWN_RANK6_MG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK2_EG, CANDIDATE_PASSED_PAWN_RANK2_EG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK3_EG, CANDIDATE_PASSED_PAWN_RANK3_EG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK4_EG, CANDIDATE_PASSED_PAWN_RANK4_EG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK5_EG, CANDIDATE_PASSED_PAWN_RANK5_EG)
    ASSIGN(TP_CANDIDATE_PASSED_PAWN_RANK6_EG, CANDIDATE_PASSED_PAWN_RANK6_EG)

    eval_reset();
}

struct tuning_param* tuning_param_create_list(void)
{
    struct tuning_param *params;
    int                 start;
    int                 stop;
    int                 k;

    params = malloc(sizeof(struct tuning_param)*NUM_TUNING_PARAMS);

    DEFINE(TP_DOUBLE_PAWNS_MG, DOUBLE_PAWNS_MG)
    DEFINE(TP_DOUBLE_PAWNS_EG, DOUBLE_PAWNS_EG)
    DEFINE(TP_ISOLATED_PAWN_MG, ISOLATED_PAWN_MG)
    DEFINE(TP_ISOLATED_PAWN_EG, ISOLATED_PAWN_EG)
    DEFINE(TP_ROOK_OPEN_FILE_MG, ROOK_OPEN_FILE_MG)
    DEFINE(TP_ROOK_OPEN_FILE_EG, ROOK_OPEN_FILE_EG)
    DEFINE(TP_ROOK_HALF_OPEN_FILE_MG, ROOK_HALF_OPEN_FILE_MG)
    DEFINE(TP_ROOK_HALF_OPEN_FILE_EG, ROOK_HALF_OPEN_FILE_EG)
    DEFINE(TP_QUEEN_OPEN_FILE_MG, QUEEN_OPEN_FILE_MG)
    DEFINE(TP_QUEEN_OPEN_FILE_EG, QUEEN_OPEN_FILE_EG)
    DEFINE(TP_QUEEN_HALF_OPEN_FILE_MG, QUEEN_HALF_OPEN_FILE_MG)
    DEFINE(TP_QUEEN_HALF_OPEN_FILE_EG, QUEEN_HALF_OPEN_FILE_EG)
    DEFINE(TP_ROOK_ON_7TH_MG, ROOK_ON_7TH_MG)
    DEFINE(TP_ROOK_ON_7TH_EG, ROOK_ON_7TH_EG)
    DEFINE(TP_BISHOP_PAIR_MG, BISHOP_PAIR_MG)
    DEFINE(TP_BISHOP_PAIR_EG, BISHOP_PAIR_EG)
    DEFINE(TP_PAWN_SHIELD_RANK1, PAWN_SHIELD_RANK1)
    DEFINE(TP_PAWN_SHIELD_RANK2, PAWN_SHIELD_RANK2)
    DEFINE(TP_PAWN_SHIELD_HOLE, PAWN_SHIELD_HOLE)
    DEFINE(TP_PASSED_PAWN_RANK2_MG, PASSED_PAWN_RANK2_MG)
    DEFINE(TP_PASSED_PAWN_RANK3_MG, PASSED_PAWN_RANK3_MG)
    DEFINE(TP_PASSED_PAWN_RANK4_MG, PASSED_PAWN_RANK4_MG)
    DEFINE(TP_PASSED_PAWN_RANK5_MG, PASSED_PAWN_RANK5_MG)
    DEFINE(TP_PASSED_PAWN_RANK6_MG, PASSED_PAWN_RANK6_MG)
    DEFINE(TP_PASSED_PAWN_RANK7_MG, PASSED_PAWN_RANK7_MG)
    DEFINE(TP_PASSED_PAWN_RANK2_EG, PASSED_PAWN_RANK2_EG)
    DEFINE(TP_PASSED_PAWN_RANK3_EG, PASSED_PAWN_RANK3_EG)
    DEFINE(TP_PASSED_PAWN_RANK4_EG, PASSED_PAWN_RANK4_EG)
    DEFINE(TP_PASSED_PAWN_RANK5_EG, PASSED_PAWN_RANK5_EG)
    DEFINE(TP_PASSED_PAWN_RANK6_EG, PASSED_PAWN_RANK6_EG)
    DEFINE(TP_PASSED_PAWN_RANK7_EG, PASSED_PAWN_RANK7_EG)
    DEFINE(TP_KNIGHT_MOBILITY_MG, KNIGHT_MOBILITY_MG)
    DEFINE(TP_BISHOP_MOBILITY_MG, BISHOP_MOBILITY_MG)
    DEFINE(TP_ROOK_MOBILITY_MG, ROOK_MOBILITY_MG)
    DEFINE(TP_QUEEN_MOBILITY_MG, QUEEN_MOBILITY_MG)
    DEFINE(TP_KNIGHT_MOBILITY_EG, KNIGHT_MOBILITY_EG)
    DEFINE(TP_BISHOP_MOBILITY_EG, BISHOP_MOBILITY_EG)
    DEFINE(TP_ROOK_MOBILITY_EG, ROOK_MOBILITY_EG)
    DEFINE(TP_QUEEN_MOBILITY_EG, QUEEN_MOBILITY_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_PAWN_MG, PSQ_TABLE_PAWN_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KNIGHT_MG, PSQ_TABLE_KNIGHT_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_BISHOP_MG, PSQ_TABLE_BISHOP_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_ROOK_MG, PSQ_TABLE_ROOK_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_QUEEN_MG, PSQ_TABLE_QUEEN_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KING_MG, PSQ_TABLE_KING_MG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_PAWN_EG, PSQ_TABLE_PAWN_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KNIGHT_EG, PSQ_TABLE_KNIGHT_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_BISHOP_EG, PSQ_TABLE_BISHOP_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_ROOK_EG, PSQ_TABLE_ROOK_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_QUEEN_EG, PSQ_TABLE_QUEEN_EG)
    DEFINE_MULTIPLE(TP_PSQ_TABLE_KING_EG, PSQ_TABLE_KING_EG)
    DEFINE(TP_KNIGHT_MATERIAL_VALUE_MG, KNIGHT_MATERIAL_VALUE_MG)
    DEFINE(TP_BISHOP_MATERIAL_VALUE_MG, BISHOP_MATERIAL_VALUE_MG)
    DEFINE(TP_ROOK_MATERIAL_VALUE_MG, ROOK_MATERIAL_VALUE_MG)
    DEFINE(TP_QUEEN_MATERIAL_VALUE_MG, QUEEN_MATERIAL_VALUE_MG)
    DEFINE(TP_KNIGHT_MATERIAL_VALUE_EG, KNIGHT_MATERIAL_VALUE_EG)
    DEFINE(TP_BISHOP_MATERIAL_VALUE_EG, BISHOP_MATERIAL_VALUE_EG)
    DEFINE(TP_ROOK_MATERIAL_VALUE_EG, ROOK_MATERIAL_VALUE_EG)
    DEFINE(TP_QUEEN_MATERIAL_VALUE_EG, QUEEN_MATERIAL_VALUE_EG)
    DEFINE(TP_KING_ATTACK_SCALE_MG, KING_ATTACK_SCALE_MG)
    DEFINE(TP_KING_ATTACK_SCALE_EG, KING_ATTACK_SCALE_EG)
    DEFINE(TP_KNIGHT_OUTPOST, KNIGHT_OUTPOST)
    DEFINE(TP_PROTECTED_KNIGHT_OUTPOST, PROTECTED_KNIGHT_OUTPOST)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK2_MG, CANDIDATE_PASSED_PAWN_RANK2_MG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK3_MG, CANDIDATE_PASSED_PAWN_RANK3_MG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK4_MG, CANDIDATE_PASSED_PAWN_RANK4_MG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK5_MG, CANDIDATE_PASSED_PAWN_RANK5_MG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK6_MG, CANDIDATE_PASSED_PAWN_RANK6_MG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK2_EG, CANDIDATE_PASSED_PAWN_RANK2_EG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK3_EG, CANDIDATE_PASSED_PAWN_RANK3_EG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK4_EG, CANDIDATE_PASSED_PAWN_RANK4_EG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK5_EG, CANDIDATE_PASSED_PAWN_RANK5_EG)
    DEFINE(TP_CANDIDATE_PASSED_PAWN_RANK6_EG, CANDIDATE_PASSED_PAWN_RANK6_EG)

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

int tuning_param_index(int decl)
{
    return parameter_declarations[decl].start;
}
