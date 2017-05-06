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

extern int DOUBLE_PAWNS;
extern int ISOLATED_PAWN;
extern int ROOK_OPEN_FILE;
extern int ROOK_HALF_OPEN_FILE;
extern int QUEEN_OPEN_FILE;
extern int QUEEN_HALF_OPEN_FILE;
extern int ROOK_ON_7TH_MG;
extern int ROOK_ON_7TH_EG;
extern int BISHOP_PAIR;
extern int PAWN_SHIELD_RANK1;
extern int PAWN_SHIELD_RANK2;
extern int PAWN_SHIELD_HOLE;
extern int PASSED_PAWN_RANK2;
extern int PASSED_PAWN_RANK3;
extern int PASSED_PAWN_RANK4;
extern int PASSED_PAWN_RANK5;
extern int PASSED_PAWN_RANK6;
extern int PASSED_PAWN_RANK7;
extern int KNIGHT_MOBILITY_MG;
extern int BISHOP_MOBILITY_MG;
extern int ROOK_MOBILITY_MG;
extern int QUEEN_MOBILITY_MG;
extern int KNIGHT_MOBILITY_EG;
extern int BISHOP_MOBILITY_EG;
extern int ROOK_MOBILITY_EG;
extern int QUEEN_MOBILITY_EG;
extern int PSQ_TABLE_PAWN[NSQUARES];
extern int PSQ_TABLE_KNIGHT[NSQUARES];
extern int PSQ_TABLE_BISHOP[NSQUARES];
extern int PSQ_TABLE_ROOK[NSQUARES];
extern int PSQ_TABLE_KING_MG[NSQUARES];
extern int PSQ_TABLE_KING_EG[NSQUARES];

#endif
