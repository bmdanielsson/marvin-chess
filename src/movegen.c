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
#include "board.h"
#include "bitboard.h"
#include "validation.h"
#include "debug.h"

#define PROMOTE_QUEEN   0x01
#define PROMOTE_ROOK    0x02
#define PROMOTE_BISHOP  0x04
#define PROMOTE_KNIGHT  0x08
#define PROMOTE_UNDER   PROMOTE_ROOK|PROMOTE_BISHOP|PROMOTE_KNIGHT
#define PROMOTE_ALL     PROMOTE_UNDER|PROMOTE_QUEEN
#define PROMOTE_NONE    0x00

static void add_move(struct movelist *list, int from, int to, int promotion,
                     int flags)
{
    assert(list->nmoves < MAX_MOVES);

    list->moves[list->nmoves] = MOVE(from, to, promotion, flags);
    list->nmoves++;
}

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
        add_move(list, from, pos->ep_sq, NO_PIECE, EN_PASSANT);
    }
}

static void gen_kingside_castling_moves(struct position *pos,
                                        struct movelist *list)
{
    /*
     * There is no need to check if the kings destination square
     * is attacked or not since pseudo-legal moves are generated.
     * If the square is attacked then the king moves in to check and
     * the move will be rejected later.
     *
     * Also there is no need to check that there is a king and a rook in the
     * correct squares because if there aren't then the corresponding
     * castling permission bit will not be set.
     */

    if (pos->stm == WHITE) {
        if ((pos->castle&WHITE_KINGSIDE) &&
            (pos->pieces[F1] == NO_PIECE) &&
            (pos->pieces[G1] == NO_PIECE) &&
            (!bb_is_attacked(pos, E1, BLACK)) &&
            (!bb_is_attacked(pos, F1, BLACK))) {
            add_move(list, E1, G1, NO_PIECE, KINGSIDE_CASTLE);
        }
    } else {
        if ((pos->castle&BLACK_KINGSIDE) &&
            (pos->pieces[F8] == NO_PIECE) &&
            (pos->pieces[G8] == NO_PIECE) &&
            (!bb_is_attacked(pos, E8, WHITE)) &&
            (!bb_is_attacked(pos, F8, WHITE))) {
            add_move(list, E8, G8, NO_PIECE, KINGSIDE_CASTLE);
        }
    }
}

static void gen_queenside_castling_moves(struct position *pos,
                                         struct movelist *list)
{
    /*
     * There is no need to check if the kings destination square
     * is attacked or not since pseudo-legal moves are generated.
     * If the square is attacked then the king moves in to check and
     * the move will be rejected later.
     *
     * Also there is no need to check that there is a king and a rook in the
     * correct squares because if there aren't then the corresponding
     * castling permission bit will not be set.
     */

    if (pos->stm == WHITE) {
        if ((pos->castle&WHITE_QUEENSIDE) &&
            (pos->pieces[B1] == NO_PIECE) &&
            (pos->pieces[C1] == NO_PIECE) &&
            (pos->pieces[D1] == NO_PIECE) &&
            (!bb_is_attacked(pos, D1, BLACK)) &&
            (!bb_is_attacked(pos, E1, BLACK))) {
            add_move(list, E1, C1, NO_PIECE, QUEENSIDE_CASTLE);
        }
    } else {
        if ((pos->castle&BLACK_QUEENSIDE) &&
            (pos->pieces[B8] == NO_PIECE) &&
            (pos->pieces[C8] == NO_PIECE) &&
            (pos->pieces[D8] == NO_PIECE) &&
            (!bb_is_attacked(pos, D8, WHITE)) &&
            (!bb_is_attacked(pos, E8, WHITE))) {
            add_move(list, E8, C8, NO_PIECE, QUEENSIDE_CASTLE);
        }
    }
}

static void add_pawn_moves(struct position *pos, struct movelist *list,
                           int from, uint64_t moves, uint8_t promotion)
{
    uint64_t opp;
    int      flags;
    int      to;

    opp = pos->bb_sides[FLIP_COLOR(pos->stm)];

    while (moves != 0ULL) {
        flags = 0;
        to = POPBIT(&moves);
        if (sq_mask[to]&opp) {
            flags |= CAPTURE;
        }
        if (sq_mask[to]&(rank_mask[RANK_1]|rank_mask[RANK_8])) {
            flags |= PROMOTION;
            if (promotion&PROMOTE_QUEEN) {
                add_move(list, from, to, QUEEN+pos->stm, flags);
            }
            if (promotion&PROMOTE_ROOK) {
                add_move(list, from, to, ROOK+pos->stm, flags);
            }
            if (promotion&PROMOTE_BISHOP) {
                add_move(list, from, to, BISHOP+pos->stm, flags);
            }
            if (promotion&PROMOTE_KNIGHT) {
                add_move(list, from, to, KNIGHT+pos->stm, flags);
            }
        } else {
            add_move(list, from, to, NO_PIECE, flags);
        }
    }
}

static void add_moves(struct position *pos, struct movelist *list, int from,
                      uint64_t moves)
{
    uint64_t opp;
    int      flags;
    int      to;

    opp = pos->bb_sides[FLIP_COLOR(pos->stm)];

    while (moves != 0ULL) {
        flags = 0;
        to = POPBIT(&moves);
        if (sq_mask[to]&opp) {
            flags |= CAPTURE;
        }
        add_move(list, from, to, NO_PIECE, flags);
    }
}

void gen_moves(struct position *pos, struct movelist *list)
{
    int      sq;
    int      piece;
    uint64_t moves;
    uint64_t pieces;

    assert(valid_position(pos));
    assert(list != NULL);

    list->nmoves = 0;

    /* If the side to move is in check then generate evasions */
    if (board_in_check(pos, pos->stm)) {
        gen_check_evasions(pos, list);
        return;
    }

    pieces = pos->bb_sides[pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        piece = pos->pieces[sq];

        moves = 0ULL;
        switch (piece) {
        case WHITE_PAWN:
        case BLACK_PAWN:
            moves |= bb_pawn_attacks_from(sq, pos->stm);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            moves |= bb_pawn_moves(pos->bb_all, sq, pos->stm);
            add_pawn_moves(pos, list, sq, moves, PROMOTE_ALL);
            break;
        case WHITE_KNIGHT:
        case BLACK_KNIGHT:
            moves |= bb_knight_moves(sq);
            moves &= (~pos->bb_sides[pos->stm]);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_BISHOP:
        case BLACK_BISHOP:
            moves |= bb_bishop_moves(pos->bb_all, sq);
            moves &= (~pos->bb_sides[pos->stm]);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_ROOK:
        case BLACK_ROOK:
            moves |= bb_rook_moves(pos->bb_all, sq);
            moves &= (~pos->bb_sides[pos->stm]);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_QUEEN:
        case BLACK_QUEEN:
            moves |= bb_queen_moves(pos->bb_all, sq);
            moves &= (~pos->bb_sides[pos->stm]);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_KING:
        case BLACK_KING:
            moves |= bb_king_moves(sq);
            moves &= (~pos->bb_sides[pos->stm]);
            add_moves(pos, list, sq, moves);
            break;
        case NO_PIECE:
            break;
        default:
            assert(false);
            break;
        }
    }

    gen_en_passant_moves(pos, list);
    gen_kingside_castling_moves(pos, list);
    gen_queenside_castling_moves(pos, list);
}

void gen_legal_moves(struct position *pos, struct movelist *list)
{
    struct movelist temp_list;
    int             k;
    int             count;
    uint32_t        move;

    assert(valid_position(pos));
    assert(list != NULL);

    list->nmoves = 0;
    count = 0;
    gen_moves(pos, &temp_list);
    for (k=0;k<temp_list.nmoves;k++) {
        move = temp_list.moves[k];
        if (board_make_move(pos, move)) {
            list->moves[count++] = move;
            list->nmoves++;
            board_unmake_move(pos);
        }
    }
}

void gen_quiet_moves(struct position *pos, struct movelist *list)
{
    int      sq;
    int      piece;
    uint64_t moves;
    uint64_t pieces;

    assert(valid_position(pos));
    assert(list != NULL);

    pieces = pos->bb_sides[pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        piece = pos->pieces[sq];

        moves = 0ULL;
        switch (piece) {
        case WHITE_PAWN:
        case BLACK_PAWN:
            moves |= bb_pawn_moves(pos->bb_all, sq, pos->stm);
            add_pawn_moves(pos, list, sq, moves, PROMOTE_NONE);
            break;
        case WHITE_KNIGHT:
        case BLACK_KNIGHT:
            moves |= bb_knight_moves(sq);
            moves &= (~pos->bb_all);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_BISHOP:
        case BLACK_BISHOP:
            moves |= bb_bishop_moves(pos->bb_all, sq);
            moves &= (~pos->bb_all);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_ROOK:
        case BLACK_ROOK:
            moves |= bb_rook_moves(pos->bb_all, sq);
            moves &= (~pos->bb_all);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_QUEEN:
        case BLACK_QUEEN:
            moves |= bb_queen_moves(pos->bb_all, sq);
            moves &= (~pos->bb_all);
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_KING:
        case BLACK_KING:
            moves |= bb_king_moves(sq);
            moves &= (~pos->bb_all);
            add_moves(pos, list, sq, moves);
            break;
        case NO_PIECE:
            break;
        default:
            assert(false);
            break;
        }
    }

    gen_kingside_castling_moves(pos, list);
    gen_queenside_castling_moves(pos, list);
}

void gen_capture_moves(struct position *pos, struct movelist *list)
{
    int      sq;
    int      piece;
    uint64_t moves;
    uint64_t pieces;

    assert(valid_position(pos));
    assert(list != NULL);

    pieces = pos->bb_sides[pos->stm];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        piece = pos->pieces[sq];

        moves = 0ULL;
        switch (piece) {
        case WHITE_PAWN:
        case BLACK_PAWN:
            moves |= bb_pawn_attacks_from(sq, pos->stm);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            add_pawn_moves(pos, list, sq, moves, PROMOTE_ALL);
            break;
        case WHITE_KNIGHT:
        case BLACK_KNIGHT:
            moves |= bb_knight_moves(sq);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_BISHOP:
        case BLACK_BISHOP:
            moves |= bb_bishop_moves(pos->bb_all, sq);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_ROOK:
        case BLACK_ROOK:
            moves |= bb_rook_moves(pos->bb_all, sq);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_QUEEN:
        case BLACK_QUEEN:
            moves |= bb_queen_moves(pos->bb_all, sq);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            add_moves(pos, list, sq, moves);
            break;
        case WHITE_KING:
        case BLACK_KING:
            moves |= bb_king_moves(sq);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
            add_moves(pos, list, sq, moves);
            break;
        case NO_PIECE:
            break;
        default:
            assert(false);
            break;
        }
    }

    gen_en_passant_moves(pos, list);
}

void gen_promotion_moves(struct position *pos, struct movelist *list,
                         bool capture, bool underpromote)
{
    uint64_t pieces;
    uint64_t moves;
    int      from;
    uint8_t  promotion;

    assert(valid_position(pos));
    assert(list != NULL);

    promotion = PROMOTE_QUEEN;
    if (underpromote) {
        promotion |= PROMOTE_UNDER;
    }

    pieces = pos->bb_pieces[PAWN+pos->stm];
    while (pieces != 0ULL) {
        from = POPBIT(&pieces);
        moves = 0ULL;
        if (capture) {
            moves |= bb_pawn_attacks_from(from, pos->stm);
            moves &= pos->bb_sides[FLIP_COLOR(pos->stm)];
        }
        moves |= bb_pawn_moves(pos->bb_all, from, pos->stm);

        /* Mask off moves where the destination is not on the 1st or 8th rank */
        moves &= (rank_mask[RANK_1]|rank_mask[RANK_8]);

        add_pawn_moves(pos, list, from, moves, promotion);
    }
}

void gen_check_evasions(struct position *pos, struct movelist *list)
{
    int      kingsq;
    int      to;
    uint64_t moves;
    uint64_t attackers;
    uint64_t slide;
    uint64_t temp;
    uint64_t blockers;
    uint64_t occ;
    int      attacksq;
    int      attacker;
    int      from;
    int      piece;
    int      rdelta;
    int      fdelta;
    int      blocksq;

    assert(valid_position(pos));
    assert(list != NULL);

    list->nmoves = 0;

    /* Find the location of our king */
    kingsq = LSB(pos->bb_pieces[KING+pos->stm]);

    /*
     * First try to move the king. Find all
     * moves to a safe square.
     */
    moves = bb_king_moves(kingsq)&(~pos->bb_sides[pos->stm]);
    while (moves != 0ULL) {
        to = POPBIT(&moves);
        occ = pos->bb_all&(~pos->bb_pieces[KING+pos->stm]);
        if (bb_attacks_to(pos, occ, to, FLIP_COLOR(pos->stm)) == 0ULL) {
            add_moves(pos, list, kingsq, sq_mask[to]);
        }
    }

    /*
     * If there is more than one attacker there is nothing
     * more to try. But if there is only one attacker
     * then also try to capture the attacking piece. If
     * the attacker is a slider then also try to block it.
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
    moves = bb_attacks_to(pos, pos->bb_all, attacksq, pos->stm)&
                                            (~pos->bb_pieces[KING+pos->stm]);
    while (moves != 0ULL) {
        from = POPBIT(&moves);
        piece = pos->pieces[from];
        if (VALUE(piece) == PAWN) {
            add_pawn_moves(pos, list, from, sq_mask[attacksq], PROMOTE_ALL);
        } else {
            add_moves(pos, list, from, sq_mask[attacksq]);
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
     * If the attacking piece is a slider then
     * also try to block it.
     */
    fdelta = 0;
    rdelta = 0;
    switch (attacker) {
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        rdelta = (RANKNR(attacksq) > RANKNR(kingsq))?-1:1;
        fdelta = (FILENR(attacksq) > FILENR(kingsq))?-1:1;
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        if (RANKNR(attacksq) == RANKNR(kingsq)) {
            rdelta = 0;
            fdelta = (FILENR(attacksq) > FILENR(kingsq))?-1:1;
        } else if (FILENR(attacksq) == FILENR(kingsq)) {
            rdelta = (RANKNR(attacksq) > RANKNR(kingsq))?-1:1;
            fdelta = 0;
        }
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        if (A1H8(attacksq) == A1H8(kingsq)) {
            rdelta = (RANKNR(attacksq) > RANKNR(kingsq))?-1:1;
            fdelta = (FILENR(attacksq) > FILENR(kingsq))?-1:1;
        } else if (A8H1(attacksq) == A8H1(kingsq)) {
            rdelta = (RANKNR(attacksq) > RANKNR(kingsq))?-1:1;
            fdelta = (FILENR(attacksq) > FILENR(kingsq))?-1:1;
        } else if (RANKNR(attacksq) == RANKNR(kingsq)) {
            rdelta = 0;
            fdelta = (FILENR(attacksq) > FILENR(kingsq))?-1:1;
        } else if (FILENR(attacksq) == FILENR(kingsq)) {
            rdelta = (RANKNR(attacksq) > RANKNR(kingsq))?-1:1;
            fdelta = 0;
        }
        break;
    default:
        return;
    }
    if ((fdelta == 0) && (rdelta == 0)) {
        return;
    }
    slide = bb_slider_moves(pos->bb_all, attacksq, fdelta, rdelta)&
                                                            (~sq_mask[kingsq]);

    /* Try to put a piece between the attacker and the king */
    temp = slide;
    while (slide != 0ULL) {
        blocksq = POPBIT(&slide);

        blockers = bb_attacks_to(pos, pos->bb_all, blocksq, pos->stm);
        blockers &= (~pos->bb_pieces[KING+pos->stm]);
        blockers &= (~pos->bb_pieces[PAWN+pos->stm]);
        while (blockers != 0ULL) {
            from = POPBIT(&blockers);
            add_moves(pos, list, from, sq_mask[blocksq]);
        }
        blockers = bb_pawn_moves_to(pos->bb_all, blocksq, pos->stm)&
                                                pos->bb_pieces[PAWN+pos->stm];
        while (blockers != 0ULL) {
            from = POPBIT(&blockers);
            add_pawn_moves(pos, list, from, sq_mask[blocksq], PROMOTE_ALL);
        }
    }
    slide = temp;

    /*
     * Finally see if the check can be blocked
     * by doing an en-passant capture.
     */
    if ((pos->ep_sq != NO_SQUARE) && ISBITSET(slide, pos->ep_sq)) {
        gen_en_passant_moves(pos, list);
    }
}
