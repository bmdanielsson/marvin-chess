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
#define DEFINE(ep)                                                  \
    start = parameter_declarations[(TP_ ## ep)].start;              \
    params[start].min = parameter_declarations[(TP_ ## ep)].min;    \
    params[start].max = parameter_declarations[(TP_ ## ep)].max;    \
    params[start].current = (ep);                                   \
    params[start].active = false;

/*
 * Define a tuning parameter of array type and the connection
 * evaluation parameter.
 */
#define DEFINE_MULTIPLE(ep)                                         \
    start = parameter_declarations[(TP_ ## ep)].start;              \
    stop = parameter_declarations[(TP_ ## ep)].stop;                \
    for (k=start;k<=stop;k++) {                                     \
        params[k].min = parameter_declarations[(TP_ ## ep)].min;    \
        params[k].max = parameter_declarations[(TP_ ## ep)].max;    \
        params[k].current = (ep)[k-start];                          \
        params[k].active = false;                                   \
    }

/*
 * Assign the value of a tuning parameter to the corresponding
 * evaluation parameter.
 */
#define ASSIGN(ep)                                      \
    start = parameter_declarations[(TP_ ## ep)].start;  \
    (ep) = params[start].current;

/*
 * Assign the value of a tuning parameter of array type to the corresponding
 * evaluation parameter.
 */
#define ASSIGN_MULTIPLE(ep)                             \
    start = parameter_declarations[(TP_ ## ep)].start;  \
    stop = parameter_declarations[(TP_ ## ep)].stop;    \
    for (k=start;k<=stop;k++) {                         \
        (ep)[k-start] = params[k].current;              \
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
    {"friendly_king_passer_dist", 829, 829, -30, 0},
    {"opponent_king_passer_dist", 830, 830, 0, 30},
    {"backward_pawn_mg", 831, 831, -30, 0},
    {"backward_pawn_eg", 832, 832, -30, 0}
};

void tuning_param_assign_current(struct tuning_param *params)
{
    int start;
    int stop;
    int k;

    ASSIGN(DOUBLE_PAWNS_MG)
    ASSIGN(DOUBLE_PAWNS_EG)
    ASSIGN(ISOLATED_PAWN_MG)
    ASSIGN(ISOLATED_PAWN_EG)
    ASSIGN(ROOK_OPEN_FILE_MG)
    ASSIGN(ROOK_OPEN_FILE_EG)
    ASSIGN(ROOK_HALF_OPEN_FILE_MG)
    ASSIGN(ROOK_HALF_OPEN_FILE_EG)
    ASSIGN(QUEEN_OPEN_FILE_MG)
    ASSIGN(QUEEN_OPEN_FILE_EG)
    ASSIGN(QUEEN_HALF_OPEN_FILE_MG)
    ASSIGN(QUEEN_HALF_OPEN_FILE_EG)
    ASSIGN(ROOK_ON_7TH_MG)
    ASSIGN(ROOK_ON_7TH_EG)
    ASSIGN(BISHOP_PAIR_MG)
    ASSIGN(BISHOP_PAIR_EG)
    ASSIGN(PAWN_SHIELD_RANK1)
    ASSIGN(PAWN_SHIELD_RANK2)
    ASSIGN(PAWN_SHIELD_HOLE)
    ASSIGN(PASSED_PAWN_RANK2_MG)
    ASSIGN(PASSED_PAWN_RANK3_MG)
    ASSIGN(PASSED_PAWN_RANK4_MG)
    ASSIGN(PASSED_PAWN_RANK5_MG)
    ASSIGN(PASSED_PAWN_RANK6_MG)
    ASSIGN(PASSED_PAWN_RANK7_MG)
    ASSIGN(PASSED_PAWN_RANK2_EG)
    ASSIGN(PASSED_PAWN_RANK3_EG)
    ASSIGN(PASSED_PAWN_RANK4_EG)
    ASSIGN(PASSED_PAWN_RANK5_EG)
    ASSIGN(PASSED_PAWN_RANK6_EG)
    ASSIGN(PASSED_PAWN_RANK7_EG)
    ASSIGN(KNIGHT_MOBILITY_MG)
    ASSIGN(BISHOP_MOBILITY_MG)
    ASSIGN(ROOK_MOBILITY_MG)
    ASSIGN(QUEEN_MOBILITY_MG)
    ASSIGN(KNIGHT_MOBILITY_EG)
    ASSIGN(BISHOP_MOBILITY_EG)
    ASSIGN(ROOK_MOBILITY_EG)
    ASSIGN(QUEEN_MOBILITY_EG)
    ASSIGN_MULTIPLE(PSQ_TABLE_PAWN_MG)
    ASSIGN_MULTIPLE(PSQ_TABLE_KNIGHT_MG)
    ASSIGN_MULTIPLE(PSQ_TABLE_BISHOP_MG)
    ASSIGN_MULTIPLE(PSQ_TABLE_ROOK_MG)
    ASSIGN_MULTIPLE(PSQ_TABLE_QUEEN_MG)
    ASSIGN_MULTIPLE(PSQ_TABLE_KING_MG)
    ASSIGN_MULTIPLE(PSQ_TABLE_PAWN_EG)
    ASSIGN_MULTIPLE(PSQ_TABLE_KNIGHT_EG)
    ASSIGN_MULTIPLE(PSQ_TABLE_BISHOP_EG)
    ASSIGN_MULTIPLE(PSQ_TABLE_ROOK_EG)
    ASSIGN_MULTIPLE(PSQ_TABLE_QUEEN_EG)
    ASSIGN_MULTIPLE(PSQ_TABLE_KING_EG)
    ASSIGN(KNIGHT_MATERIAL_VALUE_MG)
    ASSIGN(BISHOP_MATERIAL_VALUE_MG)
    ASSIGN(ROOK_MATERIAL_VALUE_MG)
    ASSIGN(QUEEN_MATERIAL_VALUE_MG)
    ASSIGN(KNIGHT_MATERIAL_VALUE_EG)
    ASSIGN(BISHOP_MATERIAL_VALUE_EG)
    ASSIGN(ROOK_MATERIAL_VALUE_EG)
    ASSIGN(QUEEN_MATERIAL_VALUE_EG)
    ASSIGN(KING_ATTACK_SCALE_MG)
    ASSIGN(KING_ATTACK_SCALE_EG)
    ASSIGN(KNIGHT_OUTPOST)
    ASSIGN(PROTECTED_KNIGHT_OUTPOST)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK2_MG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK3_MG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK4_MG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK5_MG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK6_MG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK2_EG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK3_EG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK4_EG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK5_EG)
    ASSIGN(CANDIDATE_PASSED_PAWN_RANK6_EG)
    ASSIGN(FRIENDLY_KING_PASSER_DIST)
    ASSIGN(OPPONENT_KING_PASSER_DIST)
    ASSIGN(BACKWARD_PAWN_MG)
    ASSIGN(BACKWARD_PAWN_EG)

    eval_reset();
}

struct tuning_param* tuning_param_create_list(void)
{
    struct tuning_param *params;
    int                 start;
    int                 stop;
    int                 k;

    params = malloc(sizeof(struct tuning_param)*NUM_TUNING_PARAMS);

    DEFINE(DOUBLE_PAWNS_MG)
    DEFINE(DOUBLE_PAWNS_EG)
    DEFINE(ISOLATED_PAWN_MG)
    DEFINE(ISOLATED_PAWN_EG)
    DEFINE(ROOK_OPEN_FILE_MG)
    DEFINE(ROOK_OPEN_FILE_EG)
    DEFINE(ROOK_HALF_OPEN_FILE_MG)
    DEFINE(ROOK_HALF_OPEN_FILE_EG)
    DEFINE(QUEEN_OPEN_FILE_MG)
    DEFINE(QUEEN_OPEN_FILE_EG)
    DEFINE(QUEEN_HALF_OPEN_FILE_MG)
    DEFINE(QUEEN_HALF_OPEN_FILE_EG)
    DEFINE(ROOK_ON_7TH_MG)
    DEFINE(ROOK_ON_7TH_EG)
    DEFINE(BISHOP_PAIR_MG)
    DEFINE(BISHOP_PAIR_EG)
    DEFINE(PAWN_SHIELD_RANK1)
    DEFINE(PAWN_SHIELD_RANK2)
    DEFINE(PAWN_SHIELD_HOLE)
    DEFINE(PASSED_PAWN_RANK2_MG)
    DEFINE(PASSED_PAWN_RANK3_MG)
    DEFINE(PASSED_PAWN_RANK4_MG)
    DEFINE(PASSED_PAWN_RANK5_MG)
    DEFINE(PASSED_PAWN_RANK6_MG)
    DEFINE(PASSED_PAWN_RANK7_MG)
    DEFINE(PASSED_PAWN_RANK2_EG)
    DEFINE(PASSED_PAWN_RANK3_EG)
    DEFINE(PASSED_PAWN_RANK4_EG)
    DEFINE(PASSED_PAWN_RANK5_EG)
    DEFINE(PASSED_PAWN_RANK6_EG)
    DEFINE(PASSED_PAWN_RANK7_EG)
    DEFINE(KNIGHT_MOBILITY_MG)
    DEFINE(BISHOP_MOBILITY_MG)
    DEFINE(ROOK_MOBILITY_MG)
    DEFINE(QUEEN_MOBILITY_MG)
    DEFINE(KNIGHT_MOBILITY_EG)
    DEFINE(BISHOP_MOBILITY_EG)
    DEFINE(ROOK_MOBILITY_EG)
    DEFINE(QUEEN_MOBILITY_EG)
    DEFINE_MULTIPLE(PSQ_TABLE_PAWN_MG)
    DEFINE_MULTIPLE(PSQ_TABLE_KNIGHT_MG)
    DEFINE_MULTIPLE(PSQ_TABLE_BISHOP_MG)
    DEFINE_MULTIPLE(PSQ_TABLE_ROOK_MG)
    DEFINE_MULTIPLE(PSQ_TABLE_QUEEN_MG)
    DEFINE_MULTIPLE(PSQ_TABLE_KING_MG)
    DEFINE_MULTIPLE(PSQ_TABLE_PAWN_EG)
    DEFINE_MULTIPLE(PSQ_TABLE_KNIGHT_EG)
    DEFINE_MULTIPLE(PSQ_TABLE_BISHOP_EG)
    DEFINE_MULTIPLE(PSQ_TABLE_ROOK_EG)
    DEFINE_MULTIPLE(PSQ_TABLE_QUEEN_EG)
    DEFINE_MULTIPLE(PSQ_TABLE_KING_EG)
    DEFINE(KNIGHT_MATERIAL_VALUE_MG)
    DEFINE(BISHOP_MATERIAL_VALUE_MG)
    DEFINE(ROOK_MATERIAL_VALUE_MG)
    DEFINE(QUEEN_MATERIAL_VALUE_MG)
    DEFINE(KNIGHT_MATERIAL_VALUE_EG)
    DEFINE(BISHOP_MATERIAL_VALUE_EG)
    DEFINE(ROOK_MATERIAL_VALUE_EG)
    DEFINE(QUEEN_MATERIAL_VALUE_EG)
    DEFINE(KING_ATTACK_SCALE_MG)
    DEFINE(KING_ATTACK_SCALE_EG)
    DEFINE(KNIGHT_OUTPOST)
    DEFINE(PROTECTED_KNIGHT_OUTPOST)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK2_MG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK3_MG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK4_MG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK5_MG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK6_MG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK2_EG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK3_EG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK4_EG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK5_EG)
    DEFINE(CANDIDATE_PASSED_PAWN_RANK6_EG)
    DEFINE(FRIENDLY_KING_PASSER_DIST)
    DEFINE(OPPONENT_KING_PASSER_DIST)
    DEFINE(BACKWARD_PAWN_MG)
    DEFINE(BACKWARD_PAWN_EG)

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
