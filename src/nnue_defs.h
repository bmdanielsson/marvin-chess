/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2020 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * This file contains declarations copied from Stockfish
 * that are required fopr NNUE.
 */
#ifndef NNUE_DEFS_H
#define NNUE_DEFS_H

#ifdef __cplusplus

#include <string>

class Position;

constexpr int MAX_PLY = 246;

enum Color {
  WHITE, BLACK, COLOR_NB = 2
};

enum Piece {
  NO_PIECE,
  W_PAWN = 1, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN = 9, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
  PIECE_NB = 16
};

enum PieceId {
  PIECE_ID_ZERO   = 0,
  PIECE_ID_KING   = 30,
  PIECE_ID_WKING  = 30,
  PIECE_ID_BKING  = 31,
  PIECE_ID_NONE   = 32
};

enum Square : int {
  SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
  SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
  SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
  SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
  SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
  SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
  SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
  SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
  SQ_NONE,

  SQUARE_ZERO = 0,
  SQUARE_NB   = 64
};

// unique number for each piece type on each square
enum PieceSquare : uint32_t {
  PS_NONE     =  0,
  PS_W_PAWN   =  1,
  PS_B_PAWN   =  1 * SQUARE_NB + 1,
  PS_W_KNIGHT =  2 * SQUARE_NB + 1,
  PS_B_KNIGHT =  3 * SQUARE_NB + 1,
  PS_W_BISHOP =  4 * SQUARE_NB + 1,
  PS_B_BISHOP =  5 * SQUARE_NB + 1,
  PS_W_ROOK   =  6 * SQUARE_NB + 1,
  PS_B_ROOK   =  7 * SQUARE_NB + 1,
  PS_W_QUEEN  =  8 * SQUARE_NB + 1,
  PS_B_QUEEN  =  9 * SQUARE_NB + 1,
  PS_W_KING   = 10 * SQUARE_NB + 1,
  PS_END      = PS_W_KING, // pieces without kings (pawns included)
  PS_B_KING   = 11 * SQUARE_NB + 1,
  PS_END2     = 12 * SQUARE_NB + 1
};

enum Value : int {
  VALUE_ZERO      = 0,
  VALUE_DRAW      = 0,
  VALUE_KNOWN_WIN = 10000,
  VALUE_MATE      = 32000,
  VALUE_INFINITE  = 32001,
  VALUE_NONE      = 32002,

  VALUE_TB_WIN_IN_MAX_PLY  =  VALUE_MATE - 2 * MAX_PLY,
  VALUE_TB_LOSS_IN_MAX_PLY = -VALUE_TB_WIN_IN_MAX_PLY,
  VALUE_MATE_IN_MAX_PLY  =  VALUE_MATE - MAX_PLY,
  VALUE_MATED_IN_MAX_PLY = -VALUE_MATE_IN_MAX_PLY,

  PawnValueMg   = 124,   PawnValueEg   = 206,
  KnightValueMg = 781,   KnightValueEg = 854,
  BishopValueMg = 825,   BishopValueEg = 915,
  RookValueMg   = 1276,  RookValueEg   = 1380,
  QueenValueMg  = 2538,  QueenValueEg  = 2682,
  Tempo = 28,

  MidgameLimit  = 15258, EndgameLimit  = 3915
};

namespace Eval {

  namespace NNUE {

    Value evaluate(const Position& pos);
    Value compute_eval(const Position& pos);
    void  update_eval(const Position& pos);
    bool  load_eval_file(const std::string& evalFile);

  } // namespace NNUE

} // namespace Eval

struct ExtPieceSquare {
  PieceSquare from[COLOR_NB];
};

// Array for finding the PieceSquare corresponding to the piece on the board
extern ExtPieceSquare kpp_board_index[PIECE_NB];

// For differential evaluation of pieces that changed since last turn
struct DirtyPiece {

  // Number of changed pieces
  int dirty_num;

  // The ids of changed pieces, max. 2 pieces can change in one move
  PieceId pieceId[2];

  // What changed from the piece with that piece number
  ExtPieceSquare old_piece[2];
  ExtPieceSquare new_piece[2];
};

// Return relative square when turning the board 180 degrees
constexpr Square rotate180(Square sq) {
  return (Square)(sq ^ 0x3F);
}

class EvalList {

public:
  // Max. number of pieces without kings is 30 but must be a multiple of 4 in AVX2
  static const int MAX_LENGTH = 32;

  // Array that holds the piece id for the pieces on the board
  PieceId piece_id_list[SQUARE_NB];

  // List of pieces, separate from White and Black POV
  PieceSquare* piece_list_fw() const { return const_cast<PieceSquare*>(pieceListFw); }
  PieceSquare* piece_list_fb() const { return const_cast<PieceSquare*>(pieceListFb); }

  // Place the piece pc with piece_id on the square sq on the board
  void put_piece(PieceId piece_id, Square sq, Piece pc)
  {
      if (pc != NO_PIECE)
      {
          pieceListFw[piece_id] = PieceSquare(kpp_board_index[pc].from[WHITE] + sq);
          pieceListFb[piece_id] = PieceSquare(kpp_board_index[pc].from[BLACK] + rotate180(sq));
          piece_id_list[sq] = piece_id;
      }
      else
      {
          pieceListFw[piece_id] = PS_NONE;
          pieceListFb[piece_id] = PS_NONE;
          piece_id_list[sq] = piece_id;
      }
  }

  // Convert the specified piece_id piece to ExtPieceSquare type and return it
  ExtPieceSquare piece_with_id(PieceId piece_id) const
  {
      ExtPieceSquare eps;
      eps.from[WHITE] = pieceListFw[piece_id];
      eps.from[BLACK] = pieceListFb[piece_id];
      return eps;
  }

  PieceSquare pieceListFw[MAX_LENGTH];
  PieceSquare pieceListFb[MAX_LENGTH];
};

namespace Utility {

// Clamp a value between lo and hi. Available in c++17.
template<class T> constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

}

#endif // __cplusplus

#endif
