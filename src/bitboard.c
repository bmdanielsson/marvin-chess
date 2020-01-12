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

#include "bitboard.h"
#include "validation.h"

/* Shift to use for rook magics */
#define ROOK_BITS_SHIFT 52

/* Shift to use for bishop magics */
#define BISHOP_BITS_SHIFT 55

/*
 * Magics for rooks. The magics have been generated
 * by Pradyumna (Pradu) Kannan.
 * http://www.pradu.us/old/Nov27_2008/Buzz/research/magic/magics.txt
 */
static const unsigned long long rook_magics[NSQUARES]=
{
    0x0080001020400080ULL, 0x0040001000200040ULL,
    0x0080081000200080ULL, 0x0080040800100080ULL,
    0x0080020400080080ULL, 0x0080010200040080ULL,
    0x0080008001000200ULL, 0x0080002040800100ULL,
    0x0000800020400080ULL, 0x0000400020005000ULL,
    0x0000801000200080ULL, 0x0000800800100080ULL,
    0x0000800400080080ULL, 0x0000800200040080ULL,
    0x0000800100020080ULL, 0x0000800040800100ULL,
    0x0000208000400080ULL, 0x0000404000201000ULL,
    0x0000808010002000ULL, 0x0000808008001000ULL,
    0x0000808004000800ULL, 0x0000808002000400ULL,
    0x0000010100020004ULL, 0x0000020000408104ULL,
    0x0000208080004000ULL, 0x0000200040005000ULL,
    0x0000100080200080ULL, 0x0000080080100080ULL,
    0x0000040080080080ULL, 0x0000020080040080ULL,
    0x0000010080800200ULL, 0x0000800080004100ULL,
    0x0000204000800080ULL, 0x0000200040401000ULL,
    0x0000100080802000ULL, 0x0000080080801000ULL,
    0x0000040080800800ULL, 0x0000020080800400ULL,
    0x0000020001010004ULL, 0x0000800040800100ULL,
    0x0000204000808000ULL, 0x0000200040008080ULL,
    0x0000100020008080ULL, 0x0000080010008080ULL,
    0x0000040008008080ULL, 0x0000020004008080ULL,
    0x0000010002008080ULL, 0x0000004081020004ULL,
    0x0000204000800080ULL, 0x0000200040008080ULL,
    0x0000100020008080ULL, 0x0000080010008080ULL,
    0x0000040008008080ULL, 0x0000020004008080ULL,
    0x0000800100020080ULL, 0x0000800041000080ULL,
    0x00FFFCDDFCED714AULL, 0x007FFCDDFCED714AULL,
    0x003FFFCDFFD88096ULL, 0x0000040810002101ULL,
    0x0001000204080011ULL, 0x0001000204000801ULL,
    0x0001000082000401ULL, 0x0001FFFAABFAD1A2ULL
};

/*
 * Occupancy masks for rooks. Each bit sets represent a blocker for the
 * rooks movement. Edge squares are not included since they are always
 * blockers.
 *
 * Eaxmple:
 * Mask for rook on A1 include squares A2-A7 and B1-G1
 *
 *   ........
 *   x.......
 *   x.......
 *   x.......
 *   x.......
 *   x.......
 *   x.......
 *   Rxxxxxx.
 */
static const unsigned long long magic_rook_mask[NSQUARES]=
{
    0x000101010101017EULL, 0x000202020202027CULL,
    0x000404040404047AULL, 0x0008080808080876ULL,
    0x001010101010106EULL, 0x002020202020205EULL,
    0x004040404040403EULL, 0x008080808080807EULL,
    0x0001010101017E00ULL, 0x0002020202027C00ULL,
    0x0004040404047A00ULL, 0x0008080808087600ULL,
    0x0010101010106E00ULL, 0x0020202020205E00ULL,
    0x0040404040403E00ULL, 0x0080808080807E00ULL,
    0x00010101017E0100ULL, 0x00020202027C0200ULL,
    0x00040404047A0400ULL, 0x0008080808760800ULL,
    0x00101010106E1000ULL, 0x00202020205E2000ULL,
    0x00404040403E4000ULL, 0x00808080807E8000ULL,
    0x000101017E010100ULL, 0x000202027C020200ULL,
    0x000404047A040400ULL, 0x0008080876080800ULL,
    0x001010106E101000ULL, 0x002020205E202000ULL,
    0x004040403E404000ULL, 0x008080807E808000ULL,
    0x0001017E01010100ULL, 0x0002027C02020200ULL,
    0x0004047A04040400ULL, 0x0008087608080800ULL,
    0x0010106E10101000ULL, 0x0020205E20202000ULL,
    0x0040403E40404000ULL, 0x0080807E80808000ULL,
    0x00017E0101010100ULL, 0x00027C0202020200ULL,
    0x00047A0404040400ULL, 0x0008760808080800ULL,
    0x00106E1010101000ULL, 0x00205E2020202000ULL,
    0x00403E4040404000ULL, 0x00807E8080808000ULL,
    0x007E010101010100ULL, 0x007C020202020200ULL,
    0x007A040404040400ULL, 0x0076080808080800ULL,
    0x006E101010101000ULL, 0x005E202020202000ULL,
    0x003E404040404000ULL, 0x007E808080808000ULL,
    0x7E01010101010100ULL, 0x7C02020202020200ULL,
    0x7A04040404040400ULL, 0x7608080808080800ULL,
    0x6E10101010101000ULL, 0x5E20202020202000ULL,
    0x3E40404040404000ULL, 0x7E80808080808000ULL
};

/*
 * Magics for bishops. The magics have been generated
 * by Pradyumna (Pradu) Kannan.
 * http://www.pradu.us/old/Nov27_2008/Buzz/research/magic/magics.txt
 */
static const unsigned long long bishop_magics[NSQUARES]=
{
    0x0002020202020200ULL, 0x0002020202020000ULL,
    0x0004010202000000ULL, 0x0004040080000000ULL,
    0x0001104000000000ULL, 0x0000821040000000ULL,
    0x0000410410400000ULL, 0x0000104104104000ULL,
    0x0000040404040400ULL, 0x0000020202020200ULL,
    0x0000040102020000ULL, 0x0000040400800000ULL,
    0x0000011040000000ULL, 0x0000008210400000ULL,
    0x0000004104104000ULL, 0x0000002082082000ULL,
    0x0004000808080800ULL, 0x0002000404040400ULL,
    0x0001000202020200ULL, 0x0000800802004000ULL,
    0x0000800400A00000ULL, 0x0000200100884000ULL,
    0x0000400082082000ULL, 0x0000200041041000ULL,
    0x0002080010101000ULL, 0x0001040008080800ULL,
    0x0000208004010400ULL, 0x0000404004010200ULL,
    0x0000840000802000ULL, 0x0000404002011000ULL,
    0x0000808001041000ULL, 0x0000404000820800ULL,
    0x0001041000202000ULL, 0x0000820800101000ULL,
    0x0000104400080800ULL, 0x0000020080080080ULL,
    0x0000404040040100ULL, 0x0000808100020100ULL,
    0x0001010100020800ULL, 0x0000808080010400ULL,
    0x0000820820004000ULL, 0x0000410410002000ULL,
    0x0000082088001000ULL, 0x0000002011000800ULL,
    0x0000080100400400ULL, 0x0001010101000200ULL,
    0x0002020202000400ULL, 0x0001010101000200ULL,
    0x0000410410400000ULL, 0x0000208208200000ULL,
    0x0000002084100000ULL, 0x0000000020880000ULL,
    0x0000001002020000ULL, 0x0000040408020000ULL,
    0x0004040404040000ULL, 0x0002020202020000ULL,
    0x0000104104104000ULL, 0x0000002082082000ULL,
    0x0000000020841000ULL, 0x0000000000208800ULL,
    0x0000000010020200ULL, 0x0000000404080200ULL,
    0x0000040404040400ULL, 0x0002020202020200ULL
};

/*
 * Occupancy masks for bishops. Each bit sets represent a blocker for the
 * bishops movement. Edge squares are not included since they are always
 * blockers.
 *
 * Eaxmple:
 * Mask for bishop on C1 include squares B2, D2, E3, F4 and G5
 *
 *   ........
 *   ........
 *   ........
 *   ......x.
 *   .....x..
 *   ....x...
 *   .x.x....
 *   ..B.....
 */
static const unsigned long long magic_bishop_mask[NSQUARES]=
{
    0x0040201008040200ULL, 0x0000402010080400ULL,
    0x0000004020100A00ULL, 0x0000000040221400ULL,
    0x0000000002442800ULL, 0x0000000204085000ULL,
    0x0000020408102000ULL, 0x0002040810204000ULL,
    0x0020100804020000ULL, 0x0040201008040000ULL,
    0x00004020100A0000ULL, 0x0000004022140000ULL,
    0x0000000244280000ULL, 0x0000020408500000ULL,
    0x0002040810200000ULL, 0x0004081020400000ULL,
    0x0010080402000200ULL, 0x0020100804000400ULL,
    0x004020100A000A00ULL, 0x0000402214001400ULL,
    0x0000024428002800ULL, 0x0002040850005000ULL,
    0x0004081020002000ULL, 0x0008102040004000ULL,
    0x0008040200020400ULL, 0x0010080400040800ULL,
    0x0020100A000A1000ULL, 0x0040221400142200ULL,
    0x0002442800284400ULL, 0x0004085000500800ULL,
    0x0008102000201000ULL, 0x0010204000402000ULL,
    0x0004020002040800ULL, 0x0008040004081000ULL,
    0x00100A000A102000ULL, 0x0022140014224000ULL,
    0x0044280028440200ULL, 0x0008500050080400ULL,
    0x0010200020100800ULL, 0x0020400040201000ULL,
    0x0002000204081000ULL, 0x0004000408102000ULL,
    0x000A000A10204000ULL, 0x0014001422400000ULL,
    0x0028002844020000ULL, 0x0050005008040200ULL,
    0x0020002010080400ULL, 0x0040004020100800ULL,
    0x0000020408102000ULL, 0x0000040810204000ULL,
    0x00000A1020400000ULL, 0x0000142240000000ULL,
    0x0000284402000000ULL, 0x0000500804020000ULL,
    0x0000201008040200ULL, 0x0000402010080400ULL,
    0x0002040810204000ULL, 0x0004081020400000ULL,
    0x000A102040000000ULL, 0x0014224000000000ULL,
    0x0028440200000000ULL, 0x0050080402000000ULL,
    0x0020100804020000ULL, 0x0040201008040200ULL
};

/*
 * Database of rook moves. The second dimensions is the number
 * of possible occupancy combinations for a rook on a given square.
 */
static uint64_t rook_moves_db[NSQUARES][1<<12];

/*
 * Database of bishop moves. The second dimensions is the number
 * of possible occupancy combinations for a bishop on a given square.
 */
static uint64_t bishop_moves_db[NSQUARES][1<<9];

/* Arrays containing bitboards for all possible king moves */
static uint64_t king_moves_table[NSQUARES];

/* Arrays containing bitboards for all possible knight moves */
static uint64_t knight_moves_table[NSQUARES];

/*
 * Arrays containing bitboards for all possible pawn
 * moves (excluding captures).
 */
static uint64_t pawn_moves_table[NSQUARES][NSIDES];

/*
 * Arrays containing bitboards for all possible pawn
 * captures (excluding en-passant captures). For example
 * bitboard for white pawn on C6 contains B7 and D7.
 */
static uint64_t pawn_attacks_from_table[NSQUARES][NSIDES];

/*
 * Arrays containing bitboards for all possible squares that pawns
 * can attack a given square from. For example bitboard for white
 * pawn on C6 contains B5 and D5.
 */
static uint64_t pawn_attacks_to_table[NSQUARES][NSIDES];

/*
 * Generate a bitboard with all possible slider moves in a specified direction
 * for a given square/occupancy combination.
 */
static uint64_t get_slider_moves(int sq, int fdir, int rdir, uint64_t occ)
{
    uint64_t moves = 0ULL;
    int      target;
    int      rank;
    int      file;

    file = FILENR(sq) + fdir;
    rank = RANKNR(sq) + rdir;
    while (!SQUAREOFFBOARD(file, rank)) {
        target = SQUARE(file, rank);
        SETBIT(moves, target);
        if (ISBITSET(occ, target)) {
            break;
        }
        file += fdir;
        rank += rdir;
    }

    return moves;
}

/* Get the occupancy combination sqecified by index */
static uint64_t get_occupancy_combination(int index, uint64_t *occbits,
                                          int nblockers)
{
    uint64_t occ;
    int      k;

    /* Iterate over all blockers */
    occ = 0ULL;
    for (k=0;k<nblockers;k++) {
        /*
         * If the a bit is set in the index for this blocker
         * then it should be included in the occupancy bitboard.
         */
        if ((index&(1<<k)) != 0) {
            occ |= occbits[k];
        }
    }
    return occ;
}

/*
 * Initialize the magic bitboard databases for rooks and bishops. A good
 * description about how magic bitboards can be found at:
 * http://www.afewmorelines.com/understanding-magic-bitboards-in-chess-programming/
 *
 * The databases contains a bitboard with possible moves for each
 * square/occupancy combination.
 */
static void init_magic_databases(void)
{
    uint64_t occbits[12];
    uint64_t mask;
    uint64_t occ;
    uint64_t moves;
    int      index;
    int      bit;
    int      nblockers;
    int      sq;
    int      k;
    int      nocc;

    /* Populate the bishop database */
    for (sq=0;sq<NSQUARES;sq++) {
        /* Setup this iteration */
        nblockers = 0;
        mask = magic_bishop_mask[sq];

        /*
         * Separate the bits of the occupancy mask into separate
         * bitboards where each bitboard only has one square set.
         * A mask can have at most 12 bits set since that is the
         * maximum number of possible blockers for a rook. For a
         * bishop the maximum number of occupancy combinations
         * is 9.
         */
        while (mask != 0ULL) {
            bit = pop_bit(&mask);
            occbits[nblockers++] = 1ULL << bit;
        }
        assert(nblockers <= 9);

        /*
         * Calculate how many possible occupancy
         * combinations there are.
         */
        nocc = 1 << nblockers;

        /*
         * Iterate over all possible occupancy combinations and generate a
         * bitboard with bishop moves for each combination and store it in
         * the database.
         */
        for (k=0;k<nocc;k++) {
            occ = get_occupancy_combination(k, occbits, nblockers);
            moves = get_slider_moves(sq, -1, 1, occ);
            moves |= get_slider_moves(sq, 1, 1, occ);
            moves |= get_slider_moves(sq, -1, -1, occ);
            moves |= get_slider_moves(sq, 1, -1, occ);
            index = (occ*bishop_magics[sq])>>BISHOP_BITS_SHIFT;
            bishop_moves_db[sq][index] = moves;
        }
    }

    /* Populate the rook database */
    for (sq=0;sq<NSQUARES;sq++) {
        /* Setup this iteration */
        nblockers = 0;
        mask = magic_rook_mask[sq];

        /*
         * Separate the bits of the occupancy mask into separate
         * bitboards where each bitboard only has one square set.
         * A mask can have at most 12 bits set since that is the
         * maximum number of possible blockers for a rook. For a
         * bishop the maximum number of occupancy combinations
         * is 9.
         */
        while (mask != 0ULL) {
            bit = pop_bit(&mask);
            occbits[nblockers++] = 1ULL << bit;
        }
        assert(nblockers <= 12);

        /*
         * Calculate how many possible occupancy
         * combinations there are.
         */
        nocc = 1 << nblockers;

        /*
         * Iterate over all possible occupancy combinations and generate a
         * bitboard with rook moves for each combination and store it in
         * the database.
         */
        for (k=0;k<nocc;k++) {
            occ = get_occupancy_combination(k, occbits, nblockers);
            moves = get_slider_moves(sq, 1, 0, occ);
            moves |= get_slider_moves(sq, -1, 0, occ);
            moves |= get_slider_moves(sq, 0, 1, occ);
            moves |= get_slider_moves(sq, 0, -1, occ);
            index = (occ*rook_magics[sq])>>ROOK_BITS_SHIFT;
            rook_moves_db[sq][index] = moves;
        }
    }
}

static void precalc_pawn_moves(void)
{
    int sq;
    int rank;
    int file;

    /* White pawns */
    for (sq=0;sq<NSQUARES;sq++) {
        rank = RANKNR(sq);
        file = FILENR(sq);

        pawn_moves_table[sq][WHITE] = 0ULL;
        pawn_attacks_from_table[sq][WHITE] = 0ULL;
        pawn_attacks_to_table[sq][WHITE] = 0ULL;

        /* Handle first and last rank separately */
        if (rank == RANK_1) {
            continue;
        } else if (rank == RANK_8) {
            if (file != FILE_A) {
                SETBIT(pawn_attacks_to_table[sq][WHITE], sq-9);
            }
            if (file != FILE_H) {
                SETBIT(pawn_attacks_to_table[sq][WHITE], sq-7);
            }
            continue;
        }

        if (file != FILE_A) {
            SETBIT(pawn_attacks_to_table[sq][WHITE], sq-9);
            SETBIT(pawn_attacks_from_table[sq][WHITE], sq+7);
        }
        if (file != FILE_H) {
            SETBIT(pawn_attacks_to_table[sq][WHITE], sq-7);
            SETBIT(pawn_attacks_from_table[sq][WHITE], sq+9);
        }

        SETBIT(pawn_moves_table[sq][WHITE], sq+8);

        if (rank == RANK_2) {
            SETBIT(pawn_moves_table[sq][WHITE], sq+16);
        }
    }

    /* Black pawns */
    for (sq=0;sq<NSQUARES;sq++) {
        rank = RANKNR(sq);
        file = FILENR(sq);

        pawn_moves_table[sq][BLACK] = 0ULL;
        pawn_attacks_from_table[sq][BLACK] = 0ULL;
        pawn_attacks_to_table[sq][BLACK] = 0ULL;

        /* Handle first and last rank separately */
        if (rank == RANK_8) {
            continue;
        } else if (rank == RANK_1) {
            if (file != FILE_A) {
                SETBIT(pawn_attacks_to_table[sq][BLACK], sq+7);
            }
            if (file != FILE_H) {
                SETBIT(pawn_attacks_to_table[sq][BLACK], sq+9);
            }
            continue;
        }

        if (file != FILE_A) {
            SETBIT(pawn_attacks_to_table[sq][BLACK], sq+7);
            SETBIT(pawn_attacks_from_table[sq][BLACK], sq-9);
        }
        if (file != FILE_H) {
            SETBIT(pawn_attacks_to_table[sq][BLACK], sq+9);
            SETBIT(pawn_attacks_from_table[sq][BLACK], sq-7);
        }

        SETBIT(pawn_moves_table[sq][BLACK], sq-8);

        if (rank == RANK_7) {
            SETBIT(pawn_moves_table[sq][BLACK], sq-16);
        }
    }
}

static void precalc_king_moves(void)
{
    int offset_file[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    int offset_rank[8] = {0, -1, -1, -1, 0, 1, 1, 1};
    int sq;
    int ray;
    int file;
    int rank;

    for (sq=0;sq<NSQUARES;sq++) {
        king_moves_table[sq] = 0ULL;
        for (ray=0;ray<8;ray++) {
            file = FILENR(sq) + offset_file[ray];
            rank = RANKNR(sq) + offset_rank[ray];
            if ((file > -1) && (file < 8) &&
                (rank > -1) && (rank < 8)) {
                SETBIT(king_moves_table[sq], SQUARE(file, rank));
            }
        }
    }
}

static void precalc_knight_moves(void)
{
	int offset_file[8] = {2, 1, -1, -2, -2, -1, 1, 2};
    int offset_rank[8] = {-1, -2, -2, -1, 1, 2, 2, 1};
    int sq;
    int type;
    int file;
    int rank;

    for (sq=0;sq<NSQUARES;sq++) {
        knight_moves_table[sq] = 0ULL;
        for (type=0;type<8;type++) {
            file = FILENR(sq) + offset_file[type];
            rank = RANKNR(sq) + offset_rank[type];
            if ((file > -1) && (file < 8) &&
                (rank > -1) && (rank < 8)) {
                SETBIT(knight_moves_table[sq], SQUARE(file, rank));
            }
        }
    }
}

void bb_init(void)
{
    init_magic_databases();
    precalc_pawn_moves();
    precalc_king_moves();
    precalc_knight_moves();
}

uint64_t bb_pawn_moves(uint64_t occ, int from, int side)
{
    uint64_t moves;
    uint64_t mask;
    int      rank;

    assert(valid_square(from));
    assert(valid_side(side));

    moves = pawn_moves_table[from][side];

    /* Mask off moves where the destination square is blocked */
    moves &= (~occ);

    /*
     * If the square is on the 2nd or 7th rank then we also need to mask of
     * moves two step forward where 3rd or 6th rank is blocked.
     */
    rank = RANKNR(from);
    if ((side == WHITE) && (rank == RANK_2)) {
        mask = rank_mask[RANK_3];
        mask &= occ;
        mask <<= 8;
        moves &= (~mask);
    } else if ((side == BLACK) && (rank == RANK_7)) {
        mask = rank_mask[RANK_6];
        mask &= occ;
        mask >>= 8;
        moves &= (~mask);
    }

    return moves;
}

uint64_t bb_pawn_moves_to(uint64_t occ, int to, int side)
{
    uint64_t moves;
    int      rank;

    rank = RANKNR(to);
    moves = 0ULL;
    if (side == WHITE) {
        if (rank > RANK_2) {
            moves |= sq_mask[to-8];
        }
        if ((rank == RANK_4) && ((occ&sq_mask[to-8]) == 0)) {
            moves |= sq_mask[to-16];
        }
    } else {
        if (rank < RANK_7) {
            moves |= sq_mask[to+8];
        }
        if ((rank == RANK_5) && ((occ&sq_mask[to+8]) == 0)) {
            moves |= sq_mask[to+16];
        }
    }

    return moves;
}

uint64_t bb_pawn_attacks_from(int from, int side)
{
    assert(valid_square(from));
    assert(valid_side(side));

    return pawn_attacks_from_table[from][side];
}

uint64_t bb_pawn_attacks_to(int to, int side)
{
    assert(valid_square(to));
    assert(valid_side(side));

    return pawn_attacks_to_table[to][side];
}

uint64_t bb_knight_moves(int from)
{
    assert(valid_square(from));

    return knight_moves_table[from];
}

uint64_t bb_bishop_moves(uint64_t occ, int from)
{
    uint64_t index;

    assert(valid_square(from));

    index = occ&magic_bishop_mask[from];
    index *= bishop_magics[from];
    index >>= BISHOP_BITS_SHIFT;
    assert(index < (1<<9));

    return bishop_moves_db[from][index];
}

uint64_t bb_rook_moves(uint64_t occ, int from)
{
    uint64_t index;

    assert(valid_square(from));

    index = occ&magic_rook_mask[from];
    index *= rook_magics[from];
    index >>= ROOK_BITS_SHIFT;
    assert(index < (1<<12));

    return rook_moves_db[from][index];
}

uint64_t bb_queen_moves(uint64_t occ, int from)
{
    assert(valid_square(from));

    return bb_rook_moves(occ, from)|bb_bishop_moves(occ, from);
}

uint64_t bb_king_moves(int from)
{
    assert(valid_square(from));

    return king_moves_table[from];
}

uint64_t bb_attacks_to(struct position *pos, uint64_t occ, int to, int side)
{
    uint64_t attacks;

    assert(valid_position(pos));
    assert(valid_square(to));
    assert(valid_side(side));

    /* Generate attacks */
    attacks = 0ULL;
    attacks |= king_moves_table[to]&(pos->bb_pieces[side+KING]);
    attacks |= (bb_queen_moves(occ, to)&(pos->bb_pieces[side+QUEEN]));
    attacks |= (bb_rook_moves(occ, to)&(pos->bb_pieces[side+ROOK]));
    attacks |= (bb_bishop_moves(occ, to)&(pos->bb_pieces[side+BISHOP]));
    attacks |= knight_moves_table[to]&(pos->bb_pieces[side+KNIGHT]);
    attacks |= pawn_attacks_to_table[to][side]&(pos->bb_pieces[side+PAWN]);

    return attacks;
}

bool bb_is_attacked(struct position *pos, int square, int side)
{
    assert(valid_position(pos));
    assert(valid_square(square));
    assert(valid_side(side));

    return (bb_attacks_to(pos, pos->bb_all, square, side) != 0ULL);
}

uint64_t bb_slider_moves(uint64_t occ, int from, int fdelta, int rdelta)
{
    assert(valid_square(from));
    assert(fdelta == 0 || fdelta == 1 || fdelta == -1);
    assert(rdelta == 0 || rdelta == 1 || rdelta == -1);

    return get_slider_moves(from, fdelta, rdelta, occ);
}

uint64_t bb_moves_for_piece(uint64_t occ, int from, int piece)
{
    uint64_t moves;

    assert(valid_square(from));
    assert(valid_piece(piece));

    switch (piece) {
    case WHITE_PAWN:
    case BLACK_PAWN:
        moves = bb_pawn_moves(occ, from, COLOR(piece));
        break;
    case WHITE_KNIGHT:
    case BLACK_KNIGHT:
        moves = bb_knight_moves(from);
        break;
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        moves = bb_bishop_moves(occ, from);
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        moves = bb_rook_moves(occ, from);
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        moves = bb_queen_moves(occ, from);
        break;
    case WHITE_KING:
    case BLACK_KING:
        moves = bb_king_moves(from);
        break;
    default:
        assert(false);
        moves = 0ULL;
        break;
    }

    return moves;
}

uint64_t bb_pawn_pushes(uint64_t pawns, uint64_t occ, int side)
{
    uint64_t push = 0ULL;

    if (side == WHITE) {
        push = (pawns<<8)&(~occ);
        push |= (((push&rank_mask[RANK_3])<<8)&(~occ));
    } else {
        push = (pawns>>8)&(~occ);
        push |= (((push&rank_mask[RANK_6])>>8)&(~occ));
    }

    return push;
}

uint64_t bb_pawn_attacks(uint64_t pawns, int side)
{
    uint64_t attack = 0ULL;

    if (side == WHITE) {
        attack = (pawns&(~file_mask[FILE_A]))<<7;
        attack |= ((pawns&(~file_mask[FILE_H]))<<9);
    } else {
        attack = (pawns&(~file_mask[FILE_A]))>>9;
        attack |= ((pawns&(~file_mask[FILE_H]))>>7);
    }

    return attack;
}
