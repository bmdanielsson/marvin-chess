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
#include "egtb.h"
#include "tbprobe.h"
#include "bitboard.h"

void egtb_init(char *path)
{
    tb_init(path);
}

bool egtb_should_probe(struct position *pos)
{
    return BITCOUNT(pos->bb_all) <= (int)TB_LARGEST;
}

bool egtb_probe_dtz_tables(struct position *pos, uint32_t *move, int *score)
{
    unsigned int res;
    int          wdl;
    int          promotion;
    int          flags;
    int          from;
    int          to;

    res = tb_probe_root(pos->bb_sides[WHITE], pos->bb_sides[BLACK],
                    pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING],
                    pos->bb_pieces[WHITE_QUEEN]|pos->bb_pieces[BLACK_QUEEN],
                    pos->bb_pieces[WHITE_ROOK]|pos->bb_pieces[BLACK_ROOK],
                    pos->bb_pieces[WHITE_BISHOP]|pos->bb_pieces[BLACK_BISHOP],
                    pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT],
                    pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN],
                    pos->fifty, pos->castle,
                    pos->ep_sq != NO_SQUARE?pos->ep_sq:0,
                    pos->stm == WHITE, NULL);
    if (res == TB_RESULT_FAILED) {
        return false;
    }
    wdl = TB_GET_WDL(res);
    switch (wdl) {
    case TB_LOSS:
        *score = -TABLEBASE_WIN;
        break;
    case TB_WIN:
        *score = TABLEBASE_WIN;
        break;
    case TB_BLESSED_LOSS:
    case TB_CURSED_WIN:
    case TB_DRAW:
    default:
        *score = 0;
        break;
    }

    from = TB_GET_FROM(res);
    to = TB_GET_TO(res);
    flags = NORMAL;
    promotion = NO_PIECE;
    if (TB_GET_EP(res) != 0) {
        flags = EN_PASSANT;
    } else {
        if (pos->pieces[to] != NO_PIECE) {
            flags |= CAPTURE;
        }
        switch (TB_GET_PROMOTES(res)) {
        case TB_PROMOTES_QUEEN:
            flags |= PROMOTION;
            promotion = QUEEN + pos->stm;
           break;
        case TB_PROMOTES_ROOK:
            flags |= PROMOTION;
            promotion = ROOK + pos->stm;
            break;
        case TB_PROMOTES_BISHOP:
            flags |= PROMOTION;
            promotion = BISHOP + pos->stm;
            break;
        case TB_PROMOTES_KNIGHT:
            flags |= PROMOTION;
            promotion = KNIGHT + pos->stm;
            break;
        case TB_PROMOTES_NONE:
        default:
            break;
        }
    }
    *move = MOVE(from, to, promotion, flags);

    return true;
}

bool egtb_probe_wdl_tables(struct position *pos, int *score)
{
    unsigned int res;

    res = tb_probe_wdl(pos->bb_sides[WHITE], pos->bb_sides[BLACK],
                    pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING],
                    pos->bb_pieces[WHITE_QUEEN]|pos->bb_pieces[BLACK_QUEEN],
                    pos->bb_pieces[WHITE_ROOK]|pos->bb_pieces[BLACK_ROOK],
                    pos->bb_pieces[WHITE_BISHOP]|pos->bb_pieces[BLACK_BISHOP],
                    pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT],
                    pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN],
                    pos->fifty, pos->castle,
                    pos->ep_sq != NO_SQUARE?pos->ep_sq:0,
                    pos->stm == WHITE);
    if (res == TB_RESULT_FAILED) {
        *score = 0;
        return false;
    }

    switch (res) {
    case TB_WIN:
        *score = TABLEBASE_WIN - pos->height;
        break;
    case TB_LOSS:
        *score = TABLEBASE_LOSS + pos->height;
        break;
    default:
        *score = 0;
        break;
    }

    return true;
}

