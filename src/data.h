/*
 * Marvin - an UCI/XBoard compatible chess engine
 * Copyright (C) 2023 Martin Danielsson
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
#ifndef DATA_H
#define DATA_H

#include <stdint.h>

#include "types.h"

/* Bitboard mask for each square */
extern uint64_t sq_mask[NSQUARES];

/* Bitboard mask for white squares */
extern uint64_t white_square_mask;

/* Bitboard mask for black squares */
extern uint64_t black_square_mask;

/* Masks for all ranks */
extern uint64_t rank_mask[NRANKS];

/* Mask for all ranks realtive to each side */
extern uint64_t relative_rank_mask[NSIDES][NRANKS];

/* Masks for all files */
extern uint64_t file_mask[NFILES];

/* Masks for all diagonals in the a1h8 direction */
extern uint64_t a1h8_masks[NDIAGONALS];

/* Masks for all diagonals in the a8h1 direction */
extern uint64_t a8h1_masks[NDIAGONALS];

/*
 * Bitboard of the front attackspan of a specific square
 *
 * x.x.....
 * x.x.....
 * x.x.....
 * x.x.....
 * x.x.....
 * x.x.....
 * .w......
 * ........
 * ........
 */
extern uint64_t front_attackspan[NSIDES][NSQUARES];

/*
 * Bitboard of the rear attackspan of a specific square
 *
 * ........
 * ........
 * ........
 * ........
 * ........
 * ........
 * xwx.....
 * x.x.....
 * x.x.....
 */
extern uint64_t rear_attackspan[NSIDES][NSQUARES];

/*
 * Bitboard of the front span of a specific square
 *
 * .x......
 * .x......
 * .x......
 * .x......
 * .x......
 * .x......
 * .w......
 * ........
 * ........
 */
extern uint64_t front_span[NSIDES][NSQUARES];

/*
 * Bitboard of the rear span of a specific square
 *
 * ........
 * ........
 * ........
 * .w......
 * .x......
 * .x......
 * .x......
 * .x......
 * .x......
 */
extern uint64_t rear_span[NSIDES][NSQUARES];

/*
 * Masks for the king zone for all sides/squares. The king zone
 * is defined as illustrated below:
 *
 *  xxx
 *  xKx
 *  xxx
 */
extern uint64_t king_zone[NSIDES][NSQUARES];

/* Character representation for each piece */
extern char piece2char[NPIECES+1];

/* Table mapping a square to an A1H8 diagonal index */
extern int sq2diag_a1h8[NSQUARES];

/* Table mapping a square to an A8H1 diagonal index */
extern int sq2diag_a8h1[NSQUARES];

/* Table containing the square color for all squares on the chess table */
extern int sq_color[NSQUARES];

/* Masks of squares that are considered possible outposts */
extern uint64_t outpost_squares[NSIDES];

/*
 * Bitboard of the squares considered for space evaluation
 *
 * ........
 * ........
 * ........
 * ........
 * .xxxxxx.
 * .xxxxxx.
 * .xxxxxx.
 * ........
 */
extern uint64_t space_eval_squares[NSIDES];

/* Destination square for the king when doing king side castling */
extern int kingside_castle_to[NSIDES];

/* Destination square for the king when doing queen side castling */
extern int queenside_castle_to[NSIDES];

/* Phase independent material values used during search */
extern int material_values[NPIECES];

/*
 * Bitboards for the rank, file or diagonal where both squares are
 * located. If the squares are not on the same rank, file or diagonal
 * then the bitboard is empty.
 */
extern uint64_t line_mask[NSQUARES][NSQUARES];

/* Initialize global data */
void data_init(void);

#endif
