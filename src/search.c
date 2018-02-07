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

#include "search.h"
#include "engine.h"
#include "timectl.h"
#include "board.h"
#include "movegen.h"
#include "moveselect.h"
#include "eval.h"
#include "validation.h"
#include "debug.h"
#include "hash.h"
#include "see.h"
#include "utils.h"
#include "debug.h"
#include "bitboard.h"
#include "tbprobe.h"
#include "smp.h"

/* Calculates if it is time to check the clock and poll for commands */
#define CHECKUP(n) (((n)&1023)==0)

/* Different exceptions that can happen during search */
#define EXCEPTION_COMMAND 1
#define EXCEPTION_STOP 2
#define EXCEPTION_TIMEOUT 3

/* Configuration constants for null move pruning */
#define NULLMOVE_DEPTH 3
#define NULLMOVE_BASE_REDUCTION 2
#define NULLMOVE_DIVISOR 6

/*
 * Margins used for futility pruning. The array should be
 * indexed by depth-1.
 */
#define FUTILITY_DEPTH 3
static int futility_margin[FUTILITY_DEPTH] = {300, 500, 900};

/*
 * Margins used for razoring. The array should be
 * indexed by depth-1.
 */
#define RAZORING_DEPTH 3
static int razoring_margin[RAZORING_DEPTH] = {100, 200, 300};

/*
 * Aspiration window sizes. If the search fails low or high
 * then the window is set to the next size in order. The
 * last entry in the array should always be INFINITE_SCORE.
 */
static int aspiration_window[] = {25, 50, 100, 200, 400, INFINITE_SCORE};

/* Move counts for the different depths to use for late move pruning */
#define LMP_DEPTH 6
static int lmp_counts[LMP_DEPTH+1] = {0, 5, 10, 20, 35, 55};

static void update_history_table(struct search_worker *worker, uint32_t move,
                                 int depth)
{
    int             side;
    int             from;
    int             to;
    struct position *pos;

    if (ISCAPTURE(move) || ISENPASSANT(move)) {
        return;
    }

    /* Update the history table and rescale entries if necessary */
    pos = &worker->pos;
    from = FROM(move);
    to = TO(move);
    worker->history_table[pos->stm][from][to] += depth;
    if (worker->history_table[pos->stm][from][to] > MAX_HISTORY_SCORE) {
        for (side=0;side<NSIDES;side++) {
            for (from=0;from<NSQUARES;from++) {
                for (to=0;to<NSQUARES;to++) {
                    worker->history_table[side][from][to] /= 2;
                }
            }
        }
    }
}

static void add_killer_move(struct search_worker *worker, uint32_t move,
                            int see_score)
{
    struct position *pos;

    if ((ISCAPTURE(move) || ISENPASSANT(move)) && (see_score >= 0)) {
        return;
    }

    pos = &worker->pos;
    if (move == worker->killer_table[pos->sply][0]) {
        return;
    }

    worker->killer_table[pos->sply][1] = worker->killer_table[pos->sply][0];
    worker->killer_table[pos->sply][0] = move;
}

static bool is_killer_move(struct search_worker *worker, uint32_t move)
{
    struct position *pos;

    pos = &worker->pos;
    return (worker->killer_table[pos->sply][0] == move) ||
            (worker->killer_table[pos->sply][1] == move);
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

static bool is_tactical_move(uint32_t move)
{
    return ISCAPTURE(move) || ISENPASSANT(move) || ISPROMOTION(move);
}

static bool probe_wdl_tables(struct search_worker *worker, int *score)
{
    unsigned int    res;
    struct position *pos;

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

    *score = (res == TB_WIN)?TABLEBASE_WIN-pos->sply:
             (res == TB_LOSS)?-TABLEBASE_WIN+pos->sply:0;

    return true;
}

static void update_pv(struct search_worker *worker, uint32_t move)
{
    struct position *pos;

    pos = &worker->pos;
    worker->pv_table[pos->sply].moves[0] = move;
    memcpy(&worker->pv_table[pos->sply].moves[1],
           &worker->pv_table[pos->sply+1].moves[0],
           worker->pv_table[pos->sply+1].length*sizeof(uint32_t));
    worker->pv_table[pos->sply].length =
                                    worker->pv_table[pos->sply+1].length + 1;
}

static void checkup(struct search_worker *worker)
{
    /* Check if the worker is requested to stop */
    if (!worker->resolving_root_fail && smp_should_stop(worker)) {
        longjmp(worker->env, EXCEPTION_STOP);
    }

    /* Check if the time is up or if we have received a new command */
    if (!CHECKUP(worker->nodes)) {
        return;
    }

    /* Perform checkup */
    if (!tc_check_time(worker)) {
        longjmp(worker->env, EXCEPTION_TIMEOUT);
    }
    if ((worker->id == 0) && engine_check_input(worker)) {
        smp_stop_all();
        longjmp(worker->env, EXCEPTION_COMMAND);
    }
}

static int quiescence(struct search_worker *worker, int depth, int alpha,
                      int beta)
{
    int             score;
    int             see_score;
    int             best_score;
    uint32_t        move;
    bool            found_move;
    bool            in_check;
    struct position *pos;

    pos = &worker->pos;

    /* Update search statistics */
    if (depth < 0) {
        worker->nodes++;
        worker->qnodes++;
    }

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Reset the search tree for this ply */
    worker->pv_table[pos->sply].length = 0;

    /* Check if we should considered the game as a draw */
    if (board_is_repetition(pos) || (pos->fifty >= 100)) {
        return 0;
    }

    /* Evaluate the position */
    score = eval_evaluate(worker);

    /* If we have reached the maximum depth then we stop */
    if (pos->sply >= MAX_PLY) {
        return score;
    }

    /*
     * Allow a "do nothing" option to avoid playing into bad captures. For
     * instance if the only available capture looses a queen then this move
     * would never be played.
     */
    in_check = board_in_check(pos, pos->stm);
    best_score = -INFINITE_SCORE;
    if (!in_check) {
        best_score = score;
        if (score >= beta) {
            return score;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    /* Initialize the move selector for this node */
    select_init_node(worker, depth, true, false);
    (void)hash_tt_lookup(pos, 0, alpha, beta, &move, &score);
    select_set_tt_move(worker, move);

    /* Search all moves */
    found_move = false;
    while (select_get_quiscence_move(worker, &move, &see_score)) {
        /*
         * Don't bother searching captures that
         * lose material according to SEE.
         */
        if (!in_check && ISCAPTURE(move) && (see_score < 0)) {
            continue;
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
                  bool try_null)
{
    int             score;
    int             tb_score;
    int             see_score;
    int             best_score;
    uint32_t        move;
    uint32_t        best_move;
    int             movenumber;
    bool            found_move;
    int             reduction;
    int             futility_pruning;
    bool            in_check;
    int             tt_flag;
    bool            found_pv;
    bool            pv_node;
    bool            pawn_push;
    bool            killer;
    int             hist;
    bool            tactical;
    struct position *pos;

    pos = &worker->pos;

    /* Set node type */
    pv_node = (beta-alpha) > 1;

    /* Update search statistics */
    worker->nodes++;

    /* Check if we have reached the full depth of the search */
    if (depth <= 0) {
        return quiescence(worker, 0, alpha, beta);
    }

    /* Check if the time is up or if we have received a new command */
    checkup(worker);

    /* Check if the selective depth should be updated */
    if (pos->sply > worker->seldepth) {
        worker->seldepth = pos->sply;
    }

    /* Reset the search tree for this ply */
    worker->pv_table[pos->sply].length = 0;

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

    /* Initialize the move selector for this node */
    select_init_node(worker, depth, false, false);

    /*
     * Search one ply deeper if in check to
     * easier detect checkmates after a repeated
     * series of checks.
     */
    in_check = board_in_check(pos, pos->stm);
    if (in_check) {
        depth++;
    }

    /*
     * Check the main transposition table to see if the positon
     * have been searched before.
     */
    score = -INFINITE_SCORE;
    move = NOMOVE;
    if (hash_tt_lookup(pos, depth, alpha, beta, &move, &score)) {
        return score;
    }
    select_set_tt_move(worker, move);

    /* Probe tablebases */
    if (worker->state->probe_wdl &&
        (BITCOUNT(pos->bb_all) <= (int)TB_LARGEST)) {
        if (probe_wdl_tables(worker, &tb_score)) {
            return tb_score;
        }
    }

    /*
     * Evaluate the position in order to get a score
     * to use for pruning decisions.
     */
    score = eval_evaluate(worker);

    /* Reverse futility pruning */
    if ((depth <= FUTILITY_DEPTH) &&
        !in_check &&
        !pv_node &&
        board_has_non_pawn(pos, pos->stm) &&
        ((score-futility_margin[depth-1]) >= beta)) {
        return score;
    }

    /*
     * Try Razoring. If the current score indicates that we are far below
     * alpha then we're in a really bad place and it's no point doing a
     * full search.
     */
    if (!in_check &&
        !pv_node &&
        (move == NOMOVE) &&
        (depth <= RAZORING_DEPTH) &&
        ((score+razoring_margin[depth-1]) <= alpha)) {
        if (depth == 1) {
            return quiescence(worker, 0, alpha, beta);
        }
        depth--;
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
        score = -search(worker, depth-reduction-1, -beta, -beta+1, false);
        board_unmake_null_move(pos);
        if (score >= beta) {
            /*
             * Since the score is based on doing a null move a checkmate
             * score doesn't necessarilly indicate a forced mate. So
             * return beta instead in this case.
             */
            return score < FORCED_MATE?score:beta;
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
        ((score+futility_margin[depth-1]) <= alpha)) {
        futility_pruning = true;
    }

    /* Search all moves */
    best_score = -INFINITE_SCORE;
    best_move = NOMOVE;
    tt_flag = TT_ALPHA;
    movenumber = 0;
    found_move = false;
    found_pv = false;
    while (select_get_move(worker, &move, &see_score)) {
        pawn_push = is_pawn_push(pos, move);
        killer = is_killer_move(worker, move);
        hist = worker->history_table[pos->stm][FROM(move)][TO(move)];
        if (!board_make_move(pos, move)) {
            continue;
        }
        tactical = is_tactical_move(move) || in_check ||
                    board_in_check(pos, pos->stm);
        movenumber++;
        found_move = true;

        /*
         * If the futility pruning flag is set then prune all moves except
         * tactical ones. Also make sure to search at least one move.
         */
        if (futility_pruning &&
            (movenumber > 1) &&
            !tactical) {
            board_unmake_move(pos);
            continue;
        }

        /*
         * LMP (Late Move Pruning). If a move is sorted late in the list and
         * it has not been good in the past then prune it unless there are
         * obvious tactics. Also make sure to search at least one move.
         */
        if (!pv_node &&
            (depth < LMP_DEPTH) &&
            (movenumber > lmp_counts[depth]) &&
            (movenumber > 1) &&
            !tactical &&
            !pawn_push &&
            !killer &&
            (abs(alpha) < KNOWN_WIN) &&
            (hist == 0)) {
            board_unmake_move(pos);
            continue;
        }

        /*
         * LMR (Late Move Reduction). With good move ordering later moves
         * are unlikely to be good. Therefore search them to a reduced
         * depth. Exceptions are made for tactical moves, like captures and
         * promotions.
         */
        reduction = ((movenumber > 3) && (depth > 3) && !tactical)?1:0;
        if ((reduction > 0) && (movenumber > 6)) {
            reduction++;
        }

        /* Recursivly search the move */
        if (!found_pv) {
            /*
             * Perform a full search until a pv move is found. Usually
             * this is the first move.
             */
            score = -search(worker, depth-1, -beta, -alpha, true);
        } else {
            /* Perform a reduced depth search with a zero window */
            score = -search(worker, depth-reduction-1, -alpha-1, -alpha, true);

            /* Re-search with full depth if the move improved alpha */
            if ((score > alpha) && (reduction > 0)) {
                score = -search(worker, depth-1, -alpha-1, -alpha, true);
            }

            /*
             * Re-search with full depth and a full window if alpha was
             * improved. If this is not a pv node then the full window
             * is actually a null window so there is no need to re-search.
             */
            if (pv_node && (score > alpha)) {
                score = -search(worker, depth-1, -beta, -alpha, true);
            }
        }
        board_unmake_move(pos);

        /* Check if we have found a new best move */
        if (score > best_score) {
            best_score = score;
            best_move = move;
            found_pv = true;

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
                    add_killer_move(worker, move, see_score);
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
                update_history_table(worker, move, depth);
            }
        }
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
    hash_tt_store(pos, best_move, depth, best_score, tt_flag);

    return best_score;
}

static int search_root(struct search_worker *worker, int depth, int alpha,
                       int beta)
{
    int             score;
    int             see_score;
    int             best_score;
    uint32_t        move;
    uint32_t        best_move;
    int             tt_flag;
    struct position *pos;

    pos = &worker->pos;

    checkup(worker);

    /* Reset the search tree for this ply */
    worker->pv_table[0].length = 0;

    /*
     * Initialize the move selector for this node. Also
     * initialize the best move found to the PV move.
     */
    select_init_node(worker, depth, false, true);
    (void)hash_tt_lookup(pos, depth, alpha, beta, &move, &score);
    select_set_tt_move(worker, move);
    best_move = move;
    worker->best_move = move;
	worker->ponder_move = NOMOVE;

    /* Update score for root moves */
    select_update_root_move_scores(worker);

    /* Search all moves */
    tt_flag = TT_ALPHA;
    best_score = -INFINITE_SCORE;
    worker->currmovenumber = 0;
    while (select_get_root_move(worker, &move, &see_score)) {
        /* Send stats for the first worker */
        worker->currmovenumber++;
        worker->currmove = move;
        if ((worker->id == 0) &&
            (worker->depth > worker->state->completed_depth))  {
            engine_send_move_info(worker);
        }

        /* Recursivly search the move */
        (void)board_make_move(pos, move);
        score = -search(worker, depth-1, -beta, -alpha, true);
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
                    add_killer_move(worker, move, see_score);
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
                update_history_table(worker, move, depth);

                /* Send stats for the first worker */
                if ((worker->id == 0) &&
                    (worker->depth > worker->state->completed_depth))  {
                    engine_send_pv_info(worker, &worker->pv_table[0],
                                        worker->depth, worker->seldepth,
                                        best_score, smp_nodes());
                }

                /*
                 * Update the best move from the search and the ponder move.
                 * The moves are only updated when the score is inside the
                 * aspiration window since it's only then that the score can
                 * be trusted.
                 */
                worker->best_move = move;
                worker->best_score = best_score;
                worker->ponder_move = (worker->pv_table[0].length > 1)?
                                            worker->pv_table[0].moves[1]:NOMOVE;
            }
        }
    }

    /* Store the result for this node in the transposition table */
    hash_tt_store(pos, best_move, depth, best_score, tt_flag);

    return best_score;
}

void search_reset_data(struct gamestate *state)
{
    state->root_moves.nmoves = 0;
    state->exit_on_mate = true;
    state->silent = false;
    state->sd = MAX_SEARCH_DEPTH;
}

void search_find_best_move(struct search_worker *worker)
{
    volatile uint32_t best_move;
    volatile uint32_t ponder_move;
    volatile int      alpha;
    volatile int      beta;
    volatile int      awindex;
    volatile int      bwindex;
    int               score;
    int               depth;
    int               exception;

    assert(valid_position(&worker->pos));

    /* Initialize best move information */
    best_move = worker->root_moves.moves[0];
    ponder_move = NOMOVE;

    /* Setup the first iteration */
    depth = 1 + worker->id%2;
    alpha = -INFINITE_SCORE;
    beta = INFINITE_SCORE;
    awindex = 0;
    bwindex = 0;

    /* Main iterative deepening loop */
    while (tc_new_iteration(worker) && (depth <= worker->state->sd)) {
		/* Search */
        exception = setjmp(worker->env);
        if (exception == 0) {
            worker->depth = depth;
            worker->seldepth = 0;
            alpha = MAX(alpha, -INFINITE_SCORE);
            beta = MIN(beta, INFINITE_SCORE);
            score = search_root(worker, depth, alpha, beta);
        } else {
            assert(worker->best_move != NOMOVE);
            best_move = worker->best_move;
            ponder_move = worker->ponder_move;
            break;
        }

        /*
         * If the score is outside of the alpha/beta bounds then
         * increase the window and re-search.
         */
        if (score <= alpha) {
            awindex++;
            alpha = score - aspiration_window[awindex];
            worker->resolving_root_fail = true;
            continue;
        }
        if (score >= beta) {
            bwindex++;
            beta = score + aspiration_window[bwindex];
            continue;
        }
        worker->resolving_root_fail = false;

        /* Update best move */
        best_move = worker->best_move;
        ponder_move = worker->ponder_move;

        /* Report iteration as completed */
        smp_complete_iteration(worker, &depth);

        /*
         * Check if the score indicates a known win in
         * which case there is no point in searching any
         * further.
         */
        if (worker->state->exit_on_mate && !worker->state->pondering) {
            if ((score > KNOWN_WIN) || (score < (-KNOWN_WIN))) {
                break;
            }
        }

        /*
         * Setup the next iteration. There is not much to gain
         * from having an aspiration window for the first few
         * iterations so an infinite window is used to start with.
         */
        awindex = 0;
        bwindex = 0;
        if (depth > 5) {
            alpha = score - aspiration_window[awindex];
            beta = score + aspiration_window[bwindex];
        } else {
            alpha = -INFINITE_SCORE;
            beta = INFINITE_SCORE;
        }
    }

    /* Stop all other workers */
    smp_stop_all();

    /* Copy information back about the best move before returning */
    worker->best_move = best_move;
    worker->ponder_move = ponder_move;

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

int search_get_quiscence_score(struct gamestate *state, struct pv *pv)
{
    struct search_worker *worker;
    int                  k;
    int                  l;
    int                  m;
    int                  score;

    tc_configure_time_control(TC_INFINITE, 0, 0, 0);

    worker = malloc(sizeof(struct search_worker));
    memset(worker, 0, sizeof(struct search_worker));

    search_reset_data(state);
    state->pondering = false;
    state->probe_wdl = false;
    state->sd = 0;
    state->silent = true;

    worker->pos = state->pos;
    worker->root_moves = state->root_moves;
    for (k=0;k<MAX_PLY;k++) {
        worker->killer_table[k][0] = NOMOVE;
        worker->killer_table[k][1] = NOMOVE;
    }
    for (k=0;k<NSIDES;k++) {
        for (l=0;l<NSQUARES;l++) {
            for (m=0;m<NSQUARES;m++) {
                worker->history_table[k][l][m] = 0;
            }
        }
    }
    worker->depth = 0;
    worker->nodes = 0;
    worker->id = 0;
    worker->resolving_root_fail = false;
    worker->ppms[0].nmoves = 0;
    worker->pawntt = NULL;
    worker->pawntt_size = 0;
    worker->state = state;
    worker->pos.state = state;
    worker->pos.worker = worker;

    pv->length = 0;
    score = quiescence(worker, 0, -INFINITE_SCORE, INFINITE_SCORE);
    copy_pv(&worker->pv_table[0], pv);

    free(worker);

    return score;
}
