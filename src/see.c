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

#include "see.h"
#include "eval.h"
#include "bitboard.h"
#include "validation.h"

int see_material[NPIECES] = {
    100,    /* White pawn */
    100,    /* Black pawn */
    325,    /* White knight */
    325,    /* Black knight */
    325,    /* White bishop */
    325,    /* Black bishop */
    500,    /* White rook */
    500,    /* Black rook */
    975,    /* White queen */
    975,    /* Black queen */
    20000,  /* White king */
    20000   /* Black king */
};

static uint64_t find_xray_attackers(struct gamestate *pos, uint64_t occ,
                                    int target, uint64_t attacker)
{
    int      sq;
    int      piece;
    int      piece_value;
    uint64_t xrays;
    int      fdelta;
    int      rdelta;
    uint64_t mask;
    uint64_t attackers;

    /*
     * First check if there can actually be an xray attacker
     * hidden behind the current attacker.
     */
    sq = LSB(attacker);
    piece = pos->pieces[sq];
    piece_value = VALUE(piece);
    if ((piece_value == KING) || (piece_value == KNIGHT)) {
        return 0ULL;
    }

    /* Find the normalized direction of potential xray attackers */
    fdelta = FILENR(sq) - FILENR(target);
    if (fdelta != 0) {
        fdelta /= abs(fdelta);
    }
    rdelta = RANKNR(sq) - RANKNR(target);
    if (rdelta != 0) {
        rdelta /= abs(rdelta);
    }

    /* Generate slider moves in the xray direction */
    xrays = bb_slider_moves(occ, sq, fdelta, rdelta);

    /*
     * If the xrays set contains a piece that can move in the given
     * direction then that piece can attack the target square and
     * should be added as an attacker.
     */
    attackers = 0ULL;
    mask = 0ULL;
    if ((fdelta == 0) || (rdelta == 0)) {
        mask = pos->bb_pieces[WHITE_ROOK] | pos->bb_pieces[WHITE_QUEEN] |
                    pos->bb_pieces[BLACK_ROOK] | pos->bb_pieces[BLACK_QUEEN];
    } else {
        mask = pos->bb_pieces[WHITE_BISHOP] | pos->bb_pieces[WHITE_QUEEN] |
                    pos->bb_pieces[BLACK_BISHOP] | pos->bb_pieces[BLACK_QUEEN];
    }
    attackers = xrays & mask;

    return attackers;
}

static uint64_t find_next_attacker(struct gamestate *pos, uint64_t attackers,
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

/*
 * The algorithm used is based on the swap algorithm from the
 * chessprogramming wiki:
 * https://chessprogramming.wikispaces.com/SEE+-+The+Swap+Algorithm
 */
int see_calculate_score(struct gamestate *pos, uint32_t move)
{
    int      score[32];
    int      depth;
    int      to;
    int      from;
    int      piece;
    int      side;
    uint64_t attackers;
    uint64_t attacker;
    uint64_t occ;

    assert(valid_board(pos));
    assert(valid_move(move));
    assert(ISCAPTURE(move));
    assert(!ISENPASSANT(move));

    to = TO(move);
    from = FROM(move);
    piece = pos->pieces[from];
    attacker = sq_mask[from];
    side = COLOR(piece);
    depth = 0;
    score[0] = see_material[pos->pieces[to]];
    occ = pos->bb_all;

    /* Find all pieces that attacks the target square */
    attackers = bb_attacks_to(pos, pos->bb_all, to, WHITE) |
                                bb_attacks_to(pos, pos->bb_all, to, BLACK);

    /* Iterate until there are no more attackers */
    do {
        /* Resolve capture and calculate score at this depth */
        depth++;
        score[depth] = see_material[piece] - score[depth-1];

        /* Remove the attacker from the set of attackers */
        attackers &= ~attacker;
        occ &= ~attacker;

        /* Find xray attackers and add them to the set of attackers */
        attackers |= find_xray_attackers(pos, occ, to, attacker);

        /* Find the next attacker to consider */
        side = FLIP_COLOR(side);
        attacker = find_next_attacker(pos, attackers, side, &piece);
    } while (attacker != 0ULL);

    /* Min-max the score array to find the final score */
    depth--;
    while (depth > 0) {
        if (score[depth] > -score[depth-1]) {
            score[depth-1] = -score[depth];
        }
        depth--;
    }

    return score[0];
}

