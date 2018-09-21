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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "eval.h"
#include "evalparams.h"
#include "validation.h"
#include "bitboard.h"
#include "hash.h"
#include "fen.h"
#include "utils.h"

/* Phase valuse for different piece types */
#define PAWN_PHASE      0
#define KNIGHT_PHASE    1
#define BISHOP_PHASE    1
#define ROOK_PHASE      2
#define QUEEN_PHASE     4

/* Attack weights for the different piece types */
#define KNIGHT_ATTACK_WEIGHT    1
#define BISHOP_ATTACK_WEIGHT    1
#define ROOK_ATTACK_WEIGHT      2
#define QUEEN_ATTACK_WEIGHT     4

/*
 * Different evaluation components. The first two elements of each array
 * is the evaluation for each side, and the third contains the summarized
 * evaluation of the specific component (from white's pov).
 */
struct eval {
    bool in_pawntt;
    struct pawntt_item pawntt;
    bool endgame[NSIDES];
    uint64_t coverage[NSIDES];
    int nbr_king_attackers[NPIECES];

    int material[NPHASES][NSIDES];
    int material_adj[NPHASES][NSIDES];
    int psq[NPHASES][NSIDES];
    int pawn_structure[NPHASES][NSIDES];
    int king_safety[NPHASES][NSIDES];
    int king_preassure[NPHASES][NSIDES];
    int positional[NPHASES][NSIDES];
    int mobility[NPHASES][NSIDES];

#ifdef TRACE
    struct eval_trace *trace;
#endif
};

/*
 * Table with mobility scores for the different pieces. The table is
 * initialized by the eval_reset function.
 */
static int mobility_table_mg[NPIECES];
static int mobility_table_eg[NPIECES];

/*
 * Table with scores for passed pawns based on rank. The table is
 * initialized by the eval_reset function.
 */
static int passed_pawn_scores_mg[NRANKS];
static int passed_pawn_scores_eg[NRANKS];

/*
 * Table with scores for candidate passed pawns based on rank. The table is
 * initialized by the eval_reset function.
 */
static int candidate_passed_pawn_scores_mg[NRANKS];
static int candidate_passed_pawn_scores_eg[NRANKS];

/* Table of attack weights for all pieces */
static int piece_attack_weights[NPIECES] = {
    0, 0,
    KNIGHT_ATTACK_WEIGHT, KNIGHT_ATTACK_WEIGHT,
    BISHOP_ATTACK_WEIGHT, BISHOP_ATTACK_WEIGHT,
    ROOK_ATTACK_WEIGHT, ROOK_ATTACK_WEIGHT,
    QUEEN_ATTACK_WEIGHT, QUEEN_ATTACK_WEIGHT,
    0, 0
};

/* Weights for the number of king attackers */
static int nbr_attackers_weight[6] = {
    0, 0, 45, 100, 100, 100
};

/*
 * Calculate a numerical value between 0 and 256 for
 * the current phase of the game. The formula is taken from
 * https://chessprogramming.wikispaces.com/Tapered+Eval
 */
static int calculate_game_phase(struct position *pos)
{
    int total_phase;
    int phase;

    total_phase = 24;
    phase = total_phase;
    phase -= BITCOUNT(pos->bb_pieces[WHITE_KNIGHT]);
    phase -= BITCOUNT(pos->bb_pieces[BLACK_KNIGHT]);
    phase -= BITCOUNT(pos->bb_pieces[WHITE_BISHOP]);
    phase -= BITCOUNT(pos->bb_pieces[BLACK_BISHOP]);
    phase -= 2*BITCOUNT(pos->bb_pieces[WHITE_ROOK]);
    phase -= 2*BITCOUNT(pos->bb_pieces[BLACK_ROOK]);
    phase -= 4*BITCOUNT(pos->bb_pieces[WHITE_QUEEN]);
    phase -= 4*BITCOUNT(pos->bb_pieces[BLACK_QUEEN]);
    phase = (phase*256 + (total_phase/2))/total_phase;

    /*
     * Guard against negative phase values. The phase value might
     * become negative in case of promotion.
     */
    return MAX(phase, 0);
}

/*
 * Calculate a score that is an interpolation of the middlegame and endgame
 * based on the current phase of the game.
 */
static int calculate_tapered_eval(int phase, int score_mg, int score_eg)
{
    return ((score_mg*(256 - phase)) + (score_eg*phase))/256;
}

/*
 * The number of moves it takes for a king to move
 * from one square to another.
 */
static int king_distance(int from, int to)
{
    int file_delta;
    int rank_delta;
    int nmoves;

    file_delta = abs(FILENR(to) - FILENR(from));
    rank_delta = abs(RANKNR(to) - RANKNR(from));

    nmoves = file_delta > rank_delta?rank_delta:file_delta;
    file_delta -= nmoves;
    rank_delta -= nmoves;
    nmoves += (file_delta + rank_delta);

    return nmoves;
}

static void evaluate_pawn_shield(struct position *pos, struct eval *eval,
                                 int side)
{
    uint64_t           pawns;
    struct pawntt_item *item;
    int                delta;
    int                k;

    item = &eval->pawntt;

    /* Queenside pawn shield */
    for (k=0;k<3;k++) {
        pawns = pos->bb_pieces[PAWN+side]&file_mask[FILE_A+k];
        if (pawns != 0ULL) {
            delta = (side==WHITE)?RANKNR(LSB(pawns)):7-RANKNR(MSB(pawns));
        } else {
            delta = 0;
        }
        item->pawn_shield[side][QUEENSIDE][k] = delta;
    }

    /* Kingside pawn shield */
    for (k=0;k<3;k++) {
        pawns = pos->bb_pieces[PAWN+side]&file_mask[FILE_F+k];
        if (pawns != 0ULL) {
            delta = (side==WHITE)?RANKNR(LSB(pawns)):7-RANKNR(MSB(pawns));
        } else {
            delta = 0;
        }
        item->pawn_shield[side][KINGSIDE][k] = delta;
    }
}

static bool is_backward_pawn(struct position *pos, int side, int sq)
{
    int      oside;
    int      file;
    int      rank;
    int      rank1;
    int      rank2;
    int      stop_sq;
    int      pass_sq;
    bool     home;
    uint64_t neighbours;
    uint64_t all_pawns;

    /* Setup set helper variables */
    oside = FLIP_COLOR(side);
    file = FILENR(sq);
    rank = RANKNR(sq);
    rank1 = (side == WHITE)?rank+1:rank-1;
    rank2 = (side == WHITE)?rank+2:rank-2;
    home = ((side == WHITE) && (rank == RANK_2)) ||
           ((side == BLACK) && (rank == RANK_7));
    all_pawns = pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN];

    /* Find friendly pawns on neighbouring files */
    neighbours = 0ULL;
    if (file != FILE_A) {
        neighbours |= file_mask[file-1];
    }
    if (file != FILE_H) {
        neighbours |= file_mask[file+1];
    }
    neighbours &= pos->bb_pieces[PAWN+side];

    /* Check if all neighbours are more advanced */
    if (!ISEMPTY(neighbours&rear_attackspan[side][sq])) {
        return false;
    }

    /*
     * Check if the pawn can be captured by another pawn. If it can
     * then it is considered backward because it can only get to
     * safety if it gets to move right now.
     */
    if (!ISEMPTY(bb_pawn_attacks_to(sq, oside)&pos->bb_pieces[PAWN+oside])) {
        return true;
    }

    /* Check if there is a friendly pawn that it can catch up to in one move */
    if (!ISEMPTY(neighbours&rank_mask[rank1])) {
        pass_sq = NO_SQUARE;
        stop_sq = (side==WHITE)?sq+8:sq-8;
    } else if (home && !ISEMPTY(neighbours&rank_mask[rank2])) {
        pass_sq = (side==WHITE)?sq+8:sq-8;
        stop_sq = (side==WHITE)?sq+16:sq-16;
    } else {
        return true;
    }

    /*
     * If there are pawns to catch up with then check if is
     * safe to do so. First step is to check that there are
     * no other pawns blocking the way.
     */
    if ((pass_sq != NO_SQUARE) && !ISEMPTY(all_pawns&sq_mask[pass_sq])) {
        return true;
    } else if (!ISEMPTY(all_pawns&sq_mask[stop_sq])) {
        return true;
    }

    /*
     * If there are no pawns blocking the way then verify that
     * there is no opposing pawn attacking the destination
     * square.
     */
    if (!ISEMPTY(bb_pawn_attacks_to(stop_sq, oside)&
                                                pos->bb_pieces[PAWN+oside])) {
        return true;
    } else if ((pass_sq != NO_SQUARE) &&
               !ISEMPTY(bb_pawn_attacks_to(pass_sq, oside)&
                                                pos->bb_pieces[PAWN+oside])) {
        /*
         * If the square that pawn jumps over is attacked then
         * it means that it can be captured en-passant on the
         * destination square.
         */
        return true;
    }

    return false;
}

/*
 * - double pawns
 * - isolated pawns
 * - passed pawns
 * - candidate passed pawns
 * - backward pawn
 * - pawn shield
 */
static void evaluate_pawn_structure(struct position *pos, struct eval *eval,
                                    int side)
{
    uint64_t            pieces;
    int                 sq;
    int                 file;
    int                 rank;
    int                 rel_rank;
    int                 oside;
    bool                isolated;
    uint64_t            attackspan;
    uint64_t            attackers;
    uint64_t            defenders;
    uint64_t            helpers;
    uint64_t            sentries;
    struct pawntt_item  *item;

    item = &eval->pawntt;
    oside = FLIP_COLOR(side);
    pieces = pos->bb_pieces[PAWN+side];
    while (pieces != 0ULL) {
        isolated = false;
        sq = POPBIT(&pieces);
        rank = RANKNR(sq);
        rel_rank = (side==WHITE)?rank:7-rank;
        attackspan = rear_attackspan[side][sq]|front_attackspan[side][sq];

        /* Look for isolated pawns */
        if ((attackspan&pos->bb_pieces[side+PAWN]) == 0ULL) {
            isolated = true;
            item->score[MIDDLEGAME][side] += ISOLATED_PAWN_MG;
            item->score[ENDGAME][side] += ISOLATED_PAWN_EG;
            TRACE_M(TP_ISOLATED_PAWN_MG, TP_ISOLATED_PAWN_EG, 1);
        }

        /* Look for passed pawns */
        if (ISEMPTY(front_attackspan[side][sq]&pos->bb_pieces[oside+PAWN]) &&
            ISEMPTY(front_span[side][sq]&pos->bb_pieces[oside+PAWN])) {
            SETBIT(item->passers, sq);
            item->score[MIDDLEGAME][side] += passed_pawn_scores_mg[rel_rank];
            item->score[ENDGAME][side] += passed_pawn_scores_eg[rel_rank];
            TRACE_OM(TP_PASSED_PAWN_RANK2_MG, TP_PASSED_PAWN_RANK2_EG,
                     rel_rank-1, 1);
        }

        /* Look for candidate passed pawns */
        sentries = front_attackspan[side][sq]&pos->bb_pieces[oside+PAWN];
        helpers = rear_attackspan[side][sq]&pos->bb_pieces[side+PAWN];
        attackers = bb_pawn_attacks_to(sq, oside)&pos->bb_pieces[oside+PAWN];
        defenders = bb_pawn_attacks_to(sq, side)&pos->bb_pieces[side+PAWN];
        if (!ISBITSET(item->passers&pos->bb_pieces[side], sq) &&
            ISEMPTY(front_span[side][sq]&pos->bb_pieces[oside+PAWN]) &&
            (BITCOUNT(helpers) >= BITCOUNT(sentries)) &&
            (BITCOUNT(defenders) >= BITCOUNT(attackers))) {
            SETBIT(item->candidates, sq);
            item->score[MIDDLEGAME][side] +=
                                    candidate_passed_pawn_scores_mg[rel_rank];
            item->score[ENDGAME][side] +=
                                    candidate_passed_pawn_scores_eg[rel_rank];
            TRACE_OM(TP_CANDIDATE_PASSED_PAWN_RANK2_MG,
                     TP_CANDIDATE_PASSED_PAWN_RANK2_EG, rel_rank-1, 1);
        }

        /* Check if the pawn is considered backward */
        if (!isolated && is_backward_pawn(pos, side, sq)) {
            item->score[MIDDLEGAME][side] += BACKWARD_PAWN_MG;
            item->score[ENDGAME][side] += BACKWARD_PAWN_EG;
            TRACE_M(TP_BACKWARD_PAWN_MG, TP_BACKWARD_PAWN_EG, 1);
        }

        /* Update pawn coverage */
        item->coverage[side] |= bb_pawn_attacks_from(sq, side);
    }

    /* Look for double pawns */
    for (file=0;file<NFILES;file++) {
        if (BITCOUNT(pos->bb_pieces[side+PAWN]&file_mask[file]) >= 2) {
            item->score[MIDDLEGAME][side] += DOUBLE_PAWNS_MG;
            item->score[ENDGAME][side] += DOUBLE_PAWNS_EG;
            TRACE_M(TP_DOUBLE_PAWNS_MG, TP_DOUBLE_PAWNS_EG, 1);
        }
    }

    /*
     * Calculate a pawnshield score. This score will be used
     * later when evaluating king safety.
     */
    evaluate_pawn_shield(pos, eval, side);
}

/*
 * Evaluate interaction between passed pawns and other pieces. The parts that
 * only depend on other pawns are evaluated by evaluate_pawn_structure.
 *
 * - king distance
 */
static void evaluate_passers(struct position *pos, struct eval *eval, int side)
{
    struct pawntt_item *item;
    uint64_t           passers;
    int                sq;
    int                to;
    int                dist;
    int                odist;

    item = &eval->pawntt;

    /* Iterate over all passers */
    passers = item->passers&pos->bb_pieces[side];
    while (passers != 0ULL) {
        sq = POPBIT(&passers);

        /*
         * Find the distance from each king to the square
         * directly in front of the pawn.
         */
        to = (side == WHITE)?sq+8:sq-8;
        dist = king_distance(LSB(pos->bb_pieces[KING+side]), to);
        odist = king_distance(LSB(pos->bb_pieces[KING+FLIP_COLOR(side)]), to);

        /* Calculate a score based on the distance to each king */
        eval->positional[ENDGAME][side] += OPPONENT_KING_PASSER_DIST*odist;
        eval->positional[ENDGAME][side] += FRIENDLY_KING_PASSER_DIST*dist;
        TRACE_M(-1, TP_FRIENDLY_KING_PASSER_DIST, dist);
        TRACE_M(-1, TP_OPPONENT_KING_PASSER_DIST, odist);
    }
}

/*
 * - mobility
 * - outposts
 */
static void evaluate_knights(struct position *pos, struct eval *eval, int side)
{
    uint64_t pieces;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t coverage;
    int      sq;
    int      king_sq;
    int      opp_side;

    /* Calculate mobility */
    coverage = 0ULL;
    pieces = pos->bb_pieces[KNIGHT+side];
    opp_side = FLIP_COLOR(side);
    king_sq = LSB(pos->bb_pieces[KING+opp_side]);
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        moves = bb_knight_moves(sq);
        coverage |= moves;
        moves &= (~pos->bb_sides[side]);

        /* Mobility */
        safe_moves = moves&(~eval->pawntt.coverage[FLIP_COLOR(side)]);
        eval->mobility[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_mg[KNIGHT+side]);
        eval->mobility[ENDGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_eg[KNIGHT+side]);
        TRACE_M(TP_KNIGHT_MOBILITY_MG, TP_KNIGHT_MOBILITY_EG,
                BITCOUNT(safe_moves));

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_attack_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[KNIGHT+side]++;
        }

        /* Outposts */
        if (sq_mask[sq]&outpost_squares[side] &&
            ((front_attackspan[side][sq]&pos->bb_pieces[opp_side+PAWN]) == 0)) {
            if (eval->pawntt.coverage[side]&sq_mask[sq]) {
                eval->positional[MIDDLEGAME][side] += PROTECTED_KNIGHT_OUTPOST;
                TRACE_M(TP_PROTECTED_KNIGHT_OUTPOST, -1, 1);
            } else {
                eval->positional[MIDDLEGAME][side] += KNIGHT_OUTPOST;
                TRACE_M(TP_KNIGHT_OUTPOST, -1, 1);
            }
        }
    }

    /* Update coverage */
    eval->coverage[side] |= coverage;
}

/*
 * - bishop pair
 * - mobility
 */
static void evaluate_bishops(struct position *pos, struct eval *eval, int side)
{
    uint64_t pieces;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t coverage;
    int      sq;
    int      king_sq;
    int      opp_side;

    /*
     * Check if both bishops are still on the board. To be correct
     * also check if the two (or more) bishops operate on different
     * color squares. The only case when a player can have
     * two bishops on the same color squares is if he underpromotes
     * to a bishop. This is so unlikely that it should be safe to assume
     * that the bishops operate on different color squares.
     */
    if (BITCOUNT(pos->bb_pieces[side+BISHOP]) >= 2) {
        eval->material_adj[MIDDLEGAME][side] += BISHOP_PAIR_MG;
        eval->material_adj[ENDGAME][side] += BISHOP_PAIR_EG;
        TRACE_M(TP_BISHOP_PAIR_MG, TP_BISHOP_PAIR_EG, 1);
    }

    /* Calculate mobility */
    coverage = 0ULL;
    pieces = pos->bb_pieces[BISHOP+side];
    opp_side = FLIP_COLOR(side);
    king_sq = LSB(pos->bb_pieces[KING+opp_side]);
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        moves = bb_bishop_moves(pos->bb_all, sq);
        coverage |= moves;
        moves &= (~pos->bb_sides[side]);

        /* Mobility */
        safe_moves = moves&(~eval->pawntt.coverage[FLIP_COLOR(side)]);
        eval->mobility[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_mg[BISHOP+side]);
        eval->mobility[ENDGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_eg[BISHOP+side]);
        TRACE_M(TP_BISHOP_MOBILITY_MG, TP_BISHOP_MOBILITY_EG,
                BITCOUNT(safe_moves));

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_attack_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[BISHOP+side]++;
        }
    }

    /* Update coverage */
    eval->coverage[side] |= coverage;
}

/*
 * - open and half-open files
 * - mobility
 */
static void evaluate_rooks(struct position *pos, struct eval *eval, int side)
{
    const uint64_t rank7[NSIDES] = {rank_mask[RANK_7], rank_mask[RANK_2]};
    const uint64_t rank8[NSIDES] = {rank_mask[RANK_8], rank_mask[RANK_1]};
    uint64_t pieces;
    uint64_t all_pawns;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t coverage;
    int      sq;
    int      file;
    int      king_sq;
    int      opp_side;

    coverage = 0ULL;
    all_pawns = pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN];
    pieces = pos->bb_pieces[ROOK+side];
    opp_side = FLIP_COLOR(side);
    king_sq = LSB(pos->bb_pieces[KING+opp_side]);
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        file = FILENR(sq);
        moves = bb_rook_moves(pos->bb_all, sq);
        coverage |= moves;
        moves &= (~pos->bb_sides[side]);

        /* Open and half-open files */
        if ((file_mask[file]&all_pawns) == 0ULL) {
            eval->positional[MIDDLEGAME][side] += ROOK_OPEN_FILE_MG;
            eval->positional[ENDGAME][side] += ROOK_OPEN_FILE_EG;
            TRACE_M(TP_ROOK_OPEN_FILE_MG, TP_ROOK_OPEN_FILE_EG, 1);
        } else if ((file_mask[file]&pos->bb_pieces[PAWN+side]) == 0ULL) {
            eval->positional[MIDDLEGAME][side] += ROOK_HALF_OPEN_FILE_MG;
            eval->positional[ENDGAME][side] += ROOK_HALF_OPEN_FILE_EG;
            TRACE_M(TP_ROOK_HALF_OPEN_FILE_MG, TP_ROOK_HALF_OPEN_FILE_EG, 1);
        }

        /* 7th rank */
        if (ISBITSET(rank7[side], sq)) {
            /*
             * Only give bonus if the enemy king is on the 8th rank
             * or if there are enenmy pawns on the 7th rank.
             */
            if ((pos->bb_pieces[KING+FLIP_COLOR(side)]&rank8[side]) ||
                (pos->bb_pieces[PAWN+FLIP_COLOR(side)]&rank7[side])) {
                eval->positional[MIDDLEGAME][side] += ROOK_ON_7TH_MG;
                eval->positional[ENDGAME][side] += ROOK_ON_7TH_EG;
                TRACE_M(TP_ROOK_ON_7TH_MG, TP_ROOK_ON_7TH_EG, 1);
            }
        }

        /* Mobility */
        safe_moves = moves&(~eval->pawntt.coverage[FLIP_COLOR(side)]);
        eval->mobility[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_mg[ROOK+side]);
        eval->mobility[ENDGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_eg[ROOK+side]);
        TRACE_M(TP_ROOK_MOBILITY_MG, TP_ROOK_MOBILITY_EG,
                BITCOUNT(safe_moves));

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_attack_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[ROOK+side]++;
        }
    }

    /* Update coverage */
    eval->coverage[side] |= coverage;
}

/*
 * - open and half-open files
 * - mobility
 */
static void evaluate_queens(struct position *pos, struct eval *eval, int side)
{
    uint64_t pieces;
    uint64_t all_pawns;
    uint64_t moves;
    uint64_t safe_moves;
    int      sq;
    int      file;
    int      king_sq;
    int      opp_side;

    all_pawns = pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN];
    pieces = pos->bb_pieces[QUEEN+side];
    opp_side = FLIP_COLOR(side);
    king_sq = LSB(pos->bb_pieces[KING+opp_side]);
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        file = FILENR(sq);
        moves = bb_queen_moves(pos->bb_all, sq)&(~pos->bb_sides[side]);

        /* Open and half-open files */
        if ((file_mask[file]&all_pawns) == 0ULL) {
            eval->positional[MIDDLEGAME][side] += QUEEN_OPEN_FILE_MG;
            eval->positional[ENDGAME][side] += QUEEN_OPEN_FILE_EG;
            TRACE_M(TP_QUEEN_OPEN_FILE_MG, TP_QUEEN_OPEN_FILE_EG, 1);
        } else if ((file_mask[file]&pos->bb_pieces[PAWN+side]) == 0ULL) {
            eval->positional[MIDDLEGAME][side] += QUEEN_HALF_OPEN_FILE_MG;
            eval->positional[ENDGAME][side] += QUEEN_HALF_OPEN_FILE_EG;
            TRACE_M(TP_QUEEN_HALF_OPEN_FILE_MG, TP_QUEEN_HALF_OPEN_FILE_EG, 1);
        }

        /* Mobility */
        safe_moves = moves&(~eval->coverage[FLIP_COLOR(side)]);
        eval->mobility[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_mg[QUEEN+side]);
        eval->mobility[ENDGAME][side] += (BITCOUNT(safe_moves)*
                                                mobility_table_eg[QUEEN+side]);
        TRACE_M(TP_QUEEN_MOBILITY_MG, TP_QUEEN_MOBILITY_EG,
                BITCOUNT(safe_moves));

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_attack_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[QUEEN+side]++;
        }
    }
}

/*
 * - pawn shield
 * - king preassure
 */
static void evaluate_king(struct position *pos, struct eval *eval, int side)
{
    uint64_t            queenside[NSIDES] = {
                                        sq_mask[A1]|sq_mask[B1]|sq_mask[C1],
                                        sq_mask[A8]|sq_mask[B8]|sq_mask[C8]};
    uint64_t            kingside[NSIDES] = {
                                        sq_mask[F1]|sq_mask[G1]|sq_mask[H1],
                                        sq_mask[F8]|sq_mask[G8]|sq_mask[H8]};
    int                 scores[] = {PAWN_SHIELD_HOLE, PAWN_SHIELD_RANK1,
                                    PAWN_SHIELD_RANK2};
    struct pawntt_item  *item;
    int                 piece;
    int                 nattackers;
    int                 score;
    bool                shield;
    int                 castling_side;
    int                 type;
    int                 k;

    /*
     * If the king has moved to the side then it is good to keep a
     * shield of pawns in front of it. The only exception is if
     * there is a rook between the king and the corner. In this case
     * keeping a pawn shield will get the rook trapped.
     */
    shield = false;
    item = &eval->pawntt;
    if (queenside[side]&pos->bb_pieces[KING+side]) {
        if (!((pos->bb_pieces[ROOK+side]&queenside[side]) &&
              (LSB(pos->bb_pieces[ROOK+side]&queenside[side]) <
               LSB(pos->bb_pieces[KING+side])))) {
            shield = true;
            castling_side = QUEENSIDE;
        }
    } else if (kingside[side]&pos->bb_pieces[KING+side]) {
        if (!((pos->bb_pieces[ROOK+side]&kingside[side]) &&
              (LSB(pos->bb_pieces[ROOK+side]&kingside[side]) >
               LSB(pos->bb_pieces[KING+side])))) {
            shield = true;
            castling_side = KINGSIDE;
        }
    }
    if (shield) {
        for (k=0;k<3;k++) {
            type = item->pawn_shield[side][castling_side][k];
            switch (type) {
            case 0:
                eval->king_safety[MIDDLEGAME][side] += scores[type];
                TRACE_M(TP_PAWN_SHIELD_HOLE, -1, 1);
                break;
            case 1:
            case 2:
                eval->king_safety[MIDDLEGAME][side] += scores[type];
                TRACE_OM(TP_PAWN_SHIELD_RANK1, -1, type-1, 1);
                break;
            default:
                break;
            }
        }
    }

    /*
     * In the end game it's more important to have an active king so
     * don't try to hide it behind a pawn shield.
     */
    eval->king_safety[ENDGAME][side] = 0;

    /* Calculate preassure on the enemy king */
    nattackers = 0;
    score = 0;
    eval->king_preassure[MIDDLEGAME][side] = 0;
    eval->king_preassure[ENDGAME][side] = 0;
    for (piece=KNIGHT+side;piece<NPIECES;piece+=2) {
        score += piece_attack_weights[piece]*eval->nbr_king_attackers[piece];
        nattackers += eval->nbr_king_attackers[piece];
    }
    if (nattackers >= (int)(sizeof(nbr_attackers_weight)/sizeof(int))) {
        nattackers = (sizeof(nbr_attackers_weight)/sizeof(int)) - 1;
    }
    score *= nbr_attackers_weight[nattackers];
    eval->king_preassure[MIDDLEGAME][side] = (score*KING_ATTACK_SCALE_MG)/100;
    eval->king_preassure[ENDGAME][side] = (score*KING_ATTACK_SCALE_EG)/100;
    TRACE_MD(TP_KING_ATTACK_SCALE_MG, TP_KING_ATTACK_SCALE_EG, score, 100);
}

static int do_eval_material(struct position *pos, struct eval *eval, int side,
                            bool endgame)
{
    int score;
    int piece;

    assert(valid_position(pos));
    assert(valid_side(side));

    (void)eval;

    score = 0;
    for (piece=side;piece<NPIECES;piece+=2) {
        if (!endgame) {
            score += BITCOUNT(pos->bb_pieces[piece])*material_values_mg[piece];
        } else {
            score += BITCOUNT(pos->bb_pieces[piece])*material_values_eg[piece];
        }
        TRACE_MATERIAL(piece, endgame, BITCOUNT(pos->bb_pieces[piece]));
    }

    return score;
}

static int do_eval_psq(struct position *pos, struct eval *eval, int side,
                       bool endgame)
{
    int      score;
    uint64_t pieces;
    int      sq;
    int      piece;

    assert(valid_position(pos));
    assert(valid_side(side));

    (void)eval;

    score = 0;
    pieces = pos->bb_sides[side];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        piece = pos->pieces[sq];
        if (side == BLACK) {
            sq = MIRROR(sq);
        }
        switch (piece) {
        case WHITE_PAWN:
        case BLACK_PAWN:
            score += endgame?PSQ_TABLE_PAWN_EG[sq]:PSQ_TABLE_PAWN_MG[sq];
            break;
        case WHITE_KNIGHT:
        case BLACK_KNIGHT:
            score += endgame?PSQ_TABLE_KNIGHT_EG[sq]:PSQ_TABLE_KNIGHT_MG[sq];
            break;
        case WHITE_BISHOP:
        case BLACK_BISHOP:
            score += endgame?PSQ_TABLE_BISHOP_EG[sq]:PSQ_TABLE_BISHOP_MG[sq];
            break;
        case WHITE_ROOK:
        case BLACK_ROOK:
            score += endgame?PSQ_TABLE_ROOK_EG[sq]:PSQ_TABLE_ROOK_MG[sq];
            break;
        case WHITE_QUEEN:
        case BLACK_QUEEN:
            score += endgame?PSQ_TABLE_QUEEN_EG[sq]:PSQ_TABLE_QUEEN_MG[sq];
            break;
        case WHITE_KING:
        case BLACK_KING:
            score += endgame?PSQ_TABLE_KING_EG[sq]:PSQ_TABLE_KING_MG[sq];
            break;
        default:
            break;
        }
        TRACE_PSQ(piece, sq, endgame);
    }

    return score;
}

static void do_eval(struct search_worker *worker, struct position *pos,
                    struct eval *eval)
{
    int k;

    memset(eval, 0, sizeof(struct eval));

    /* Copy scores that were updated iteratively to the eval struct */
    for (k=0;k<NPHASES;k++) {
        eval->psq[k][WHITE] = pos->psq[k][WHITE];
        eval->psq[k][BLACK] = pos->psq[k][BLACK];
        eval->material[k][WHITE] = pos->material[k][WHITE];
        eval->material[k][BLACK] = pos->material[k][BLACK];
    }

    /* Check if the position is present in the pawn transposition table */
    eval->in_pawntt = (worker != NULL)?
                                hash_pawntt_lookup(worker, &eval->pawntt):false;

    /* Evaluate the position from each sides pov */
    if (!eval->in_pawntt) {
        hash_pawntt_init_item(&eval->pawntt);
        evaluate_pawn_structure(pos, eval, WHITE);
        evaluate_pawn_structure(pos, eval, BLACK);
    }
    eval->coverage[WHITE] |= eval->pawntt.coverage[WHITE];
    eval->coverage[BLACK] |= eval->pawntt.coverage[BLACK];
    evaluate_knights(pos, eval, WHITE);
    evaluate_knights(pos, eval, BLACK);
    evaluate_bishops(pos, eval, WHITE);
    evaluate_bishops(pos, eval, BLACK);
    evaluate_rooks(pos, eval, WHITE);
    evaluate_rooks(pos, eval, BLACK);
    evaluate_queens(pos, eval, WHITE);
    evaluate_queens(pos, eval, BLACK);
    evaluate_king(pos, eval, WHITE);
    evaluate_king(pos, eval, BLACK);
    evaluate_passers(pos, eval, WHITE);
    evaluate_passers(pos, eval, BLACK);

    /*
     * Update the evaluation scores with information from
     * the pawn tranposition table. If the position was not
     * found then the item field have been updated during the
     * evaluation and therefore contains the correct information.
     */
    for (k=0;k<NPHASES;k++) {
        eval->pawn_structure[k][WHITE] = eval->pawntt.score[k][WHITE];
        eval->pawn_structure[k][BLACK] = eval->pawntt.score[k][BLACK];
    }

    /* Update the pawn hash table */
    if (!eval->in_pawntt && (worker != NULL)) {
        hash_pawntt_store(worker, &eval->pawntt);
    }
}

void eval_reset(void)
{
    /* Initialize the mobility table */
    mobility_table_mg[WHITE_PAWN] = 0;
    mobility_table_mg[BLACK_PAWN] = 0;
    mobility_table_mg[WHITE_KNIGHT] = KNIGHT_MOBILITY_MG;
    mobility_table_mg[BLACK_KNIGHT] = KNIGHT_MOBILITY_MG;
    mobility_table_mg[WHITE_BISHOP] = BISHOP_MOBILITY_MG;
    mobility_table_mg[BLACK_BISHOP] = BISHOP_MOBILITY_MG;
    mobility_table_mg[WHITE_ROOK] = ROOK_MOBILITY_MG;
    mobility_table_mg[BLACK_ROOK] = ROOK_MOBILITY_MG;
    mobility_table_mg[WHITE_QUEEN] = QUEEN_MOBILITY_MG;
    mobility_table_mg[BLACK_QUEEN] = QUEEN_MOBILITY_MG;
    mobility_table_mg[WHITE_KING] = 0;
    mobility_table_mg[BLACK_KING] = 0;
    mobility_table_eg[WHITE_PAWN] = 0;
    mobility_table_eg[BLACK_PAWN] = 0;
    mobility_table_eg[WHITE_KNIGHT] = KNIGHT_MOBILITY_EG;
    mobility_table_eg[BLACK_KNIGHT] = KNIGHT_MOBILITY_EG;
    mobility_table_eg[WHITE_BISHOP] = BISHOP_MOBILITY_EG;
    mobility_table_eg[BLACK_BISHOP] = BISHOP_MOBILITY_EG;
    mobility_table_eg[WHITE_ROOK] = ROOK_MOBILITY_EG;
    mobility_table_eg[BLACK_ROOK] = ROOK_MOBILITY_EG;
    mobility_table_eg[WHITE_QUEEN] = QUEEN_MOBILITY_EG;
    mobility_table_eg[BLACK_QUEEN] = QUEEN_MOBILITY_EG;
    mobility_table_eg[WHITE_KING] = 0;
    mobility_table_eg[BLACK_KING] = 0;

    /* Initialize the passed pawns table */
    passed_pawn_scores_mg[RANK_1] = 0;
    passed_pawn_scores_mg[RANK_2] = PASSED_PAWN_RANK2_MG;
    passed_pawn_scores_mg[RANK_3] = PASSED_PAWN_RANK3_MG;
    passed_pawn_scores_mg[RANK_4] = PASSED_PAWN_RANK4_MG;
    passed_pawn_scores_mg[RANK_5] = PASSED_PAWN_RANK5_MG;
    passed_pawn_scores_mg[RANK_6] = PASSED_PAWN_RANK6_MG;
    passed_pawn_scores_mg[RANK_7] = PASSED_PAWN_RANK7_MG;
    passed_pawn_scores_mg[RANK_8] = 0;
    passed_pawn_scores_eg[RANK_1] = 0;
    passed_pawn_scores_eg[RANK_2] = PASSED_PAWN_RANK2_EG;
    passed_pawn_scores_eg[RANK_3] = PASSED_PAWN_RANK3_EG;
    passed_pawn_scores_eg[RANK_4] = PASSED_PAWN_RANK4_EG;
    passed_pawn_scores_eg[RANK_5] = PASSED_PAWN_RANK5_EG;
    passed_pawn_scores_eg[RANK_6] = PASSED_PAWN_RANK6_EG;
    passed_pawn_scores_eg[RANK_7] = PASSED_PAWN_RANK7_EG;
    passed_pawn_scores_eg[RANK_8] = 0;

    /* Initialize the candidate pawns table */
    candidate_passed_pawn_scores_mg[RANK_1] = 0;
    candidate_passed_pawn_scores_mg[RANK_2] = CANDIDATE_PASSED_PAWN_RANK2_MG;
    candidate_passed_pawn_scores_mg[RANK_3] = CANDIDATE_PASSED_PAWN_RANK3_MG;
    candidate_passed_pawn_scores_mg[RANK_4] = CANDIDATE_PASSED_PAWN_RANK4_MG;
    candidate_passed_pawn_scores_mg[RANK_5] = CANDIDATE_PASSED_PAWN_RANK5_MG;
    candidate_passed_pawn_scores_mg[RANK_6] = CANDIDATE_PASSED_PAWN_RANK6_MG;
    candidate_passed_pawn_scores_mg[RANK_7] = 0;
    candidate_passed_pawn_scores_mg[RANK_8] = 0;
    candidate_passed_pawn_scores_eg[RANK_1] = 0;
    candidate_passed_pawn_scores_eg[RANK_2] = CANDIDATE_PASSED_PAWN_RANK2_EG;
    candidate_passed_pawn_scores_eg[RANK_3] = CANDIDATE_PASSED_PAWN_RANK3_EG;
    candidate_passed_pawn_scores_eg[RANK_4] = CANDIDATE_PASSED_PAWN_RANK4_EG;
    candidate_passed_pawn_scores_eg[RANK_5] = CANDIDATE_PASSED_PAWN_RANK5_EG;
    candidate_passed_pawn_scores_eg[RANK_6] = CANDIDATE_PASSED_PAWN_RANK6_EG;
    candidate_passed_pawn_scores_eg[RANK_7] = 0;
    candidate_passed_pawn_scores_eg[RANK_8] = 0;

    /* Initialize the material table */
    material_values_mg[WHITE_PAWN] = PAWN_BASE_VALUE;
    material_values_mg[BLACK_PAWN] = PAWN_BASE_VALUE;
    material_values_mg[WHITE_KNIGHT] = KNIGHT_MATERIAL_VALUE_MG;
    material_values_mg[BLACK_KNIGHT] = KNIGHT_MATERIAL_VALUE_MG;
    material_values_mg[WHITE_BISHOP] = BISHOP_MATERIAL_VALUE_MG;
    material_values_mg[BLACK_BISHOP] = BISHOP_MATERIAL_VALUE_MG;
    material_values_mg[WHITE_ROOK] = ROOK_MATERIAL_VALUE_MG;
    material_values_mg[BLACK_ROOK] = ROOK_MATERIAL_VALUE_MG;
    material_values_mg[WHITE_QUEEN] = QUEEN_MATERIAL_VALUE_MG;
    material_values_mg[BLACK_QUEEN] = QUEEN_MATERIAL_VALUE_MG;
    material_values_mg[WHITE_KING] = 20000;
    material_values_mg[BLACK_KING] = 20000;
    material_values_eg[WHITE_PAWN] = PAWN_BASE_VALUE;
    material_values_eg[BLACK_PAWN] = PAWN_BASE_VALUE;
    material_values_eg[WHITE_KNIGHT] = KNIGHT_MATERIAL_VALUE_EG;
    material_values_eg[BLACK_KNIGHT] = KNIGHT_MATERIAL_VALUE_EG;
    material_values_eg[WHITE_BISHOP] = BISHOP_MATERIAL_VALUE_EG;
    material_values_eg[BLACK_BISHOP] = BISHOP_MATERIAL_VALUE_EG;
    material_values_eg[WHITE_ROOK] = ROOK_MATERIAL_VALUE_EG;
    material_values_eg[BLACK_ROOK] = ROOK_MATERIAL_VALUE_EG;
    material_values_eg[WHITE_QUEEN] = QUEEN_MATERIAL_VALUE_EG;
    material_values_eg[BLACK_QUEEN] = QUEEN_MATERIAL_VALUE_EG;
    material_values_eg[WHITE_KING] = 20000;
    material_values_eg[BLACK_KING] = 20000;
}

int eval_evaluate(struct search_worker *worker)
{
    struct eval eval;
    int         k;
    int         phase;
    int         score[NPHASES];

    assert(valid_position(&worker->pos));
    assert(valid_scores(&worker->pos));

    /*
     * If no player have enough material left
     * to checkmate then it's a draw.
     */
    if (eval_is_material_draw(&worker->pos)) {
        return 0;
    }

    /* Evaluate the position */
    do_eval(worker, &worker->pos, &eval);

    /* Summarize each evaluation term from side to moves's pov */
    for (k=0;k<NPHASES;k++) {
        score[k] = 0;

        score[k] += eval.material[k][WHITE];
        score[k] += eval.material_adj[k][WHITE];
        score[k] += eval.psq[k][WHITE];
        score[k] += eval.pawn_structure[k][WHITE];
        score[k] += eval.king_safety[k][WHITE];
        score[k] += eval.king_preassure[k][WHITE];
        score[k] += eval.positional[k][WHITE];
        score[k] += eval.mobility[k][WHITE];

        score[k] -= eval.material[k][BLACK];
        score[k] -= eval.material_adj[k][BLACK];
        score[k] -= eval.psq[k][BLACK];
        score[k] -= eval.pawn_structure[k][BLACK];
        score[k] -= eval.king_safety[k][BLACK];
        score[k] -= eval.king_preassure[k][BLACK];
        score[k] -= eval.positional[k][BLACK];
        score[k] -= eval.mobility[k][BLACK];

        score[k] = (worker->pos.stm == WHITE)?score[k]:-score[k];
    }

    /* Return score adjusted for game phase */
    phase = calculate_game_phase(&worker->pos);
    return calculate_tapered_eval(phase, score[MIDDLEGAME], score[ENDGAME]);
}

int eval_evaluate_full(struct position *pos, bool display)
{
    struct eval eval;
    int         k;
    int         phase;
    int         score;
    int         sum[NPHASES][NSIDES];
    int         sum_white_pov[NPHASES];
    int         material[NPHASES];
    int         material_adj[NPHASES];
    int         psq[NPHASES];
    int         pawn_structure[NPHASES];
    int         king_safety[NPHASES];
    int         king_preassure[NPHASES];
    int         positional[NPHASES];
    int         mobility[NPHASES];

    assert(valid_position(pos));
    assert(valid_scores(pos));

    /*
     * If no player have enough material left
     * to checkmate then it's a draw.
     */
    if (eval_is_material_draw(pos)) {
        if (display) {
            printf("Draw by insufficient material\n");
            printf("Score: 0\n");
        }
        return 0;
    }

    /* Evaluate the position */
    do_eval(NULL, pos, &eval);

    /* Summarize each evaluation term from white's pov */
    for (k=0;k<NPHASES;k++) {
        material[k] = eval.material[k][WHITE];
        material[k] -= eval.material[k][BLACK];

        material_adj[k] = eval.material_adj[k][WHITE];
        material_adj[k] -= eval.material_adj[k][BLACK];

        psq[k] = eval.psq[k][WHITE];
        psq[k] -= eval.psq[k][BLACK];

        pawn_structure[k] = eval.pawn_structure[k][WHITE];
        pawn_structure[k] -= eval.pawn_structure[k][BLACK];

        king_safety[k] = eval.king_safety[k][WHITE];
        king_safety[k] -= eval.king_safety[k][BLACK];

        king_preassure[k] = eval.king_preassure[k][WHITE];
        king_preassure[k] -= eval.king_preassure[k][BLACK];

        positional[k] = eval.positional[k][WHITE];
        positional[k] -= eval.positional[k][BLACK];

        mobility[k] = eval.mobility[k][WHITE];
        mobility[k] -= eval.mobility[k][BLACK];
    }

    /* Summarize the evaluation terms for each side */
    for (k=0;k<NPHASES;k++) {
        sum[k][WHITE] = eval.material[k][WHITE] + eval.material_adj[k][WHITE] +
                        eval.psq[k][WHITE] + eval.pawn_structure[k][WHITE] +
                        eval.king_safety[k][WHITE] +
                        eval.king_preassure[k][WHITE] +
                        eval.positional[k][WHITE] + eval.mobility[k][WHITE];
        sum[k][BLACK] = eval.material[k][BLACK] + eval.material_adj[k][BLACK] +
                        eval.psq[k][BLACK] + eval.pawn_structure[k][BLACK] +
                        eval.king_safety[k][BLACK] +
                        eval.king_preassure[k][BLACK] +
                        eval.positional[k][BLACK] + eval.mobility[k][BLACK];
        sum_white_pov[k] = sum[k][WHITE] - sum[k][BLACK];
    }

    /* Adjust score for game phase */
    phase = calculate_game_phase(pos);
    score = calculate_tapered_eval(phase, sum_white_pov[MIDDLEGAME],
                                   sum_white_pov[ENDGAME]);
    if (!display) {
        return (pos->stm == WHITE)?score:-score;
    }

    /* Print the evaluation */
    printf("  Evaluation Term       White        Black         Total\n");
    printf("                      MG     EG    MG     EG     MG     EG\n");
    printf("-------------------------------------------------------------\n");
    printf("Material                                      %5d   %5d\n",
           material[MIDDLEGAME], material[ENDGAME]);
    printf("Material adjustment %5d  %5d %5d  %5d %5d   %5d\n",
           eval.material_adj[MIDDLEGAME][WHITE],
           eval.material_adj[ENDGAME][WHITE],
           eval.material_adj[MIDDLEGAME][BLACK],
           eval.material_adj[ENDGAME][BLACK],
           material_adj[MIDDLEGAME],
           material_adj[ENDGAME]);
    printf("Piece/square tables %5d  %5d %5d  %5d %5d   %5d\n",
           eval.psq[MIDDLEGAME][WHITE], eval.psq[ENDGAME][WHITE],
           eval.psq[MIDDLEGAME][BLACK], eval.psq[ENDGAME][BLACK],
           psq[MIDDLEGAME], psq[ENDGAME]);
    printf("Pawn structure      %5d  %5d %5d  %5d %5d   %5d\n",
           eval.pawn_structure[MIDDLEGAME][WHITE],
           eval.pawn_structure[ENDGAME][WHITE],
           eval.pawn_structure[MIDDLEGAME][BLACK],
           eval.pawn_structure[ENDGAME][BLACK],
           pawn_structure[MIDDLEGAME],
           pawn_structure[ENDGAME]);
    printf("King safety         %5d  %5d %5d  %5d %5d   %5d\n",
           eval.king_safety[MIDDLEGAME][WHITE],
           eval.king_safety[ENDGAME][WHITE],
           eval.king_safety[MIDDLEGAME][BLACK],
           eval.king_safety[ENDGAME][BLACK],
           king_safety[MIDDLEGAME],
           king_safety[ENDGAME]);
    printf("King preassure      %5d  %5d %5d  %5d %5d   %5d\n",
           eval.king_preassure[MIDDLEGAME][WHITE],
           eval.king_preassure[ENDGAME][WHITE],
           eval.king_preassure[MIDDLEGAME][BLACK],
           eval.king_preassure[ENDGAME][BLACK],
           king_preassure[MIDDLEGAME],
           king_preassure[ENDGAME]);
    printf("Positional themes   %5d  %5d %5d  %5d %5d   %5d\n",
           eval.positional[MIDDLEGAME][WHITE], eval.positional[ENDGAME][WHITE],
           eval.positional[MIDDLEGAME][BLACK], eval.positional[ENDGAME][BLACK],
           positional[MIDDLEGAME], positional[ENDGAME]);
    printf("Mobility            %5d  %5d %5d  %5d %5d   %5d\n",
           eval.mobility[MIDDLEGAME][WHITE], eval.mobility[ENDGAME][WHITE],
           eval.mobility[MIDDLEGAME][BLACK], eval.mobility[ENDGAME][BLACK],
           mobility[MIDDLEGAME], mobility[ENDGAME]);
    printf("-------------------------------------------------------------\n");
    printf("Total                                         %5d   %5d\n",
           sum_white_pov[MIDDLEGAME], sum_white_pov[ENDGAME]);
    printf("\n");
    printf("Game phase: %d [0, 256]\n", phase);
    printf("Score:      %d (for white)\n", score);

    return (pos->stm == WHITE)?score:-score;
}

int eval_material(struct position *pos, int side, bool endgame)
{
#ifdef TRACE
    struct eval eval;

    memset(&eval, 0, sizeof(struct eval));
    return do_eval_material(pos, &eval, side, endgame);
#else
    return do_eval_material(pos, NULL, side, endgame);
#endif
}

void eval_update_material_score(struct position *pos, int add, int piece)
{
    int delta;
    int color;

    assert(valid_position(pos));
    assert(valid_piece(piece));

    delta = add?1:-1;
    color = COLOR(piece);
    switch (piece) {
    case WHITE_PAWN:
    case BLACK_PAWN:
        pos->material[MIDDLEGAME][color] += (delta*material_values_mg[piece]);
        pos->material[ENDGAME][color] += (delta*material_values_eg[piece]);
        break;
    case WHITE_KNIGHT:
    case BLACK_KNIGHT:
        pos->material[MIDDLEGAME][color] += (delta*material_values_mg[piece]);
        pos->material[ENDGAME][color] += (delta*material_values_eg[piece]);
        break;
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        pos->material[MIDDLEGAME][color] += (delta*material_values_mg[piece]);
        pos->material[ENDGAME][color] += (delta*material_values_eg[piece]);
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        pos->material[MIDDLEGAME][color] += (delta*material_values_mg[piece]);
        pos->material[ENDGAME][color] += (delta*material_values_eg[piece]);
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        pos->material[MIDDLEGAME][color] += (delta*material_values_mg[piece]);
        pos->material[ENDGAME][color] += (delta*material_values_eg[piece]);
        break;
    case WHITE_KING:
    case BLACK_KING:
        break;
    default:
        assert(false);
        break;
    }
}

int eval_psq(struct position *pos, int side, bool endgame)
{
#ifdef TRACE
    struct eval eval;

    memset(&eval, 0, sizeof(struct eval));
    return do_eval_psq(pos, &eval, side, endgame);
#else
    return do_eval_psq(pos, NULL, side, endgame);
#endif
}

void eval_update_psq_score(struct position *pos, int add, int piece, int sq)
{
    int delta;
    int color;

    assert(valid_position(pos));
    assert(valid_piece(piece));
    assert(valid_square(sq));

    delta = add?1:-1;
    sq = (COLOR(piece)==WHITE)?sq:MIRROR(sq);
    color = COLOR(piece);
    switch (piece) {
    case WHITE_PAWN:
    case BLACK_PAWN:
        pos->psq[MIDDLEGAME][color] += (delta*PSQ_TABLE_PAWN_MG[sq]);
        pos->psq[ENDGAME][color] += (delta*PSQ_TABLE_PAWN_EG[sq]);
        break;
    case WHITE_KNIGHT:
    case BLACK_KNIGHT:
        pos->psq[MIDDLEGAME][color] += (delta*PSQ_TABLE_KNIGHT_MG[sq]);
        pos->psq[ENDGAME][color] += (delta*PSQ_TABLE_KNIGHT_EG[sq]);
        break;
    case WHITE_BISHOP:
    case BLACK_BISHOP:
        pos->psq[MIDDLEGAME][color] += (delta*PSQ_TABLE_BISHOP_MG[sq]);
        pos->psq[ENDGAME][color] += (delta*PSQ_TABLE_BISHOP_EG[sq]);
        break;
    case WHITE_ROOK:
    case BLACK_ROOK:
        pos->psq[MIDDLEGAME][color] += (delta*PSQ_TABLE_ROOK_MG[sq]);
        pos->psq[ENDGAME][color] += (delta*PSQ_TABLE_ROOK_EG[sq]);
        break;
    case WHITE_QUEEN:
    case BLACK_QUEEN:
        pos->psq[MIDDLEGAME][color] += (delta*PSQ_TABLE_QUEEN_MG[sq]);
        pos->psq[ENDGAME][color] += (delta*PSQ_TABLE_QUEEN_EG[sq]);
        break;
    case WHITE_KING:
    case BLACK_KING:
        pos->psq[MIDDLEGAME][color] += (delta*PSQ_TABLE_KING_MG[sq]);
        pos->psq[ENDGAME][color] += (delta*PSQ_TABLE_KING_EG[sq]);
        break;
    default:
        assert(false);
        break;
    }
}

/*
 * The following combination of pieces can never lead to chekmate:
 * - King vs King
 * - King+Knight vs King
 * - King+Bishops vs King (if the bishops operate on the same color squares)
 */
bool eval_is_material_draw(struct position *pos)
{
    int wb;
    int wn;
    int bb;
    int bn;

    if ((pos->bb_pieces[WHITE_PAWN] != 0ULL) ||
        (pos->bb_pieces[BLACK_PAWN] != 0ULL) ||
        (pos->bb_pieces[WHITE_ROOK] != 0ULL) ||
        (pos->bb_pieces[BLACK_ROOK] != 0ULL) ||
        (pos->bb_pieces[WHITE_QUEEN] != 0ULL) ||
        (pos->bb_pieces[BLACK_QUEEN] != 0ULL)) {
        return false;
    }

    wn = BITCOUNT(pos->bb_pieces[WHITE_KNIGHT]);
    bn = BITCOUNT(pos->bb_pieces[BLACK_KNIGHT]);
    wb = BITCOUNT(pos->bb_pieces[WHITE_BISHOP]);
    bb = BITCOUNT(pos->bb_pieces[BLACK_BISHOP]);

    /* King vs King */
    if ((wn == 0) && (bn == 0) && (wb == 0) && (bb == 0)) {
        return true;
    }
    /* Knight+King vs King */
    if ((wn == 1) && (bn == 0) && (wb == 0) && (bb == 0)) {
        return true;
    }
    /* King vs King+Knight */
    if ((wn == 0) && (bn == 1) && (wb == 0) && (bb == 0)) {
        return true;
    }
    /* King+Bishops vs King */
    if ((wn == 0) && (bn == 0) && (wb > 0) && (bb == 0)) {
        return !(((pos->bb_pieces[WHITE_BISHOP]&white_square_mask) != 0ULL) &&
                    ((pos->bb_pieces[WHITE_BISHOP]&black_square_mask) != 0ULL));
    }
    /* King vs King+Bishops */
    if ((wn == 0) && (bn == 0) && (wb == 0) && (bb > 0)) {
        return !(((pos->bb_pieces[BLACK_BISHOP]&white_square_mask) != 0ULL) &&
                    ((pos->bb_pieces[BLACK_BISHOP]&black_square_mask) != 0ULL));
    }

    return false;
}

#ifdef TRACE
void eval_generate_trace(struct position *pos, struct eval_trace *trace)
{
    struct eval eval;

    assert(valid_position(pos));
    assert(trace != NULL);

    /* Clear the trace */
    memset(trace, 0, sizeof(struct eval_trace));
    memset(&eval, 0, sizeof(struct eval));
    eval.trace = trace;

    /* Calculate game phase */
    trace->phase = calculate_game_phase(pos);

    /* If there is insufficiernt mating material there is nothing to do */
    if (eval_is_material_draw(pos)) {
        return;
    }

    /* Trace material evaluation */
    pos->material[MIDDLEGAME][WHITE] = do_eval_material(pos, &eval, WHITE,
                                                        false);
    pos->material[MIDDLEGAME][BLACK] = do_eval_material(pos, &eval, BLACK,
                                                        false);
    pos->material[ENDGAME][WHITE] = do_eval_material(pos, &eval, WHITE, true);
    pos->material[ENDGAME][BLACK] = do_eval_material(pos, &eval, BLACK, true);

    /* Trace psq evaluation */
    pos->psq[MIDDLEGAME][WHITE] = do_eval_psq(pos, &eval, WHITE, false);
    pos->psq[MIDDLEGAME][BLACK] = do_eval_psq(pos, &eval, BLACK, false);
    pos->psq[ENDGAME][WHITE] = do_eval_psq(pos, &eval, WHITE, true);
    pos->psq[ENDGAME][BLACK] = do_eval_psq(pos, &eval, BLACK, true);

    /* Trace pawn structure evaluation */
    hash_pawntt_init_item(&eval.pawntt);
    evaluate_pawn_structure(pos, &eval, WHITE);
    evaluate_pawn_structure(pos, &eval, BLACK);
    eval.coverage[WHITE] |= eval.pawntt.coverage[WHITE];
    eval.coverage[BLACK] |= eval.pawntt.coverage[BLACK];

    /* Trace piece evaluation */
    evaluate_knights(pos, &eval, WHITE);
    evaluate_knights(pos, &eval, BLACK);
    evaluate_bishops(pos, &eval, WHITE);
    evaluate_bishops(pos, &eval, BLACK);
    evaluate_rooks(pos, &eval, WHITE);
    evaluate_rooks(pos, &eval, BLACK);
    evaluate_queens(pos, &eval, WHITE);
    evaluate_queens(pos, &eval, BLACK);
    evaluate_king(pos, &eval, WHITE);
    evaluate_king(pos, &eval, BLACK);
    evaluate_passers(pos, &eval, WHITE);
    evaluate_passers(pos, &eval, BLACK);
}
#endif
