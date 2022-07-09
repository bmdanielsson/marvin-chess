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
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include "see.h"
#include "eval.h"
#include "evalparams.h"
#include "bitboard.h"
#include "validation.h"
#include "fen.h"

bool see_ge(struct position *pos, uint32_t move, int threshold)
{
    int      see_score;
    int      old_score;
    int      sq;
    int      maximizer;
    int      stm;
    int      piece;
    int      victim;
    int      val;
    uint64_t attackers;
    uint64_t attacker;
    uint64_t occ;
    uint64_t bq;
    uint64_t rq;

    assert(valid_position(pos));
    assert(valid_move(move));

    /*
     * In order for the castling to be legal the destination
     * square of the rook cannot be attacked so the see score
     * is always zero.
     */
    if (ISKINGSIDECASTLE(move) || ISQUEENSIDECASTLE(move)) {
       return threshold < 0;
    }

    /* Find the score of the move */
    maximizer = pos->stm;
    stm = maximizer;
    sq = TO(move);
    piece = pos->pieces[FROM(move)];
    if (ISENPASSANT(move)) {
        see_score = material_values[PAWN+FLIP_COLOR(stm)];
    } else if (ISCAPTURE(move)) {
        see_score = material_values[pos->pieces[sq]];
    } else {
        see_score = 0;
    }

    /* Check if it's possible to exit early */
    if (VALUE(piece) != KING) {
        if (see_score < threshold) {
            return false;
        }
        if ((see_score-material_values[piece]) >= threshold) {
            return true;
        }
    }

    /* Apply the move */
    occ = pos->bb_all&(~sq_mask[FROM(move)]);
    if (ISENPASSANT(move)) {
        occ &= ~sq_mask[(pos->stm==WHITE)?sq-8:sq+8];
    }
    victim = piece;
    stm = FLIP_COLOR(stm);

    /* Find all pieces that attacks the target square */
    attackers = bb_attacks_to(pos, occ, sq, WHITE) |
                                            bb_attacks_to(pos, occ, sq, BLACK);
    attackers &= ~sq_mask[FROM(move)];

    /*
     * If the moving piece is a king and there are opponent
     *  attackers then the move is illegal.
     */
    if ((VALUE(victim) == KING) &&
        ((attackers&pos->bb_sides[stm]) != 0ULL)) {
        return false;
    }

    /* Iterate until there are no more attackers */
    bq = pos->bb_pieces[WHITE_BISHOP] | pos->bb_pieces[WHITE_QUEEN] |
         pos->bb_pieces[BLACK_BISHOP] | pos->bb_pieces[BLACK_QUEEN];
    rq = pos->bb_pieces[WHITE_ROOK] | pos->bb_pieces[WHITE_QUEEN] |
         pos->bb_pieces[BLACK_ROOK] | pos->bb_pieces[BLACK_QUEEN];
    while (!ISEMPTY(attackers)) {
        /*
         * Before recapturing compare the current score against the
         * threshold. If the side to move is the initial side to move
         * and the score is alreeady above the threshold then there is
         * no need to continue, same thing if it is the oppenents turn
         * to move and the score is below the threshold.
         */
        if (((stm == maximizer) && (see_score >= threshold)) ||
            ((stm != maximizer) && (see_score < threshold))) {
            break;
        }

        /* Find the next attacker to consider */
        attacker = 0ULL;
        for (piece=PAWN+stm;piece<NPIECES;piece+=2) {
            if ((attackers&pos->bb_pieces[piece]) != 0ULL) {
                attacker = ISOLATE(attackers&pos->bb_pieces[piece]);
                break;
            }
        }
        if (attacker == 0ULL) {
            break;
        }

        /* Update the score based on the move */
        old_score = see_score;
        see_score += (stm == maximizer)?
                            material_values[victim]:-material_values[victim];

        /* Apply the move. The current piece becomes the next victim. */
        attackers &= ~attacker;
        occ &= ~attacker;
        victim = piece;
        stm = FLIP_COLOR(stm);

        /* Look for potential xray attackers */
        val = VALUE(piece);
        if ((val == PAWN) || (val == BISHOP) || (val == QUEEN)) {
            attackers |= (bb_bishop_moves(occ, sq)&bq);
        }
        if ((val == ROOK) || (val == QUEEN)) {
            attackers |= (bb_rook_moves(occ, sq)&rq);
        }
        attackers &= occ;

        /*
         * Check the last capturing piece was a king. If it was and there
         * are still attackers the the move was illegal.
        */
        if ((VALUE(victim) == KING) &&
            ((attackers&pos->bb_sides[stm]) != 0ULL)) {
            see_score = old_score;
            break;
        }
    }

    return see_score >= threshold;
}
