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
#ifndef EVALPARAMS_H
#define EVALPARAMS_H

#include "chess.h"

extern int DOUBLE_PAWNS_MG;
extern int DOUBLE_PAWNS_EG;
extern int ISOLATED_PAWN_MG;
extern int ISOLATED_PAWN_EG;
extern int ROOK_OPEN_FILE_MG;
extern int ROOK_OPEN_FILE_EG;
extern int ROOK_HALF_OPEN_FILE_MG;
extern int ROOK_HALF_OPEN_FILE_EG;
extern int QUEEN_OPEN_FILE_MG;
extern int QUEEN_OPEN_FILE_EG;
extern int QUEEN_HALF_OPEN_FILE_MG;
extern int QUEEN_HALF_OPEN_FILE_EG;
extern int ROOK_ON_7TH_MG;
extern int ROOK_ON_7TH_EG;
extern int BISHOP_PAIR_MG;
extern int BISHOP_PAIR_EG;
extern int PAWN_SHIELD_RANK1;
extern int PAWN_SHIELD_RANK2;
extern int PAWN_SHIELD_HOLE;
extern int PASSED_PAWN_RANK2_MG;
extern int PASSED_PAWN_RANK3_MG;
extern int PASSED_PAWN_RANK4_MG;
extern int PASSED_PAWN_RANK5_MG;
extern int PASSED_PAWN_RANK6_MG;
extern int PASSED_PAWN_RANK7_MG;
extern int PASSED_PAWN_RANK2_EG;
extern int PASSED_PAWN_RANK3_EG;
extern int PASSED_PAWN_RANK4_EG;
extern int PASSED_PAWN_RANK5_EG;
extern int PASSED_PAWN_RANK6_EG;
extern int PASSED_PAWN_RANK7_EG;
extern int KNIGHT_MOBILITY_MG;
extern int BISHOP_MOBILITY_MG;
extern int ROOK_MOBILITY_MG;
extern int QUEEN_MOBILITY_MG;
extern int KNIGHT_MOBILITY_EG;
extern int BISHOP_MOBILITY_EG;
extern int ROOK_MOBILITY_EG;
extern int QUEEN_MOBILITY_EG;
extern int PSQ_TABLE_PAWN_MG[NSQUARES];
extern int PSQ_TABLE_KNIGHT_MG[NSQUARES];
extern int PSQ_TABLE_BISHOP_MG[NSQUARES];
extern int PSQ_TABLE_ROOK_MG[NSQUARES];
extern int PSQ_TABLE_QUEEN_MG[NSQUARES];
extern int PSQ_TABLE_KING_MG[NSQUARES];
extern int PSQ_TABLE_PAWN_EG[NSQUARES];
extern int PSQ_TABLE_KNIGHT_EG[NSQUARES];
extern int PSQ_TABLE_BISHOP_EG[NSQUARES];
extern int PSQ_TABLE_ROOK_EG[NSQUARES];
extern int PSQ_TABLE_QUEEN_EG[NSQUARES];
extern int PSQ_TABLE_KING_EG[NSQUARES];
extern int KNIGHT_MATERIAL_VALUE_MG;
extern int BISHOP_MATERIAL_VALUE_MG;
extern int ROOK_MATERIAL_VALUE_MG;
extern int QUEEN_MATERIAL_VALUE_MG;
extern int KNIGHT_MATERIAL_VALUE_EG;
extern int BISHOP_MATERIAL_VALUE_EG;
extern int ROOK_MATERIAL_VALUE_EG;
extern int QUEEN_MATERIAL_VALUE_EG;

#endif
