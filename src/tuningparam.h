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
#ifndef TUNINGPARAM_H
#define TUNINGPARAM_H

#include <stdio.h>

#include "chess.h"

/* The total number of tunable parameters */
#define NUM_TUNING_PARAMS 854

/* Identifiers for all tunable parameters */
enum {
    TP_DOUBLE_PAWNS_MG,
    TP_DOUBLE_PAWNS_EG,
    TP_ISOLATED_PAWN_MG,
    TP_ISOLATED_PAWN_EG,
    TP_ROOK_OPEN_FILE_MG,
    TP_ROOK_OPEN_FILE_EG,
    TP_ROOK_HALF_OPEN_FILE_MG,
    TP_ROOK_HALF_OPEN_FILE_EG,
    TP_QUEEN_OPEN_FILE_MG,
    TP_QUEEN_OPEN_FILE_EG,
    TP_QUEEN_HALF_OPEN_FILE_MG,
    TP_QUEEN_HALF_OPEN_FILE_EG,
    TP_ROOK_ON_7TH_MG,
    TP_ROOK_ON_7TH_EG,
    TP_BISHOP_PAIR_MG,
    TP_BISHOP_PAIR_EG,
    TP_PAWN_SHIELD_RANK1,
    TP_PAWN_SHIELD_RANK2,
    TP_PAWN_SHIELD_HOLE,
    TP_PASSED_PAWN_MG,
    TP_PASSED_PAWN_EG,
    TP_KNIGHT_MOBILITY_MG,
    TP_BISHOP_MOBILITY_MG,
    TP_ROOK_MOBILITY_MG,
    TP_QUEEN_MOBILITY_MG,
    TP_KNIGHT_MOBILITY_EG,
    TP_BISHOP_MOBILITY_EG,
    TP_ROOK_MOBILITY_EG,
    TP_QUEEN_MOBILITY_EG,
    TP_PSQ_TABLE_PAWN_MG,
    TP_PSQ_TABLE_KNIGHT_MG,
    TP_PSQ_TABLE_BISHOP_MG,
    TP_PSQ_TABLE_ROOK_MG,
    TP_PSQ_TABLE_QUEEN_MG,
    TP_PSQ_TABLE_KING_MG,
    TP_PSQ_TABLE_PAWN_EG,
    TP_PSQ_TABLE_KNIGHT_EG,
    TP_PSQ_TABLE_BISHOP_EG,
    TP_PSQ_TABLE_ROOK_EG,
    TP_PSQ_TABLE_QUEEN_EG,
    TP_PSQ_TABLE_KING_EG,
    TP_KNIGHT_MATERIAL_VALUE_MG,
    TP_BISHOP_MATERIAL_VALUE_MG,
    TP_ROOK_MATERIAL_VALUE_MG,
    TP_QUEEN_MATERIAL_VALUE_MG,
    TP_KNIGHT_MATERIAL_VALUE_EG,
    TP_BISHOP_MATERIAL_VALUE_EG,
    TP_ROOK_MATERIAL_VALUE_EG,
    TP_QUEEN_MATERIAL_VALUE_EG,
    TP_KING_ATTACK_SCALE_MG,
    TP_KING_ATTACK_SCALE_EG,
    TP_KNIGHT_OUTPOST,
    TP_PROTECTED_KNIGHT_OUTPOST,
    TP_CANDIDATE_PASSED_PAWN_MG,
    TP_CANDIDATE_PASSED_PAWN_EG,
    TP_FRIENDLY_KING_PASSER_DIST,
    TP_OPPONENT_KING_PASSER_DIST,
    TP_BACKWARD_PAWN_MG,
    TP_BACKWARD_PAWN_EG,
    TP_FREE_PASSED_PAWN_MG,
    TP_FREE_PASSED_PAWN_EG,
    TP_SPACE_SQUARE,
    TP_CONNECTED_PAWNS_MG,
    TP_CONNECTED_PAWNS_EG,
    NUM_PARAM_DECLARATIONS
};

/* Declaration for a tuning parameter */
struct param_decl {
    char *name;
    int  start;
    int  stop;
    int  min;
    int  max;
};

/* Tunable parameter */
struct tuning_param {
    bool   active;
    double current;
    int    min;
    int    max;
};

/*
 * Assign the current values for all tuning paramateres to the
 * corresponding evaluation parameter.
 *
 * @param params List of tunable parameters.
 */
void tuning_param_assign_current(struct tuning_param *params);

/*
 * Create a list of all tunable parameters.
 *
 * @param nparams Location to store the number of parameters at.
 * @return Returns a list of all tuning parameters.
 */
struct tuning_param* tuning_param_create_list(void);

/*
 * Destroy a list of tuning parameters.
 *
 * @params The list to destroy.
 */
void tuning_param_destroy_list(struct tuning_param *params);

/*
 * Get the declaration for a tuning parameter.
 *
 * @param name The name of the parameter.
 * @return Returns the parameter declaration.
 */
struct param_decl* tuning_param_lookup(char *name);

/*
 * Write the current values for the specified tuning parameters.
 *
 * @param fp The location to write the parameters to.
 * @param params The tuning parameters to write.
 * @param active_only Flag indicating if only actice parameters should be
 *                    printed.
 * @param zero Print parameters with a value of zero instead of the
 *             current value.
 */
void tuning_param_write_parameters(FILE *fp, struct tuning_param *params,
                                   bool active_only, bool zero);

/*
 * Get the index of a specific parameter.
 *
 * @param decl The parameter.
 * @return Returns the index of the parameter.
 */
int tuning_param_index(int decl);

#endif
