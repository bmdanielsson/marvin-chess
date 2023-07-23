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
#include "position.h"
#include "movegen.h"
#include "moveselect.h"
#include "eval.h"
#include "validation.h"
#include "hash.h"
#include "see.h"
#include "utils.h"
#include "debug.h"
#include "bitboard.h"
#include "smp.h"
#include "fen.h"
#include "history.h"
#include "nnue.h"
#include "data.h"
#include "smp.h"

/* Calculates if it is time to check the clock and poll for commands */
#define CHECKUP(n) (((n)&1023)==0)

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

/* Aspiration window constants */
#define INITIAL_ASPIRATION_WINDOW 10
#define MAX_ASPIRATION_WINDOW 700

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

static bool check_tb_cutoff(int score, int alpha, int beta)
{
    return (((score < 0) && (score <= alpha)) ||
            ((score > 0) && (score >= beta)) ||
            (score == 0));
}

static int adjust_mate_score(struct position *pos, int score)
{
    if (score > KNOWN_WIN) {
        return score - pos->height;
    } else if (score < -KNOWN_WIN) {
        return score + pos->height;
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

static void update_pv(struct search_worker *worker, uint32_t move)
{
    struct position *pos;

    pos = &worker->pos;
    worker->pv_table[pos->height].moves[0] = move;
    memcpy(&worker->pv_table[pos->height].moves[1],
           &worker->pv_table[pos->height+1].moves[0],
           worker->pv_table[pos->height+1].size*sizeof(uint32_t));
    worker->pv_table[pos->height].size =
                                    worker->pv_table[pos->height+1].size + 1;
}

void copy_pv(struct movelist *from, struct movelist *to)
{
    int k;

    to->size = from->size;
    for (k=0;k<from->size;k++) {
        to->moves[k] = from->moves[k];
    }
}

static void checkup(struct search_worker *worker)
{
    /* Check if the worker is requested to stop */
    if (smp_should_stop()) {
        longjmp(worker->env, 1);
    }

    /* Only check time limits for the main worker */
    if (worker->id != 0) {
        return;
    }

    /* Check if the node limit has been reached */
    if (((tc_get_flags()&TC_NODE_LIMIT) != 0) &&
        (smp_nodes() >= worker->state->max_nodes)) {
        smp_stop_all();
        longjmp(worker->env, 1);
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
        longjmp(worker->env, 1);
    }
    if (engine_check_input(worker)) {
        smp_stop_all();
        longjmp(worker->env, 1);
    }
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
    struct unmake       *prev = &pos->history[pos->ply-1];

    /* Update search statistics */
    if (depth < 0) {
        worker->nodes++;
        worker->qnodes++;
    }

    /* Check if the selective depth should be updated */
    if (pos->height > worker->seldepth) {
        worker->seldepth = pos->height;
    }

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Reset the search tree for this ply */
    worker->pv_table[pos->height].size = 0;

    /* Check if we should considered the game as a draw */
    if (pos_is_repetition(pos) || (pos->fifty >= 100)) {
        return 0;
    }

    /* Evaluate the position */
    static_score = eval_evaluate(pos, false);

    /* If we have reached the maximum depth then we stop */
    if (pos->height >= (MAX_PLY-1)) {
        return static_score;
    }

    /*
     * Allow a "do nothing" option to avoid playing into bad captures. For
     * instance if the only available capture looses a queen then this move
     * would never be played.
     */
    in_check = pos_in_check(pos, pos->stm);
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
    select_init_node(&ms, worker, true, in_check, tt_found?tt_item.move:NOMOVE,
                     depth==0, ISCAPTURE(prev->move)?TO(prev->move):NO_SQUARE,
                     depth);
    while (select_get_move(&ms, worker, &move)) {
        /*
         * Don't bother searching captures that
         * lose material according to SEE.
         */
        if (!in_check && ISCAPTURE(move) &&
            select_is_bad_capture_phase(&ms)) {
            continue;
        }
        if (!in_check && !ISTACTICAL(move) && !see_ge(pos, move, 0)) {
            continue;
        }

        /* Recursivly search the move */
        if (!pos_make_move(pos, move)) {
            continue;
        }
        found_move = true;
        score = -quiescence(worker, depth-1, -beta, -alpha);
        pos_unmake_move(pos);

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
    return (in_check && !found_move)?-CHECKMATE+pos->height:best_score;
}

static int search(struct search_worker *worker, int depth, int alpha, int beta,
                  bool try_null, uint32_t exclude_move)
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
    uint32_t            tt_move;
    int                 tt_score;
    struct moveselector ms;
    bool                extended;
    bool                tactical;
    int                 hist;
    int                 chist;
    int                 fhist;
    int                 reduction;
    bool                gives_check;
    int                 movenumber;
    int                 see_prune_margin[2];
    bool                pv_node;
    bool                is_root;
    int                 static_score;
    bool                improving;
    int                 tb_score;
    int                 threshold;
    bool                is_singular;
    bool                futility_pruning;
    bool                found_move;

    /* Set node type */
    pv_node = (beta-alpha) > 1;
    is_root = pos->height == 0;

    /* Setup margins for SEE pruning */
    see_prune_margin[0] = SEE_QUIET_MARGIN(depth);
    see_prune_margin[1] = SEE_TACTICAL_MARGIN(depth);

    /* Update search statistics */
    worker->nodes++;

    /* Check if we have reached the full depth of the search */
    if ((depth <= 0) || (pos->height >= MAX_SEARCH_DEPTH)) {
        return quiescence(worker, 0, alpha, beta);
    }

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Check if the selective depth should be updated */
    if (pos->height > worker->seldepth) {
        worker->seldepth = pos->height;
    }

    /* Reset the search tree for this ply */
    worker->pv_table[pos->height].size = 0;

    /*
     * Check if the game should be considered a draw. A position is
     * considered a draw already at the first repetition in order to
     * avoid accidently playing into a draw when the final repetition
     * is hidden just beyond the horizon. Stopping early also allow us
     * to spend more time analyzing other positions.
     */
    if (!is_root && (pos_is_repetition(pos) || (pos->fifty >= 100))) {
        return 0;
    }

    /*
     * Check the main transposition table to see if the positon
     * have been searched before. If this a singular extension
     * search then a search is required so a cutoff should not
     * be done for this node.
     */
    best_move = NOMOVE;
    tt_move = NOMOVE;
    tt_score = 0;
    tt_found = hash_tt_lookup(pos, &tt_item);
    if (tt_found) {
        best_move = is_root?tt_item.move:NOMOVE;
        tt_move = tt_item.move;
        tt_score = adjust_mate_score(pos, tt_item.score);
        if (!pv_node && (tt_move != exclude_move) &&
            check_tt_cutoff(&tt_item, depth, alpha, beta, tt_score)) {
            return tt_score;
        }
    }

    /* Probe tablebases */
    if (!is_root && worker->state->probe_wdl && egtb_should_probe(pos)) {
        if (egtb_probe_wdl_tables(pos, &tb_score)) {
            worker->tbhits++;
            if (check_tb_cutoff(tb_score, alpha, beta)) {
                return tb_score;
            }
        }
    }

    /*
     * Evaluate the position in order to get a score
     * to use for pruning decisions.
     */
    static_score = eval_evaluate(pos, false);
    improving = (pos->height >= 2 &&
                        static_score > pos->eval_stack[pos->height-2].score);

    /* Find out if the side to move is in check */
    in_check = pos_in_check(pos, pos->stm);

    /* Additional pruning for non-root nodes */
    is_singular = false;
    futility_pruning = false;
    if (!is_root) {
        /* Reverse futility pruning */
        if ((depth <= FUTILITY_DEPTH) && !in_check && !pv_node &&
            pos_has_non_pawn(pos, pos->stm) &&
            ((static_score-futility_margin[depth]) >= beta)) {
            return static_score;
        }

        /*
         * Try Razoring. If the current score indicates that we are far below
         * alpha then we're in a really bad place and it's no point doing a
         * full search.
         */
        if (!in_check && !pv_node && (tt_move == NOMOVE) &&
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
            pos_has_non_pawn(pos, pos->stm)) {
            reduction = NULLMOVE_BASE_REDUCTION + depth/NULLMOVE_DIVISOR;
            pos_make_null_move(pos);
            score = -search(worker, depth-reduction-1, -beta, -beta+1, false,
                            NOMOVE);
            pos_unmake_null_move(pos);
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
            pos_has_non_pawn(&worker->pos, pos->stm)) {
            threshold = beta + PROBCUT_MARGIN;

            select_init_node(&ms, worker, true, in_check, tt_move, false, NO_SQUARE, depth);
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
                if (!pos_make_move(pos, move)) {
                    continue;
                }
                score = -search(worker, depth-PROBCUT_DEPTH+1, -threshold,
                                -threshold+1, true, NOMOVE);
                pos_unmake_move(pos);
                if (score >= threshold) {
                    return score;
                }
            }
        }

        /* Check if the move from the transposition table is singular */
        if (depth >= SE_DEPTH &&
            (exclude_move == NOMOVE) &&
            (tt_move != NOMOVE) &&
            (tt_item.type == TT_BETA) &&
            (tt_item.depth >= (depth-3)) &&
            (abs(beta) < KNOWN_WIN) &&
            pos_is_move_pseudo_legal(pos, tt_move)) {
            threshold = tt_score-2*depth;

            score = search(worker, depth/2, threshold-1, threshold, true,
                           tt_move);
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
        if ((depth <= FUTILITY_DEPTH) &&
            ((static_score+futility_margin[depth]) <= alpha)) {
            futility_pruning = true;
        }
    }

    /* Search all moves */
    quiets.size = 0;
    tt_flag = TT_ALPHA;
    best_score = -INFINITE_SCORE;
    movenumber = 0;
    found_move = false;
    select_init_node(&ms, worker, false, in_check, tt_move, false, NO_SQUARE, depth);
    while (select_get_move(&ms, worker, &move)) {
        if (is_root && (worker->multipv > 1) && is_multipv_move(worker, move)) {
            continue;
        }
        if (is_root && (worker->state->move_filter.size > 0) &&
            !is_filtered_move(worker, move)) {
            continue;
        }

        /*
         * If this a singular extension search then skip the move
         * that is expected to be singular.
         */
        if (move == exclude_move) {
            continue;
        }

        /* Various move properties */
        gives_check = pos_move_gives_check(pos, move);
        tactical = ISTACTICAL(move) || in_check || gives_check;
        history_get_scores(worker, move, &hist, &chist, &fhist);

        /* Remeber all quiet moves */
        if (!ISTACTICAL(move)) {
            quiets.moves[quiets.size++] = move;
        }

        /* Pruning of moves at low depths */
        if (!is_root && (best_score > KNOWN_LOSS)) {
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

        /* Setup reductions and extensions */
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
        if (!extended && gives_check && (is_root || see_ge(pos, move, 0))) {
            new_depth++;
            extended = true;
        }

        /* Extend recaptures */
        if (!is_root && !extended && pos->height >= 1 && pv_node &&
            !gives_check && is_recapture(pos, move) && see_ge(pos, move, 0)) {
            new_depth++;
            extended = true;
        }

        /* Make the move */
        if (!pos_make_move(pos, move)) {
            continue;
        }
        movenumber++;
        found_move = true;

        /* Send stats for the first worker */
        if (is_root && worker->id == 0)  {
            engine_send_move_info(worker, movenumber, move);
        }

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
        pos_unmake_move(pos);

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
                        if (!is_root) {
                            counter_add_move(worker, move);
                        }
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
                if (is_root) {
                    worker->mpv_moves[worker->mpvidx] = move;
                    copy_pv(&worker->pv_table[0],
                            &worker->mpv_lines[worker->mpvidx].pv);
                    worker->mpv_lines[worker->mpvidx].score = score;
                    worker->mpv_lines[worker->mpvidx].depth = worker->depth;
                    worker->mpv_lines[worker->mpvidx].seldepth =
                                                            worker->seldepth;
                    if ((worker->id == 0) && (worker->multipv == 1)) {
                        engine_send_pv_info(worker->state,
                                            &worker->mpv_lines[0]);
                    }
                }
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
    if (!is_root && !found_move) {
        tt_flag = TT_EXACT;
        best_score = in_check?-CHECKMATE+pos->height:0;
    }

    /* Store the result for this node in the transposition table */
    hash_tt_store(pos, best_move, depth, best_score, tt_flag);

    return best_score;
}

static void search_aspiration_window(struct search_worker *worker, int depth,
                                     int prev_score)
{
    int alpha;
    int beta;
    int awindow;
    int bwindow;
    int score;
    int reduce;

    /*
     * Setup the next iteration. There is not much to gain
     * from having an aspiration window for the first few
     * iterations so an infinite window is used to start with.
     */
    reduce = 0;
    awindow = INITIAL_ASPIRATION_WINDOW;
    bwindow = INITIAL_ASPIRATION_WINDOW;
    if (depth > 5) {
        alpha = prev_score - awindow;
        beta = prev_score + bwindow;
    } else {
        alpha = -INFINITE_SCORE;
        beta = INFINITE_SCORE;
    }

    /* Main iterative deepening loop */
    while (true) {
		/* Search */
        reduce = MIN(reduce, 3);
        worker->depth = depth;
        worker->seldepth = 0;
        alpha = MAX(alpha, -INFINITE_SCORE);
        beta = MIN(beta, INFINITE_SCORE);
        score = search(worker, depth-reduce, alpha, beta, false, NOMOVE);

        /*
         * If the score is outside of the alpha/beta bounds then
         * increase the window and re-search.
         */
        if (score <= alpha) {
            reduce = 0;
            awindow *= 2;
            if (awindow > MAX_ASPIRATION_WINDOW) {
                awindow = INFINITE_SCORE;
            }
            alpha = score - awindow;
            worker->resolving_root_fail = true;
            if ((worker->id == 0) && (worker->multipv == 1)) {
                engine_send_bound_info(worker, score, false);
            }
            continue;
        }
        if (score >= beta) {
            reduce++;
            bwindow *= 2;
            if (bwindow > MAX_ASPIRATION_WINDOW) {
                bwindow = INFINITE_SCORE;
            }
            beta = score + bwindow;
            if (worker->id == 0) {
                engine_send_bound_info(worker, score, true);
            }
            continue;
        }
        worker->resolving_root_fail = false;
        break;
    }
}

static thread_retval_t worker_search_func(void *data)
{
    struct search_worker *worker = data;
    int                  score;
    int                  depth;
    int                  mpvidx;

    assert(valid_position(&worker->pos));

    /* Reset NNUE state */
    nnue_reset_accumulator(&worker->pos);

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

    return (thread_retval_t)0;
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

uint32_t search_position(struct gamestate *state, bool pondering,
                         uint32_t *ponder_move, int *score)
{
    int                  k;
    struct search_worker *worker;
    struct pvinfo        *best_pv;
    struct movelist      legal;
    bool                 send_pv;
    uint32_t             best_move;
    uint32_t             move;

    assert(state != NULL);
    assert(valid_position(&state->pos));
    assert(smp_number_of_workers() > 0);

    /* Reset the best move information */
    best_move = NOMOVE;
    if (ponder_move != NULL) {
        *ponder_move = NOMOVE;
    }

	/*
	 * Allocate time for the search. In pondering mode time
     * is allocated when the engine stops pondering and
     * enters the normal search.
	 */
	if (!pondering) {
		tc_allocate_time();
	}

    /* Prepare for search */
    hash_tt_age_table();
    state->probe_wdl = true;
    state->root_in_tb = false;
    state->root_tb_score = 0;
    state->pondering = pondering;
    state->pos.height = 0;
    state->completed_depth = 0;

    /* Probe tablebases for the root position */
    if (egtb_should_probe(&state->pos) &&
        (state->move_filter.size == 0) &&
        (state->multipv == 1)) {
        state->root_in_tb = egtb_probe_dtz_tables(&state->pos, &move,
                                                  &state->root_tb_score);
        if (state->root_in_tb) {
            state->move_filter.moves[0] = move;
            state->move_filter.size = 1;
        }
        state->probe_wdl = !state->root_in_tb;
    }

    /*
     * Initialize the best move to the first legal root
     * move to make sure a legal move is always returned.
     */
    gen_legal_moves(&state->pos, &legal);
    if (legal.size == 0) {
        return NOMOVE;
    }
    if (state->move_filter.size == 0) {
        best_move = legal.moves[0];
    } else {
        best_move = state->move_filter.moves[0];
    }

    /* Check if the number of multipv lines have to be reduced */
    state->multipv = MIN(state->multipv, legal.size);
    if (state->move_filter.size > 0) {
        state->multipv = MIN(state->multipv, state->move_filter.size);
    }

    /*
     * If there is only one legal move then there is no
     * need to do a search. Instead save the time for later.
     */
    if ((legal.size == 1) &&
        !state->pondering &&
        ((tc_get_flags()&TC_TIME_LIMIT) != 0) &&
        ((tc_get_flags()&(TC_INFINITE_TIME|TC_FIXED_TIME)) == 0)) {
        return best_move;
    }

    /* Prepare workers for a new search */
    smp_prepare_workers(state);

    /* Start helpers */
    for (k=1;k<smp_number_of_workers();k++) {
        smp_start_worker(smp_get_worker(k), (thread_func_t)worker_search_func);
    }

    /* Start the master worker thread */
    (void)worker_search_func(smp_get_worker(0));

    /* Wait for all helpers to finish */
    for (k=1;k<smp_number_of_workers();k++) {
        smp_wait_for_worker(smp_get_worker(k));
    }

    /* Find the worker with the best move */
    worker = smp_get_worker(0);
    best_pv = &worker->mpv_lines[0];
    send_pv = false;
    if (state->multipv == 1) {
        for (k=1;k<smp_number_of_workers();k++) {
            worker = smp_get_worker(k);
            if (worker->mpv_lines[0].pv.size < 1) {
                continue;
            }
            if ((worker->mpv_lines[0].depth >= best_pv->depth) ||
                ((worker->mpv_lines[0].depth == best_pv->depth) &&
                 (worker->mpv_lines[0].score > best_pv->score))) {
                best_pv = &worker->mpv_lines[0];
                send_pv = true;
            }
        }
    }

    /*
     * If the best worker is not the first worker then send
     * an extra pv line to the GUI.
     */
    if (send_pv) {
        engine_send_pv_info(state, best_pv);
    }

    /* Get the best move and the ponder move */
    if (best_pv->pv.size >= 1) {
        best_move = best_pv->pv.moves[0];
        if (score != NULL) {
            *score = best_pv->score;
        }
        if (ponder_move != NULL) {
            *ponder_move = (best_pv->pv.size > 1)?best_pv->pv.moves[1]:NOMOVE;
        }
    }

    /* Reset move filter since it's not needed anymore */
    state->move_filter.size = 0;

    /* Reset workers */
    smp_reset_workers();

    return best_move;
}
