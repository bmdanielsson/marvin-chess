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
    {"pawn_shield", 16, 18, -100, 100},
    {"passed_pawn_mg", 19, 25, 0, 200},
    {"passed_pawn_eg", 26, 32, 0, 200},
    {"knight_mobility_mg", 33, 33, 0, 15},
    {"bishop_mobility_mg", 34, 34, 0, 15},
    {"rook_mobility_mg", 35, 35, 0, 15},
    {"queen_mobility_mg", 36, 36, 0, 15},
    {"knight_mobility_eg", 37, 37, 0, 15},
    {"bishop_mobility_eg", 38, 38, 0, 15},
    {"rook_mobility_eg", 39, 39, 0, 15},
    {"queen_mobility_eg", 40, 40, 0, 15},
    {"psq_table_pawn_mg", 41, 104, -200, 200},
    {"psq_table_knight_mg", 105, 168, -200, 200},
    {"psq_table_bishop_mg", 169, 232, -200, 200},
    {"psq_table_rook_mg", 233, 296, -200, 200},
    {"psq_table_queen_mg", 297, 360, -200, 200},
    {"psq_table_king_mg", 361, 424, -200, 200},
    {"psq_table_pawn_eg", 425, 488, -200, 200},
    {"psq_table_knight_eg", 489, 552, -200, 200},
    {"psq_table_bishop_eg", 553, 616, -200, 200},
    {"psq_table_rook_eg", 617, 680, -200, 200},
    {"psq_table_queen_eg", 681, 744, -200, 200},
    {"psq_table_king_eg", 745, 808, -200, 200},
    {"knight_material_value_mg", 809, 809, 200, 600},
    {"bishop_material_value_mg", 810, 810, 200, 600},
    {"rook_material_value_mg", 811, 811, 400, 800},
    {"queen_material_value_mg", 812, 812, 700, 1600},
    {"knight_material_value_eg", 813, 813, 200, 600},
    {"bishop_material_value_eg", 814, 814, 200, 600},
    {"rook_material_value_eg", 815, 815, 400, 800},
    {"queen_material_value_eg", 816, 816, 700, 1600},
    {"king_attack_scale_mg", 817, 817, 0, 100},
    {"king_attack_scale_eg", 818, 818, 0, 100},
    {"knight_outpost", 819, 819, 0, 100},
    {"protected_knight_outpost", 820, 820, 0, 100},
    {"candidate_passed_pawn_mg", 821, 826, 0, 200},
    {"candidate_passed_pawn_eg", 827, 832, 0, 200},
    {"friendly_king_passer_dist", 833, 833, -30, 0},
    {"opponent_king_passer_dist", 834, 834, 0, 30},
    {"backward_pawn_mg", 835, 835, -30, 0},
    {"backward_pawn_eg", 836, 836, -30, 0},
    {"free_passed_pawn_mg", 837, 837, 0, 200},
    {"free_passed_pawn_eg", 838, 838, 0, 200},
    {"space_square", 839, 839, 0, 50},
    {"connected_pawns_mg", 840, 846, 0, 200},
    {"connected_pawns_eg", 847, 853, 0, 200},
    {"threat_minor_by_pawn_mg", 854, 854, 0, 100},
    {"threat_minor_by_pawn_eg", 855, 855, 0, 100},
    {"threat_pawn_push_mg", 856, 856, 0, 100},
    {"threat_pawn_push_eg", 857, 857, 0, 100},
    {"threat_by_knight_mg", 858, 862, 0, 100},
    {"threat_by_knight_eg", 863, 867, 0, 100},
    {"threat_by_bishop_mg", 868, 872, 0, 100},
    {"threat_by_bishop_eg", 873, 877, 0, 100},
    {"threat_by_rook_mg", 878, 882, 0, 100},
    {"threat_by_rook_eg", 883, 887, 0, 100},
    {"threat_by_queen_mg", 888, 892, 0, 100},
    {"threat_by_queen_eg", 893, 897, 0, 100}
};

static void validate_value(FILE *fp, char *name, struct tuning_param *param)
{
    if ((int)param->current < param->min) {
        fprintf(fp, "# %s: value is below minimum (%d/%d)\n", name,
                (int)param->current, param->min);
    } else if ((int)param->current > param->max) {
        fprintf(fp, "# %s: value is above maximum (%d/%d)\n", name,
                (int)param->current, param->max);
    }
}

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
    ASSIGN_MULTIPLE(PAWN_SHIELD)
    ASSIGN_MULTIPLE(PASSED_PAWN_MG)
    ASSIGN_MULTIPLE(PASSED_PAWN_EG)
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
    ASSIGN_MULTIPLE(CANDIDATE_PASSED_PAWN_MG)
    ASSIGN_MULTIPLE(CANDIDATE_PASSED_PAWN_EG)
    ASSIGN(FRIENDLY_KING_PASSER_DIST)
    ASSIGN(OPPONENT_KING_PASSER_DIST)
    ASSIGN(BACKWARD_PAWN_MG)
    ASSIGN(BACKWARD_PAWN_EG)
    ASSIGN(FREE_PASSED_PAWN_MG)
    ASSIGN(FREE_PASSED_PAWN_EG)
    ASSIGN(SPACE_SQUARE)
    ASSIGN_MULTIPLE(CONNECTED_PAWNS_MG)
    ASSIGN_MULTIPLE(CONNECTED_PAWNS_EG)
    ASSIGN(THREAT_MINOR_BY_PAWN_MG)
    ASSIGN(THREAT_MINOR_BY_PAWN_EG)
    ASSIGN(THREAT_PAWN_PUSH_MG)
    ASSIGN(THREAT_PAWN_PUSH_EG)
    ASSIGN_MULTIPLE(THREAT_BY_KNIGHT_MG)
    ASSIGN_MULTIPLE(THREAT_BY_KNIGHT_EG)
    ASSIGN_MULTIPLE(THREAT_BY_BISHOP_MG)
    ASSIGN_MULTIPLE(THREAT_BY_BISHOP_EG)
    ASSIGN_MULTIPLE(THREAT_BY_ROOK_MG)
    ASSIGN_MULTIPLE(THREAT_BY_ROOK_EG)
    ASSIGN_MULTIPLE(THREAT_BY_QUEEN_MG)
    ASSIGN_MULTIPLE(THREAT_BY_QUEEN_EG)
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
    DEFINE_MULTIPLE(PAWN_SHIELD)
    DEFINE_MULTIPLE(PASSED_PAWN_MG)
    DEFINE_MULTIPLE(PASSED_PAWN_EG)
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
    DEFINE_MULTIPLE(CANDIDATE_PASSED_PAWN_MG)
    DEFINE_MULTIPLE(CANDIDATE_PASSED_PAWN_EG)
    DEFINE(FRIENDLY_KING_PASSER_DIST)
    DEFINE(OPPONENT_KING_PASSER_DIST)
    DEFINE(BACKWARD_PAWN_MG)
    DEFINE(BACKWARD_PAWN_EG)
    DEFINE(FREE_PASSED_PAWN_MG)
    DEFINE(FREE_PASSED_PAWN_EG)
    DEFINE(SPACE_SQUARE)
    DEFINE_MULTIPLE(CONNECTED_PAWNS_MG)
    DEFINE_MULTIPLE(CONNECTED_PAWNS_EG)
    DEFINE(THREAT_MINOR_BY_PAWN_MG)
    DEFINE(THREAT_MINOR_BY_PAWN_EG)
    DEFINE(THREAT_PAWN_PUSH_MG)
    DEFINE(THREAT_PAWN_PUSH_EG)
    DEFINE_MULTIPLE(THREAT_BY_KNIGHT_MG)
    DEFINE_MULTIPLE(THREAT_BY_KNIGHT_EG)
    DEFINE_MULTIPLE(THREAT_BY_BISHOP_MG)
    DEFINE_MULTIPLE(THREAT_BY_BISHOP_EG)
    DEFINE_MULTIPLE(THREAT_BY_ROOK_MG)
    DEFINE_MULTIPLE(THREAT_BY_ROOK_EG)
    DEFINE_MULTIPLE(THREAT_BY_QUEEN_MG)
    DEFINE_MULTIPLE(THREAT_BY_QUEEN_EG)

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
                                   bool active_only, bool zero)
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
            validate_value(fp, parameter_declarations[k].name, &params[start]);
            fprintf(fp, "%s %d\n", parameter_declarations[k].name,
                    zero?0:(int)params[start].current);
        } else {
            for (l=start;l<=stop;l++) {
                validate_value(fp, parameter_declarations[k].name, &params[l]);
            }
            fprintf(fp, "%s {", parameter_declarations[k].name);
            for (l=start;l<=stop;l++) {
                fprintf(fp, "%d", zero?0:(int)params[l].current);
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
