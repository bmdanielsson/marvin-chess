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

#include "movegen.h"
#include "position.h"
#include "bitboard.h"
#include "validation.h"
#include "debug.h"
#include "data.h"

#define ADD_MOVE(l,f,t,p,fl) l->moves[l->size++] = MOVE((f), (t), (p), (fl))

static void gen_en_passant_moves(struct position *pos, struct movelist *list)
{
    uint64_t pieces;
    int      pawn_pos;
    int      from;
    int      file;
    int      offset;

    /* Check if en passant capture is possible */
    if (pos->ep_sq == NO_SQUARE) {
        return;
    }

    file = FILENR(pos->ep_sq);
    offset = (pos->stm == WHITE)?-8:8;
    pieces = 0ULL;

    /* Find the square of the pawn that can be captured */
    pawn_pos = pos->ep_sq + offset;

    /* Find location of capturers if there are any */
    if (file != FILE_A) {
        SETBIT(pieces, pawn_pos-1);
    }
    if (file != FILE_H) {
        SETBIT(pieces, pawn_pos+1);
    }
    pieces &= pos->bb_pieces[PAWN+pos->stm];

    /* Add en passant captures to move list */
    while (pieces != 0ULL) {
        from = POPBIT(&pieces);
        ADD_MOVE(list, from, pos->ep_sq, NO_PIECE, EN_PASSANT);
    }
}

static void gen_kingside_castling_moves(struct position *pos,
                                        struct movelist *list)
{
    int king_start = LSB(pos->bb_pieces[KING+pos->stm]);
    int rook_start = (pos->stm == WHITE)?pos->castle_wk:pos->castle_bk;

    if (!pos_is_castling_allowed(pos, KINGSIDE_CASTLE)) {
        return;
    }

    ADD_MOVE(list, king_start, rook_start, NO_PIECE, KINGSIDE_CASTLE);
}

static void gen_queenside_castling_moves(struct position *pos,
                                         struct movelist *list)
{
    int king_start = LSB(pos->bb_pieces[KING+pos->stm]);
    int rook_start = (pos->stm == WHITE)?pos->castle_wq:pos->castle_bq;

    if (!pos_is_castling_allowed(pos, QUEENSIDE_CASTLE)) {
        return;
    }

    ADD_MOVE(list, king_start, rook_start, NO_PIECE, QUEENSIDE_CASTLE);
}

static void add_promotion_moves(struct position *pos, struct movelist *list,
                                int from, uint64_t moves, int flags,
                                bool underpromote)
{
    int to;

    while (moves != 0ULL) {
        to = POPBIT(&moves);
        ADD_MOVE(list, from, to, QUEEN+pos->stm, flags);
        if (underpromote) {
            ADD_MOVE(list, from, to, ROOK+pos->stm, flags);
            ADD_MOVE(list, from, to, BISHOP+pos->stm, flags);
            ADD_MOVE(list, from, to, KNIGHT+pos->stm, flags);
        }
    }
}

static void add_moves(struct movelist *list, int from, uint64_t moves,
                      int flags)
{
    int to;

    while (moves != 0ULL) {
        to = POPBIT(&moves);
        ADD_MOVE(list, from, to, NO_PIECE, flags);
    }
}

static void gen_pawn_moves(struct position *pos, struct movelist *list,
                           uint64_t mask)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[PAWN+pos->stm];
    pieces &= (pos->stm == WHITE)?(~rank_mask[RANK_7]):(~rank_mask[RANK_2]);
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_moves(list, sq, bb_pawn_moves(pos->bb_all, sq, pos->stm)&mask, 0);
    }
}

static void gen_pawn_captures(struct position *pos, struct movelist *list,
                              uint64_t mask)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[PAWN+pos->stm];
    pieces &= (pos->stm == WHITE)?(~rank_mask[RANK_7]):(~rank_mask[RANK_2]);
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_moves(list, sq, bb_pawn_attacks_from(sq, pos->stm)&mask, CAPTURE);
    }
}

static void gen_promotions(struct position *pos, struct movelist *list,
                           bool underpromote, uint64_t mask)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[PAWN+pos->stm];
    pieces &= (pos->stm == WHITE)?rank_mask[RANK_7]:rank_mask[RANK_2];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_promotion_moves(pos, list, sq,
                            bb_pawn_moves(pos->bb_all, sq, pos->stm)&mask,
                            PROMOTION, underpromote);
    }
}

static void gen_capture_promotions(struct position *pos, struct movelist *list,
                                   bool underpromote, uint64_t mask)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[PAWN+pos->stm];
    pieces &= (pos->stm == WHITE)?rank_mask[RANK_7]:rank_mask[RANK_2];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_promotion_moves(pos, list, sq,
                            bb_pawn_attacks_from(sq, pos->stm)&mask,
                            CAPTURE|PROMOTION, underpromote);
    }
}

static void gen_knight_moves(struct position *pos, struct movelist *list,
                             uint64_t mask, int flags)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[KNIGHT+pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_moves(list, sq, bb_knight_moves(sq)&mask, flags);
    }
}

static void gen_diagonal_slider_moves(struct position *pos,
                                      struct movelist *list, uint64_t mask,
                                      int flags)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[BISHOP+pos->stm]|pos->bb_pieces[QUEEN+pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_moves(list, sq, bb_bishop_moves(pos->bb_all, sq)&mask, flags);
    }
}

static void gen_straight_slider_moves(struct position *pos,
                                      struct movelist *list, uint64_t mask,
                                      int flags)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[ROOK+pos->stm]|pos->bb_pieces[QUEEN+pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_moves(list, sq, bb_rook_moves(pos->bb_all, sq)&mask, flags);
    }
}

static void gen_king_moves(struct position *pos, struct movelist *list,
                           uint64_t mask, int flags)
{
    uint64_t pieces;
    int      sq;

    pieces = pos->bb_pieces[KING+pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        add_moves(list, sq, bb_king_moves(sq)&mask, flags);
    }
}

void gen_moves(struct position *pos, struct movelist *list)
{
    assert(valid_position(pos));
    assert(list != NULL);

    list->size = 0;

    /* If the side to move is in check then generate evasions */
    if (pos_in_check(pos, pos->stm)) {
        gen_check_evasions(pos, list);
        return;
    }

    gen_quiet_moves(pos, list);
    gen_capture_moves(pos, list);
    gen_promotion_moves(pos, list, true);
}

void gen_legal_moves(struct position *pos, struct movelist *list)
{
    struct movelist temp_list;
    int             k;
    int             count;
    uint32_t        move;

    assert(valid_position(pos));
    assert(list != NULL);

    list->size = 0;
    count = 0;
    gen_moves(pos, &temp_list);
    for (k=0;k<temp_list.size;k++) {
        move = temp_list.moves[k];
        if (pos_make_move(pos, move)) {
            list->moves[count++] = move;
            list->size++;
            pos_unmake_move(pos);
        }
    }
}

void gen_check_evasions(struct position *pos, struct movelist *list)
{
    assert(valid_position(pos));
    assert(list != NULL);

    list->size = 0;

    gen_check_evasion_quiet(pos, list);
    gen_check_evasion_tactical(pos, list);
}

void gen_check_evasion_quiet(struct position *pos, struct movelist *list)
{
    int      kingsq;
    int      to;
    uint64_t moves;
    uint64_t attackers;
    uint64_t slide;
    uint64_t blockers;
    uint64_t occ;
    int      attacksq;
    int      attacker;
    int      from;
    int      blocksq;

    /* Find the location of our king */
    kingsq = LSB(pos->bb_pieces[KING+pos->stm]);

    /*
     * First try to move the king. Find all
     * moves to a safe square (excluding captures).
     */
    occ = pos->bb_all&(~pos->bb_pieces[KING+pos->stm]);
    moves = bb_king_moves(kingsq)&(~pos->bb_all);
    while (moves != 0ULL) {
        to = POPBIT(&moves);
        if (bb_attacks_to(pos, occ, to, FLIP_COLOR(pos->stm)) == 0ULL) {
            ADD_MOVE(list, kingsq, to, NO_PIECE,
                     pos->pieces[to] != NO_PIECE?CAPTURE:0);
        }
    }

    /*
     * If there is more than one attacker there is nothing
     * more to try. But if there is only one attacker and
     * the attacker is a slider then also try to block it.
     */
    attackers = bb_attacks_to(pos, pos->bb_all, kingsq, FLIP_COLOR(pos->stm));
    if (BITCOUNT(attackers) > 1) {
        return;
    }
    attacksq = LSB(attackers);
    attacker = pos->pieces[attacksq];

    /*
     * If the attacking piece is a slider then find all squares
     * between the piece and the king.
     */
    occ = pos->bb_all;
    if ((A1H8(attacksq) == A1H8(kingsq)) || (A8H1(attacksq) == A8H1(kingsq))) {
        if ((VALUE(attacker) != BISHOP) && (VALUE(attacker) != QUEEN)) {
            return;
        }
        slide = bb_bishop_moves(occ, attacksq)&bb_bishop_moves(occ, kingsq);
    } else if ((RANKNR(attacksq) == RANKNR(kingsq)) ||
               (FILENR(attacksq) == FILENR(kingsq))) {
        if ((VALUE(attacker) != ROOK) && (VALUE(attacker) != QUEEN)) {
            return;
        }
        slide = bb_rook_moves(occ, attacksq)&bb_rook_moves(occ, kingsq);
    } else {
        return;
    }
    slide &= (~sq_mask[attacksq]);
    slide &= (~sq_mask[kingsq]);

    /* Try to put a piece between the attacker and the king */
    while (slide != 0ULL) {
        blocksq = POPBIT(&slide);

        /* Piece blockers */
        blockers = bb_attacks_to(pos, occ, blocksq, pos->stm);
        blockers &= (~pos->bb_pieces[KING+pos->stm]);
        blockers &= (~pos->bb_pieces[PAWN+pos->stm]);
        while (blockers != 0ULL) {
            from = POPBIT(&blockers);
            ADD_MOVE(list, from, blocksq, NO_PIECE, 0);
        }

        /* Pawn blockers (excluding promotions) */
        if ((RANKNR(blocksq) == RANK_1) || (RANKNR(blocksq) == RANK_8)) {
            continue;
        }
        blockers = bb_pawn_moves_to(occ, blocksq, pos->stm);
        blockers &= pos->bb_pieces[PAWN+pos->stm];
        while (blockers != 0ULL) {
            from = POPBIT(&blockers);
            ADD_MOVE(list, from, blocksq, NO_PIECE, 0);
        }
    }
}

void gen_check_evasion_tactical(struct position *pos, struct movelist *list)
{
    int      kingsq;
    int      to;
    uint64_t moves;
    uint64_t attackers;
    uint64_t slide;
    uint64_t blockers;
    uint64_t occ;
    int      attacksq;
    int      attacker;
    int      from;
    int      piece;
    bool     promotion;

    /* Find the location of our king */
    kingsq = LSB(pos->bb_pieces[KING+pos->stm]);

    /*
     * First try to move the king. Find all
     * captures to a safe square.
     */
    occ = pos->bb_all&(~pos->bb_pieces[KING+pos->stm]);
    moves = bb_king_moves(kingsq)&(pos->bb_sides[FLIP_COLOR(pos->stm)]);
    while (moves != 0ULL) {
        to = POPBIT(&moves);
        if (bb_attacks_to(pos, occ, to, FLIP_COLOR(pos->stm)) == 0ULL) {
            ADD_MOVE(list, kingsq, to, NO_PIECE,
                     pos->pieces[to] != NO_PIECE?CAPTURE:0);
        }
    }

    /*
     * If there is more than one attacker there is nothing
     * more to try. But if there is only one attacker
     * then also try to capture the attacking piece.
     */
    attackers = bb_attacks_to(pos, pos->bb_all, kingsq, FLIP_COLOR(pos->stm));
    if (BITCOUNT(attackers) > 1) {
        return;
    }
    attacksq = LSB(attackers);
    attacker = pos->pieces[attacksq];

    /*
     * Find all captures of the attacking piece. Captures with
     * the king are excluded since they have already been counted
     * above.
     */
    promotion =
        ((sq_mask[attacksq]&(rank_mask[RANK_1]|rank_mask[RANK_8])) != 0ULL);
    moves = bb_attacks_to(pos, pos->bb_all, attacksq, pos->stm)&
                                            (~pos->bb_pieces[KING+pos->stm]);
    while (moves != 0ULL) {
        from = POPBIT(&moves);
        piece = pos->pieces[from];
        if ((VALUE(piece) == PAWN) && promotion) {
            add_promotion_moves(pos, list, from, sq_mask[attacksq],
                                CAPTURE|PROMOTION, true);
        } else {
            ADD_MOVE(list, from, attacksq, NO_PIECE, CAPTURE);
        }
    }

    /*
     * If the attacking piece is a pawn then also have to check
     * if it can be captured en-passant.
     */
    if ((VALUE(attacker) == PAWN) && (pos->stm == WHITE) &&
        (attacksq == (pos->ep_sq-8))) {
        gen_en_passant_moves(pos, list);
    } else if ((VALUE(attacker) == PAWN) && (pos->stm == BLACK) &&
               (attacksq == (pos->ep_sq+8))) {
        gen_en_passant_moves(pos, list);
    }

    /*
     * If the king is not on the first or last rank then
     * there are no more cases to consider.
     */
    if ((RANKNR(attacksq) != RANKNR(kingsq)) ||
        ((pos->stm == WHITE) && (RANKNR(kingsq) != RANK_8)) ||
        ((pos->stm == BLACK) && (RANKNR(kingsq) != RANK_1)) ||
        ((VALUE(attacker) != ROOK) && (VALUE(attacker) != QUEEN))) {
        return;
    }

    /*
     * Try to block by promoting a pawn. Promotions that are also
     * captures are handled earlier.
     */
    occ = pos->bb_all;
    slide = bb_rook_moves(occ, attacksq)&bb_rook_moves(occ, kingsq);
    slide &= (~sq_mask[attacksq]);
    slide &= (~sq_mask[kingsq]);
    if (pos->stm == WHITE) {
        slide &= rank_mask[RANK_8];
        blockers = (slide >> 8)&pos->bb_pieces[WHITE_PAWN];
    } else {
        slide &= rank_mask[RANK_1];
        blockers = (slide << 8)&pos->bb_pieces[BLACK_PAWN];
    }
    while (blockers != 0ULL) {
        from = POPBIT(&blockers);
        add_promotion_moves(pos, list, from,
                            pos->stm==WHITE?sq_mask[from+8]:sq_mask[from-8],
                            PROMOTION, true);
    }
}

void gen_quiet_moves(struct position *pos, struct movelist *list)
{
    uint64_t mask;

    assert(valid_position(pos));
    assert(list != NULL);

    /* Setup masks for which moves to include */
    mask = ~pos->bb_all;

    /* Generate standard moves */
    gen_knight_moves(pos, list, mask, 0);
    gen_diagonal_slider_moves(pos, list, mask, 0);
    gen_straight_slider_moves(pos, list, mask, 0);
    gen_king_moves(pos, list, mask, 0);
    gen_pawn_moves(pos, list, ~pos->bb_all);

    /* Generate castling moves */
    gen_kingside_castling_moves(pos, list);
    gen_queenside_castling_moves(pos, list);
}

void gen_capture_moves(struct position *pos, struct movelist *list)
{
    uint64_t opp_mask;

    assert(valid_position(pos));
    assert(list != NULL);

    /* Setup masks for which moves to include */
    opp_mask = pos->bb_sides[FLIP_COLOR(pos->stm)];

    /* Generate piece captures */
    gen_knight_moves(pos, list, opp_mask, CAPTURE);
    gen_diagonal_slider_moves(pos, list, opp_mask, CAPTURE);
    gen_straight_slider_moves(pos, list, opp_mask, CAPTURE);
    gen_king_moves(pos, list, opp_mask, CAPTURE);

    /* Generate pawn captures */
    gen_pawn_captures(pos, list, opp_mask);
    gen_capture_promotions(pos, list, true, opp_mask);

    /* Generate en-passant captures */
    gen_en_passant_moves(pos, list);
}

void gen_promotion_moves(struct position *pos, struct movelist *list,
                         bool underpromote)
{
    assert(valid_position(pos));
    assert(list != NULL);

    gen_promotions(pos, list, underpromote, ~pos->bb_all);
}
