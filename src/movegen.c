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

static void gen_en_passant_moves(struct gamestate *pos, struct movelist *list)
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

static void gen_kingside_castling_moves(struct gamestate *pos,
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

static void gen_queenside_castling_moves(struct gamestate *pos,
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

static void add_pawn_moves(struct gamestate *pos, struct movelist *list,
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

static void add_moves(struct gamestate *pos, struct movelist *list, int from,
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

static void gen_captures(struct gamestate *pos, struct movelist *list)
{
    int      sq;
    int      piece;
    uint64_t moves;
    uint64_t pieces;

    assert(valid_board(pos));
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

static void gen_promotions(struct gamestate *pos, struct movelist *list,
                           bool capture, bool underpromote)
{
    uint64_t pieces;
    uint64_t moves;
    int      from;
    uint8_t  promotion;

    assert(valid_board(pos));
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

/*
 * Catures are assumed to be handled elsewhere so capturing
 * moves that gives check are excluded. The same goes for
 * queen promotions.
 */
static void gen_direct_checks(struct gamestate *pos, struct movelist *list)
{
    int      kingsq;
    int      from;
    int      to;
    uint64_t attacks;
    uint64_t moves;

    /* Find the opponent king */
    kingsq = LSB(pos->bb_pieces[KING+FLIP_COLOR(pos->stm)]);

    /*
     * Find all squares from which pawns can attack the king. Then
     * for each of those squares find the pawns that can move there.
     */
    attacks = bb_pawn_attacks_to(kingsq, pos->stm);
    attacks &= (~pos->bb_all);
    while (attacks != 0ULL) {
        to = POPBIT(&attacks);
        moves = bb_pawn_moves_to(pos->bb_all, to, pos->stm);
        moves &= pos->bb_pieces[PAWN+pos->stm];

        while (moves != 0ULL) {
            from = POPBIT(&moves);
            add_pawn_moves(pos, list, from, sq_mask[to], PROMOTE_NONE);
        }
    }

    /*
     * Find all squares from which knights can attack the king. Then
     * for each of those squares find the knights that can move there.
     */
    attacks = bb_knight_moves(kingsq);
    attacks &= (~pos->bb_all);
    while (attacks != 0ULL) {
        to = POPBIT(&attacks);
        moves = bb_knight_moves(to);
        moves &= pos->bb_pieces[KNIGHT+pos->stm];

        while (moves != 0ULL) {
            from = POPBIT(&moves);
            add_moves(pos, list, from, sq_mask[to]);
        }
    }

    /*
     * Find all squares from which bishops can attack the king. Then
     * for each of those squares find the bishops and queens that can
     * move there.
     */
    attacks = bb_bishop_moves(pos->bb_all, kingsq);
    attacks &= (~pos->bb_all);
    while (attacks != 0ULL) {
        to = POPBIT(&attacks);
        moves = bb_bishop_moves(pos->bb_all, to)&
        pos->bb_pieces[BISHOP+pos->stm];
        moves |= (bb_queen_moves(pos->bb_all, to)&
                  pos->bb_pieces[QUEEN+pos->stm]);

        while (moves != 0ULL) {
            from = POPBIT(&moves);
            add_moves(pos, list, from, sq_mask[to]);
        }
    }

    /*
     * Find all squares from which rooks can attack the king. Then
     * for each of those squares find the rooks and queens that can
     * move there.
     */
    attacks = bb_rook_moves(pos->bb_all, kingsq);
    attacks &= (~pos->bb_all);
    while (attacks != 0ULL) {
        to = POPBIT(&attacks);
        moves = bb_rook_moves(pos->bb_all, to)&pos->bb_pieces[ROOK+pos->stm];
        moves |= (bb_queen_moves(pos->bb_all, to)&
                  pos->bb_pieces[QUEEN+pos->stm]);

        while (moves != 0ULL) {
            from = POPBIT(&moves);
            add_moves(pos, list, from, sq_mask[to]);
        }
    }

    /*
     * Queen promotions are handled elsewhere and should therefore
     * not be included. Since all squares that can be attacked by
     * bishops and rooks also can be attacked by queens we can also
     * exclude. those promotions. So what remains is promotions to
     * knight.
     *
     * Find all squares on the back rank from which knights can attack
     * the king. Then for each of those squares find the pawns that can
     * move there.
     */
    attacks = bb_knight_moves(kingsq);
    attacks &= (~pos->bb_all);
    attacks &= rank_mask[pos->stm == WHITE?RANK_8:RANK_1];
    while (attacks != 0ULL) {
        to = POPBIT(&attacks);
        moves = bb_pawn_moves_to(pos->bb_all, to, pos->stm);
        moves &= pos->bb_pieces[PAWN+pos->stm];

        while (moves != 0ULL) {
            from = POPBIT(&moves);
            add_pawn_moves(pos, list, from, sq_mask[to], PROMOTE_KNIGHT);
        }
    }

    /*
     * If the rook end square after a castling is one of the squares
     * from which a rook can attack the king then also consider
     * castling moves.
     */
    attacks = bb_rook_moves(pos->bb_all, kingsq);
    if (attacks&sq_mask[pos->stm == WHITE?F1:F8]) {
        gen_kingside_castling_moves(pos, list);
    }
    if (attacks&sq_mask[pos->stm == WHITE?D1:D8]) {
        gen_queenside_castling_moves(pos, list);
    }
}

/*
 * Catures are assumed to be handled elsewhere so capturing
 * moves that finds a discovered check are excluded. The same
 * goes for queen promotions.
 */
static void gen_discovered_checks(struct gamestate *pos, struct movelist *list)
{
    uint64_t pieces;
    uint64_t ray;
    uint64_t slide;
    uint64_t moves1;
    uint64_t moves2;
    uint64_t checkers;
    uint64_t occ;
    int      piece;
    int      from;
    int      kingsq;
    int      rdelta;
    int      fdelta;

    /* Find the opponent king */
    kingsq = LSB(pos->bb_pieces[KING+FLIP_COLOR(pos->stm)]);

    /*
     * Only sliding pieces can generate a discovered check
     * so loop over all bishops, rooks and queens.
     */
    pieces = pos->bb_pieces[QUEEN+pos->stm]|pos->bb_pieces[ROOK+pos->stm]|
                                                pos->bb_pieces[BISHOP+pos->stm];
    while (pieces != 0ULL) {
        from = POPBIT(&pieces);
        piece = pos->pieces[from];

        /*
         * Check if the sliding piece is located on the same ray as
         * the enemy king. If it is then generate all sliding moves
         * for that piece that is in the direction of the king.
         */
        slide = 0ULL;
        ray = 0ULL;
        rdelta = 0;
        fdelta = 0;
        switch (piece) {
        case WHITE_BISHOP:
        case BLACK_BISHOP:
            if (A1H8(from) == A1H8(kingsq)) {
                ray = A1H8(from);
            } else if (A8H1(from) == A8H1(kingsq)) {
                ray = A8H1(from);
            }
            rdelta = (RANKNR(from) > RANKNR(kingsq))?-1:1;
            fdelta = (FILENR(from) > FILENR(kingsq))?-1:1;
            break;
        case WHITE_ROOK:
        case BLACK_ROOK:
            if (RANKNR(from) == RANKNR(kingsq)) {
                ray = rank_mask[RANKNR(from)];
                rdelta = 0;
                fdelta = (FILENR(from) > FILENR(kingsq))?-1:1;
            } else if (FILENR(from) == FILENR(kingsq)) {
                ray = file_mask[FILENR(from)];
                rdelta = (RANKNR(from) > RANKNR(kingsq))?-1:1;
                fdelta = 0;
            }
            break;
        case WHITE_QUEEN:
        case BLACK_QUEEN:
            if (A1H8(from) == A1H8(kingsq)) {
                ray = A1H8(from);
                rdelta = (RANKNR(from) > RANKNR(kingsq))?-1:1;
                fdelta = (FILENR(from) > FILENR(kingsq))?-1:1;
            } else if (A8H1(from) == A8H1(kingsq)) {
                ray = A8H1(from);
                rdelta = (RANKNR(from) > RANKNR(kingsq))?-1:1;
                fdelta = (FILENR(from) > FILENR(kingsq))?-1:1;
            } else if (RANKNR(from) == RANKNR(kingsq)) {
                ray = rank_mask[RANKNR(from)];
                rdelta = 0;
                fdelta = (FILENR(from) > FILENR(kingsq))?-1:1;
            } else if (FILENR(from) == FILENR(kingsq)) {
                ray = file_mask[FILENR(from)];
                rdelta = (RANKNR(from) > RANKNR(kingsq))?-1:1;
                fdelta = 0;
            }
            break;
        default:
            assert(false);
            continue;
        }
        if ((fdelta == 0) && (rdelta == 0)) {
            continue;
        }
        slide = bb_slider_moves(pos->bb_all, from, fdelta, rdelta);

        /*
         * Limit the slider moves to squares that are occupied by a
         * friendly pieces. There should be at most one such square.
         * The piece on that square is candidate to be moved in order
         * to find a discovered check.
         */
        slide &= pos->bb_sides[pos->stm];
        assert(BITCOUNT(slide) <= 1);
        if (slide == 0ULL) {
            continue;
        }

        /*
         * If a friendly piece is found that is a candidate to
         * be moved then remove that piece and check if the
         * king can be attacked. If the king can be attacked
         * then generate all moves for the piece that is not
         * along the relevant ray.
         */
        occ = pos->bb_all&(~slide);
        moves1 = bb_slider_moves(occ, from, fdelta, rdelta);
        if (moves1&sq_mask[kingsq]) {
            from = LSB(slide);
            piece = pos->pieces[from];
            moves2 = bb_moves_for_piece(pos->bb_all, from, piece)&(~ray);
            moves2 &= (~pos->bb_all);
            if (VALUE(piece) == PAWN) {
                /*
                 * Since direct checks are handled separately exclude
                 * all checking moves. The only case where this
                 * can happen for pawns is with promotion. Queen
                 * promotions are handled elsewhere so they don't need
                 * to be considered. Additionally don't consider
                 * promotion to rook and bishop since they are pointless.
                 * That leaves promotion to knight.
                 */
                if (moves2&(rank_mask[RANK_1]|rank_mask[RANK_8])) {
                    checkers = bb_moves_for_piece(pos->bb_all, kingsq,
                                                  KNIGHT+pos->stm);
                    moves2 &= (~checkers);
                }
                add_pawn_moves(pos, list, from, moves2, PROMOTE_KNIGHT);
            } else {
                /*
                 * Since direct checks are handled separately we need
                 * to exclude checking moves.
                 */
                checkers = bb_moves_for_piece(pos->bb_all, kingsq, piece);
                moves2 &= (~checkers);
                add_moves(pos, list, from, moves2);
            }
        }
    }
}

static void gen_check_evasions(struct gamestate *pos, struct movelist *list)
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

static void gen_checks(struct gamestate *pos, struct movelist *list)
{
    gen_direct_checks(pos, list);
    gen_discovered_checks(pos, list);
}

void gen_moves(struct gamestate *pos, struct movelist *list)
{
    int      sq;
    int      piece;
    uint64_t moves;
    uint64_t pieces;

    assert(valid_board(pos));
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

void gen_legal_moves(struct gamestate *pos, struct movelist *list)
{
    struct movelist temp_list;
    int             k;
    int             count;
    uint32_t        move;

    assert(valid_board(pos));
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

void gen_quiscence_moves(struct gamestate *pos, struct movelist *list,
                         bool checks)
{
    assert(valid_board(pos));
    assert(list != NULL);

    list->nmoves = 0;

    /* If the side to move is in check then generate evasions */
    if (board_in_check(pos, pos->stm)) {
        gen_check_evasions(pos, list);
        return;
    }

    gen_captures(pos, list);
    gen_promotions(pos, list, false, false);
    if (checks) {
        gen_checks(pos, list);
    }

    assert(valid_gen_quiscenece_moves(pos, checks, list));
}

void gen_normal_moves(struct gamestate *pos, struct movelist *list)
{
    int      sq;
    int      piece;
    uint64_t moves;
    uint64_t pieces;

    assert(valid_board(pos));
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

void gen_capture_moves(struct gamestate *pos, struct movelist *list)
{
    assert(valid_board(pos));
    assert(list != NULL);

    gen_captures(pos, list);
}

void gen_promotion_moves(struct gamestate *pos, struct movelist *list)
{
    assert(valid_board(pos));
    assert(list != NULL);

    gen_promotions(pos, list, false, true);
}
