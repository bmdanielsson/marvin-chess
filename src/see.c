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

int see_material[NPIECES] = {100, 100,      /* pawn */
                             392, 392,      /* knight */
                             406, 406,      /* bishop */
                             654, 654,      /* rook */
                             1381, 1381,    /* queen */
                             20000, 20000}; /* king */

static uint64_t find_xray_attackers(struct position *pos, uint64_t occ,
                                    int target, uint64_t last_attacker)
{
    int      sq;
    uint64_t mask;

    /*
     * Check if there can actually be an xray attacker
     * hidden behind the last attacker.
     */
    if (last_attacker&
                (pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT])) {
        return 0ULL;
    }

    sq = LSB(last_attacker);
    occ |= sq_mask[target];
    if ((RANKNR(target) == RANKNR(sq)) || (FILENR(target) == FILENR(sq)))  {
        mask = pos->bb_pieces[WHITE_ROOK] | pos->bb_pieces[WHITE_QUEEN] |
               pos->bb_pieces[BLACK_ROOK] | pos->bb_pieces[BLACK_QUEEN];
        mask &= occ;
        return (bb_rook_moves(occ, target)&bb_rook_moves(occ, sq))&mask;
    } else {
        mask = pos->bb_pieces[WHITE_BISHOP] | pos->bb_pieces[WHITE_QUEEN] |
               pos->bb_pieces[BLACK_BISHOP] | pos->bb_pieces[BLACK_QUEEN];
        mask &= occ;
        return (bb_bishop_moves(occ, target)&bb_bishop_moves(occ, sq))&mask;
    }
}

static uint64_t find_next_attacker(struct position *pos, uint64_t attackers,
                                   int side, int *piece)
{
    uint64_t bb;

    for (*piece=PAWN+side;*piece<NPIECES;*piece+=2) {
        bb = attackers & pos->bb_pieces[*piece];
        if (bb != 0ULL) {
            return ISOLATE(bb);
        }
    }
    return 0ULL;
}

bool see_ge(struct position *pos, uint32_t move, int threshold)
{
    int      see_score;
    int      sq;
    int      maximizer;
    int      stm;
    int      piece;
    int      victim;
    uint64_t attackers;
    uint64_t attacker;
    uint64_t occ;

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
        see_score = see_material[PAWN+FLIP_COLOR(stm)];
    } else if (ISCAPTURE(move)) {
        see_score = see_material[pos->pieces[sq]];
    } else {
        see_score = 0;
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

    /* Iterate until there are no more attackers */
    while (!ISEMPTY(attackers)) {
        /*
         * Before recapturing compare the current score against the
         * threshold. If the side to move is the initial side to move
         * and the score is alreeady above the threshold then there is
         * no need to continue, same thing if it is the oppenents turn
         * to move and the score is below the threshold.
         */
        if ((stm == maximizer) && (see_score >= threshold)) {
            break;
        } else if ((stm != maximizer) && (see_score < threshold)) {
            break;
        }

        /* Find the next attacker to consider */
        attacker = find_next_attacker(pos, attackers, stm, &piece);
        if (attacker == 0ULL) {
            break;
        }

        /* Update the score based on the move */
        if (stm == maximizer) {
            see_score += see_material[victim];
        } else {
            see_score -= see_material[victim];
        }

        /*
         * Apply the move. The current piece becomes the next victim
         * unless it's a promotion in which case the next victim is
         * a queen.
         */
        attackers &= ~attacker;
        occ &= ~attacker;
        victim = piece;
        stm = FLIP_COLOR(stm);

        /* Find xray attackers and add them to the set of attackers */
        attackers |= find_xray_attackers(pos, occ, sq, attacker);
    }

    return see_score >= threshold;
}
