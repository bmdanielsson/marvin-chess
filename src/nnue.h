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
#ifndef NNUE_H
#define NNUE_H

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

#ifdef __cplusplus

#include <cstddef>
#include <cstdint>

#include "nnue_accumulator.h"
#include "nnue_defs.h"

/*
 * Definition of the different pieces. Must match
 * the definitions in chess.h.
 */
enum {
    NNUE_WHITE_PAWN,
    NNUE_BLACK_PAWN,
    NNUE_WHITE_KNIGHT,
    NNUE_BLACK_KNIGHT,
    NNUE_WHITE_BISHOP,
    NNUE_BLACK_BISHOP,
    NNUE_WHITE_ROOK,
    NNUE_BLACK_ROOK,
    NNUE_WHITE_QUEEN,
    NNUE_BLACK_QUEEN,
    NNUE_WHITE_KING,
    NNUE_BLACK_KING,
    NNUE_NO_PIECE
};

/*
 * Flags for different move types. Must match
 * the definitions in chess.h.
 */
enum {
    NNUE_NORMAL  =  0,
    NNUE_CAPTURE  = 1,
    NNUE_PROMOTION = 2,
    NNUE_EN_PASSANT = 4,
    NNUE_KINGSIDE_CASTLE = 8,
    NNUE_QUEENSIDE_CASTLE = 16,
    NNUE_NULL_MOVE = 32
};

extern "C" void* aligned_malloc(int alignment, uint64_t size);
extern "C" void aligned_free(void *ptr);
#define std_aligned_alloc aligned_malloc
#define std_aligned_free aligned_free

struct StateInfo {
    StateInfo *previous;
    Eval::NNUE::Accumulator accumulator;
    DirtyPiece dirtyPiece;
};

class Position {
public:
    Position();
    Position(const Position &p); 

    // Methods called by NNUE
    StateInfo* state() const;
    Color side_to_move() const;
    const EvalList* eval_list() const;

    // Internal methods
    void clear();
    PieceId piece_id_on(Square sq);
    Piece cvt_piece(int piece);
    void setup(uint8_t *pieces, int side);
    void make_move(int from, int to, int type, int promotion, int piece);
    void unmake_move(int from, int to, int type, int promotion, int captured);
    void make_null_move();
    void unmake_null_move();

    Color m_stm;
    StateInfo m_stateStack[1024];
    int m_stackSize;
    EvalList m_evalList;
    StateInfo *m_currentState;
};

#endif // __cplusplus

EXTERN bool nnue_init(char *eval_file);
EXTERN void* nnue_create_pos(void);
EXTERN void nnue_destroy_pos(void *pos);
EXTERN void nnue_copy_pos(void *source, void *dest);
EXTERN void nnue_setup_pos(void *pos, uint8_t *pieces, int side);
EXTERN void nnue_make_move(void *pos, int from, int to, int type,
                           int promotion, int piece);
EXTERN void nnue_unmake_move(void *pos, int from, int to, int type,
                             int captured, int piece);
EXTERN void nnue_make_null_move(void *pos);
EXTERN void nnue_unmake_null_move(void *pos);
EXTERN int nnue_evaluate(void *pos);
EXTERN bool nnue_compare_pos(void *pos1, void *pos2);

#endif
