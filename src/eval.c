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
#include "debug.h"
#include "engine.h"
#include "nnue.h"
#include "data.h"
#include "position.h"

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

/* Bonus given to the side to move */
#define TEMPO_BONUS 10

/* Material difference used to decide when to switch between NNUE and HCE */
#define HYBRID_EVAL_THRESHOLD 700

/* Different evaluation components */
struct eval {
    bool endgame[NSIDES];
    uint64_t passers;
    uint64_t candidates;
    int phase;
    uint64_t rear_span[NSIDES];
    uint8_t pawn_shield[NSIDES][2][3];
    uint64_t attacked_by[NPIECES];
    uint64_t attacked[NSIDES];
    uint64_t attacked2[NSIDES];
    int nbr_king_attackers[NPIECES];
    int score[NPHASES][NSIDES];
};

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

static int material_phase_value[NPIECES] = {
    0, 0, 1, 1, 1, 1, 2, 2, 4, 4, 0, 0
};

/*
 * Calculate a score that is an interpolation of the middlegame and endgame
 * based on the current phase of the game. The formula is taken from
 * https://chessprogramming.wikispaces.com/Tapered+Eval
 */
static int calculate_tapered_eval(int matphase, int score_mg, int score_eg)
{
    int total_phase;
    int phase;

    total_phase = 24;
    phase = ((24-matphase)*256 + (total_phase/2))/total_phase;

    /*
     * Guard against negative phase values. The phase value might
     * become negative in case of promotion.
     */
    phase = MAX(phase, 0);

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

/*
 * Space is defined as the number of squares that meet the
 * following conditions:
 * - in the "space area"
 * - not occupied by friendly pawn
 * - not attacked by the enemy opponent
 * - in the rear span of a friendly pawn
 */
static void evaluate_space(struct position *pos, struct eval *eval)
{
    uint64_t squares;
    int      side;

    for (side=0;side<NSIDES;side++) {
        squares = space_eval_squares[side] & eval->rear_span[side] &
                    (~pos->bb_pieces[side+PAWN]) &
                    (~eval->attacked[FLIP_COLOR(side)]);
        eval->score[MIDDLEGAME][side] += BITCOUNT(squares)*SPACE_SQUARE;
    }
}

/* Evaluate different kinds of threats towards other pieces than the king */
static void evaluate_threats(struct position *pos, struct eval *eval, int side)
{
    uint64_t minors;
    uint64_t bb;
    uint64_t pawn_push;
    int      oside;
    int      count;
    uint64_t weak;
    uint64_t weak_pawns;
    uint64_t weak_minors;
    uint64_t targets;
    int      sq;
    int      index;

    oside = FLIP_COLOR(side);
    minors = pos->bb_pieces[oside+KNIGHT]|pos->bb_pieces[oside+BISHOP];
    weak = ~eval->attacked_by[oside+PAWN]&
                (eval->attacked2[side]|~eval->attacked2[oside]);
    weak_pawns = weak&pos->bb_pieces[oside+PAWN];
    weak_minors = weak&minors;

    /* Give a bonus for pawns attacking opponents minor pieces */
    bb = minors&eval->attacked_by[side+PAWN];
    count = BITCOUNT(bb);
    eval->score[MIDDLEGAME][side] += count*THREAT_MINOR_BY_PAWN_MG;
    eval->score[ENDGAME][side] += count*THREAT_MINOR_BY_PAWN_EG;

    /* Give a bonus for attacks on opponent pieces following a safe pawn push */
    pawn_push = bb_pawn_pushes(pos->bb_pieces[PAWN+side], pos->bb_all, side);
    pawn_push &= (~relative_rank_mask[pos->stm][RANK_8]);
    pawn_push &= (~eval->attacked_by[PAWN+oside]);
    pawn_push &= ((~eval->attacked[oside])|eval->attacked[side]);
    count = BITCOUNT(bb_pawn_attacks(pawn_push, side)&pos->bb_sides[oside]);
    eval->score[MIDDLEGAME][side] += count*THREAT_PAWN_PUSH_MG;
    eval->score[ENDGAME][side] += count*THREAT_PAWN_PUSH_EG;

    /*
     * Give a bonus for knights attacking higher value
     * pieces or weak pieces of the same or lower value.
     */
    targets = weak_pawns|weak_minors|pos->bb_pieces[oside+ROOK]|
                                            pos->bb_pieces[oside+QUEEN];
    bb = targets&eval->attacked_by[side+KNIGHT];
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        index = VALUE(pos->pieces[sq])/2;
        eval->score[MIDDLEGAME][side] += THREAT_BY_KNIGHT_MG[index];
        eval->score[ENDGAME][side] += THREAT_BY_KNIGHT_EG[index];
    }

    /*
     * Give a bonus for bishops attacking higher value
     * pieces or weak pieces of the same or lower value.
     */
    targets = weak_pawns|weak_minors|pos->bb_pieces[oside+ROOK]|
                                            pos->bb_pieces[oside+QUEEN];
    bb = targets&eval->attacked_by[side+BISHOP];
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        index = VALUE(pos->pieces[sq])/2;
        eval->score[MIDDLEGAME][side] += THREAT_BY_BISHOP_MG[index];
        eval->score[ENDGAME][side] += THREAT_BY_BISHOP_EG[index];
    }

    /*
     * Give a bonus for rooks attacking higher value
     * pieces or weak pieces of the same or lower value.
     */
    targets = weak_pawns|weak_minors|(pos->bb_pieces[oside+ROOK]&weak)|
                                            pos->bb_pieces[oside+QUEEN];
    bb = targets&eval->attacked_by[side+ROOK];
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        index = VALUE(pos->pieces[sq])/2;
        eval->score[MIDDLEGAME][side] += THREAT_BY_ROOK_MG[index];
        eval->score[ENDGAME][side] += THREAT_BY_ROOK_EG[index];
    }

    /*
     * Give a bonus for queens attacking weak pieces of
     * the same or lower value.
     */
    targets = weak_pawns|weak_minors|(pos->bb_pieces[oside+ROOK]&weak)|
                                            (pos->bb_pieces[oside+QUEEN]&weak);
    bb = targets&eval->attacked_by[side+QUEEN];
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        index = VALUE(pos->pieces[sq])/2;
        eval->score[MIDDLEGAME][side] += THREAT_BY_QUEEN_MG[index];
        eval->score[ENDGAME][side] += THREAT_BY_QUEEN_EG[index];
    }
}

static void evaluate_pawn_shield(struct position *pos, struct eval *eval,
                                 int king_sq, int side)
{
    uint64_t queenside[NSIDES] = {sq_mask[A1]|sq_mask[B1]|sq_mask[C1],
                                  sq_mask[A8]|sq_mask[B8]|sq_mask[C8]};
    uint64_t kingside[NSIDES] = {sq_mask[F1]|sq_mask[G1]|sq_mask[H1],
                                 sq_mask[F8]|sq_mask[G8]|sq_mask[H8]};
    int      king_file;
    int      king_rank;
    int      first;
    int      last;
    int      file;
    uint64_t bb;
    int      dist;
    int      sq;

    king_file = FILENR(king_sq);
    king_rank = RANKNR(king_sq);

    /* Only apply pawn shield bonus if the king is on the back rank */
    if (((side == WHITE) && (king_rank != RANK_1)) ||
        ((side == BLACK) && (king_rank != RANK_8))) {
        return;
    }

    /*
     * Don't apply pawn shield bonus if the king is on either
     * side trapping a rook in the corner.
     */
    if ((queenside[side]&sq_mask[king_sq]) &&
        (pos->bb_pieces[ROOK+side]&queenside[side]) &&
         (LSB(pos->bb_pieces[ROOK+side]&queenside[side]) <
          LSB(pos->bb_pieces[KING+side]))) {
        return;
    } else if ((kingside[side]&pos->bb_pieces[KING+side]) &&
               (pos->bb_pieces[ROOK+side]&kingside[side]) &&
               (MSB(pos->bb_pieces[ROOK+side]&kingside[side]) >
                MSB(pos->bb_pieces[KING+side]))) {
        return;
    }

    /* Don't apply pawn shield bonus if the king is in the center */
    if (king_file < FILE_D) {
        first = FILE_A;
        last = FILE_C;
    } else if (king_file > FILE_E) {
        first = FILE_F;
        last = FILE_H;
    } else {
        return;
    }

    for (file=first;file<=last;file++) {
        sq = SQUARE(file, king_rank);
        bb = front_span[side][sq]&pos->bb_pieces[PAWN+side];
        dist = (bb == 0ULL)?0:(side == WHITE)?RANKNR(LSB(bb))-RANKNR(sq):
                                              RANKNR(sq)-RANKNR(MSB(bb));
        if (dist <= 2) {
            eval->score[MIDDLEGAME][side] += PAWN_SHIELD[dist];
        }
    }
}

/*
 * A backward pawn is a pawn that can't be protected by friendly
 * pawns and that cannot safely advance.
 */
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

static void evaluate_pawn_structure(struct position *pos, struct eval *eval)
{
    uint64_t pieces;
    int      sq;
    int      psq;
    int      file;
    int      rank;
    int      rel_rank;
    int      side;
    int      oside;
    bool     isolated;
    uint64_t attackspan;
    uint64_t attackers;
    uint64_t defenders;
    uint64_t helpers;
    uint64_t sentries;
    uint64_t neighbours;
    uint64_t attacks;

    pieces = pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN];
    while (pieces != 0ULL) {
        isolated = false;
        sq = POPBIT(&pieces);
        side = COLOR(pos->pieces[sq]);
        oside = FLIP_COLOR(side);
        rank = RANKNR(sq);
        rel_rank = (side==WHITE)?rank:7-rank;
        attackspan = rear_attackspan[side][sq]|front_attackspan[side][sq];

        psq = (side == WHITE)?sq:MIRROR(sq);
        eval->score[MIDDLEGAME][side] += PSQ_TABLE_PAWN_MG[psq];
        eval->score[ENDGAME][side] += PSQ_TABLE_PAWN_EG[psq];
        eval->score[MIDDLEGAME][side] += PAWN_BASE_VALUE;
        eval->score[ENDGAME][side] += PAWN_BASE_VALUE;
        eval->phase += material_phase_value[PAWN+side];

        /* Look for isolated pawns */
        if ((attackspan&pos->bb_pieces[side+PAWN]) == 0ULL) {
            isolated = true;
            eval->score[MIDDLEGAME][side] += ISOLATED_PAWN_MG;
            eval->score[ENDGAME][side] += ISOLATED_PAWN_EG;
        }

        /* Look for passed pawns */
        if (ISEMPTY(front_attackspan[side][sq]&pos->bb_pieces[oside+PAWN]) &&
            ISEMPTY(front_span[side][sq]&pos->bb_pieces[oside+PAWN])) {
            SETBIT(eval->passers, sq);
            eval->score[MIDDLEGAME][side] += PASSED_PAWN_MG[rel_rank];
            eval->score[ENDGAME][side] += PASSED_PAWN_EG[rel_rank];
        }

        /* Look for candidate passed pawns */
        sentries = front_attackspan[side][sq]&pos->bb_pieces[oside+PAWN];
        helpers = rear_attackspan[side][sq]&pos->bb_pieces[side+PAWN];
        attackers = bb_pawn_attacks_to(sq, oside)&pos->bb_pieces[oside+PAWN];
        defenders = bb_pawn_attacks_to(sq, side)&pos->bb_pieces[side+PAWN];
        if (!ISBITSET(eval->passers&pos->bb_sides[side], sq) &&
            ISEMPTY(front_span[side][sq]&pos->bb_pieces[oside+PAWN]) &&
            (BITCOUNT(helpers) >= BITCOUNT(sentries)) &&
            (BITCOUNT(defenders) >= BITCOUNT(attackers))) {
            SETBIT(eval->candidates, sq);
            eval->score[MIDDLEGAME][side] += CANDIDATE_PASSED_PAWN_MG[rel_rank];
            eval->score[ENDGAME][side] += CANDIDATE_PASSED_PAWN_EG[rel_rank];
        }

        /* Check if the pawn is considered backward */
        if (!isolated && is_backward_pawn(pos, side, sq)) {
            eval->score[MIDDLEGAME][side] += BACKWARD_PAWN_MG;
            eval->score[ENDGAME][side] += BACKWARD_PAWN_EG;
        }

        /* Check if the pawn is connected */
        neighbours = rear_attackspan[side][sq]&pos->bb_pieces[side+PAWN];
        if (!ISEMPTY(neighbours&rank_mask[rank]) ||
            !ISEMPTY(neighbours&bb_pawn_attacks_to(sq, side))) {
            eval->score[MIDDLEGAME][side] += CONNECTED_PAWNS_MG[rel_rank];
            eval->score[ENDGAME][side] += CONNECTED_PAWNS_EG[rel_rank];
        }

        /* Update pawn attacks */
        attacks = bb_pawn_attacks_from(sq, side);
        eval->attacked2[side] |= (attacks&eval->attacked[side]);
        eval->attacked[side] |= attacks;
        eval->attacked_by[side+PAWN] |= attacks;

        /* Update rear span information */
        eval->rear_span[side] |= rear_span[side][sq];
    }

    /* Look for double pawns */
    for (side=0;side<NSIDES;side++) {
        for (file=0;file<NFILES;file++) {
            if (BITCOUNT(pos->bb_pieces[side+PAWN]&file_mask[file]) >= 2) {
                eval->score[MIDDLEGAME][side] += DOUBLE_PAWNS_MG;
                eval->score[ENDGAME][side] += DOUBLE_PAWNS_EG;
            }
        }
    }
}

/*
 * A free passed pawn is a passed pawn on the 6th or 7th rank
 * that can safly advance at least one square.
 */
static bool is_free_pawn(struct position *pos, struct eval *eval, int side,
                         int sq)
{
    int stop_sq;
    int rank;

    rank = RANKNR(sq);
    if ((side == WHITE && rank < RANK_6) ||
        (side == BLACK && rank > RANK_3)) {
        return false;
    }

    stop_sq = (side == WHITE)?sq+8:sq-8;
    if (ISBITSET(pos->bb_all, stop_sq)) {
        return false;
    }

    if (ISBITSET(eval->attacked[FLIP_COLOR(side)], stop_sq)) {
        return false;
    }

    return true;
}

/*
 * Evaluate interaction between passed pawns and other pieces. The parts that
 * only depend on other pawns are evaluated by evaluate_pawn_structure.
 */
static void evaluate_passers(struct position *pos, struct eval *eval)
{
    uint64_t passers;
    int      sq;
    int      to;
    int      side;
    int      dist;
    int      odist;

    /* Iterate over all passers */
    passers = eval->passers;
    while (passers != 0ULL) {
        sq = POPBIT(&passers);
        side = COLOR(pos->pieces[sq]);

        /*
         * Find the distance from each king to the square
         * directly in front of the pawn.
         */
        to = (side == WHITE)?sq+8:sq-8;
        dist = king_distance(LSB(pos->bb_pieces[KING+side]), to);
        odist = king_distance(LSB(pos->bb_pieces[KING+FLIP_COLOR(side)]), to);

        /* Calculate a score based on the distance to each king */
        eval->score[ENDGAME][side] += OPPONENT_KING_PASSER_DIST*odist;
        eval->score[ENDGAME][side] += FRIENDLY_KING_PASSER_DIST*dist;

        /* Free pawn */
        if (is_free_pawn(pos, eval, side, sq)) {
            eval->score[MIDDLEGAME][side] += FREE_PASSED_PAWN_MG;
            eval->score[ENDGAME][side] += FREE_PASSED_PAWN_EG;
        }
    }
}

static void evaluate_knights(struct position *pos, struct eval *eval)
{
    uint64_t pieces;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t attacks;
    int      sq;
    int      psq;
    int      king_sq;
    int      side;
    int      opp_side;

    pieces = pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        side = COLOR(pos->pieces[sq]);
        opp_side = FLIP_COLOR(side);
        king_sq = LSB(pos->bb_pieces[KING+opp_side]);
        moves = bb_knight_moves(sq);
        attacks = moves;
        moves &= (~pos->bb_sides[side]);

        psq = (side == WHITE)?sq:MIRROR(sq);
        eval->score[MIDDLEGAME][side] += PSQ_TABLE_KNIGHT_MG[psq];
        eval->score[ENDGAME][side] += PSQ_TABLE_KNIGHT_EG[psq];
        eval->score[MIDDLEGAME][side] += KNIGHT_MATERIAL_VALUE_MG;
        eval->score[ENDGAME][side] += KNIGHT_MATERIAL_VALUE_EG;
        eval->phase += material_phase_value[KNIGHT+side];

        /* Mobility */
        safe_moves = moves&(~eval->attacked_by[PAWN+FLIP_COLOR(side)]);
        eval->score[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                            KNIGHT_MOBILITY_MG);
        eval->score[ENDGAME][side] += (BITCOUNT(safe_moves)*KNIGHT_MOBILITY_EG);

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[KNIGHT+side]++;
        }

        /* Outposts */
        if (sq_mask[sq]&outpost_squares[side] &&
            ((front_attackspan[side][sq]&pos->bb_pieces[opp_side+PAWN]) == 0)) {
            if (eval->attacked_by[PAWN+side]&sq_mask[sq]) {
                eval->score[MIDDLEGAME][side] += PROTECTED_KNIGHT_OUTPOST;
            } else {
                eval->score[MIDDLEGAME][side] += KNIGHT_OUTPOST;
            }
        }

        /* Update attacks */
        eval->attacked_by[KNIGHT+side] |= attacks;
        eval->attacked2[side] |= (attacks&eval->attacked[side]);
        eval->attacked[side] |= attacks;
    }
}

static void evaluate_bishops(struct position *pos, struct eval *eval)
{
    uint64_t pieces;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t attacks;
    int      sq;
    int      psq;
    int      king_sq;
    int      side;
    int      opp_side;

    /*
     * Check if both bishops are still on the board. To be correct
     * also check if the two (or more) bishops operate on different
     * color squares. The only case when a player can have
     * two bishops on the same color squares is if he underpromotes
     * to a bishop. This is so unlikely that it should be safe to assume
     * that the bishops operate on different color squares.
     */
    for (side=0;side<NSIDES;side++) {
        if (BITCOUNT(pos->bb_pieces[side+BISHOP]) >= 2) {
            eval->score[MIDDLEGAME][side] += BISHOP_PAIR_MG;
            eval->score[ENDGAME][side] += BISHOP_PAIR_EG;
        }
    }

    pieces = pos->bb_pieces[WHITE_BISHOP]|pos->bb_pieces[BLACK_BISHOP];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        side = COLOR(pos->pieces[sq]);
        opp_side = FLIP_COLOR(side);
        king_sq = LSB(pos->bb_pieces[KING+opp_side]);
        moves = bb_bishop_moves(pos->bb_all, sq);
        attacks = moves;
        moves &= (~pos->bb_sides[side]);

        psq = (side == WHITE)?sq:MIRROR(sq);
        eval->score[MIDDLEGAME][side] += PSQ_TABLE_BISHOP_MG[psq];
        eval->score[ENDGAME][side] += PSQ_TABLE_BISHOP_EG[psq];
        eval->score[MIDDLEGAME][side] += BISHOP_MATERIAL_VALUE_MG;
        eval->score[ENDGAME][side] += BISHOP_MATERIAL_VALUE_EG;
        eval->phase += material_phase_value[BISHOP+side];

        /* Mobility */
        safe_moves = moves&(~eval->attacked_by[PAWN+FLIP_COLOR(side)]);
        eval->score[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                            BISHOP_MOBILITY_MG);
        eval->score[ENDGAME][side] += (BITCOUNT(safe_moves)*
                                                            BISHOP_MOBILITY_EG);

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[BISHOP+side]++;
        }

        /* Update attacks */
        eval->attacked_by[BISHOP+side] |= attacks;
        eval->attacked2[side] |= (attacks&eval->attacked[side]);
        eval->attacked[side] |= attacks;
    }
}

static void evaluate_rooks(struct position *pos, struct eval *eval)
{
    uint64_t rank7[NSIDES] = {rank_mask[RANK_7], rank_mask[RANK_2]};
    uint64_t rank8[NSIDES] = {rank_mask[RANK_8], rank_mask[RANK_1]};
    uint64_t pieces;
    uint64_t all_pawns;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t attacks;
    int      sq;
    int      psq;
    int      file;
    int      king_sq;
    int      side;
    int      opp_side;

    all_pawns = pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN];
    pieces = pos->bb_pieces[WHITE_ROOK]|pos->bb_pieces[BLACK_ROOK];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        side = COLOR(pos->pieces[sq]);
        opp_side = FLIP_COLOR(side);
        king_sq = LSB(pos->bb_pieces[KING+opp_side]);
        file = FILENR(sq);
        moves = bb_rook_moves(pos->bb_all, sq);
        attacks = moves;
        moves &= (~pos->bb_sides[side]);

        psq = (side == WHITE)?sq:MIRROR(sq);
        eval->score[MIDDLEGAME][side] += PSQ_TABLE_ROOK_MG[psq];
        eval->score[ENDGAME][side] += PSQ_TABLE_ROOK_EG[psq];
        eval->score[MIDDLEGAME][side] += ROOK_MATERIAL_VALUE_MG;
        eval->score[ENDGAME][side] += ROOK_MATERIAL_VALUE_EG;
        eval->phase += material_phase_value[ROOK+side];

        /* Open and half-open files */
        if ((file_mask[file]&all_pawns) == 0ULL) {
            eval->score[MIDDLEGAME][side] += ROOK_OPEN_FILE_MG;
            eval->score[ENDGAME][side] += ROOK_OPEN_FILE_EG;
        } else if ((file_mask[file]&pos->bb_pieces[PAWN+side]) == 0ULL) {
            eval->score[MIDDLEGAME][side] += ROOK_HALF_OPEN_FILE_MG;
            eval->score[ENDGAME][side] += ROOK_HALF_OPEN_FILE_EG;
        }

        /* 7th rank */
        if (ISBITSET(rank7[side], sq)) {
            /*
             * Only give bonus if the enemy king is on the 8th rank
             * or if there are enenmy pawns on the 7th rank.
             */
            if ((pos->bb_pieces[KING+FLIP_COLOR(side)]&rank8[side]) ||
                (pos->bb_pieces[PAWN+FLIP_COLOR(side)]&rank7[side])) {
                eval->score[MIDDLEGAME][side] += ROOK_ON_7TH_MG;
                eval->score[ENDGAME][side] += ROOK_ON_7TH_EG;
            }
        }

        /* Mobility */
        safe_moves = moves&(~eval->attacked_by[PAWN+FLIP_COLOR(side)]);
        eval->score[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                            ROOK_MOBILITY_MG);
        eval->score[ENDGAME][side] += (BITCOUNT(safe_moves)*ROOK_MOBILITY_EG);

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[ROOK+side]++;
        }

        /* Update attacks */
        eval->attacked_by[ROOK+side] |= attacks;
        eval->attacked2[side] |= (attacks&eval->attacked[side]);
        eval->attacked[side] |= attacks;
    }
}

static void evaluate_queens(struct position *pos, struct eval *eval)
{
    uint64_t pieces;
    uint64_t all_pawns;
    uint64_t moves;
    uint64_t safe_moves;
    uint64_t attacks;
    uint64_t unsafe;
    int      opp_side;
    int      sq;
    int      psq;
    int      file;
    int      king_sq;
    int      side;

    all_pawns = pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN];
    pieces = pos->bb_pieces[WHITE_QUEEN]|pos->bb_pieces[BLACK_QUEEN];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        side = COLOR(pos->pieces[sq]);
        opp_side = FLIP_COLOR(side);
        file = FILENR(sq);
        moves = bb_queen_moves(pos->bb_all, sq);
        attacks = moves;
        moves &= (~pos->bb_sides[side]);
        king_sq = LSB(pos->bb_pieces[KING+opp_side]);
        unsafe = eval->attacked_by[PAWN+opp_side]|
                 eval->attacked_by[KNIGHT+opp_side]|
                 eval->attacked_by[BISHOP+opp_side]|
                 eval->attacked_by[ROOK+opp_side];

        psq = (side == WHITE)?sq:MIRROR(sq);
        eval->score[MIDDLEGAME][side] += PSQ_TABLE_QUEEN_MG[psq];
        eval->score[ENDGAME][side] += PSQ_TABLE_QUEEN_EG[psq];
        eval->score[MIDDLEGAME][side] += QUEEN_MATERIAL_VALUE_MG;
        eval->score[ENDGAME][side] += QUEEN_MATERIAL_VALUE_EG;
        eval->phase += material_phase_value[QUEEN+side];

        /* Open and half-open files */
        if ((file_mask[file]&all_pawns) == 0ULL) {
            eval->score[MIDDLEGAME][side] += QUEEN_OPEN_FILE_MG;
            eval->score[ENDGAME][side] += QUEEN_OPEN_FILE_EG;
        } else if ((file_mask[file]&pos->bb_pieces[PAWN+side]) == 0ULL) {
            eval->score[MIDDLEGAME][side] += QUEEN_HALF_OPEN_FILE_MG;
            eval->score[ENDGAME][side] += QUEEN_HALF_OPEN_FILE_EG;
        }

        /* Mobility */
        safe_moves = moves&(~unsafe);
        eval->score[MIDDLEGAME][side] += (BITCOUNT(safe_moves)*
                                                            QUEEN_MOBILITY_MG);
        eval->score[ENDGAME][side] += (BITCOUNT(safe_moves)*QUEEN_MOBILITY_EG);

        /* Preassure on enemy king */
        if (!ISEMPTY(moves&king_zone[opp_side][king_sq])) {
            eval->nbr_king_attackers[QUEEN+side]++;
        }

        /* Update attacks */
        eval->attacked_by[QUEEN+side] |= attacks;
        eval->attacked2[side] |= (attacks&eval->attacked[side]);
        eval->attacked[side] |= attacks;
    }
}

static void evaluate_kings(struct position *pos, struct eval *eval)
{
    int      piece;
    int      nattackers;
    int      score;
    int      sq;
    int      psq;
    int      side;
    uint64_t pieces;

    pieces = pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING];
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        side = COLOR(pos->pieces[sq]);

        psq = (side == WHITE)?sq:MIRROR(sq);
        eval->score[MIDDLEGAME][side] += PSQ_TABLE_KING_MG[psq];
        eval->score[ENDGAME][side] += PSQ_TABLE_KING_EG[psq];

        /* Calculate preassure on the enemy king */
        nattackers = 0;
        score = 0;
        for (piece=KNIGHT+side;piece<NPIECES;piece+=2) {
            score +=
                    piece_attack_weights[piece]*eval->nbr_king_attackers[piece];
            nattackers += eval->nbr_king_attackers[piece];
        }
        if (nattackers >= (int)(sizeof(nbr_attackers_weight)/sizeof(int))) {
            nattackers = (sizeof(nbr_attackers_weight)/sizeof(int)) - 1;
        }
        score *= nbr_attackers_weight[nattackers];
        eval->score[MIDDLEGAME][side] += (score*KING_ATTACK_SCALE_MG)/100;
        eval->score[ENDGAME][side] += (score*KING_ATTACK_SCALE_EG)/100;

        /* Evaluate pawn shield */
        evaluate_pawn_shield(pos, eval, sq, side);
    }
}

/*
 * Initialize attack tables with king attack information. This is
 * done here since the information is needed during king evaluation
 * later.
 */
static void init_attack_tables(struct position *pos, struct eval *eval)
{
    uint64_t attacks;
    int      sq;

    /* White king attacks */
    sq = LSB(pos->bb_pieces[WHITE_KING]);
    attacks = bb_king_moves(sq);
    eval->attacked_by[WHITE_KING] |= attacks;
    eval->attacked2[WHITE] |= (attacks&eval->attacked[WHITE]);
    eval->attacked[WHITE] |= eval->attacked_by[WHITE_KING];

    /* Black king attacks */
    sq = LSB(pos->bb_pieces[BLACK_KING]);
    attacks = bb_king_moves(sq);
    eval->attacked_by[BLACK_KING] |= attacks;
    eval->attacked2[BLACK] |= (attacks&eval->attacked[BLACK]);
    eval->attacked[BLACK] |= eval->attacked_by[BLACK_KING];
}

static void do_eval(struct position *pos, struct eval *eval)
{
    /* Initialize eval struct */
    memset(eval, 0, sizeof(struct eval));

    /* Init attack table */
    init_attack_tables(pos, eval);

    /* Evaluate the position */
    evaluate_pawn_structure(pos, eval);
    evaluate_knights(pos, eval);
    evaluate_bishops(pos, eval);
    evaluate_rooks(pos, eval);
    evaluate_queens(pos, eval);
    evaluate_kings(pos, eval);
    evaluate_passers(pos, eval);
    evaluate_space(pos, eval);
    evaluate_threats(pos, eval, WHITE);
    evaluate_threats(pos, eval, BLACK);
}

int eval_evaluate(struct position *pos, bool force_hce)
{
    struct eval eval;
    int         k;
    int         score[NPHASES];
    int         tapered_score;
    int         nnue_score;

    assert(valid_position(pos));

    /* Check if NNUE or classic eval should be used */
    if (engine_using_nnue && engine_loaded_net && !force_hce) {
        nnue_score = nnue_evaluate(pos);
        return nnue_score;
    }

    /* Evaluate the position */
    do_eval(pos, &eval);

    /* Summarize each evaluation term from side to moves's pov */
    for (k=0;k<NPHASES;k++) {
        score[k] = eval.score[k][WHITE] - eval.score[k][BLACK];
        score[k] = (pos->stm == WHITE)?score[k]:-score[k];
    }

    /* Return score adjusted for game phase */
    tapered_score = calculate_tapered_eval(eval.phase, score[MIDDLEGAME],
                                           score[ENDGAME]);

    pos->eval_stack[pos->height].score = tapered_score + TEMPO_BONUS;
    return pos->eval_stack[pos->height].score;
}

void eval_init_material(struct position *pos)
{
    uint64_t pieces;
    int      sq;
    int      piece;

    assert(valid_position(pos));

    /* Iterate over all pieces */
    pos->material = 0;
    pieces = pos->bb_all;
    while (pieces != 0ULL) {
        sq = POPBIT(&pieces);
        piece = pos->pieces[sq];
        eval_update_material(pos, piece, true);
    }
}

void eval_update_material(struct position *pos, int piece, bool added)
{
    int delta;

    delta = added?1:-1;
    if (COLOR(piece) == BLACK) {
        delta *= -1;
    }

    pos->material += delta*material_values[piece];
}
