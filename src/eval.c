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

#include "eval.h"
#include "evalparams.h"
#include "validation.h"
#include "bitboard.h"
#include "hash.h"
#include "fen.h"
#include "utils.h"
#include "trace.h"

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
 * Material values for all pieces. The table is
 * initialized by the eval_reset function.
 */
static int material_values_mg[NPIECES];
static int material_values_eg[NPIECES];

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
static int calculate_game_phase(struct gamestate *pos)
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

static void evaluate_pawn_shield(struct gamestate *pos,
                                 struct pawntt_item *item, int side)
{
    const uint64_t rank1[NSIDES] = {rank_mask[RANK_2], rank_mask[RANK_7]};
    const uint64_t rank2[NSIDES] = {rank_mask[RANK_3], rank_mask[RANK_6]};
    uint64_t pawns;
    uint64_t shield1;
    uint64_t shield2;
    int      score;

    pawns = pos->bb_pieces[PAWN+side];

    /* Queenside pawn shield */
    score = 0;
    shield1 = pawns&file_mask[FILE_A]&rank1[side];
    shield2 = pawns&file_mask[FILE_A]&rank2[side];
    if (shield1 != 0ULL) {
        score += PAWN_SHIELD_RANK1;
        TRACE_TRACK_PAWN_SHIELD(FILE_A, TP_PAWN_SHIELD_RANK1);
    } else if (shield2 != 0ULL) {
        score += PAWN_SHIELD_RANK2;
        TRACE_TRACK_PAWN_SHIELD(FILE_A, TP_PAWN_SHIELD_RANK2);
    }
    shield1 = pawns&file_mask[FILE_B]&rank1[side];
    shield2 = pawns&file_mask[FILE_B]&rank2[side];
    if (shield1 != 0ULL) {
        score += PAWN_SHIELD_RANK1;
        TRACE_TRACK_PAWN_SHIELD(FILE_B, TP_PAWN_SHIELD_RANK1);
    } else if (shield2 != 0ULL) {
        score += PAWN_SHIELD_RANK2;
        TRACE_TRACK_PAWN_SHIELD(FILE_B, TP_PAWN_SHIELD_RANK2);
    }
    shield1 = pawns&file_mask[FILE_C]&rank1[side];
    shield2 = pawns&file_mask[FILE_C]&rank2[side];
    if (shield1 != 0ULL) {
        score += PAWN_SHIELD_RANK1;
        TRACE_TRACK_PAWN_SHIELD(FILE_C, TP_PAWN_SHIELD_RANK1);
    } else if (shield2 != 0ULL) {
        score += PAWN_SHIELD_RANK2;
        TRACE_TRACK_PAWN_SHIELD(FILE_C, TP_PAWN_SHIELD_RANK2);
    }
    if ((file_mask[FILE_A]&pos->bb_pieces[PAWN+side]) == 0ULL) {
        score += PAWN_SHIELD_HOLE;
        TRACE_TRACK_PAWN_SHIELD(FILE_A, TP_PAWN_SHIELD_HOLE);
    }
    if ((file_mask[FILE_B]&pos->bb_pieces[PAWN+side]) == 0ULL) {
        score += PAWN_SHIELD_HOLE;
        TRACE_TRACK_PAWN_SHIELD(FILE_B, TP_PAWN_SHIELD_HOLE);
    }
    if ((file_mask[FILE_C]&pos->bb_pieces[PAWN+side]) == 0ULL) {
        score += PAWN_SHIELD_HOLE;
        TRACE_TRACK_PAWN_SHIELD(FILE_C, TP_PAWN_SHIELD_HOLE);
    }
    item->pawn_shield[side][QUEENSIDE] = MAX(score, 0);

    /* Kingside pawn shield */
    score = 0;
    shield1 = pawns&file_mask[FILE_F]&rank1[side];
    shield2 = pawns&file_mask[FILE_F]&rank2[side];
    if (shield1 != 0ULL) {
        score += PAWN_SHIELD_RANK1;
        TRACE_TRACK_PAWN_SHIELD(FILE_F, TP_PAWN_SHIELD_RANK1);
    } else if (shield2 != 0ULL) {
        score += PAWN_SHIELD_RANK2;
        TRACE_TRACK_PAWN_SHIELD(FILE_F, TP_PAWN_SHIELD_RANK2);
    }
    shield1 = pawns&file_mask[FILE_G]&rank1[side];
    shield2 = pawns&file_mask[FILE_G]&rank2[side];
    if (shield1 != 0ULL) {
        score += PAWN_SHIELD_RANK1;
        TRACE_TRACK_PAWN_SHIELD(FILE_G, TP_PAWN_SHIELD_RANK1);
    } else if (shield2 != 0ULL) {
        score += PAWN_SHIELD_RANK2;
        TRACE_TRACK_PAWN_SHIELD(FILE_G, TP_PAWN_SHIELD_RANK2);
    }
    shield1 = pawns&file_mask[FILE_H]&rank1[side];
    shield2 = pawns&file_mask[FILE_H]&rank2[side];
    if (shield1 != 0ULL) {
        score += PAWN_SHIELD_RANK1;
        TRACE_TRACK_PAWN_SHIELD(FILE_H, TP_PAWN_SHIELD_RANK1);
    } else if (shield2 != 0ULL) {
        score += PAWN_SHIELD_RANK2;
        TRACE_TRACK_PAWN_SHIELD(FILE_H, TP_PAWN_SHIELD_RANK2);
    }
    if ((file_mask[FILE_F]&pos->bb_pieces[PAWN+side]) == 0ULL) {
        score += PAWN_SHIELD_HOLE;
        TRACE_TRACK_PAWN_SHIELD(FILE_F, TP_PAWN_SHIELD_HOLE);
    }
    if ((file_mask[FILE_G]&pos->bb_pieces[PAWN+side]) == 0ULL) {
        score += PAWN_SHIELD_HOLE;
        TRACE_TRACK_PAWN_SHIELD(FILE_G, TP_PAWN_SHIELD_HOLE);
    }
    if ((file_mask[FILE_H]&pos->bb_pieces[PAWN+side]) == 0ULL) {
        score += PAWN_SHIELD_HOLE;
        TRACE_TRACK_PAWN_SHIELD(FILE_H, TP_PAWN_SHIELD_HOLE);
    }
    item->pawn_shield[side][KINGSIDE] = MAX(score, 0);
}

/*
 * - double pawns
 * - isolated pawns
 * - passed pawns
 * - pawn shield
 */
static void evaluate_pawn_structure(struct gamestate *pos, struct eval *eval,
                                    int side)
{
    uint64_t            pieces;
    int                 sq;
    int                 file;
    int                 rank;
    int                 opp_side;
    uint64_t            attackspan;
    struct pawntt_item  *item;

    item = &eval->pawntt;
    opp_side = FLIP_COLOR(side);
    pieces = pos->bb_pieces[PAWN+side];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        rank = RANKNR(sq);
        attackspan = rear_attackspan[side][sq]|front_attackspan[side][sq];

        /* Look for isolated pawns */
        if ((attackspan&pos->bb_pieces[side+PAWN]) == 0ULL) {
            item->score[MIDDLEGAME][side] += ISOLATED_PAWN_MG;
            item->score[ENDGAME][side] += ISOLATED_PAWN_EG;
            TRACE_M(TP_ISOLATED_PAWN_MG, TP_ISOLATED_PAWN_EG, 1);
        }

        /* Look for passed pawns */
        if (ISEMPTY(front_attackspan[side][sq]&pos->bb_pieces[opp_side+PAWN]) &&
            ISEMPTY(front_span[side][sq]&pos->bb_pieces[opp_side+PAWN])) {
            item->score[MIDDLEGAME][side] +=
                                passed_pawn_scores_mg[side==WHITE?rank:7-rank];
            item->score[ENDGAME][side] +=
                                passed_pawn_scores_eg[side==WHITE?rank:7-rank];
            TRACE_OM(TP_PASSED_PAWN_RANK2_MG, TP_PASSED_PAWN_RANK2_EG,
                         ((side == WHITE)?rank:7-rank)-1, 1);
            SETBIT(item->passers[side], sq);
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
    evaluate_pawn_shield(pos, item, side);
}

/*
 * - mobility
 */
static void evaluate_knights(struct gamestate *pos, struct eval *eval, int side)
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
    }

    /* Update coverage */
    eval->coverage[side] |= coverage;
}

/*
 * - bishop pair
 * - mobility
 */
static void evaluate_bishops(struct gamestate *pos, struct eval *eval, int side)
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
static void evaluate_rooks(struct gamestate *pos, struct eval *eval, int side)
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
static void evaluate_queens(struct gamestate *pos, struct eval *eval, int side)
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
static void evaluate_king(struct gamestate *pos, struct eval *eval, int side)
{
    const uint64_t      queenside[NSIDES] = {
                                        sq_mask[A1]|sq_mask[B1]|sq_mask[C1],
                                        sq_mask[A8]|sq_mask[B8]|sq_mask[C8]};
    const uint64_t      kingside[NSIDES] = {
                                        sq_mask[F1]|sq_mask[G1]|sq_mask[H1],
                                        sq_mask[F8]|sq_mask[G8]|sq_mask[H8]};
    struct pawntt_item  *item;
    int                 piece;
    int                 nattackers;
    int                 score;

    /*
     * If the king has moved to the side then it is good to keep a
     * shield of pawns in front of it. The only exception is if
     * there is a rook between the king and the corner. In this case
     * keeping a pawn shield will get the rook trapped.
     */
    item = &eval->pawntt;
    if (queenside[side]&pos->bb_pieces[KING+side]) {
        if (!((pos->bb_pieces[ROOK+side]&queenside[side]) &&
              (LSB(pos->bb_pieces[ROOK+side]&queenside[side]) <
               LSB(pos->bb_pieces[KING+side])))) {
            eval->king_safety[MIDDLEGAME][side] =
                                            item->pawn_shield[side][QUEENSIDE];
            TRACE_PAWN_SHIELD(QUEENSIDE);
        }
    } else if (kingside[side]&pos->bb_pieces[KING+side]) {
        if (!((pos->bb_pieces[ROOK+side]&kingside[side]) &&
              (LSB(pos->bb_pieces[ROOK+side]&kingside[side]) >
               LSB(pos->bb_pieces[KING+side])))) {
            eval->king_safety[MIDDLEGAME][side] =
                                            item->pawn_shield[side][KINGSIDE];
            TRACE_PAWN_SHIELD(KINGSIDE);
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

static void do_eval(struct gamestate *pos, struct eval *eval)
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
    eval->in_pawntt = hash_pawntt_lookup(pos, &eval->pawntt);

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
    if (!eval->in_pawntt) {
        hash_pawntt_store(pos, &eval->pawntt);
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

int eval_evaluate(struct gamestate *pos)
{
    struct eval eval;
    int         k;
    int         phase;
    int         score[NPHASES];

    assert(valid_board(pos));
    assert(valid_scores(pos));

    /*
     * If no player have enough material left
     * to checkmate then it's a draw.
     */
    if (eval_is_material_draw(pos)) {
        return 0;
    }

    /* Evaluate the position */
    do_eval(pos, &eval);

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

        score[k] = (pos->stm == WHITE)?score[k]:-score[k];
    }

    /* Return score adjusted for game phase */
    phase = calculate_game_phase(pos);
    return calculate_tapered_eval(phase, score[MIDDLEGAME], score[ENDGAME]);
}

void eval_display(struct gamestate *pos)
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

    assert(valid_board(pos));
    assert(valid_scores(pos));

    /*
     * If no player have enough material left
     * to checkmate then it's a draw.
     */
    if (eval_is_material_draw(pos)) {
        printf("Draw by insufficient material\n");
        printf("Score: 0\n");
        return;
    }

    /* Evaluate the position */
    do_eval(pos, &eval);

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
}

int eval_material(struct gamestate *pos, int side, bool endgame)
{
    int score;
    int piece;

    assert(valid_board(pos));
    assert(valid_side(side));

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

void eval_update_material_score(struct gamestate *pos, int add, int piece)
{
    int delta;
    int color;

    assert(valid_board(pos));
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

int eval_psq(struct gamestate *pos, int side, bool endgame)
{
    int      score;
    uint64_t pieces;
    int      sq;
    int      piece;

    assert(valid_board(pos));
    assert(valid_side(side));

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

void eval_update_psq_score(struct gamestate *pos, int add, int piece, int sq)
{
    int delta;
    int color;

    assert(valid_board(pos));
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
bool eval_is_material_draw(struct gamestate *pos)
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
void eval_generate_trace(struct gamestate *pos)
{
    struct eval eval;

    assert(valid_board(pos));
    assert(pos->trace != NULL);

    /* Clear the trace */
    memset(pos->trace, 0, sizeof(struct eval_trace));
    memset(&eval, 0, sizeof(struct eval));

    /* Calculate game phase */
    pos->trace->phase = calculate_game_phase(pos);

    /* If there is insufficiernt mating material there is nothing to do */
    if (eval_is_material_draw(pos)) {
        return;
    }

    /* Trace material evaluation */
    pos->material[MIDDLEGAME][WHITE] = eval_material(pos, WHITE, false);
    pos->material[MIDDLEGAME][BLACK] = eval_material(pos, BLACK, false);
    pos->material[ENDGAME][WHITE] = eval_material(pos, WHITE, true);
    pos->material[ENDGAME][BLACK] = eval_material(pos, BLACK, true);

    /* Trace psq evaluation */
    pos->psq[MIDDLEGAME][WHITE] = eval_psq(pos, WHITE, false);
    pos->psq[MIDDLEGAME][BLACK] = eval_psq(pos, BLACK, false);
    pos->psq[ENDGAME][WHITE] = eval_psq(pos, WHITE, true);
    pos->psq[ENDGAME][BLACK] = eval_psq(pos, BLACK, true);

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
}
#endif
