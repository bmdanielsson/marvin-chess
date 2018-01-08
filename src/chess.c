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
#include <stdlib.h>

#include "chess.h"
#include "validation.h"
#include "movegen.h"
#include "config.h"
#include "hash.h"
#include "board.h"
#include "bitboard.h"
#include "engine.h"

uint64_t sq_mask[NSQUARES];

uint64_t white_square_mask;

uint64_t black_square_mask;

uint64_t rank_mask[NRANKS];

uint64_t file_mask[NFILES];

uint64_t a1h8_masks[NDIAGONALS];

uint64_t a8h1_masks[NDIAGONALS];

uint64_t front_attackspan[NSIDES][NSQUARES];

uint64_t rear_attackspan[NSIDES][NSQUARES];

uint64_t front_span[NSIDES][NSQUARES];

uint64_t king_attack_zone[NSIDES][NSQUARES];

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

int mirror_table[NSQUARES];

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

static void init_king_attack_zones(void)
{
    int sq;
    int rank;
    int file;

    for (sq=0;sq<NSQUARES;sq++) {
        rank = RANKNR(sq);
        file = FILENR(sq);

        king_attack_zone[WHITE][sq] = 0ULL;
        king_attack_zone[BLACK][sq] = 0ULL;

        /* White king zone */
        if (rank < RANK_8) {
            if (file > FILE_A) {
                SETBIT(king_attack_zone[WHITE][sq], sq+7);
            }
            SETBIT(king_attack_zone[WHITE][sq], sq+8);
            if (file < FILE_H) {
                SETBIT(king_attack_zone[WHITE][sq], sq+9);
            }
        }
        if (file > FILE_A) {
            SETBIT(king_attack_zone[WHITE][sq], sq-1);
        }
        if (file < FILE_H) {
            SETBIT(king_attack_zone[WHITE][sq], sq+1);
        }
        if (rank > RANK_1) {
            if (file > FILE_A) {
                SETBIT(king_attack_zone[WHITE][sq], sq-9);
            }
            SETBIT(king_attack_zone[WHITE][sq], sq-8);
            if (file < FILE_H) {
                SETBIT(king_attack_zone[WHITE][sq], sq-7);
            }
        }

        /* Black king zone */
        if (rank > RANK_1) {
            if (file > FILE_A) {
                SETBIT(king_attack_zone[BLACK][sq], sq-9);
            }
            SETBIT(king_attack_zone[BLACK][sq], sq-8);
            if (file < FILE_H) {
                SETBIT(king_attack_zone[BLACK][sq], sq-7);
            }
        }
        if (file > FILE_A) {
            SETBIT(king_attack_zone[BLACK][sq], sq-1);
        }
        if (file < FILE_H) {
            SETBIT(king_attack_zone[BLACK][sq], sq+1);
        }
        if (rank < RANK_8) {
            if (file > FILE_A) {
                SETBIT(king_attack_zone[BLACK][sq], sq+7);
            }
            SETBIT(king_attack_zone[BLACK][sq], sq+8);
            if (file < FILE_H) {
                SETBIT(king_attack_zone[BLACK][sq], sq+9);
            }
        }
    }
}

void chess_data_init(void)
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
    }

    /* Initialize file masks */
    for (k=0;k<NFILES;k++) {
        file_mask[k] = 0ULL;
        for (l=0;l<NRANKS;l++) {
            file_mask[k] |= sq_mask[SQUARE(k, l)];
        }
    }

    /* Initialize mirror table */
    for(k=0;k<NSQUARES;k++) {
        rank = RANKNR(k);
        file = FILENR(k);
        mirror_table[k] = SQUARE(file, 7-rank);
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

        if ((sq >= 56) || (sq <= 7)) {
            continue;
        }

        for (k=sq+8;k<=63;k+=8) {
            front_span[WHITE][sq] |= sq_mask[k];
        }
        for (k=sq-8;k>=0;k-=8) {
            front_span[BLACK][sq] |= sq_mask[k];
        }
    }

    /* Initialize king attack zone masks */
    init_king_attack_zones();
}

struct gamestate* create_game_state(void)
{
    struct gamestate *state;

    state = malloc(sizeof(struct gamestate));
    if (state == NULL) {
        return NULL;
    }
    memset(state, 0, sizeof(struct gamestate));
    hash_tt_create_table(engine_default_hash_size);
    board_reset(&state->pos);
    board_start_position(&state->pos);

    return state;
}

void destroy_game_state(struct gamestate *state)
{
    assert(state != NULL);

    hash_tt_destroy_table();
    free(state);
}

void reset_game_state(struct gamestate *state)
{
    board_start_position(&state->pos);
    hash_tt_clear_table();
}

void move2str(uint32_t move, char *str)
{
    int from;
    int to;
    int promotion;

    assert(valid_move(move));
    assert(str != NULL);

    from = FROM(move);
    to = TO(move);
    promotion = PROMOTION(move);

    if (ISNULLMOVE(move)) {
        strcpy(str, "null");
        return;
    }

    str[0] = FILENR(from) + 'a';
    str[1] = RANKNR(from) + '1';
    str[2] = FILENR(to) + 'a';
    str[3] = RANKNR(to) + '1';
    if (ISPROMOTION(move)) {
        switch (VALUE(promotion)) {
        case KNIGHT:
            str[4] = 'n';
            break;
        case BISHOP:
            str[4] = 'b';
            break;
        case ROOK:
            str[4] = 'r';
            break;
        case QUEEN:
            str[4] = 'q';
            break;
        default:
            assert(false);
            break;
        }
        str[5] = '\0';
    } else {
        str[4] = '\0';
    }
}

uint32_t str2move(char *str, struct position *pos)
{
    uint32_t        move;
    struct movelist list;
    int             promotion;
    int             from;
    int             to;
    int             k;

    assert(str != NULL);
    assert(valid_position(pos));

    /* Make sure that the string is at least 4 characters long */
    if (strlen(str) < 4) {
        return NOMOVE;
    }

    /* Get from/to squares and a potential promotion piece */
    from = SQUARE(str[0]-'a', str[1]-'1');
    to = SQUARE(str[2]-'a', str[3]-'1');
    switch (str[4]) {
    case 'n':
        promotion = KNIGHT + pos->stm;
        break;
    case 'b':
        promotion = BISHOP + pos->stm;
        break;
    case 'r':
        promotion = ROOK + pos->stm;
        break;
    case 'q':
        promotion = QUEEN + pos->stm;
        break;
    default:
        promotion = NO_PIECE;
        break;
    }

    /*
     * Generate all moves for the currect position and make sure
     * that the move is among them.
     */
    gen_moves(pos, &list);
    for (k=0;k<list.nmoves;k++) {
        move = list.moves[k];
        if ((from == FROM(move)) && (to == TO(move))) {
            if (ISPROMOTION(move)) {
                if (promotion == PROMOTION(move)) {
                    return move;
                }
            } else {
                return move;
            }
        }
    }

    return NOMOVE;
}

void copy_pv(struct pv *from, struct pv *to)
{
    int k;

    to->length = from->length;
    for (k=0;k<from->length;k++) {
        to->moves[k] = from->moves[k];
    }
}
