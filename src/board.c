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

#include "board.h"
#include "fen.h"
#include "key.h"
#include "bitboard.h"
#include "validation.h"
#include "debug.h"
#include "eval.h"
#include "hash.h"
#include "search.h"
#include "movegen.h"

/*
 * Array of masks for updating castling permissions. For instance
 * a mask of 13 on A1 means that if a piece is moved to/from this
 * square then WHITE can still castle king side and black can still
 * castle both king side and queen side.
 */
static int castling_permission_masks[NSQUARES] = {
    13, 15, 15, 15, 12, 15, 15, 14,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
     7, 15, 15, 15,  3, 15, 15, 11
};

/* Point value for the different pieces */
static int point_values[NPIECES] = {1, 1, 3, 3, 3, 3, 5, 5, 9, 9, 0, 0};

static int point_value(struct position *pos)
{
    int sq;
    int piece;
    int wp;
    int bp;

    wp = 0;
    bp = 0;
    for (sq=0;sq<NSQUARES;sq++) {
        piece = pos->pieces[sq];
        if (piece == NO_PIECE) {
            continue;
        } else if (COLOR(piece) == WHITE) {
            wp += point_values[piece];
        } else {
            bp += point_values[piece];
        }
    }
    return pos->stm == WHITE?wp-bp:-(wp-bp);
}

static int quiet(struct position *pos, int alpha, int beta, struct pv *pv)
{
    int             score;
    int             best_score;
    int             static_score;
    uint32_t        move;
    struct movelist list;
    int             k;
    struct pv       line;
    bool            in_check;

    in_check = board_in_check(pos, pos->stm);
    static_score = point_value(pos);
    best_score = -INFINITE_SCORE;

    if (!in_check) {
        best_score = static_score;
        if (static_score >= beta) {
            return static_score;
        }
        if (static_score > alpha) {
            alpha = static_score;
        }
    }

    list.nmoves = 0;
    if (in_check) {
        gen_legal_moves(pos, &list);
        if (list.nmoves == 0) {
            return -CHECKMATE;
        }
    } else {
        gen_capture_moves(pos, &list);
        gen_promotion_moves(pos, &list, false);
    }
    for (k=0;k<list.nmoves;k++) {
        move = list.moves[k];
        if (!board_make_move(pos, move)) {
            continue;
        }
        line.length = 0;
        score = -quiet(pos, -beta, -alpha, &line);
        board_unmake_move(pos);

        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                if (score >= beta) {
                    break;
                }
                alpha = score;
                pv->moves[0] = move;
                memcpy(pv->moves+1, line.moves, line.length*sizeof(uint32_t));
                pv->length = line.length + 1;
            }
        }
    }

    return best_score;
}

static void add_piece(struct position *pos, int piece, int square)
{
    SETBIT(pos->bb_pieces[piece], square);
    SETBIT(pos->bb_sides[COLOR(piece)], square);
    SETBIT(pos->bb_all, square);
    pos->pieces[square] = piece;
    eval_update_material_score(pos, true, piece);
    eval_update_psq_score(pos, true, piece, square);
}

static void remove_piece(struct position *pos, int piece, int square)
{
    CLEARBIT(pos->bb_pieces[piece], square);
    CLEARBIT(pos->bb_sides[COLOR(piece)], square);
    CLEARBIT(pos->bb_all, square);
    pos->pieces[square] = NO_PIECE;
    eval_update_material_score(pos, false, piece);
    eval_update_psq_score(pos, false, piece, square);
}

static void move_piece(struct position *pos, int piece, int from, int to)
{
    remove_piece(pos, piece, from);
    add_piece(pos, piece, to);
}

static struct unmake* push_history(struct position *pos)
{
    struct unmake *elem;

    assert(pos->ply < MAX_HISTORY_SIZE);

    elem = &pos->history[pos->ply];
    pos->ply++;
    pos->sply++;

    return elem;
}

static struct unmake* pop_history(struct position *pos)
{
    assert(pos->ply > 0);

    pos->ply--;
    if (pos->sply > 0) {
        pos->sply--;
    }

    return &pos->history[pos->ply];
}

void board_reset(struct position *pos)
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
    pos->pawnkey = 0ULL;

    pos->ep_sq = NO_SQUARE;
    pos->castle = 0;
    pos->stm = NO_SIDE;
    pos->ply = 0;
    pos->sply = 0;
    pos->fifty = 0;
}

void board_start_position(struct position *pos)
{
    assert(pos != NULL);

    (void)board_setup_from_fen(pos, FEN_STARTPOS);
    assert(valid_position(pos));
}

bool board_setup_from_fen(struct position *pos, char *fenstr)
{
    assert(pos != NULL);
    assert(fenstr != NULL);

    board_reset(pos);
    return fen_setup_board(pos, fenstr, false) && valid_position(pos);
}


bool board_in_check(struct position *pos, int side)
{
    assert(valid_position(pos));
    assert(valid_side(side));

    return bb_is_attacked(pos, LSB(pos->bb_pieces[KING+side]),
                          FLIP_COLOR(side));
}

bool board_make_move(struct position *pos, uint32_t move)
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
    assert(board_is_move_pseudo_legal(pos, move));
    assert(pos->ply < MAX_MOVES);


    from = FROM(move);
    to = TO(move);
    promotion = PROMOTION(move);

    /* Find the pieces involved in the move */
    capture = pos->pieces[to];
    piece = pos->pieces[from];

    /* Update the history */
    elem = push_history(pos);
    elem->move = move;
    elem->capture = capture;
    elem->castle = pos->castle;
    elem->ep_sq = pos->ep_sq;
    elem->fifty = pos->fifty;
    elem->key = pos->key;
    elem->pawnkey = pos->pawnkey;

    /* Check if the move enables an en passant capture */
    if ((VALUE(piece) == PAWN) && (abs(to-from) == 16)) {
        pos->ep_sq = (pos->stm == WHITE)?to-8:to+8;
    } else {
        pos->ep_sq = NO_SQUARE;
    }
    pos->key = key_update_ep_square(pos->key, elem->ep_sq, pos->ep_sq);

    /* Update castling availability */
    pos->castle &= castling_permission_masks[from];
    pos->castle &= castling_permission_masks[to];
    pos->key = key_update_castling(pos->key, elem->castle, pos->castle);

    /* Remove piece from current position */
    remove_piece(pos, piece, from);
    pos->key = key_update_piece(pos->key, piece, from);
    if (VALUE(piece) == PAWN) {
        pos->pawnkey = key_update_piece(pos->pawnkey, piece, from);
    }

    /* If necessary remove captured piece */
    if (ISCAPTURE(move)) {
        remove_piece(pos, capture, to);
        pos->key = key_update_piece(pos->key, capture, to);
        if (VALUE(capture) == PAWN) {
            pos->pawnkey = key_update_piece(pos->pawnkey, capture, to);
        }
    } else if (ISENPASSANT(move)) {
        ep = (pos->stm == WHITE)?to-8:to+8;
        remove_piece(pos, PAWN+FLIP_COLOR(pos->stm), ep);
        pos->key = key_update_piece(pos->key, PAWN+FLIP_COLOR(pos->stm), ep);
        pos->pawnkey = key_update_piece(pos->pawnkey, PAWN+FLIP_COLOR(pos->stm),
                                        ep);
    }

    /* Add piece to new position */
    if (ISPROMOTION(move)) {
        add_piece(pos, promotion, to);
        pos->key = key_update_piece(pos->key, promotion, to);
    } else {
        add_piece(pos, piece, to);
        pos->key = key_update_piece(pos->key, piece, to);
        if (VALUE(piece) == PAWN) {
            pos->pawnkey = key_update_piece(pos->pawnkey, piece, to);
        }
    }

    /* If this is a castling we have to move the rook */
    if (ISKINGSIDECASTLE(move)) {
        move_piece(pos, pos->stm+ROOK, to+1, to-1);
        pos->key = key_update_piece(pos->key, pos->stm+ROOK, to+1);
        pos->key = key_update_piece(pos->key, pos->stm+ROOK, to-1);
    } else if (ISQUEENSIDECASTLE(move)) {
        move_piece(pos, pos->stm+ROOK, to-2, to+1);
        pos->key = key_update_piece(pos->key, pos->stm+ROOK, to-2);
        pos->key = key_update_piece(pos->key, pos->stm+ROOK, to+1);
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
    if (board_in_check(pos, FLIP_COLOR(pos->stm))) {
        board_unmake_move(pos);
        return false;
    }

    assert(pos->key == key_generate(pos));
    assert(pos->pawnkey == key_generate_pawnkey(pos));
    assert(valid_position(pos));
    assert(valid_scores(pos));

    return true;
}

void board_unmake_move(struct position *pos)
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
    pos->pawnkey = elem->pawnkey;

    /* Extract some information for later use */
    to = TO(move);
    from = FROM(move);
    piece = pos->pieces[to];
    color = pos->stm;
    move_color = FLIP_COLOR(color);

    /* Remove piece from current position */
    if (ISPROMOTION(move)) {
        remove_piece(pos, piece, to);
        piece = PAWN + move_color;
    } else {
        remove_piece(pos, piece, to);
    }

    /* Add piece to previous position */
    add_piece(pos, piece, from);

    /* Restore captured piece if necessary */
    if (ISCAPTURE(move)) {
        add_piece(pos, elem->capture, to);
    } else if (ISENPASSANT(move)) {
        add_piece(pos, PAWN+color, (move_color==WHITE)?to-8:to+8);
    }

    /*
     * If this a castling then move the rook
     * back to it's original position.
     */
    if (ISKINGSIDECASTLE(move)) {
        remove_piece(pos, move_color+ROOK, to-1);
        add_piece(pos, move_color+ROOK, to+1);
    } else if (ISQUEENSIDECASTLE(move)) {
        remove_piece(pos, move_color+ROOK, to+1);
        add_piece(pos, move_color+ROOK, to-2);
    }

    /* Update fullmove counter */
    if (pos->stm == WHITE) {
        pos->fullmove--;
    }

    /* Update position and game information */
    pos->stm = move_color;

    assert(pos->key == key_generate(pos));
    assert(pos->pawnkey == key_generate_pawnkey(pos));
    assert(valid_position(pos));
    assert(valid_scores(pos));
}

void board_make_null_move(struct position *pos)
{
    struct unmake *elem;

    assert(valid_position(pos));
    assert(!board_in_check(pos, pos->stm));

    /* Update the history */
    elem = push_history(pos);
    elem->move = NULLMOVE;
    elem->capture = NO_PIECE;
    elem->castle = pos->castle;
    elem->ep_sq = pos->ep_sq;
    elem->fifty = pos->fifty;
    elem->key = pos->key;
    elem->pawnkey = pos->pawnkey;

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
    assert(pos->pawnkey == key_generate_pawnkey(pos));
    assert(valid_position(pos));
}

void board_unmake_null_move(struct position *pos)
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
    pos->pawnkey = elem->pawnkey;

    /* Update the state structure */
    if (pos->stm == WHITE) {
        pos->fullmove--;
    }
    pos->stm = FLIP_COLOR(pos->stm);

    assert(pos->key == key_generate(pos));
    assert(pos->pawnkey == key_generate_pawnkey(pos));
    assert(valid_position(pos));
}

bool board_is_repetition(struct position *pos)
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

bool board_has_non_pawn(struct position *pos, int side)
{
    assert(valid_position(pos));
    assert(valid_side(side));

    return (pos->bb_pieces[KNIGHT+side]|pos->bb_pieces[BISHOP+side]|
                pos->bb_pieces[ROOK+side]|pos->bb_pieces[QUEEN+side]) != 0ULL;
}

bool board_is_move_pseudo_legal(struct position *pos, uint32_t move)
{
    uint64_t    bb;
    int         from;
    int         to;
    int         piece;
    int         opp;
    int         sq;
    int         victim;

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
        const int emptysq1[2] = {6, 62};
        const int emptysq2[2] = {5, 61};
        const int kingsq[2] = {4, 60};
        const int rooksq[2] = {7, 63};
        const int availability[2] = {WHITE_KINGSIDE, BLACK_KINGSIDE};

        /* Check castling */
        return ((pos->castle&availability[pos->stm]) &&
                (pos->pieces[emptysq2[pos->stm]] == NO_PIECE) &&
                (pos->pieces[emptysq1[pos->stm]] == NO_PIECE) &&
                (pos->pieces[kingsq[pos->stm]] == (KING+pos->stm)) &&
                (pos->pieces[rooksq[pos->stm]] == (ROOK+pos->stm)) &&
                (!bb_is_attacked(pos, kingsq[pos->stm], opp)) &&
                (!bb_is_attacked(pos, emptysq2[pos->stm], opp)) &&
                (from == kingsq[pos->stm]) &&
                (to == emptysq1[pos->stm]));
    } else if (ISQUEENSIDECASTLE(move)) {
        const int emptysq1[2] = {1, 57};
        const int emptysq2[2] = {2, 58};
        const int emptysq3[2] = {3, 59};
        const int kingsq[2] = {4, 60};
        const int rooksq[2] = {0, 56};
        const int availability[2] = {WHITE_QUEENSIDE, BLACK_QUEENSIDE};

        /* Check castling */
        return ((pos->castle&availability[pos->stm]) &&
                (pos->pieces[emptysq3[pos->stm]] == NO_PIECE) &&
                (pos->pieces[emptysq2[pos->stm]] == NO_PIECE) &&
                (pos->pieces[emptysq1[pos->stm]] == NO_PIECE) &&
                (pos->pieces[kingsq[pos->stm]] == (KING+pos->stm)) &&
                (pos->pieces[rooksq[pos->stm]] == (ROOK+pos->stm)) &&
                (!bb_is_attacked(pos, kingsq[pos->stm], opp)) &&
                (!bb_is_attacked(pos, emptysq3[pos->stm], opp)) &&
                (from == kingsq[pos->stm]) &&
                (to == emptysq2[pos->stm]));
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

bool board_move_gives_check(struct position *pos, uint32_t move)
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
        if (!board_make_move(pos, move)) {
            return false;
        }
        gives_check = board_in_check(pos, pos->stm);
        board_unmake_move(pos);
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

void board_quiet(struct position *pos, struct pv *pv)
{
    pv->length = 0;
    (void)quiet(pos, -INFINITE_SCORE, INFINITE_SCORE, pv);
}
