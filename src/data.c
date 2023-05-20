/*
 * Marvin - an UCI/XBoard compatible chess engine
 * Copyright (C) 2023 Martin Danielsson
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
#include "data.h"
#include "bitboard.h"

uint64_t sq_mask[NSQUARES];

uint64_t white_square_mask;

uint64_t black_square_mask;

uint64_t rank_mask[NRANKS];

uint64_t relative_rank_mask[NSIDES][NRANKS];

uint64_t file_mask[NFILES];

uint64_t a1h8_masks[NDIAGONALS];

uint64_t a8h1_masks[NDIAGONALS];

uint64_t front_attackspan[NSIDES][NSQUARES];

uint64_t rear_attackspan[NSIDES][NSQUARES];

uint64_t front_span[NSIDES][NSQUARES];

uint64_t rear_span[NSIDES][NSQUARES];

uint64_t king_zone[NSIDES][NSQUARES];

char piece2char[NPIECES+1] = {
    'P', 'p', 'N', 'n', 'B', 'b', 'R', 'r', 'Q', 'q', 'K', 'k', '.'
};

int sq2diag_a1h8[NSQUARES] = {
    0, 8, 9, 10, 11, 12, 13, 14,
    1, 0, 8,  9, 10, 11, 12, 13,
    2, 1, 0,  8,  9, 10, 11, 12,
    3, 2, 1,  0,  8,  9, 10, 11,
    4, 3, 2,  1,  0,  8,  9, 10,
    5, 4, 3,  2,  1,  0,  8,  9,
    6, 5, 4,  3,  2,  1,  0,  8,
    7, 6, 5,  4,  3,  2,  1,  0
};

int sq2diag_a8h1[NSQUARES] = {
    0, 1, 2,  3,  4,  5,  6,  7,
    1, 2, 3,  4,  5,  6,  7,  8,
    2, 3, 4,  5,  6,  7,  8,  9,
    3, 4, 5,  6,  7,  8,  9, 10,
    4, 5, 6,  7,  8,  9, 10, 11,
    5, 6, 7,  8,  9, 10, 11, 12,
    6, 7, 8,  9, 10, 11, 12, 13,
    7, 8, 9, 10, 11, 12, 13, 14
};

int sq_color[NSQUARES] = {
    BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE,
    WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK,
    BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE,
    WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK,
    BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE,
    WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK,
    BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE,
    WHITE, BLACK, WHITE, BLACK, WHITE, BLACK, WHITE, BLACK
};

uint64_t outpost_squares[NSIDES];

uint64_t space_eval_squares[NSIDES];

int kingside_castle_to[NSIDES] = {G1, G8};

int queenside_castle_to[NSIDES] = {C1, C8};

int material_values[NPIECES] = {100,  100,      /* pawn */
                                392,  392,      /* knight */
                                406,  406,      /* bishop */
                                654,  654,      /* rook */
                                1381, 1381,     /* queen */
                                0,    0};       /* king */

uint64_t line_mask[NSQUARES][NSQUARES];

static void init_king_zones(void)
{
    int sq;
    int rank;
    int file;

    for (sq=0;sq<NSQUARES;sq++) {
        rank = RANKNR(sq);
        file = FILENR(sq);

        king_zone[WHITE][sq] = 0ULL;
        king_zone[BLACK][sq] = 0ULL;

        /* White king zone */
        if (rank < RANK_8) {
            if (file > FILE_A) {
                SETBIT(king_zone[WHITE][sq], sq+7);
            }
            SETBIT(king_zone[WHITE][sq], sq+8);
            if (file < FILE_H) {
                SETBIT(king_zone[WHITE][sq], sq+9);
            }
        }
        if (file > FILE_A) {
            SETBIT(king_zone[WHITE][sq], sq-1);
        }
        if (file < FILE_H) {
            SETBIT(king_zone[WHITE][sq], sq+1);
        }
        if (rank > RANK_1) {
            if (file > FILE_A) {
                SETBIT(king_zone[WHITE][sq], sq-9);
            }
            SETBIT(king_zone[WHITE][sq], sq-8);
            if (file < FILE_H) {
                SETBIT(king_zone[WHITE][sq], sq-7);
            }
        }

        /* Black king zone */
        if (rank > RANK_1) {
            if (file > FILE_A) {
                SETBIT(king_zone[BLACK][sq], sq-9);
            }
            SETBIT(king_zone[BLACK][sq], sq-8);
            if (file < FILE_H) {
                SETBIT(king_zone[BLACK][sq], sq-7);
            }
        }
        if (file > FILE_A) {
            SETBIT(king_zone[BLACK][sq], sq-1);
        }
        if (file < FILE_H) {
            SETBIT(king_zone[BLACK][sq], sq+1);
        }
        if (rank < RANK_8) {
            if (file > FILE_A) {
                SETBIT(king_zone[BLACK][sq], sq+7);
            }
            SETBIT(king_zone[BLACK][sq], sq+8);
            if (file < FILE_H) {
                SETBIT(king_zone[BLACK][sq], sq+9);
            }
        }
    }
}

void data_init(void)
{
    int k;
    int l;
    int rank;
    int file;
    int sq;

    /* Initialize square masks */
    white_square_mask = 0ULL;
    black_square_mask = 0ULL;
    for(k=0;k<NSQUARES;k++) {
        sq_mask[k] = 1ULL << k;
        if (sq_color[k] == WHITE) {
            white_square_mask |= sq_mask[k];
        } else {
            black_square_mask |= sq_mask[k];
        }
    }

    /* Initialize rank masks */
    for (k=0;k<NRANKS;k++) {
        rank_mask[k] = 0ULL;
        for (l=0;l<NFILES;l++) {
            rank_mask[k] |= sq_mask[SQUARE(l, k)];
        }
        relative_rank_mask[WHITE][k] = rank_mask[k];
        relative_rank_mask[BLACK][NRANKS-1-k] = rank_mask[k];
    }

    /* Initialize file masks */
    for (k=0;k<NFILES;k++) {
        file_mask[k] = 0ULL;
        for (l=0;l<NRANKS;l++) {
            file_mask[k] |= sq_mask[SQUARE(k, l)];
        }
    }

    /* Initialize the A1H8 diagonal masks */
    file = FILE_A;
    k = 0;
    for (rank=RANK_1;rank<=RANK_8;rank++) {
        sq = SQUARE(file, rank);
        a1h8_masks[k] = bb_slider_moves(0ULL, sq, 1, 1)|sq_mask[sq];
        k++;
    }
    rank = RANK_1;
    for (file=FILE_B;file<=FILE_H;file++) {
        sq = SQUARE(file, rank);
        a1h8_masks[k] = bb_slider_moves(0ULL, sq, 1, 1)|sq_mask[sq];
        k++;
    }

    /* Initialize the A8H1 diagonal masks */
    file = FILE_A;
    k = 0;
    for (rank=RANK_1;rank<=RANK_8;rank++) {
        sq = SQUARE(file, rank);
        a8h1_masks[k] = bb_slider_moves(0ULL, sq, 1, -1)|sq_mask[sq];
        k++;
    }
    rank = RANK_8;
    for (file=FILE_B;file<=FILE_H;file++) {
        sq = SQUARE(file, rank);
        a8h1_masks[k] = bb_slider_moves(0ULL, sq, 1, -1)|sq_mask[sq];
        k++;
    }

    /* Initialize the front attackspans masks */
    for (sq=0;sq<NSQUARES;sq++) {
        front_attackspan[WHITE][sq] = 0ULL;
        front_attackspan[BLACK][sq] = 0ULL;
        if ((sq <= 7) || (sq >= 56)) {
            continue;
        }
        for (k=sq;k<56;k+=8) {
            file = FILENR(sq);
            if (file != FILE_A) {
                front_attackspan[WHITE][sq] |= sq_mask[k+7];
            }
            if (file != FILE_H) {
                front_attackspan[WHITE][sq] |= sq_mask[k+9];
            }
        }
        for (k=sq;k>7;k-=8) {
            file = FILENR(sq);
            if (file != FILE_A) {
                front_attackspan[BLACK][sq] |= sq_mask[k-9];
            }
            if (file != FILE_H) {
                front_attackspan[BLACK][sq] |= sq_mask[k-7];
            }
        }
    }

    /* Initialize the rear attackspans masks */
    for (sq=0;sq<NSQUARES;sq++) {
        rear_attackspan[WHITE][sq] = 0ULL;
        rear_attackspan[BLACK][sq] = 0ULL;
        if ((sq <= 7) || (sq >= 56)) {
            continue;
        }
        for (k=sq;k>=0;k-=8) {
            file = FILENR(sq);
            if (file != FILE_A) {
                rear_attackspan[WHITE][sq] |= sq_mask[k-1];
            }
            if (file != FILE_H) {
                rear_attackspan[WHITE][sq] |= sq_mask[k+1];
            }
        }
        for (k=sq;k<=63;k+=8) {
            file = FILENR(sq);
            if (file != FILE_A) {
                rear_attackspan[BLACK][sq] |= sq_mask[k-1];
            }
            if (file != FILE_H) {
                rear_attackspan[BLACK][sq] |= sq_mask[k+1];
            }
        }
    }

    /* Initialize the front span masks */
    for (sq=0;sq<NSQUARES;sq++) {
        front_span[WHITE][sq] = 0ULL;
        front_span[BLACK][sq] = 0ULL;

        for (k=sq+8;k<=63;k+=8) {
            front_span[WHITE][sq] |= sq_mask[k];
        }
        for (k=sq-8;k>=0;k-=8) {
            front_span[BLACK][sq] |= sq_mask[k];
        }
    }

    /* Initialize the rear span masks */
    for (sq=0;sq<NSQUARES;sq++) {
        rear_span[WHITE][sq] = 0ULL;
        rear_span[BLACK][sq] = 0ULL;

        for (k=sq-8;k>=0;k-=8) {
            rear_span[WHITE][sq] |= sq_mask[k];
        }
        for (k=sq+8;k<=63;k+=8) {
            rear_span[BLACK][sq] |= sq_mask[k];
        }
    }

    /* Outpost squares */
    outpost_squares[WHITE] =
                        rank_mask[RANK_4]|rank_mask[RANK_5]|rank_mask[RANK_6];
    outpost_squares[BLACK] =
                        rank_mask[RANK_5]|rank_mask[RANK_4]|rank_mask[RANK_3];

    /* Space evaluation squares */
    space_eval_squares[WHITE] =
                        rank_mask[RANK_2]|rank_mask[RANK_3]|rank_mask[RANK_4];
    space_eval_squares[BLACK] =
                        rank_mask[RANK_5]|rank_mask[RANK_6]|rank_mask[RANK_7];
    space_eval_squares[WHITE] &= (file_mask[FILE_B]|file_mask[FILE_C]|
                                  file_mask[FILE_D]|file_mask[FILE_E]|
                                  file_mask[FILE_F]|file_mask[FILE_G]);
    space_eval_squares[BLACK] &= (file_mask[FILE_B]|file_mask[FILE_C]|
                                  file_mask[FILE_D]|file_mask[FILE_E]|
                                  file_mask[FILE_F]|file_mask[FILE_G]);

    /* Line masks */
    for (k=0;k<NSQUARES;k++) {
        for (l=0;l<NSQUARES;l++) {
            if (RANKNR(k) == RANKNR(l)) {
                line_mask[k][l] = rank_mask[RANKNR(k)];
            } else if (FILENR(k) == FILENR(l)) {
                line_mask[k][l] = file_mask[FILENR(k)];
            } else if (sq2diag_a1h8[k] == sq2diag_a1h8[l]) {
                line_mask[k][l] = a1h8_masks[sq2diag_a1h8[k]];
            } else if (sq2diag_a8h1[k] == sq2diag_a8h1[l]) {
                line_mask[k][l] = a8h1_masks[sq2diag_a8h1[k]];
            } else {
                line_mask[k][l] = 0ULL;
            }
        }
    }

    /* Initialize king attack zone masks */
    init_king_zones();
}
