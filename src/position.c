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
#include <stdlib.h>
#include <string.h>

#include "position.h"
#include "data.h"
#include "fen.h"
#include "key.h"
#include "bitboard.h"
#include "validation.h"
#include "debug.h"
#include "hash.h"
#include "search.h"
#include "movegen.h"
#include "engine.h"
#include "nnue.h"

static void update_material(struct position *pos, int piece, bool added)
{
    int delta;

    delta = added?1:-1;
    if (COLOR(piece) == BLACK) {
        delta *= -1;
    }

    pos->material += delta*material_values[piece];
}

static void update_castling_availability(struct position *pos, int from, int to)
{
    /* If the king moves castling becomes unavailable for both directions */
    if (pos->pieces[from] == (pos->stm+KING)) {
        if (pos->stm == WHITE) {
            pos->castle &= ~WHITE_KINGSIDE;
            pos->castle &= ~WHITE_QUEENSIDE;
        } else {
            pos->castle &= ~BLACK_KINGSIDE;
            pos->castle &= ~BLACK_QUEENSIDE;
        }
    }

    /* If a rook moves castling becomes unavailable for that direction */
    if ((pos->stm == WHITE) && (pos->pieces[from] == WHITE_ROOK)) {
        if (from == pos->castle_wk) {
            pos->castle &= ~WHITE_KINGSIDE;
        } else if (from == pos->castle_wq) {
            pos->castle &= ~WHITE_QUEENSIDE;
        }
    } else if ((pos->stm == BLACK) && (pos->pieces[from] == BLACK_ROOK)) {
        if (from == pos->castle_bk) {
            pos->castle &= ~BLACK_KINGSIDE;
        } else if (from == pos->castle_bq) {
            pos->castle &= ~BLACK_QUEENSIDE;
        }
    }

    /*
     * If an opponent rook is captured castling becomes
     * unavailable for that direction.
     */
    if (pos->stm == WHITE) {
        if (to == pos->castle_bk) {
            pos->castle &= ~BLACK_KINGSIDE;
        } else if (to == pos->castle_bq) {
            pos->castle &= ~BLACK_QUEENSIDE;
        }
    } else if (pos->stm == BLACK) {
        if (to == pos->castle_wk) {
            pos->castle &= ~WHITE_KINGSIDE;
        } else if (to == pos->castle_wq) {
            pos->castle &= ~WHITE_QUEENSIDE;
        }
    }
}

static void add_piece(struct position *pos, int piece, int square)
{
    SETBIT(pos->bb_pieces[piece], square);
    SETBIT(pos->bb_sides[COLOR(piece)], square);
    SETBIT(pos->bb_all, square);
    pos->pieces[square] = piece;
}

static void remove_piece(struct position *pos, int piece, int square)
{
    CLEARBIT(pos->bb_pieces[piece], square);
    CLEARBIT(pos->bb_sides[COLOR(piece)], square);
    CLEARBIT(pos->bb_all, square);
    pos->pieces[square] = NO_PIECE;
}

static struct unmake* push_history(struct position *pos)
{
    struct unmake *elem;

    assert(pos->ply < MAX_HISTORY_SIZE);

    elem = &pos->history[pos->ply];
    pos->ply++;
    pos->height++;

    return elem;
}

static struct unmake* pop_history(struct position *pos)
{
    assert(pos->ply > 0);

    pos->ply--;
    if (pos->height > 0) {
        pos->height--;
    }

    return &pos->history[pos->ply];
}

void pos_setup_start_position(struct position *pos)
{
    assert(pos != NULL);

    (void)pos_setup_from_fen(pos, FEN_STARTPOS);
    assert(valid_position(pos));
}

void pos_reset(struct position *pos)
{
    int k;

    assert(pos != NULL);

    for (k=0;k<NSQUARES;k++) {
        pos->pieces[k] = NO_PIECE;
    }
    for (k=0;k<NPIECES;k++) {
        pos->bb_pieces[k] = 0ULL;
    }
    for (k=0;k<NSIDES;k++) {
        pos->bb_sides[k] = 0ULL;
    }
    pos->bb_all = 0ULL;

    pos->key = 0ULL;

    pos->ep_sq = NO_SQUARE;
    pos->castle = 0;
    pos->stm = NO_SIDE;
    pos->ply = 0;
    pos->height = 0;
    pos->fifty = 0;

    nnue_reset_accumulator(pos);
}

bool pos_setup_from_fen(struct position *pos, char *fenstr)
{
    uint64_t pieces;
    int      sq;
    int      piece;

    assert(pos != NULL);
    assert(fenstr != NULL);

    pos_reset(pos);
    if (!fen_setup_board(pos, fenstr) || !valid_position(pos)) {
        return false;
    }

    pos->material = 0;
    pieces = pos->bb_all;
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        piece = pos->pieces[sq];
        update_material(pos, piece, true);
    }


    return true;
}

void pos_move2str(uint32_t move, char *str)
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

uint32_t pos_str2move(char *str, struct position *pos)
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

bool pos_in_check(struct position *pos, int side)
{
    assert(valid_position(pos));
    assert(valid_side(side));

    return bb_is_attacked(pos, LSB(pos->bb_pieces[KING+side]),
                          FLIP_COLOR(side));
}

bool pos_make_move(struct position *pos, uint32_t move)
{
    struct unmake *elem;
    int           capture;
    int           piece;
    int           from;
    int           to;
    int           promotion;
    int           ep;

    assert(valid_position(pos));
    assert(valid_move(move));
    assert(pos_is_move_pseudo_legal(pos, move));
    assert(pos->ply < MAX_MOVES);

    from = FROM(move);
    to = TO_CASTLE(move);
    promotion = PROMOTION(move);

    /* Find the pieces involved in the move */
    capture = pos->pieces[to];
    piece = pos->pieces[from];

    /* Update the history */
    elem = push_history(pos);
    elem->move = move;
    elem->piece = piece;
    elem->capture = capture;
    elem->castle = pos->castle;
    elem->ep_sq = pos->ep_sq;
    elem->fifty = pos->fifty;
    elem->key = pos->key;

    /* Update NNUE */
    nnue_make_move(pos, move);

    /* Check if the move enables an en passant capture */
    if ((VALUE(piece) == PAWN) && (abs(to-from) == 16)) {
        pos->ep_sq = (pos->stm == WHITE)?to-8:to+8;
    } else {
        pos->ep_sq = NO_SQUARE;
    }
    pos->key = key_update_ep_square(pos->key, elem->ep_sq, pos->ep_sq);

    /* Update castling availability */
    update_castling_availability(pos, from, to);
    pos->key = key_update_castling(pos->key, elem->castle, pos->castle);

    /* Remove piece from current position */
    remove_piece(pos, piece, from);
    pos->key = key_update_piece(pos->key, piece, from);

    /* If necessary remove captured piece */
    if (ISCAPTURE(move)) {
        remove_piece(pos, capture, to);
        pos->key = key_update_piece(pos->key, capture, to);
        update_material(pos, capture, false);
    } else if (ISENPASSANT(move)) {
        ep = (pos->stm == WHITE)?to-8:to+8;
        remove_piece(pos, PAWN+FLIP_COLOR(pos->stm), ep);
        pos->key = key_update_piece(pos->key, PAWN+FLIP_COLOR(pos->stm), ep);
        update_material(pos, PAWN+FLIP_COLOR(pos->stm), false);
    }

    /* If this is a castling we have to remove the rook as well */
    if (ISKINGSIDECASTLE(move) || ISQUEENSIDECASTLE(move)) {
        remove_piece(pos, pos->stm+ROOK, TO(move));
        pos->key = key_update_piece(pos->key, pos->stm+ROOK, TO(move));
    }

    /* Add piece to new position */
    if (ISPROMOTION(move)) {
        add_piece(pos, promotion, to);
        pos->key = key_update_piece(pos->key, promotion, to);
        update_material(pos, piece, false);
        update_material(pos, promotion, true);
    } else {
        add_piece(pos, piece, to);
        pos->key = key_update_piece(pos->key, piece, to);
    }

    /* If this is a castling we have to add the rook */
    if (ISKINGSIDECASTLE(move)) {
        add_piece(pos, pos->stm+ROOK, (pos->stm==WHITE)?F1:F8);
        pos->key = key_update_piece(pos->key, pos->stm+ROOK,
                                    (pos->stm==WHITE)?F1:F8);
    } else if (ISQUEENSIDECASTLE(move)) {
        add_piece(pos, pos->stm+ROOK, (pos->stm==WHITE)?D1:D8);
        pos->key = key_update_piece(pos->key, pos->stm+ROOK,
                                    (pos->stm==WHITE)?D1:D8);
    }

    /* Update the fifty move draw counter */
    if (ISCAPTURE(move) || (VALUE(piece) == PAWN)) {
        pos->fifty = 0;
    } else {
        pos->fifty++;
    }

    /* Update fullmove counter */
    if (pos->stm == BLACK) {
        pos->fullmove++;
    }

    /* Change side to move */
    pos->stm = FLIP_COLOR(pos->stm);
    pos->key = key_update_side(pos->key, pos->stm);

    /* Prefetch hash table entries */
    if (pos->state != NULL) {
        hash_prefetch(pos->worker);
    }

    /*
     * If the king was left in check then the move
     * was illegal and should be undone.
     */
    if (pos_in_check(pos, FLIP_COLOR(pos->stm))) {
        pos_unmake_move(pos);
        return false;
    }

    assert(pos->key == key_generate(pos));
    assert(valid_position(pos));

    return true;
}

void pos_unmake_move(struct position *pos)
{
    struct unmake *elem;
    uint32_t      move;
    int           to;
    int           from;
    int           piece;
    int           color;
    int           move_color;

    assert(valid_position(pos));

    /* Pop the top element from the history stack */
    elem = pop_history(pos);
    move = elem->move;
    pos->castle = elem->castle;
    pos->ep_sq = elem->ep_sq;
    pos->fifty = elem->fifty;
    pos->key = elem->key;

    /* Extract some information for later use */
    to = TO_CASTLE(move);
    from = FROM(move);
    color = pos->stm;
    move_color = FLIP_COLOR(color);

    /* Find the moving piece */
    piece = pos->pieces[to];

    /* Remove piece from current position */
    if (ISPROMOTION(move)) {
        remove_piece(pos, piece, to);
        update_material(pos, piece, false);
        piece = PAWN + move_color;
        update_material(pos, piece, true);
    } else {
        remove_piece(pos, piece, to);
    }

    /*
     * If this a castling then remove the rook
     * from it's current position.
     */
    if (ISKINGSIDECASTLE(move)) {
        remove_piece(pos, move_color+ROOK, (move_color==WHITE)?F1:F8);
    } else if (ISQUEENSIDECASTLE(move)) {
        remove_piece(pos, move_color+ROOK, (move_color==WHITE)?D1:D8);
    }

    /* Add piece to previous position */
    add_piece(pos, piece, from);

    /* Restore captured piece if necessary */
    if (ISCAPTURE(move)) {
        add_piece(pos, elem->capture, to);
        update_material(pos, elem->capture, true);
    } else if (ISENPASSANT(move)) {
        add_piece(pos, PAWN+color, (move_color==WHITE)?to-8:to+8);
        update_material(pos, PAWN+color, true);
    }

    /*
     * If this a castling then put the rook
     * back on it's original position.
     */
    if (ISKINGSIDECASTLE(move) || ISQUEENSIDECASTLE(move)) {
        add_piece(pos, move_color+ROOK, TO(move));
    }

    /* Update fullmove counter */
    if (pos->stm == WHITE) {
        pos->fullmove--;
    }

    /* Update position and game information */
    pos->stm = move_color;

    assert(pos->key == key_generate(pos));
    assert(valid_position(pos));
}

void pos_make_null_move(struct position *pos)
{
    struct unmake *elem;

    assert(valid_position(pos));
    assert(!pos_in_check(pos, pos->stm));

    /* Update the history */
    elem = push_history(pos);
    elem->move = NULLMOVE;
    elem->piece = NO_PIECE;
    elem->capture = NO_PIECE;
    elem->castle = pos->castle;
    elem->ep_sq = pos->ep_sq;
    elem->fifty = pos->fifty;
    elem->key = pos->key;

    /* Update NNUE */
    nnue_make_null_move(pos);

    /* Update the state structure */
    pos->ep_sq = NO_SQUARE;
    pos->key = key_update_ep_square(pos->key, elem->ep_sq, pos->ep_sq);
    pos->fifty++;
    if (pos->stm == BLACK) {
        pos->fullmove++;
    }
    pos->stm = FLIP_COLOR(pos->stm);
    pos->key = key_update_side(pos->key, pos->stm);

    /* Prefetch hash table entries */
    if (pos->state != NULL) {
        hash_prefetch(pos->worker);
    }

    assert(pos->key == key_generate(pos));
    assert(valid_position(pos));
}

void pos_unmake_null_move(struct position *pos)
{
    struct unmake *elem;

    assert(valid_position(pos));
    assert(ISNULLMOVE(pos->history[pos->ply-1].move));

    /* Pop the top element from the history stack */
    elem = pop_history(pos);
    pos->castle = elem->castle;
    pos->ep_sq = elem->ep_sq;
    pos->fifty = elem->fifty;
    pos->key = elem->key;

    /* Update the state structure */
    if (pos->stm == WHITE) {
        pos->fullmove--;
    }
    pos->stm = FLIP_COLOR(pos->stm);

    assert(pos->key == key_generate(pos));
    assert(valid_position(pos));
}

bool pos_is_repetition(struct position *pos)
{
    int idx;

    assert(valid_position(pos));

    /*
     * Pawn moves and captures are irreversible and so there is no need to
     * to check older positions for repetitions. Since the fifty counter
     * already keeps track of this to handle the fifty move rule this
     * counter can be used here as well.
     *
     * Also there is no need to consider position where the other side is to
     * move so only check every other position in the history.
     */
    idx = pos->ply - 2;
    while ((idx >= 0) && (idx >= (pos->ply - pos->fifty))) {
        if (pos->history[idx].key == pos->key) {
            return true;
        }
        idx -= 2;
    }

    return false;
}

bool pos_has_non_pawn(struct position *pos, int side)
{
    assert(valid_position(pos));
    assert(valid_side(side));

    return (pos->bb_pieces[KNIGHT+side]|pos->bb_pieces[BISHOP+side]|
                pos->bb_pieces[ROOK+side]|pos->bb_pieces[QUEEN+side]) != 0ULL;
}

bool pos_is_move_pseudo_legal(struct position *pos, uint32_t move)
{
    uint64_t bb;
    int      from;
    int      to;
    int      piece;
    int      opp;
    int      sq;
    int      victim;

    assert(valid_position(pos));
    assert(valid_move(move));
    assert(move != NOMOVE);

    from = FROM(move);
    to = TO(move);
    piece = pos->pieces[from];
    opp = FLIP_COLOR(pos->stm);
    victim = pos->pieces[to];

    /* Check that the moved piece has the correct color */
    if ((piece == NO_PIECE) || (COLOR(piece) != pos->stm)) {
        return false;
    }

    /* If the move is a promotion the the piece must be a pawn */
    if ((ISPROMOTION(move)) && (VALUE(piece) != PAWN)) {
        return false;
    }

    /*
     * If the moving piece is a pawn and the destination square is on
     * the first or eigth rank then the move must be a promotion.
     */
    if ((VALUE(piece) == PAWN) &&
        (sq_mask[to]&(rank_mask[RANK_1]|rank_mask[RANK_8])) &&
        (!ISPROMOTION(move))) {
        return false;
    }

    /* Handle special moves */
    if (ISENPASSANT(move)) {
        const int offset[2] = {-8, 8};

        /* Check that the piece is a pawn */
        if (VALUE(piece) != PAWN) {
            return false;
        }

        /*
         * Check that the piece was moved to the en-passant
         * target square and that the square is empty. */
        if ((to != pos->ep_sq) || (victim != NO_PIECE)) {
            return false;
        }

        /* Check that there is an enemy piece that can be captured */
        sq = pos->ep_sq + offset[pos->stm];
        if (!(pos->bb_pieces[PAWN+opp]&sq_mask[sq])) {
            return false;
        }

        /* Check that the from square is in the correct location */
        if ((sq != (from-1)) && (sq != (from+1))) {
            return false;
        }

        return true;
    } else if (ISKINGSIDECASTLE(move)) {
        const int kingsq[NSIDES] = {LSB(pos->bb_pieces[WHITE_KING]),
                                    LSB(pos->bb_pieces[BLACK_KING])};
        const int rooksq[NSIDES] = {pos->castle_wk, pos->castle_bk};

        return ((from == kingsq[pos->stm]) &&
                (to == rooksq[pos->stm]) &&
                (pos->pieces[kingsq[pos->stm]] == (KING+pos->stm)) &&
                (pos->pieces[rooksq[pos->stm]] == (ROOK+pos->stm)) &&
                pos_is_castling_allowed(pos, KINGSIDE_CASTLE));
    } else if (ISQUEENSIDECASTLE(move)) {
        const int kingsq[NSIDES] = {LSB(pos->bb_pieces[WHITE_KING]),
                                    LSB(pos->bb_pieces[BLACK_KING])};
        const int rooksq[NSIDES] = {pos->castle_wq, pos->castle_bq};

        return ((from == kingsq[pos->stm]) &&
                (to == rooksq[pos->stm]) &&
                (pos->pieces[kingsq[pos->stm]] == (KING+pos->stm)) &&
                (pos->pieces[rooksq[pos->stm]] == (ROOK+pos->stm)) &&
                pos_is_castling_allowed(pos, QUEENSIDE_CASTLE));
    }

    /*
     * If the move is a capture then there must be
     * an enemy piece on the destination square. And
     * if it is not a capture then the destination square
     * must be empty.
     */
    if (ISCAPTURE(move)) {
        if ((victim == NO_PIECE) || (COLOR(victim) != opp)) {
            return false;
        }
    } else {
        if (victim != NO_PIECE) {
            return false;
        }
    }

    /* Handle normal moves */
    bb = 0ULL;
    switch (piece) {
    case WHITE_PAWN:
    case BLACK_PAWN:
        bb |= bb_pawn_attacks_from(from, pos->stm);
        bb &= pos->bb_sides[FLIP_COLOR(pos->stm)];
        bb |= bb_pawn_moves(pos->bb_all, from, pos->stm);
        break;
    case WHITE_KNIGHT:
    case BLACK_KNIGHT:
        bb |= bb_knight_moves(from);
        bb &= (~pos->bb_sides[pos->stm]);
        break;
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        bb |= bb_bishop_moves(pos->bb_all, from);
        bb &= (~pos->bb_sides[pos->stm]);
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        bb |= bb_rook_moves(pos->bb_all, from);
        bb &= (~pos->bb_sides[pos->stm]);
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        bb |= bb_queen_moves(pos->bb_all, from);
        bb &= (~pos->bb_sides[pos->stm]);
        break;
    case WHITE_KING:
    case BLACK_KING:
        bb |= bb_king_moves(from);
        bb &= (~pos->bb_sides[pos->stm]);
        break;
    case NO_PIECE:
    default:
            assert(false);
            return false;
    }

    return (bb&sq_mask[to]) != 0ULL;
}

bool pos_move_gives_check(struct position *pos, uint32_t move)
{
    bool gives_check;
    int  from;
    int  to;
    int  src_piece;
    int  dest_piece;
    int  capture;

    assert(valid_position(pos));
    assert(valid_move(move));
    assert(move != NOMOVE);

    /* Handle special moves separatly to simplify the rest of the code */
    if (ISENPASSANT(move) ||
        ISKINGSIDECASTLE(move) ||
        ISQUEENSIDECASTLE(move)) {
        if (!pos_make_move(pos, move)) {
            return false;
        }
        gives_check = pos_in_check(pos, pos->stm);
        pos_unmake_move(pos);
        return gives_check;
    }

    /* Extract move information */
    from = FROM(move);
    to = TO(move);
    src_piece = pos->pieces[from];
    dest_piece = ISPROMOTION(move)?PROMOTION(move):src_piece;
    capture = pos->pieces[to];

    /* Remove piece from the source square */
    CLEARBIT(pos->bb_pieces[src_piece], from);
    CLEARBIT(pos->bb_sides[pos->stm], from);
    CLEARBIT(pos->bb_all, from);
    pos->pieces[from] = NO_PIECE;

    /* Remove captured piece from the destination square */
    if (capture != NO_PIECE) {
        CLEARBIT(pos->bb_pieces[capture], to);
        CLEARBIT(pos->bb_sides[FLIP_COLOR(pos->stm)], to);
        CLEARBIT(pos->bb_all, to);
        pos->pieces[to] = NO_PIECE;
    }

    /* Add piece to the destination square */
    SETBIT(pos->bb_pieces[dest_piece], to);
    SETBIT(pos->bb_sides[pos->stm], to);
    SETBIT(pos->bb_all, to);
    pos->pieces[to] = dest_piece;

    /* Check if opponent king is attacked */
    gives_check = bb_is_attacked(pos,
                                LSB(pos->bb_pieces[KING+FLIP_COLOR(pos->stm)]),
                                pos->stm);

    /* Remove piece from the desination square */
    CLEARBIT(pos->bb_pieces[dest_piece], to);
    CLEARBIT(pos->bb_sides[pos->stm], to);
    CLEARBIT(pos->bb_all, to);
    pos->pieces[to] = NO_PIECE;

    /* Put captured piece back on destination square */
    if (capture != NO_PIECE) {
        SETBIT(pos->bb_pieces[capture], to);
        SETBIT(pos->bb_sides[FLIP_COLOR(pos->stm)], to);
        SETBIT(pos->bb_all, to);
        pos->pieces[to] = capture;
    }

    /* Put the piece back on source square */
    SETBIT(pos->bb_pieces[src_piece], from);
    SETBIT(pos->bb_sides[pos->stm], from);
    SETBIT(pos->bb_all, from);
    pos->pieces[from] = src_piece;

    return gives_check;
}

bool pos_is_castling_allowed(struct position *pos, int type)
{
    int      king_start;
    int      king_stop;
    int      rook_start;
    int      rook_stop;
    int      sq;
    int      delta;
    int      flag;
    uint64_t occ;

    /* Check castling availability flag */
    if (pos->stm == WHITE) {
        flag = (type == KINGSIDE_CASTLE)?WHITE_KINGSIDE:WHITE_QUEENSIDE;
    } else {
        flag = (type == KINGSIDE_CASTLE)?BLACK_KINGSIDE:BLACK_QUEENSIDE;
    }
    if ((pos->castle&flag) == 0) {
        return false;
    }

    /*
     * Special case handling for standard chess. This is much faster
     * than the more general handling necessary for FRC.
     */
    if (engine_variant == VARIANT_STANDARD) {
        if (type == KINGSIDE_CASTLE) {
            if (pos->stm == WHITE) {
                return ((pos->castle&WHITE_KINGSIDE) &&
                    (pos->pieces[F1] == NO_PIECE) &&
                    (pos->pieces[G1] == NO_PIECE) &&
                    (!bb_is_attacked(pos, E1, BLACK)) &&
                    (!bb_is_attacked(pos, F1, BLACK)));
            } else {
                return ((pos->castle&BLACK_KINGSIDE) &&
                    (pos->pieces[F8] == NO_PIECE) &&
                    (pos->pieces[G8] == NO_PIECE) &&
                    (!bb_is_attacked(pos, E8, WHITE)) &&
                    (!bb_is_attacked(pos, F8, WHITE)));
            }
        } else {
            if (pos->stm == WHITE) {
                return ((pos->castle&WHITE_QUEENSIDE) &&
                        (pos->pieces[B1] == NO_PIECE) &&
                        (pos->pieces[C1] == NO_PIECE) &&
                        (pos->pieces[D1] == NO_PIECE) &&
                        (!bb_is_attacked(pos, D1, BLACK)) &&
                        (!bb_is_attacked(pos, E1, BLACK)));
            } else {
                return ((pos->castle&BLACK_QUEENSIDE) &&
                        (pos->pieces[B8] == NO_PIECE) &&
                        (pos->pieces[C8] == NO_PIECE) &&
                        (pos->pieces[D8] == NO_PIECE) &&
                        (!bb_is_attacked(pos, D8, WHITE)) &&
                        (!bb_is_attacked(pos, E8, WHITE)));
            }
        }
    }

    /* Figure out start and stop squares for the king and rook */
    king_start = LSB(pos->bb_pieces[KING+pos->stm]);
    if (type == KINGSIDE_CASTLE) {
        rook_start = (pos->stm == WHITE)?pos->castle_wk:pos->castle_bk;
        king_stop = (pos->stm == WHITE)?G1:G8;
        rook_stop = (pos->stm == WHITE)?F1:F8;
    } else if (type == QUEENSIDE_CASTLE) {
        rook_start = (pos->stm == WHITE)?pos->castle_wq:pos->castle_bq;
        king_stop = (pos->stm == WHITE)?C1:C8;
        rook_stop = (pos->stm == WHITE)?D1:D8;
    } else {
        return false;
    }

    /* Mask of the king and rook from the occupancy bitboard */
    occ = pos->bb_all;
    CLEARBIT(occ, king_start);
    CLEARBIT(occ, rook_start);

    /*
     * Check that the squares between the start and stop squares
     * of the king are unoccupied and not under attack. There is
     * no need to check if there is a king on the starting square
     * since if there isn't the castling flag would not be set.
     */
    delta = (king_start < king_stop)?1:-1;
    for (sq=king_start;sq!=king_stop;sq+=delta) {
        if (ISBITSET(occ, sq)) {
            return false;
        }
        if (bb_is_attacked(pos, sq, FLIP_COLOR(pos->stm))) {
            return false;
        }
    }
    if (ISBITSET(occ, king_stop)) {
        return false;
    }
	if (bb_is_attacked(pos, sq, FLIP_COLOR(pos->stm))) {
        return false;
    }

    /*
     * Check that the squares between the start and stop squares
     * of the rook are unoccupied and not under attack. There is
     * no need to check if there is a rook on the starting square
     * since if there isn't the castling flag would not be set.
     */
    delta = (rook_start < rook_stop)?1:-1;
    for (sq=rook_start;sq!=rook_stop;sq+=delta) {
        if (ISBITSET(occ, sq)) {
            return false;
        }
    }
    if (ISBITSET(occ, rook_stop)) {
        return false;
    }

    return true;
}
