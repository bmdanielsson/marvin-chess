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
#include "search.h"
#include "eval.h"
#include "data.h"

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
        strcpy(str, "0000");
        return;
    } else if (move == NOMOVE) {
        strcpy(str, "(none)");
        return;
    }

    /*
     * Internally castling is represented as king-captures-rook so
     * for standard chess it needs to be converted to a king move.
     * Additionally when using the Xboard protocol when playing an
     * FRC game castling is represented with O-O or O-O-O.
     */
    if (engine_variant == VARIANT_STANDARD) {
        if (ISKINGSIDECASTLE(move)) {
            to = KINGCASTLE_KINGMOVE(to);
        } else if (ISQUEENSIDECASTLE(move)) {
            to = QUEENCASTLE_KINGMOVE(to);
        }
    } else if ((engine_variant == VARIANT_FRC) &&
               (engine_protocol == PROTOCOL_XBOARD)) {
        if (ISKINGSIDECASTLE(move)) {
            strcpy(str, "O-O");
            return;
        } else if (ISQUEENSIDECASTLE(move)) {
            strcpy(str, "O-O-O");
            return;
        }
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
    if (strlen(str) < 3) {
        return NOMOVE;
    }

    /*
     * When using Xboard protocol and playing an FRC game castling is
     * is represented using O-O or O-O-O.
     */
    if ((engine_variant == VARIANT_FRC) &&
        (engine_protocol == PROTOCOL_XBOARD)) {
        if (!strcmp(str, "O-O")) {
            from = LSB(pos->bb_pieces[KING+pos->stm]);
            to = (pos->stm == WHITE)?pos->castle_wk:pos->castle_bk;
            promotion = NO_PIECE;
            goto check_move;
        } else if (!strcmp(str, "O-O-O")) {
            from = LSB(pos->bb_pieces[KING+pos->stm]);
            to = (pos->stm == WHITE)?pos->castle_wq:pos->castle_bq;
            promotion = NO_PIECE;
            goto check_move;
        }
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
      * Internally castling is represented as king-captures-rook so
      * for standard chess it needs to be converted from a king move.
      */
    if ((engine_variant == VARIANT_STANDARD) &&
        (pos->pieces[from] == (pos->stm+KING)) &&
        (abs(to-from) == 2)) {
        if (to < from) {
            to = (pos->stm == WHITE)?pos->castle_wq:pos->castle_bq;
        } else if (to > from) {
            to = (pos->stm == WHITE)?pos->castle_wk:pos->castle_bk;
        }
    }

check_move:
    /*
     * Generate all moves for the currect position and make sure
     * that the move is among them.
     */
    gen_moves(pos, &list);
    for (k=0;k<list.size;k++) {
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
