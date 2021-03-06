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
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "search.h"
#include "engine.h"
#include "timectl.h"
#include "board.h"
#include "movegen.h"
#include "moveselect.h"
#include "eval.h"
#include "validation.h"
#include "hash.h"
#include "see.h"
#include "utils.h"
#include "debug.h"
#include "bitboard.h"
#include "tbprobe.h"
#include "smp.h"
#include "fen.h"
#include "history.h"
#include "nnue.h"

/* Calculates if it is time to check the clock and poll for commands */
#define CHECKUP(n) (((n)&1023)==0)

/* Different exceptions that can happen during search */
#define EXCEPTION_COMMAND    1
#define EXCEPTION_STOP       2
#define EXCEPTION_TIMEOUT    3
#define EXCEPTION_NODE_LIMIT 4

/* Configuration constants for null move pruning */
#define NULLMOVE_DEPTH 3
#define NULLMOVE_BASE_REDUCTION 2
#define NULLMOVE_DIVISOR 6

/* Margins used for futility pruning and reverse futility pruning */
#define FUTILITY_DEPTH 7
static int futility_margin[] = {0, 104, 205, 339, 437, 520, 667, 785};

/* Margins used for razoring */
#define RAZORING_DEPTH 3
static int razoring_margin[] = {0, 54, 152, 448};

/*
 * Aspiration window sizes. If the search fails low or high
 * then the window is set to the next size in order. The
 * last entry in the array should always be INFINITE_SCORE.
 */
static int aspiration_window[] = {10, 20, 40, 80, 160, 320, 640,
                                  INFINITE_SCORE};

/* Move counts for the different depths to use for late move pruning */
#define LMP_DEPTH 10
static int lmp_counts[] = {0, 4, 6, 8, 12, 17, 24, 33, 44, 57, 72};

/* Configuration constants for probcut */
#define PROBCUT_DEPTH 5
#define PROBCUT_MARGIN 210

/* Margins for SEE pruning in the main search */
#define SEE_PRUNE_DEPTH 8
#define SEE_QUIET_MARGIN(d) (-100)*(d)
#define SEE_TACTICAL_MARGIN(d) (-29)*(d)*(d)

/* Margin used for delta pruning */
#define DELTA_MARGIN 200

/* Configuration constants for singular extensions */
#define SE_DEPTH 8

/* Configuration for continuation history pruning */
#define HISTORY_PRUNING_DEPTH 3
static int counter_history_pruning_margin[] = {0, 0, -500, -1000};
static int followup_history_pruning_margin[] = {0, -500, -1000, -2000};

/* Table of base reductions for LMR indexed by depth and move number */
static int lmr_reductions[64][64];

static bool check_tt_cutoff(struct tt_item *item, int depth, int alpha,
                            int beta, int score)
{
    if (item->depth >= depth) {
        switch (item->type) {
        case TT_EXACT:
            return true;
        case TT_ALPHA:
            return score <= alpha;
        case TT_BETA:
            return score >= beta;
        default:
            return false;
        }
    }
    return false;
}

static int adjust_mate_score(struct position *pos, int score)
{
    if (score > KNOWN_WIN) {
        return score - pos->sply;
    } else if (score < -KNOWN_WIN) {
        return score + pos->sply;
    }
    return score;
}

static bool is_multipv_move(struct search_worker *worker, uint32_t move)
{
    int k;

    for (k=0;k<worker->mpvidx;k++) {
        if (move == worker->mpv_moves[k]) {
            return true;
        }
    }
    return false;
}

static bool is_filtered_move(struct search_worker *worker, uint32_t move)
{
    int k;

    for (k=0;k<worker->state->move_filter.size;k++) {
        if (move == worker->state->move_filter.moves[k]) {
            return true;
        }
    }
    return false;
}

static bool is_pawn_push(struct position *pos, uint32_t move)
{
    int from;
    int to;
    int piece;
    int color;

    from = FROM(move);
    to = TO(move);
    piece = pos->pieces[from];
    color = COLOR(piece);

    if (VALUE(pos->pieces[from]) != PAWN) {
        return false;
    }
    if ((color == WHITE) && (RANKNR(to) >= RANK_6)) {
        return true;
    }
    if ((color == BLACK) && (RANKNR(to) <= RANK_3)) {
        return true;
    }
    return false;
}

static bool is_recapture(struct position *pos, uint32_t move)
{
    struct unmake *prev = &pos->history[pos->ply-1];
    int           capture;

    if (!ISCAPTURE(prev->move) || !ISCAPTURE(move) ||
        (TO(prev->move) != TO(move))) {
        return false;
    }

    capture = VALUE(pos->pieces[TO(move)]);
    switch (VALUE(prev->capture)) {
    case PAWN:
        return capture == PAWN;
    case KNIGHT:
    case BISHOP:
        return capture == KNIGHT || capture == BISHOP;
    case ROOK:
        return capture == ROOK;
    case QUEEN:
        return capture == QUEEN;
    default:
        break;
    }

    return false;
}

static bool probe_wdl_tables(struct search_worker *worker, int alpha, int beta,
                             int *score)
{
    unsigned int    res;
    struct position *pos;
    bool            cutoff;

    pos = &worker->pos;
    res = tb_probe_wdl(pos->bb_sides[WHITE], pos->bb_sides[BLACK],
                    pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING],
                    pos->bb_pieces[WHITE_QUEEN]|pos->bb_pieces[BLACK_QUEEN],
                    pos->bb_pieces[WHITE_ROOK]|pos->bb_pieces[BLACK_ROOK],
                    pos->bb_pieces[WHITE_BISHOP]|pos->bb_pieces[BLACK_BISHOP],
                    pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT],
                    pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN],
                    pos->fifty, pos->castle,
                    pos->ep_sq != NO_SQUARE?pos->ep_sq:0,
                    pos->stm == WHITE);
    if (res == TB_RESULT_FAILED) {
        *score = 0;
        return false;
    }
    worker->tbhits++;

    switch (res) {
    case TB_WIN:
        *score = TABLEBASE_WIN-pos->sply;
        cutoff = (*score >= beta);
        break;
    case TB_LOSS:
        *score = TABLEBASE_LOSS+pos->sply;
        cutoff = (*score <= alpha);
        break;
    default:
        /* Draw */
        *score = 0;
        cutoff = true;
        break;
    }

    return cutoff;
}

static void update_pv(struct search_worker *worker, uint32_t move)
{
    struct position *pos;

    pos = &worker->pos;
    worker->pv_table[pos->sply].moves[0] = move;
    memcpy(&worker->pv_table[pos->sply].moves[1],
           &worker->pv_table[pos->sply+1].moves[0],
           worker->pv_table[pos->sply+1].size*sizeof(uint32_t));
    worker->pv_table[pos->sply].size = worker->pv_table[pos->sply+1].size + 1;
}

static void checkup(struct search_worker *worker)
{
    /* Check if the worker is requested to stop */
    if (smp_should_stop()) {
        longjmp(worker->env, EXCEPTION_STOP);
    }

    /* Only check time limits for the main worker */
    if (worker->id != 0) {
        return;
    }

    /* Check if the node limit has been reached */
    if (((tc_get_flags()&TC_NODE_LIMIT) != 0) &&
        (smp_nodes() >= worker->state->max_nodes)) {
        smp_stop_all();
        longjmp(worker->env, EXCEPTION_NODE_LIMIT);
    }

    /* Check if the time is up or if a new command have been received */
    if (!CHECKUP(worker->nodes)) {
        return;
    }

    /* Perform checkup */
    if (!worker->state->pondering &&
        ((tc_get_flags()&TC_TIME_LIMIT) != 0) &&
        !tc_check_time(worker)) {
        smp_stop_all();
        longjmp(worker->env, EXCEPTION_TIMEOUT);
    }
    if (engine_check_input(worker)) {
        smp_stop_all();
        longjmp(worker->env, EXCEPTION_COMMAND);
    }
}

static int material_gain(struct position *pos, uint32_t move)
{
    int gain;

    gain = 0;

    /* Consider gain from capture moves */
    if (ISCAPTURE(move)) {
        gain += see_material[pos->pieces[TO(move)]];
    } else if (ISENPASSANT(move)) {
        gain += see_material[PAWN+FLIP_COLOR(pos->stm)];
    }

    /* Consider additional gain from promotion moves */
    if (ISPROMOTION(move)) {
        gain += see_material[PROMOTION(move)+pos->stm];
        gain -= see_material[PAWN+pos->stm];
    }

    return gain;
}

static int quiescence(struct search_worker *worker, int depth, int alpha,
                      int beta)
{
    int                 score;
    int                 best_score;
    int                 static_score;
    uint32_t            move;
    bool                found_move;
    bool                in_check;
    bool                tt_found;
    struct tt_item      tt_item;
    struct position     *pos = &worker->pos;
    struct moveselector ms;

    /* Update search statistics */
    if (depth < 0) {
        worker->nodes++;
        worker->qnodes++;
    }

    /* Check if the selective depth should be updated */
    if (pos->sply > worker->seldepth) {
        worker->seldepth = pos->sply;
    }

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Reset the search tree for this ply */
    worker->pv_table[pos->sply].size = 0;

    /* Check if we should considered the game as a draw */
    if (board_is_repetition(pos) || (pos->fifty >= 100)) {
        return 0;
    }

    /* Evaluate the position */
    static_score = eval_evaluate(pos);

    /* If we have reached the maximum depth then we stop */
    if (pos->sply >= (MAX_PLY-1)) {
        return static_score;
    }

    /*
     * Allow a "do nothing" option to avoid playing into bad captures. For
     * instance if the only available capture looses a queen then this move
     * would never be played.
     */
    in_check = board_in_check(pos, pos->stm);
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

    /* Check if the position have been searched before */
    tt_found = hash_tt_lookup(pos, &tt_item);
    if (tt_found) {
        score = adjust_mate_score(pos, tt_item.score);
        if (check_tt_cutoff(&tt_item, 0, alpha, beta, score)) {
            return score;
        }
    }
    
    /* Search all moves */
    found_move = false;
    select_init_node(&ms, worker, true, in_check, tt_found?tt_item.move:NOMOVE);
    while (select_get_move(&ms, worker, &move)) {
        /*
         * Don't bother searching captures that
         * lose material according to SEE.
         */
        if (!in_check && ISCAPTURE(move) &&
            select_is_bad_capture_phase(&ms)) {
            continue;
        }

        /*
         * Futility pruning for the quiescence search (also known as delta
         * pruning). If the capture, even without a recapture, can't raise
         * alpha (with a certain margin) then it's probably not worth the
         * effort to search the move.
         */
        if (!in_check &&
            board_has_non_pawn(pos, FLIP_COLOR(pos->stm)) &&
            !is_pawn_push(pos, move) &&
            !board_move_gives_check(pos, move)) {
            if ((static_score+material_gain(pos, move)+DELTA_MARGIN) < alpha) {
                continue;
            }
        }

        /* Recursivly search the move */
        if (!board_make_move(pos, move)) {
            continue;
        }
        found_move = true;
        score = -quiescence(worker, depth-1, -beta, -alpha);
        board_unmake_move(pos);

        /* Check if we have found a better move */
        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                if (score >= beta) {
                    break;
                }
                alpha = score;
                update_pv(worker, move);
            }
        }
    }

    /*
     * In case the side to move is in check the all all moves are generated
     * so if no legal move was found then it must be checkmate.
     */
    return (in_check && !found_move)?-CHECKMATE+pos->sply:best_score;
}

static int search(struct search_worker *worker, int depth, int alpha, int beta,
                  bool try_null, uint32_t exclude_move)
{
    int                 score;
    int                 tt_score;
    int                 tb_score;
    int                 best_score;
    int                 static_score;
    int                 threshold;
    uint32_t            move;
    uint32_t            tt_move;
    uint32_t            best_move;
    int                 movenumber;
    bool                found_move;
    int                 reduction;
    int                 futility_pruning;
    bool                in_check;
    bool                gives_check;
    int                 tt_flag;
    bool                pv_node;
    bool                tactical;
    bool                extended;
    int                 new_depth;
    struct tt_item      tt_item;
    bool                tt_found;
    struct position     *pos = &worker->pos;
    bool                is_singular;
    struct movelist     quiets;
    int                 see_prune_margin[2];
    int                 hist;
    int                 chist;
    int                 fhist;
    struct moveselector ms;

    /* Set node type */
    pv_node = (beta-alpha) > 1;

    /* Setup margins for SEE pruning */
    see_prune_margin[0] = SEE_QUIET_MARGIN(depth);
    see_prune_margin[1] = SEE_TACTICAL_MARGIN(depth);

    /* Update search statistics */
    worker->nodes++;

    /* Check if we have reached the full depth of the search */
    if ((depth <= 0) || (pos->sply >= MAX_SEARCH_DEPTH)) {
        return quiescence(worker, 0, alpha, beta);
    }

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Check if the selective depth should be updated */
    if (pos->sply > worker->seldepth) {
        worker->seldepth = pos->sply;
    }

    /* Reset the search tree for this ply */
    worker->pv_table[pos->sply].size = 0;

    /*
     * Check if the game should be considered a draw. A position is
     * considered a draw already at the first repetition in order to
     * avoid accidently playing into a draw when the final repetition
     * is hidden just beyond the horizon. Stopping early also allow us
     * to spend more time analyzing other positions.
     */
    if (board_is_repetition(pos) || (pos->fifty >= 100)) {
        return 0;
    }

    /*
     * Check the main transposition table to see if the positon
     * have been searched before. If this a singular extension
     * search then a search is required so a cutoff should not
     * be done for this node.
     */
    tt_move = NOMOVE;
    tt_score = 0; 
    tt_found = hash_tt_lookup(pos, &tt_item);
    if (tt_found) {
        tt_move = tt_item.move;
        tt_score = adjust_mate_score(pos, tt_item.score);
        if (!pv_node && (tt_move != exclude_move) &&
            check_tt_cutoff(&tt_item, depth, alpha, beta, tt_score)) {
            return tt_score;
        }
    }

    /* Probe tablebases */
    if (worker->state->probe_wdl &&
        (BITCOUNT(pos->bb_all) <= (int)TB_LARGEST)) {
        if (probe_wdl_tables(worker, alpha, beta, &tb_score)) {
            return tb_score;
        }
    }

    /*
     * Evaluate the position in order to get a score
     * to use for pruning decisions.
     */
    static_score = tt_found?tt_item.eval_score:eval_evaluate(pos);
    pos->eval_stack[pos->sply].score = static_score;
    bool improving = (pos->sply >= 2 &&
                    static_score > pos->eval_stack[pos->sply-2].score);

    /* Reverse futility pruning */
    in_check = board_in_check(pos, pos->stm);
    if ((depth <= FUTILITY_DEPTH) &&
        !in_check &&
        !pv_node &&
        board_has_non_pawn(pos, pos->stm) &&
        ((static_score-futility_margin[depth]) >= beta)) {
        return static_score;
    }

    /*
     * Try Razoring. If the current score indicates that we are far below
     * alpha then we're in a really bad place and it's no point doing a
     * full search.
     */
    if (!in_check &&
        !pv_node &&
        (tt_move == NOMOVE) &&
        (depth <= RAZORING_DEPTH) &&
        ((static_score+razoring_margin[depth]) <= alpha)) {
        if (depth == 1) {
            return quiescence(worker, 0, alpha, beta);
        }

        threshold = alpha - razoring_margin[depth];
        score = quiescence(worker, 0, threshold, threshold+1);
        if (score <= threshold) {
            return score;
        }
    }

    /*
     * Null move pruning. If the opponent can't beat beta even when given
     * a free move then there is no point doing a full search. However
     * some care has to be taken since the idea will fail in zugzwang
     * positions.
     */
    if (try_null &&
        !in_check &&
        (depth > NULLMOVE_DEPTH) &&
        board_has_non_pawn(pos, pos->stm)) {
        reduction = NULLMOVE_BASE_REDUCTION + depth/NULLMOVE_DIVISOR;
        board_make_null_move(pos);
        score = -search(worker, depth-reduction-1, -beta, -beta+1, false,
                        NOMOVE);
        board_unmake_null_move(pos);
        if (score >= beta) {
            /*
             * Since the score is based on doing a null move a checkmate
             * score doesn't necessarilly indicate a forced mate. So
             * return beta instead in this case.
             */
            return score < KNOWN_WIN?score:beta;
        }
    }

    /*
     * Probcut. If there is a good capture and a reduced search confirms
     * that it is better than beta (with a certain margin) then it's
     * relativly safe to skip the search.
     */
    if (!pv_node && !in_check && (depth >= PROBCUT_DEPTH) &&
        board_has_non_pawn(&worker->pos, pos->stm)) {
        threshold = beta + PROBCUT_MARGIN;

        select_init_node(&ms, worker, true, in_check, tt_move);
        while (select_get_move(&ms, worker, &move)) {
            /*
             * Skip non-captures and captures that are not
             * good enough (according to SEE).
             */
            if (!ISCAPTURE(move) && !ISENPASSANT(move)) {
                continue;
            }
            if (!see_ge(pos, move, threshold-static_score)) {
                continue;
            }
            if (move == exclude_move) {
                continue;
            }

            /* Search the move */
            if (!board_make_move(pos, move)) {
                continue;
            }
            score = -search(worker, depth-PROBCUT_DEPTH+1, -threshold,
                            -threshold+1, true, NOMOVE);
            board_unmake_move(pos);
            if (score >= threshold) {
                return score;
            }
        }
    }

    /* Check if the move from the transposition table is singular */
    is_singular = false;
    if (depth >= SE_DEPTH &&
        (exclude_move == NOMOVE) &&
        (tt_move != NOMOVE) &&
        (tt_item.type == TT_BETA) &&
        (tt_item.depth >= (depth-3)) &&
        (abs(beta) < KNOWN_WIN) &&
        board_is_move_pseudo_legal(pos, tt_move)) {
        threshold = tt_score-2*depth;

        score = search(worker, depth/2, threshold-1, threshold, true, tt_move);
        if (score < threshold) {
            is_singular = true;
        }
    }

    /*
     * Decide if futility pruning should be tried for this node. The
     * basic idea is that if the current static evaluation plus a margin
     * is less than alpha then this position is probably lost so there is
     * no point searching further.
     */
    futility_pruning = false;
    if ((depth <= FUTILITY_DEPTH) &&
        ((static_score+futility_margin[depth]) <= alpha)) {
        futility_pruning = true;
    }

    /* Search all moves */
    quiets.size = 0;
    best_score = -INFINITE_SCORE;
    best_move = NOMOVE;
    tt_flag = TT_ALPHA;
    movenumber = 0;
    found_move = false;
    select_init_node(&ms, worker, false, in_check, tt_move);
    while (select_get_move(&ms, worker, &move)) {
        /*
         * If this a singular extension search then skip the move
         * that is expected to be singular.
         */
        if (move == exclude_move) {
            continue;
        }

        /* Various move properties */
        gives_check = board_move_gives_check(pos, move);
        tactical = ISTACTICAL(move) || in_check || gives_check;
        history_get_scores(worker, move, &hist, &chist, &fhist);

        /* Remeber all quiet moves */
        if (!ISTACTICAL(move)) {
            quiets.moves[quiets.size++] = move;
        }

        /* Pruning of moves at low depths */
        if (best_score > KNOWN_LOSS) {
            /*
             * If the futility pruning flag is set then prune all moves except
             * tactical ones.
             */
            if (futility_pruning && !tactical) {
                continue;
            }

            /*
             * LMP (Late Move Pruning). If a move is sorted late in the list and
             * it has not been good in the past then prune it unless there are
             * obvious tactics.
             */
            if (!pv_node &&
                (depth <= LMP_DEPTH) &&
                (movenumber > lmp_counts[depth]) &&
                (abs(alpha) < KNOWN_WIN) &&
                !tactical) {
                continue;
            }

            /* Prune moves that lose material according to SEE */
            if (depth < SEE_PRUNE_DEPTH &&
                !see_ge(pos, move, see_prune_margin[tactical])) {
                continue;
            }

            /* Prune moves based on continuation history */
            if (!tactical && (depth <= HISTORY_PRUNING_DEPTH)) {
                if (chist < counter_history_pruning_margin[depth]) {
                    continue;
                }
                if (fhist < followup_history_pruning_margin[depth]) {
                    continue;
                }
            }
        }

        new_depth = depth;
        extended = false;

        /* Singular extension */
        if (!extended && (move == tt_move) && is_singular) {
            new_depth++;
            extended = true;
        }

        /*
         * Extend checking moves unless SEE indicates
         * that the move is losing material.
         */
        if (!extended && gives_check && see_ge(pos, move, 0)) {
            new_depth++;
            extended = true;
        }

        /* Extend recaptures */
        if (!extended && pos->sply >= 1 && pv_node && !gives_check &&
            is_recapture(pos, move) && see_ge(pos, move, 0)) {
            new_depth++;
            extended = true;
        }

        /* Make the move */
        if (!board_make_move(pos, move)) {
            continue;
        }
        movenumber++;
        found_move = true;

        /*
         * LMR (Late Move Reduction). With good move ordering later moves
         * are unlikely to be good. Therefore search them to a reduced
         * depth. Exceptions are made for tactical moves, like captures and
         * promotions.
         */
        reduction = 0;
        if (!tactical && !extended && (new_depth > 2) && (movenumber > 1)) {
            reduction = lmr_reductions[MIN(new_depth, 63)][MIN(movenumber, 63)];
            reduction -= (CLAMP(((fhist+chist+hist)/5000), -2, 2));
            reduction += ((!pv_node && !improving)?1:0);
        }

        /* Recursivly search the move */
        reduction = CLAMP(reduction, 0, new_depth-1);
        if (best_score == -INFINITE_SCORE) {
            /*
             * Perform a full search until a pv move is found. Usually
             * this is the first move.
             */
            score = -search(worker, new_depth-1, -beta, -alpha, true, NOMOVE);
        } else {
            /* Perform a reduced depth search with a zero window */
            score = -search(worker, new_depth-reduction-1, -alpha-1, -alpha,
                            true, NOMOVE);

            /* Re-search with full depth if the move improved alpha */
            if ((score > alpha) && (reduction > 0)) {
                score = -search(worker, new_depth-1, -alpha-1, -alpha, true,
                                NOMOVE);
            }

            /*
             * Re-search with full depth and a full window if alpha was
             * improved. If this is not a pv node then the full window
             * is actually a null window so there is no need to re-search.
             */
            if (pv_node && (score > alpha)) {
                score = -search(worker, new_depth-1, -beta, -alpha, true,
                                NOMOVE);
            }
        }
        board_unmake_move(pos);

        /* Check if we have found a new best move */
        if (score > best_score) {
            best_score = score;
            best_move = move;

            /*
             * Check if the score is above the lower bound. In that
             * case a new PV move may have been found.
             */
            if (score > alpha) {
                /*
                 * Check if the score is above the upper bound. If it is then
                 * the move is "too good" and our opponent would never let
                 * us reach this position. This means that there is no need to
                 * search this position further.
                 */
                if (score >= beta) {
                    if (!ISTACTICAL(move) || !see_ge(pos, move, 0)) {
                        killer_add_move(worker, move);
                        counter_add_move(worker, move);
                    }
                    tt_flag = TT_BETA;
                    break;
                }

                /*
                 * Update the lower bound with the new score. Also
                 * update the principle variation with our new best move.
                 */
                tt_flag = TT_EXACT;
                alpha = score;
                update_pv(worker, move);
            }
        }
    }

    /* If the best move is a quiet move then update the history table */
    if (!ISTACTICAL(best_move) && (tt_flag == TT_BETA)) {
        history_update_tables(worker, &quiets, depth);
    }

    /*
     * If no legal move have been found then it is either checkmate
     * or stalemate. If the player is in check then it is checkmate
     * and so set the score to -CHECKMATE. Otherwise it is stalemate
     * so set the score to zero. In case of checkmate the current search
     * ply is also subtracted to make sure that a shorter mate results
     * in a higher score.
     */
    if (!found_move) {
        tt_flag = TT_EXACT;
        best_score = in_check?-CHECKMATE+pos->sply:0;
    }

    /* Store the result for this node in the transposition table */
    hash_tt_store(pos, best_move, depth, best_score, tt_flag, static_score);

    return best_score;
}

static int search_root(struct search_worker *worker, int depth, int alpha,
                       int beta)
{
    int                 score;
    int                 best_score;
    uint32_t            move;
    uint32_t            best_move;
    int                 tt_flag;
    struct position     *pos = &worker->pos;
    int                 in_check;
    int                 new_depth;
    struct movelist     quiets;
    bool                tt_found;
    struct tt_item      tt_item;
    struct moveselector ms;

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Reset the search tree for this ply */
    worker->pv_table[0].size = 0;

    /* Check the transposition table and initialize some helper variables */
    tt_found = hash_tt_lookup(pos, &tt_item);
    best_move = tt_found?tt_item.move:NOMOVE;
    in_check = board_in_check(pos, pos->stm);

    /* Trigger an update of the evaluation stack */
    (void)eval_evaluate(pos);

    /* Search all moves */
    quiets.size = 0;
    tt_flag = TT_ALPHA;
    best_score = -INFINITE_SCORE;
    worker->currmovenumber = 0;
    select_init_node(&ms, worker, false, in_check, best_move);
    while (select_get_move(&ms, worker, &move)) {
        if ((worker->multipv > 1) && is_multipv_move(worker, move)) {
            continue;
        }
        if ((worker->state->move_filter.size > 0) &&
            !is_filtered_move(worker, move)) {
            continue;
        }

        /* Send stats for the first worker */
        worker->currmovenumber++;
        worker->currmove = move;
        if (worker->id == 0)  {
            engine_send_move_info(worker);
        }

        /* Remeber all quiet moves */
        if (!ISTACTICAL(move)) {
            quiets.moves[quiets.size++] = move;
        }

        /* Make the move */
        if (!board_make_move(pos, move)) {
            continue;
        }

        /* Extend checking moves */
        new_depth = depth;
        if (board_in_check(pos, pos->stm)) {
            new_depth++;
        }

        /* Recursivly search the move */
        score = -search(worker, new_depth-1, -beta, -alpha, true, NOMOVE);
        board_unmake_move(pos);

        /* Check if a new best move have been found */
        if (score > best_score) {
            /* Update the best score and best move for this iteration */
            best_score = score;
            best_move = move;

            /* Check if the score is above the lower bound */
            if (score > alpha) {
                /*
                 * Check if the score is above the upper bound. If it is, then
                 * a re-search will be triggered with a larger aspiration
                 * window. So the search can be stopped directly in order to
                 * save some time.
                 */
                if (score >= beta) {
                    if (!ISTACTICAL(move) || !see_ge(pos, move, 0)) {
                        killer_add_move(worker, move);
                    }
                    tt_flag = TT_BETA;
                    break;
                }

                /*
                 * Update the lower bound with the new score. Also
                 * update the principle variation with our new best
                 * move.
                 */
                tt_flag = TT_EXACT;
                alpha = score;
                update_pv(worker, move);

                /*
                 * Update the best move and the ponder move. The moves
                 * are only updated when the score is inside the aspiration
                 * window since it's only then that the score can be trusted.
                 */
                worker->mpv_moves[worker->mpvidx] = move;
                copy_pv(&worker->pv_table[0],
                        &worker->mpv_lines[worker->mpvidx].pv);
                worker->mpv_lines[worker->mpvidx].score = score;
                worker->mpv_lines[worker->mpvidx].depth = worker->depth;
                worker->mpv_lines[worker->mpvidx].seldepth = worker->seldepth;
                if ((worker->id == 0) && (worker->multipv == 1)) {
                    engine_send_pv_info(worker->state, &worker->mpv_lines[0]);
                }
            }
        }
    }

    /* If the best move is a quiet move then update the history table */
    if (!ISTACTICAL(best_move) && (tt_flag == TT_BETA)) {
        history_update_tables(worker, &quiets, depth);
    }

    /* Store the result for this node in the transposition table */
    hash_tt_store(pos, best_move, depth, best_score, tt_flag,
                  eval_evaluate(pos));

    return best_score;
}

static void search_aspiration_window(struct search_worker *worker, int depth,
                                     int prev_score)
{
    int alpha;
    int beta;
    int awindex;
    int bwindex;
    int score;

    /*
     * Setup the next iteration. There is not much to gain
     * from having an aspiration window for the first few
     * iterations so an infinite window is used to start with.
     */
    awindex = 0;
    bwindex = 0;
    if (depth > 5) {
        alpha = prev_score - aspiration_window[awindex];
        beta = prev_score + aspiration_window[bwindex];
    } else {
        alpha = -INFINITE_SCORE;
        beta = INFINITE_SCORE;
    }

    /* Main iterative deepening loop */
    while (true) {
		/* Search */
        worker->depth = depth;
        worker->seldepth = 0;
        alpha = MAX(alpha, -INFINITE_SCORE);
        beta = MIN(beta, INFINITE_SCORE);
        score = search_root(worker, depth, alpha, beta);

        /*
         * If the score is outside of the alpha/beta bounds then
         * increase the window and re-search.
         */
        if (score <= alpha) {
            awindex++;
            alpha = score - aspiration_window[awindex];
            worker->resolving_root_fail = true;
            if ((worker->id == 0) && (worker->multipv == 1)) {
                engine_send_bound_info(worker, score, false);
            }
            continue;
        }
        if (score >= beta) {
            bwindex++;
            beta = score + aspiration_window[bwindex];
            if (worker->id == 0) {
                engine_send_bound_info(worker, score, true);
            }
            continue;
        }
        worker->resolving_root_fail = false;
        break;
    }
}

void search_init(void)
{
    int k;
    int l;

    for (k=1;k<64;k++) {
        for (l=1;l<64;l++) {
            lmr_reductions[k][l] = 0.5 + log(k)*log(l)/2.0;
        }
    }
}

void search_find_best_move(struct search_worker *worker)
{
    int score;
    int depth;
    int mpvidx;

    assert(valid_position(&worker->pos));

    /* Reset NNUE state */
    nnue_reset_state(&worker->pos);

    /* Setup the first iteration */
    depth = 1 + worker->id%2;

    /* Main search loop */
    score = 0;
    while (true) {
		/* Handle aborted searches */
        if (setjmp(worker->env) != 0) {
            break;
        }

        /* Multipv loop */
        for (mpvidx=0;mpvidx<worker->multipv;mpvidx++) {
            worker->mpvidx = mpvidx;
            search_aspiration_window(worker, depth, score);

            if ((worker->id == 0) && (worker->multipv > 1)) {
                engine_send_multipv_info(worker);
            }
        }
        score = worker->mpv_lines[0].score;

        /* Report iteration as completed */
        depth = smp_complete_iteration(worker);

        /*
         * Check if the score indicates a known win in
         * which case there is no point in searching any
         * further.
         */
        if (worker->state->exit_on_mate && !worker->state->pondering) {
            if ((score > KNOWN_WIN) || (score < (-KNOWN_WIN))) {
                smp_stop_all();
                break;
            }
        }

        /* Check if the worker has reached the maximum depth */
        if (depth > worker->state->sd) {
            smp_stop_all();
            break;
        }

        /*
         * Time management is handled by the master worker so  ordinary
         * workers can just continue with thye next iteration.
         */
        if (worker->id != 0) {
            continue;
        }

        /* Check if the is time for a new iteration */
        if (!tc_new_iteration(worker)) {
            smp_stop_all();
            break;
        }
    }

    /*
     * In some rare cases the search may reach the maximum depth. If this
     * happens while the engine is pondering then wait until a ponderhit
     * command is received so that the bestmove command is not sent too
     * early.
     */
	while ((worker->id == 0) && worker->state->pondering) {
		if (engine_wait_for_input(worker)) {
			smp_stop_all();
            break;
		}
		if (!worker->state->pondering) {
			smp_stop_all();
		}
	}
}
