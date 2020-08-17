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
#include <cstdio>
#include <cstring>
#include <cassert>

#include "nnue.h"

static bool eval_uses_nnue;
static std::string loaded_eval_file;

Position::Position()
{
    clear();
}

Position::Position(const Position &p)
{
    std::memcpy(m_evalList.piece_id_list, p.m_evalList.piece_id_list,
                sizeof(m_evalList.piece_id_list));
    std::memcpy(m_evalList.pieceListFw, p.m_evalList.pieceListFw,
                sizeof(m_evalList.pieceListFw));
    std::memcpy(m_evalList.pieceListFb, p.m_evalList.pieceListFb,
                sizeof(m_evalList.pieceListFb));
    m_stm = p.m_stm;
    std::memset(m_stateStack, 0, sizeof(m_stateStack));
    m_currentState = &m_stateStack[0];
    m_stackSize = 1;
}

StateInfo* Position::state() const
{
    return m_currentState;
}

Color Position::side_to_move() const
{
    return m_stm;
}

const EvalList* Position::eval_list() const
{
    return &m_evalList;
}

void Position::clear()
{
    std::memset(m_evalList.piece_id_list, 0, sizeof(m_evalList.piece_id_list));
    std::memset(m_evalList.pieceListFw, 0, sizeof(m_evalList.pieceListFw));
    std::memset(m_evalList.pieceListFb, 0, sizeof(m_evalList.pieceListFb));
    std::memset(m_stateStack, 0, sizeof(m_stateStack));
    m_currentState = &m_stateStack[0];
    m_stackSize = 1;
    m_stm = WHITE;
}

PieceId Position::piece_id_on(Square sq)
{
    return m_evalList.piece_id_list[sq];
}

Piece Position::cvt_piece(int piece)
{
    switch (piece) {
    case NNUE_WHITE_PAWN:
        return W_PAWN;
    case NNUE_BLACK_PAWN:
        return B_PAWN;
    case NNUE_WHITE_KNIGHT:
        return W_KNIGHT;
    case NNUE_BLACK_KNIGHT:
        return B_KNIGHT;
    case NNUE_WHITE_BISHOP:
        return W_BISHOP;
    case NNUE_BLACK_BISHOP:
        return B_BISHOP;
    case NNUE_WHITE_ROOK:
        return W_ROOK;
    case NNUE_BLACK_ROOK:
        return B_ROOK;
    case NNUE_WHITE_QUEEN:
        return W_QUEEN;
    case NNUE_BLACK_QUEEN:
        return B_QUEEN;
    case NNUE_WHITE_KING:
        return W_KING;
    case NNUE_BLACK_KING:
        return B_KING;
    }

    return NO_PIECE;
}

void Position::setup(uint8_t *pieces, int side)
{
    Piece   pc;
    PieceId piece_id;
    PieceId next_piece_id = PIECE_ID_ZERO;

    for (int sq=0;sq<SQUARE_NB;sq++) {
        if (pieces[sq] != NNUE_NO_PIECE) {
            pc = cvt_piece(pieces[sq]);
            piece_id = (pc == W_KING) ? PIECE_ID_WKING :
                    (pc == B_KING) ? PIECE_ID_BKING :
                    next_piece_id;
            if (piece_id != PIECE_ID_WKING && piece_id != PIECE_ID_BKING) {
                next_piece_id = (PieceId)((int)next_piece_id + 1);
            }
            m_evalList.put_piece(piece_id, (Square)sq, pc);
        }
    }
    m_stm = (Color)side; 
}

void Position::make_move(int from, int to, int type, int promotion, int piece)
{
    PieceId dp0 = PIECE_ID_NONE;
    PieceId dp1 = PIECE_ID_NONE;

    // Update state pointers
    StateInfo &info = m_stateStack[m_stackSize++];
    assert(m_stackSize > 1);
    info.previous = m_currentState;
    m_currentState = &info;
    auto &dp = m_currentState->dirtyPiece;

    // Initialize accumulator
    m_currentState->accumulator.computed_accumulation = false;
    m_currentState->accumulator.computed_score = false;

    if (type == NNUE_KINGSIDE_CASTLE) {
        Square rfrom = (Square)(to + 1);
        Square rto = (Square)(to - 1);

        dp.dirty_num = 2;
        dp0 = piece_id_on((Square)from);
        dp1 = piece_id_on(rfrom);
        dp.pieceId[0] = dp0;
        dp.old_piece[0] = m_evalList.piece_with_id(dp0);
        m_evalList.put_piece(dp0, (Square)to,
                             m_stm == WHITE ? W_KING : B_KING);
        dp.new_piece[0] = m_evalList.piece_with_id(dp0);
        dp.pieceId[1] = dp1;
        dp.old_piece[1] = m_evalList.piece_with_id(dp1);
        m_evalList.put_piece(dp1, rto, m_stm == WHITE ? W_ROOK : B_ROOK);
        dp.new_piece[1] = m_evalList.piece_with_id(dp1);
    } else if (type == NNUE_QUEENSIDE_CASTLE) {
        Square rfrom = (Square)(to - 2);
        Square rto = (Square)(to + 1);

        dp.dirty_num = 2;
        dp0 = piece_id_on((Square)from);
        dp1 = piece_id_on(rfrom);
        dp.pieceId[0] = dp0;
        dp.old_piece[0] = m_evalList.piece_with_id(dp0);
        m_evalList.put_piece(dp0, (Square)to,
                             m_stm == WHITE ? W_KING : B_KING);
        dp.new_piece[0] = m_evalList.piece_with_id(dp0);
        dp.pieceId[1] = dp1;
        dp.old_piece[1] = m_evalList.piece_with_id(dp1);
        m_evalList.put_piece(dp1, rto, m_stm == WHITE ? W_ROOK : B_ROOK);
        dp.new_piece[1] = m_evalList.piece_with_id(dp1);
    } else {
        if ((type == NNUE_EN_PASSANT) ||
            ((type&NNUE_CAPTURE) != 0)) {
            dp.dirty_num = 2;
            Square capsq = (Square)to;
            if (type == NNUE_EN_PASSANT) {
                capsq = (m_stm == WHITE)?(Square)(to-8):(Square)(to+8);
            }
            dp1 = piece_id_on(capsq);
            dp.pieceId[1] = dp1;
            dp.old_piece[1] = m_evalList.piece_with_id(dp1);
            m_evalList.put_piece(dp1, capsq, NO_PIECE);
            dp.new_piece[1] = m_evalList.piece_with_id(dp1);
        } else {
            dp.dirty_num = 1;
        }

        dp0 = piece_id_on((Square)from);
        dp.pieceId[0] = dp0;
        dp.old_piece[0] = m_evalList.piece_with_id(dp0);
        m_evalList.put_piece(dp0, (Square)to, cvt_piece(piece));
        dp.new_piece[0] = m_evalList.piece_with_id(dp0);

        if ((type&NNUE_PROMOTION) != 0) {
            dp0 = piece_id_on((Square)to);
            m_evalList.put_piece(dp0, (Square)to, cvt_piece(promotion));
            dp.new_piece[0] = m_evalList.piece_with_id(dp0);
        }
    }

    m_stm = (Color)((int)m_stm ^ 1);
}

void Position::unmake_move(int from, int to, int type, int captured,
                           int piece)
{
    assert(m_stackSize > 0);

    StateInfo &info = m_stateStack[--m_stackSize];
    PieceId dp0 = PIECE_ID_NONE;
    PieceId dp1 = PIECE_ID_NONE;
    Color opp = m_stm;
    Color side = (Color)((int)m_stm ^ 1);

    if (type == NNUE_KINGSIDE_CASTLE) {
        Square rfrom = (Square)(to + 1);
        Square rto = (Square)(to - 1);

        dp0 = piece_id_on((Square)to);
        dp1 = piece_id_on((Square)rto);
        m_evalList.put_piece(dp0, (Square)from, (side==WHITE)?W_KING:B_KING);
        m_evalList.put_piece(dp1, rfrom, (side==WHITE)?W_ROOK:B_ROOK);
    } else if (type == NNUE_QUEENSIDE_CASTLE) {
        Square rfrom = (Square)(to - 2);
        Square rto = (Square)(to + 1);

        dp0 = piece_id_on((Square)to);
        dp1 = piece_id_on(rto);
        m_evalList.put_piece(dp0, (Square)from, (side==WHITE)?W_KING:B_KING);
        m_evalList.put_piece(dp1, rfrom, (side==WHITE)?W_ROOK:B_ROOK);
    } else {
        Piece pc = cvt_piece(piece);
        if ((type&NNUE_PROMOTION) != 0) {
            pc = (side==WHITE)?W_PAWN:B_PAWN;
        }

        PieceId dp0 = m_currentState->dirtyPiece.pieceId[0];
        m_evalList.put_piece(dp0, (Square)from, pc);

        if ((type == NNUE_EN_PASSANT) ||
            ((type&NNUE_CAPTURE) != 0)) {
            Square capsq = (Square)to;
            Piece cappc = cvt_piece(captured);
            if (type == NNUE_EN_PASSANT) {
                capsq = (side == WHITE)?(Square)(to-8):(Square)(to+8);
                cappc = (opp == WHITE)?W_PAWN:B_PAWN;
            }

            PieceId dp1 = m_currentState->dirtyPiece.pieceId[1];
            assert(m_evalList.piece_with_id(dp1).from[WHITE] == PS_NONE);
            assert(m_evalList.piece_with_id(dp1).from[BLACK] == PS_NONE);
            m_evalList.put_piece(dp1, capsq, cappc);
        }
    }

    m_currentState = info.previous;
    assert(m_currentState != NULL);

    m_stm = (Color)((int)m_stm ^ 1);
}

void Position::make_null_move()
{
    StateInfo &info = m_stateStack[m_stackSize++];
    std::memcpy(&info, m_currentState, sizeof(StateInfo));
    assert(m_stackSize > 1);
    info.previous = m_currentState;
    m_currentState = &info;
    m_currentState->accumulator.computed_score = false;

    m_stm = (Color)((int)m_stm ^ 1);
}

void Position::unmake_null_move()
{
    assert(m_stackSize > 0);

    StateInfo &info = m_stateStack[--m_stackSize];
    m_currentState = info.previous;
    assert(m_currentState != NULL);

    m_stm = (Color)((int)m_stm ^ 1);
}

bool nnue_init(char *eval_file)
{
    eval_uses_nnue = false;
    std::string name{eval_file};
    if (Eval::NNUE::load_eval_file(name)) {
        loaded_eval_file = name;
        eval_uses_nnue = true;
    }

    return eval_uses_nnue;
}

void* nnue_create_pos(void)
{
    assert(eval_uses_nnue);

    return new Position();
}

void nnue_destroy_pos(void *pos)
{
    if (pos == NULL) {
        return;
    }
    delete ((Position*)pos);
}

void nnue_copy_pos(void *source, void *dest)
{
    assert(eval_uses_nnue);
    assert(source != NULL);
    assert(dest != NULL);

    Position *spos = (Position*)source;
    Position *dpos = (Position*)dest;

    *dpos = *spos;
}

void nnue_setup_pos(void *pos, uint8_t *pieces, int side)
{
    assert(eval_uses_nnue);
    assert(pos != NULL);
    assert(pieces != NULL);

    ((Position*)pos)->clear();
    ((Position*)pos)->setup(pieces, side);
}

void nnue_make_move(void *pos, int from, int to, int type, int promotion,
                    int piece)
{
    assert(eval_uses_nnue);
    assert(pos != NULL);

    ((Position*)pos)->make_move(from, to, type, promotion, piece);
}

void nnue_unmake_move(void *pos, int from, int to, int type, int captured,
                      int piece)
{
    assert(eval_uses_nnue);
    assert(pos != NULL);

    ((Position*)pos)->unmake_move(from, to, type, captured, piece);
}

void nnue_make_null_move(void *pos)
{
    assert(eval_uses_nnue);
    assert(pos != NULL);

    ((Position*)pos)->make_null_move();
}

void nnue_unmake_null_move(void *pos)
{
    assert(eval_uses_nnue);
    assert(pos != NULL);

    ((Position*)pos)->unmake_null_move();
}

int nnue_evaluate(void *pos)
{
    assert(eval_uses_nnue);
    assert(pos != NULL);

    return (int)Eval::NNUE::evaluate(*((Position*)pos));
}

bool nnue_compare_pos(void *pos1, void *pos2)
{
    assert(eval_uses_nnue);
    assert(pos1 != NULL);
    assert(pos2 != NULL);

    Position *p1 = (Position*)pos1;
    Position *p2 = (Position*)pos2;

    if (p1->m_stm != p2->m_stm) {
        return false;
    }
    if (std::memcmp(p1->m_evalList.pieceListFw, p2->m_evalList.pieceListFw,
                sizeof(p1->m_evalList.pieceListFw)) != 0) {
        return false;
    }
    if (std::memcmp(p1->m_evalList.pieceListFb, p2->m_evalList.pieceListFb,
                sizeof(p1->m_evalList.pieceListFb)) != 0) {
        return false;
    }

    return true;
}
