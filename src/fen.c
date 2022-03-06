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
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "fen.h"
#include "key.h"
#include "bitboard.h"
#include "validation.h"
#include "eval.h"
#include "board.h"

/* Returns true if c is a digit between '0' and '8'. */
#define IS_DIGIT_08(c)  ((c=='0')||(c=='1')||(c=='2')||(c=='3')|| \
                         (c=='4')||(c=='5')||(c=='6')||(c=='7')|| \
                         (c=='8')) \
                        ?true:false

/* Returns true if c is a digit between '0' and '9'. */
#define IS_DIGIT_09(c)  ((c=='0')||(c=='1')||(c=='2')||(c=='3')|| \
                         (c=='4')||(c=='5')||(c=='6')||(c=='7')|| \
                         (c=='8')||(c=='9')) \
                        ?true:false

/* Returns true if c is a piece charachter. */
#define IS_PIECE(c) ((c=='K')||(c=='Q')||(c=='R')||(c=='B')||(c=='N')||  \
                     (c=='P')|| (c=='k')||(c=='q')||(c=='r')||(c=='b')|| \
                     (c=='n')||(c=='p')) \
                    ?true:false

static uint8_t outer_rook(struct position *pos, int castle)
{
    uint8_t sq;
    uint8_t start;
    uint8_t stop;
    int     target;
    int     blocker;
    int     delta;

    if (castle == WHITE_KINGSIDE) {
        start = H1;
        stop = A1;
        delta = -1;
        target = WHITE_ROOK;
        blocker = WHITE_KING;
    } else if (castle == WHITE_QUEENSIDE) {
        start = A1;
        stop = H1;
        delta = 1;
        target = WHITE_ROOK;
        blocker = WHITE_KING;
    } else if (castle == BLACK_KINGSIDE) {
        start = H8;
        stop = A8;
        delta = -1;
        target = BLACK_ROOK;
        blocker = BLACK_KING;
    } else if (castle == BLACK_QUEENSIDE) {
        start = A8;
        stop = H8;
        delta = 1;
        target = BLACK_ROOK;
        blocker = BLACK_KING;
    } else {
        return NO_SQUARE;
    }

    for (sq=start;sq!=stop;sq+=delta) {
        if (pos->pieces[sq] == target) {
            return sq;
        } else if (pos->pieces[sq] == blocker) {
            return NO_SQUARE;
        }
    }

    return NO_SQUARE;
}

static void set_castle_from_file(struct position *pos, char file_char)
{
    int king_sq;
    int rook_sq;

    if ((file_char >= 'A') && (file_char <= 'H')) {
        rook_sq = SQUARE(file_char-'A', RANK_1);
        king_sq = LSB(pos->bb_pieces[WHITE_KING]);
        if (king_sq < rook_sq) {
            pos->castle |= WHITE_KINGSIDE;
            pos->castle_wk = rook_sq;
        } else if (king_sq > rook_sq) {
            pos->castle |= WHITE_QUEENSIDE;
            pos->castle_wq = rook_sq;
        }
    } else if ((file_char >= 'a') && (file_char <= 'h')) {
        rook_sq = SQUARE(file_char-'a', RANK_8);
        king_sq = LSB(pos->bb_pieces[BLACK_KING]);
        if (king_sq < rook_sq) {
            pos->castle |= BLACK_KINGSIDE;
            pos->castle_bk = rook_sq;
        } else if (king_sq > rook_sq) {
            pos->castle |= BLACK_QUEENSIDE;
            pos->castle_bq = rook_sq;
        }
    }
}

static int char2piece(char piece)
{
    switch (piece) {
    case 'K':
        return WHITE_KING;
    case 'Q':
        return WHITE_QUEEN;
    case 'R':
        return WHITE_ROOK;
    case 'B':
        return WHITE_BISHOP;
    case 'N':
        return WHITE_KNIGHT;
    case 'P':
        return WHITE_PAWN;
    case 'k':
        return BLACK_KING;
    case 'q':
        return BLACK_QUEEN;
    case 'r':
        return BLACK_ROOK;
    case 'b':
        return BLACK_BISHOP;
    case 'n':
        return BLACK_KNIGHT;
    case 'p':
        return BLACK_PAWN;
    default:
        return NO_PIECE;
    }
}

bool fen_setup_board(struct position *pos, char *fenstr)
{
    int  rank;
    int  file;
    char *iter;
    int  k;
    int  sq;

    assert(pos != NULL);
    assert(fenstr != NULL);

    /* Parse the piece placement field */
    iter = fenstr;
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        file = FILE_A;
        while (*iter != '/') {
            if (IS_DIGIT_08(*iter)) {
                /* Consequtive empty squares */
                file += *iter - '0';
            } else if (*iter == ' ') {
                /* End of piece placement field */
                break;
            } else if (IS_PIECE(*iter)) {
                /* Piece */
                pos->pieces[SQUARE(file, rank)] = char2piece(*iter);
                file++;
            }
            iter++;
        }
        iter++;
    }

    /* Update bitboards */
    for (sq=0;sq<NSQUARES;sq++) {
        if (pos->pieces[sq] != NO_PIECE) {
            SETBIT(pos->bb_pieces[pos->pieces[sq]], sq);
            SETBIT(pos->bb_sides[COLOR(pos->pieces[sq])], sq);
            SETBIT(pos->bb_all, sq);
        }
    }

    /* Active color field */
    switch (*iter) {
    case 'w':
        pos->stm = WHITE;
        break;
    case 'b':
        pos->stm = BLACK;
        break;
    default:
        return false;
    }
    iter++;
    if (*iter != ' ') {
        return false;
    }

    /* Castling availability field */
    iter++;
    pos->castle = 0;
    pos->castle_wk = NO_SQUARE;
    pos->castle_wq = NO_SQUARE;
    pos->castle_bk = NO_SQUARE;
    pos->castle_bq = NO_SQUARE;
    for (k=0;k<4;k++) {
        if (*iter == '-') {
            iter++;
            break;
        } else if (*iter == ' ') {
            break;
        }

        switch (*iter) {
        case 'K':
            pos->castle |= WHITE_KINGSIDE;
            pos->castle_wk = outer_rook(pos, WHITE_KINGSIDE);
            break;
        case 'Q':
            pos->castle |= WHITE_QUEENSIDE;
            pos->castle_wq = outer_rook(pos, WHITE_QUEENSIDE);
            break;
        case 'k':
            pos->castle |= BLACK_KINGSIDE;
            pos->castle_bk = outer_rook(pos, BLACK_KINGSIDE);
            break;
        case 'q':
            pos->castle |= BLACK_QUEENSIDE;
            pos->castle_bq = outer_rook(pos, BLACK_QUEENSIDE);
            break;
        /* FRC extension */
        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
        case 'G':
        case 'H':
        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
        case 'g':
        case 'h':
            set_castle_from_file(pos, *iter);
            break;
        default:
            return false;
        }
        iter++;
    }
    if (*iter != ' ') {
        return false;
    }
    if ((pos->castle < 0) || (pos->castle > 15)) {
        return false;
    }

    /* En-passant field */
    iter++;
    if (*iter == '-') {
        pos->ep_sq = NO_SQUARE;
        iter++;
    } else {
        file = *iter - 'a';
        rank = *(iter+1) - '1';
        pos->ep_sq = SQUARE(file, rank);
        iter += 2;
    }
    if ((*iter != ' ') && (*iter != '\0')) {
        return false;
    }
    if ((pos->castle < A1) || (pos->ep_sq > NO_SQUARE)) {
        return false;
    }

    /*
     * Allow the 'halfmove' and 'full move' fields to be
     * omitted in order to handle EPD strings.
     */
    iter = skip_whitespace(iter);
    if (*iter != '\0' && IS_DIGIT_09(*iter)) {
        /* Halfmove counter field */
        iter++;
        if (sscanf(iter, "%d", &pos->fifty) != 1) {
            return false;
        }
        while (IS_DIGIT_09(*iter)) {
            iter++;
        }
        if (*iter != ' ') {
            return false;
        }
        if (pos->fifty < 0) {
            return false;
        }

        /* Full move field */
        iter++;
        if (sscanf(iter, "%d", &pos->fullmove) != 1) {
            return false;
        }
        while (IS_DIGIT_09(*iter)) {
            iter++;
        }
        if (*iter != '\0') {
            return false;
        }
    } else {
        pos->fifty = 0;
        pos->fullmove = 1;
    }

    /* Generate a key for the position */
    pos->key = key_generate(pos);

    return true;
}

void fen_build_string(struct position *pos, char *fenstr)
{
    char *iter;
    int  empty_count;
    int  rank;
    int  file;
    int  sq;

    assert(valid_position(pos));

    /* Clear the string */
    memset(fenstr, 0, FEN_MAX_LENGTH);

    /* Piece placement */
    empty_count = 0;
    iter = fenstr;
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        for (file=FILE_A;file<=FILE_H;file++) {
            sq = SQUARE(file, rank);
            if (pos->pieces[sq] != NO_PIECE) {
                if (empty_count > 0) {
                    *(iter++) = '0' + empty_count;
                    empty_count = 0;
                }
                *(iter++) = piece2char[pos->pieces[sq]];
            } else {
                empty_count++;
            }
        }
        if (empty_count != 0) {
            *(iter++) = '0' + empty_count;
            empty_count = 0;
        }
        if (rank > 0) {
            *(iter++) = '/';
        }
    }
    *(iter++) = ' ';

    /* Active color */
    if (pos->stm == WHITE) {
        *(iter++) = 'w';
    } else {
        *(iter++) = 'b';
    }
    *(iter++) = ' ';

    /* Castling avliability */
    if (pos->castle == 0) {
        *(iter++) = '-';
    } else {
        if (pos->castle&WHITE_KINGSIDE) {
            *(iter++) = 'K';
        }
        if (pos->castle&WHITE_QUEENSIDE) {
            *(iter++) = 'Q';
        }
        if (pos->castle&BLACK_KINGSIDE) {
            *(iter++) = 'k';
        }
        if (pos->castle&BLACK_QUEENSIDE) {
            *(iter++) = 'q';
        }
    }
    *(iter++) = ' ';

    /* En passant target square */
    if (pos->ep_sq == NO_SQUARE) {
        *(iter++) = '-';
    } else {
        *(iter++) = 'a' + FILENR(pos->ep_sq);
        *(iter++) = '1' + RANKNR(pos->ep_sq);
    }
    *(iter++) = ' ';

    /* Halfmove clock */
    sprintf(iter, "%d", pos->fifty);
    iter += strlen(iter);
    *(iter++) = ' ';

    /* Fullmove number */
    sprintf(iter, "%d", pos->fullmove);
}
