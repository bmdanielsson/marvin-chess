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
#include "evalparams.h"

int DOUBLE_PAWNS_MG = -2;
int DOUBLE_PAWNS_EG = -23;
int ISOLATED_PAWN_MG = -20;
int ISOLATED_PAWN_EG = -8;
int ROOK_OPEN_FILE_MG = 58;
int ROOK_OPEN_FILE_EG = 0;
int ROOK_HALF_OPEN_FILE_MG = 22;
int ROOK_HALF_OPEN_FILE_EG = 10;
int QUEEN_OPEN_FILE_MG = 0;
int QUEEN_OPEN_FILE_EG = 11;
int QUEEN_HALF_OPEN_FILE_MG = 9;
int QUEEN_HALF_OPEN_FILE_EG = 12;
int ROOK_ON_7TH_MG = 1;
int ROOK_ON_7TH_EG = 29;
int BISHOP_PAIR_MG = 47;
int BISHOP_PAIR_EG = 72;
int PAWN_SHIELD_RANK1 = 36;
int PAWN_SHIELD_RANK2 = 27;
int PAWN_SHIELD_HOLE = -9;
int PASSED_PAWN_MG[7] = {
    0, 2, 0, 0, 30, 48, 153
};
int PASSED_PAWN_EG[7] = {
    0, 0, 0, 43, 84, 97, 132
};
int KNIGHT_MOBILITY_MG = 12;
int BISHOP_MOBILITY_MG = 10;
int ROOK_MOBILITY_MG = 3;
int QUEEN_MOBILITY_MG = 4;
int KNIGHT_MOBILITY_EG = 4;
int BISHOP_MOBILITY_EG = 7;
int ROOK_MOBILITY_EG = 7;
int QUEEN_MOBILITY_EG = 6;
int PSQ_TABLE_PAWN_MG[NSQUARES] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    -20, -14, -8, -1, 8, 27, 32, -22,
    -21, -13, -4, 5, 25, 9, 19, -8,
    -17, -13, 4, 25, 23, 24, -1, -9,
    -3, 1, 16, 24, 39, 38, 21, 7,
    30, 40, 50, 55, 82, 106, 91, 39,
    48, 52, 66, 85, 47, -43, -100, -101,
    0, 0, 0, 0, 0, 0, 0, 0
};
int PSQ_TABLE_KNIGHT_MG[NSQUARES] = {
    -76, -10, -15, 5, 35, 26, -7, -7,
    -42, -10, 19, 42, 34, 54, 29, 28,
    -3, 8, 26, 30, 64, 41, 46, 20,
    2, 4, 36, 43, 60, 67, 73, 32,
    3, 8, 25, 81, 48, 73, 34, 71,
    -25, 32, 26, 43, 115, 90, 58, 56,
    1, 37, 57, 36, 55, 106, 17, 52,
    -148, -106, -33, -50, 25, -90, -40, -84
};
int PSQ_TABLE_BISHOP_MG[NSQUARES] = {
    -16, 17, 11, 0, 14, 14, 15, 2,
    30, 29, 40, 18, 33, 39, 62, 9,
    4, 39, 28, 30, 32, 43, 30, 22,
    -4, 9, 20, 53, 58, 7, 6, 37,
    -11, 22, 12, 57, 44, 34, 17, -1,
    5, 23, 20, 48, 40, 106, 34, 39,
    -28, -3, 0, -37, -19, -6, -36, -12,
    -27, -67, -68, -52, -65, -74, -59, -66
};
int PSQ_TABLE_ROOK_MG[NSQUARES] = {
    8, 14, 14, 34, 41, 40, 45, 27,
    -30, -13, -10, 13, 12, 40, 46, -23,
    -32, -20, -17, -4, 12, 28, 44, 21,
    -32, -29, -11, -13, -8, 7, 29, 7,
    -15, -5, -7, 19, 21, 55, 60, 46,
    8, 19, 10, 36, 87, 77, 125, 67,
    -6, -10, 31, 19, 32, 67, 34, 66,
    23, 22, -4, 39, 24, 42, 48, 44
};
int PSQ_TABLE_QUEEN_MG[NSQUARES] = {
    10, 4, 8, 24, 17, -9, -12, -53,
    -12, 5, 8, 30, 29, 43, 42, 9,
    -28, 1, -1, 6, 20, 19, 21, 34,
    -7, -11, -3, 6, 13, 8, 9, 15,
    -29, -8, 2, 0, 29, 28, 36, 7,
    3, 10, 13, 15, 51, 140, 85, 75,
    -6, -47, 4, -38, -36, 55, 51, 114,
    -78, -23, -19, -1, -25, 22, 89, -7
};
int PSQ_TABLE_KING_MG[NSQUARES] = {
    -21, 7, -46, -92, 0, -65, 3, -10,
    51, 7, -26, -73, -54, -36, 37, 35,
    -14, -20, -43, -72, -63, -60, -36, -55,
    -11, 24, -62, -83, -90, -78, -84, -126,
    -54, 18, -83, -86, -115, -84, -23, -124,
    74, 38, 24, -44, -38, 14, 52, 12,
    153, 18, -2, 67, 43, -6, -19, 17,
    13, 191, 179, 67, -87, 43, 98, 50
};
int PSQ_TABLE_PAWN_EG[NSQUARES] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    37, 27, 20, 14, 33, 22, 12, 13,
    36, 29, 16, 16, 21, 22, 15, 15,
    41, 34, 18, 6, 6, 16, 24, 21,
    59, 50, 26, -1, 9, 14, 32, 34,
    87, 76, 38, -4, -9, 15, 43, 51,
    126, 119, 73, 40, 44, 72, 93, 109,
    0, 0, 0, 0, 0, 0, 0, 0
};
int PSQ_TABLE_KNIGHT_EG[NSQUARES] = {
    16, -19, 7, 19, 0, 18, -13, -23,
    24, 24, 16, 17, 22, 11, 11, 1,
    2, 25, 24, 43, 36, 15, 8, -2,
    9, 31, 47, 48, 56, 31, 18, 3,
    19, 44, 46, 40, 49, 45, 44, 15,
    18, 14, 46, 34, 16, 24, 10, 11,
    11, 7, 19, 12, 13, -11, 20, -21,
    -45, -20, 10, -8, -6, 4, -36, -98
};
int PSQ_TABLE_BISHOP_EG[NSQUARES] = {
    37, 32, 9, 28, 19, 14, 20, 19,
    19, 6, 20, 18, 13, 16, -1, -9,
    18, 26, 25, 29, 33, 12, 12, 15,
    23, 16, 24, 15, 7, 26, 20, 3,
    36, 20, 26, 17, 21, 19, 24, 23,
    27, 17, 18, 4, 16, 1, 20, 31,
    26, 23, 25, 31, 13, 22, 16, -4,
    -7, 14, 24, 36, 22, 14, 23, 13
};
int PSQ_TABLE_ROOK_EG[NSQUARES] = {
    34, 36, 41, 27, 23, 30, 21, 9,
    54, 35, 49, 39, 33, 24, 18, 44,
    45, 50, 56, 44, 35, 32, 17, 18,
    61, 68, 61, 60, 53, 52, 52, 41,
    66, 54, 70, 64, 49, 46, 37, 46,
    56, 59, 61, 53, 37, 34, 30, 37,
    49, 58, 38, 42, 44, 38, 38, 22,
    54, 43, 60, 43, 56, 52, 53, 51
};
int PSQ_TABLE_QUEEN_EG[NSQUARES] = {
    -32, -57, -37, -29, -25, -35, -46, -13,
    -12, -24, -18, -30, -14, -60, -63, -54,
    1, -2, 14, -1, 18, 34, 22, -23,
    -2, 10, 18, 60, 34, 68, 62, 52,
    43, 50, 51, 81, 79, 102, 107, 111,
    35, 15, 38, 82, 103, 41, 44, 62,
    3, 56, 50, 113, 113, 63, 73, 47,
    8, 6, 38, 28, 49, 2, -40, -16
};
int PSQ_TABLE_KING_EG[NSQUARES] = {
    -76, -51, -27, -3, -33, -23, -60, -82,
    -31, -9, 6, 24, 23, 9, -15, -33,
    -28, -5, 11, 37, 40, 25, 7, -9,
    -32, -9, 28, 42, 52, 44, 23, 2,
    -7, 1, 30, 35, 48, 45, 29, 22,
    -13, 5, 13, 19, 25, 39, 43, 14,
    -39, 3, -4, -14, 9, 17, 59, 35,
    -46, -48, -70, -23, 16, -2, 19, -40
};
int KNIGHT_MATERIAL_VALUE_MG = 410;
int BISHOP_MATERIAL_VALUE_MG = 440;
int ROOK_MATERIAL_VALUE_MG = 651;
int QUEEN_MATERIAL_VALUE_MG = 1453;
int KNIGHT_MATERIAL_VALUE_EG = 374;
int BISHOP_MATERIAL_VALUE_EG = 373;
int ROOK_MATERIAL_VALUE_EG = 658;
int QUEEN_MATERIAL_VALUE_EG = 1310;
int KING_ATTACK_SCALE_MG = 27;
int KING_ATTACK_SCALE_EG = 1;
int KNIGHT_OUTPOST = 17;
int PROTECTED_KNIGHT_OUTPOST = 50;
int CANDIDATE_PASSED_PAWN_MG[6] = {
    0, 0, 6, 10, 21, 87
};
int CANDIDATE_PASSED_PAWN_EG[6] = {
    0, 0, 6, 10, 19, 72
};
int FRIENDLY_KING_PASSER_DIST = -11;
int OPPONENT_KING_PASSER_DIST = 14;
int BACKWARD_PAWN_MG = -15;
int BACKWARD_PAWN_EG = -2;
int FREE_PASSED_PAWN_MG = 170;
int FREE_PASSED_PAWN_EG = 80;
int SPACE_SQUARE = 4;
int CONNECTED_PAWNS_MG[7] = {
    0, 7, 10, 12, 18, 4, 51
};
int CONNECTED_PAWNS_EG[7] = {
    0, 0, 4, 1, 6, 58, 65
};
int THREAT_MINOR_BY_PAWN_MG = 56;
int THREAT_MINOR_BY_PAWN_EG = 4;
int THREAT_PAWN_PUSH_MG = 17;
int THREAT_PAWN_PUSH_EG = 14;
int THREAT_BY_KNIGHT_MG[5] = {
    1, 11, 49, 46, 29
};
int THREAT_BY_KNIGHT_EG[5] = {
    17, 9, 26, 5, 0
};
int THREAT_BY_BISHOP_MG[5] = {
    1, 34, 15, 22, 59
};
int THREAT_BY_BISHOP_EG[5] = {
    25, 32, 17, 10, 100
};
int THREAT_BY_ROOK_MG[5] = {
    1, 27, 40, 15, 82
};
int THREAT_BY_ROOK_EG[5] = {
    15, 25, 27, 18, 30
};
int THREAT_BY_QUEEN_MG[5] = {
    0, 7, 7, 0, 0
};
int THREAT_BY_QUEEN_EG[5] = {
    0, 27, 40, 11, 0
};

