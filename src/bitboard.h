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
#ifndef BITBOARD_H
#define BITBOARD_H

#include <stdint.h>
#include <stdbool.h>

#include "chess.h"
#include "utils.h"

/* Set a bit in a bitboard */
#define SETBIT(bb, sq) bb |= sq_mask[(sq)]

/* Clear a bit in a bitboard */
#define CLEARBIT(bb, sq) bb &= ~(sq_mask[(sq)])

/* Check if a bit is set in a bitboard */
#define ISBITSET(bb, sq) ((bb&(sq_mask[(sq)])) != 0ULL)

/* Check if a bitboard has no bits set */
#define ISEMPTY(bb) ((bb) == 0ULL)

/* Count the number of set bits in a bitboard */
#define BITCOUNT(bb) pop_count((bb))

/* Pop a bit from a bitboard */
#define POPBIT(pbb) pop_bit(pbb);

/* Isolate a single bit in a bitboard */
#define ISOLATE(bb) ((bb) & -(bb))

/*
 * Gets the least significant bit of a bitboard. The macro does not
 * work if the bitboard is zero.
 */
#define LSB(bb) bitscan_forward((bb))

/*
 * Gets the most significant bit of a bitboard. The macro does not
 * work if the bitboard is zero.
 */
#define MSB(bb) bitscan_reverse((bb))

/* Initialize the bitboard component */
void bb_init(void);

/*
 * Generate a bitboard of pawn moves (excluding captures).
 *
 * @param occ Bitboard of all occupied squares.
 * @param from Location of the piece.
 * @param side The side to generate attacks for.
 * @return Bitboard of possible moves.
 */
uint64_t bb_pawn_moves(uint64_t occ, int from, int side);

/*
 * Generate a bitboard of all pawns that can move to a given square.
 *
 * @param occ Bitboard of all occupied squares.
 * @param to The target square.
 * @param side The side to generate moves for.
 * @return Bitboard of possible moves.
 */
uint64_t bb_pawn_moves_to(uint64_t occ, int to, int side);

/*
 * Generate a bitboard of pawn attacks from a given square.
 *
 * @param from Location of the piece.
 * @param side The side to generate attacks for.
 * @return Bitboard of possible moves.
 */
uint64_t bb_pawn_attacks_from(int from, int side);

/*
 * Generate a bitboard of pawn attacks to a given square.
 *
 * @param to The square to attack.
 * @param side The side to generate attacks for.
 * @return Bitboard of possible moves.
 */
uint64_t bb_pawn_attacks_to(int to, int side);

/*
 * Generate a bitboard of knight moves.
 *
 * @param from Location of the piece.
 * @return Bitboard of possible moves. The bitboard includes captures of
 *         friendly pieces which should be masked off.
 */
uint64_t bb_knight_moves(int from);

/*
 * Generate a bitboard of bishop moves.
 *
 * @param occ Bitboard of all occupied squares.
 * @param from Location of the piece.
 * @return Bitboard of possible moves. The bitboard includes captures of
 *         friendly pieces which should be masked off.
 */
uint64_t bb_bishop_moves(uint64_t occ, int from);

/*
 * Generate a bitboard of rook moves.
 *
 * @param occ Bitboard of all occupied squares.
 * @param from Location of the piece.The bitboard includes captures of
 *         friendly pieces which should be masked off.
 */
uint64_t bb_rook_moves(uint64_t occ, int from);

/*
 * Generate a bitboard of queen moves.
 *
 * @param occ Bitboard of all occupied squares.
 * @param from Location of the piece.
 * @return Bitboard of possible moves. The bitboard includes captures of
 *         friendly pieces which should be masked off.
 */
uint64_t bb_queen_moves(uint64_t occ, int from);

/*
 * Generate a bitboard of king moves.
 *
 * @param from Location of the piece.
 * @return Bitboard of possible moves. The bitboard includes captures of
 *         friendly pieces which should be masked off.
 */
uint64_t bb_king_moves(int from);

/*
 * Get all attacks to a specific square.
 *
 * @param pos The current position.
 * @param occ Bitboard of all occupied squares.
 * @param to The square to get attacks to.
 * @param side The side to get attacks for.
 * @return Bitboard of all attacks.
 */
uint64_t bb_attacks_to(struct position *pos, uint64_t occ, int to, int side);

/*
 * Tests if the given square is attacked.
 *
 * @param pos The current position.
 * @param square The square under consideration.
 * @param side The side of the attacker.
 * @return Returns true if the given square is attacked.
 */
bool bb_is_attacked(struct position *pos, int square, int side);

/*
 * Generate a bitboard of all possible slider moves from a given square
 * in a given direction.
 *
 * @param occ Bitboard of all occupied squares.
 * @param from The square to calculate slider moves from.
 * @param fdelta The file delta of the direction.
 * @param rdelta The rank delta of the direction.
 * @return Bitboard of all slider moves.
 */
uint64_t bb_slider_moves(uint64_t occ, int from, int fdelta, int rdelta);

/*
 * Generate a bitboard of all possible moves piece a piece
 * on a specific square.
 *
 * @param occ Bitboard of all occupied squares.
 * @param from The square to calculate moves from.
 * @param piece The piece to calculate moves for.
 * @return Bitboard of all moves.
 */
uint64_t bb_moves_for_piece(uint64_t occ, int from, int piece);

/*
 * Generate a bitboard of all possible pawn pushes.
 *
 * @param pawns Bitboard of all pawns.
 * @param occ Bitboard of all occupied squares.
 * @param side The side to generate pawn pushes for.
 */
uint64_t bb_pawn_pushes(uint64_t pawns, uint64_t occ, int side);

/*
 * Generate a bitboard of all possible pawn attacks.
 *
 * @param pawns Bitboard of all pawns.
 * @param side The side to generate pawn attacks for.
 */
uint64_t bb_pawn_attacks(uint64_t pawns, int side);

#endif
