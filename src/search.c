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
#include "polybook.h"
#include "debug.h"
#include "bitboard.h"
#include "tbprobe.h"

/* The number of plies to reduce the depth with when doing a null move */
#define NULLMOVE_REDUCTION 2

/* Calculates if it is time to check the clock and poll for commands */
#define CHECKUP(n) (((n)&1023)==0)

/* The depth at which to start considering futility pruning */
#define FUTILITY_DEPTH 3

/*
 * Margins used for futility pruning. The array should be
 * indexed by depth-1.
 */
static int futility_margin[FUTILITY_DEPTH] = {300, 500, 900};

/* The depth at which to start considering razoring */
#define RAZORING_DEPTH 3

/*
 * Margins used for razoring. The array should be
 * indexed by depth-1.
 */
static int razoring_margin[RAZORING_DEPTH] = {100, 200, 300};

/*
 * Aspiration window sizes. If the search fails low or high
 * then the window is set to the next size in order. The
 * last entry in the array should always be INFINITE_SCORE.
 */
static int aspiration_window[] = {25, 50, 100, 200, 400, INFINITE_SCORE};

static void update_history_table(struct gamestate *pos, uint32_t move,
                                 int depth)
{
    int side;
    int from;
    int to;

    if (ISCAPTURE(move) || ISENPASSANT(move)) {
        return;
    }

    /* Update the history table and rescale entries if necessary */
    from = FROM(move);
    to = TO(move);
    pos->history_table[pos->stm][from][to] += depth;
    if (pos->history_table[pos->stm][from][to] > MAX_HISTORY_SCORE) {
        for (side=0;side<NSIDES;side++) {
            for (from=0;from<NSQUARES;from++) {
                for (to=0;to<NSQUARES;to++) {
                    pos->history_table[side][from][to] /= 2;
                }
            }
        }
    }
}

static void add_killer_move(struct gamestate *pos, uint32_t move, int see_score)
{
    if ((ISCAPTURE(move) || ISENPASSANT(move)) && (see_score >= 0)) {
        return;
    }

    if (move == pos->killer_table[pos->sply][0]) {
        return;
    }

    pos->killer_table[pos->sply][1] = pos->killer_table[pos->sply][0];
    pos->killer_table[pos->sply][0] = move;
}

static bool probe_wdl_tables(struct gamestate *pos, int *score)
{
    unsigned int res;

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

static bool probe_dtz_tables(struct gamestate *pos, int *score)
{
    unsigned int res;
    int          wdl;
    int          promotion;
    int          flags;
    int          from;
    int          to;

    res = tb_probe_root(pos->bb_sides[WHITE], pos->bb_sides[BLACK],
                    pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING],
                    pos->bb_pieces[WHITE_QUEEN]|pos->bb_pieces[BLACK_QUEEN],
                    pos->bb_pieces[WHITE_ROOK]|pos->bb_pieces[BLACK_ROOK],
                    pos->bb_pieces[WHITE_BISHOP]|pos->bb_pieces[BLACK_BISHOP],
                    pos->bb_pieces[WHITE_KNIGHT]|pos->bb_pieces[BLACK_KNIGHT],
                    pos->bb_pieces[WHITE_PAWN]|pos->bb_pieces[BLACK_PAWN],
                    pos->fifty, pos->castle,
                    pos->ep_sq != NO_SQUARE?pos->ep_sq:0,
                    pos->stm == WHITE, NULL);
    if (res == TB_RESULT_FAILED) {
        return false;
    }
    wdl = TB_GET_WDL(res);
    switch (wdl) {
    case TB_LOSS:
        *score = -TABLEBASE_WIN;
        break;
    case TB_WIN:
        *score = TABLEBASE_WIN;
        break;
    case TB_BLESSED_LOSS:
    case TB_CURSED_WIN:
    case TB_DRAW:
    default:
        *score = 0;
        break;
    }

    from = TB_GET_FROM(res);
    to = TB_GET_TO(res);
    flags = NORMAL;
    promotion = NO_PIECE;
    if (TB_GET_EP(res) != 0) {
        flags = EN_PASSANT;
    } else {
        if (pos->pieces[to] != NO_PIECE) {
            flags |= CAPTURE;
        }
        switch (TB_GET_PROMOTES(res)) {
        case TB_PROMOTES_QUEEN:
            flags |= PROMOTION;
            promotion = QUEEN + pos->stm;
            break;
        case TB_PROMOTES_ROOK:
            flags |= PROMOTION;
            promotion = ROOK + pos->stm;
            break;
        case TB_PROMOTES_BISHOP:
            flags |= PROMOTION;
            promotion = BISHOP + pos->stm;
            break;
        case TB_PROMOTES_KNIGHT:
            flags |= PROMOTION;
            promotion = KNIGHT + pos->stm;
            break;
        case TB_PROMOTES_NONE:
        default:
            break;
        }
    }
    pos->root_moves.moves[0] = MOVE(from, to, promotion, flags);
    pos->root_moves.nmoves++;

    assert(board_is_move_pseudo_legal(pos, pos->root_moves.moves[0]));

    return true;
}

static void update_pv(struct gamestate *pos, uint32_t move)
{
    pos->pv_table[pos->sply].moves[0] = move;
    memcpy(&pos->pv_table[pos->sply].moves[1],
           &pos->pv_table[pos->sply+1].moves[0],
           pos->pv_table[pos->sply+1].length*sizeof(uint32_t));
    pos->pv_table[pos->sply].length = pos->pv_table[pos->sply+1].length + 1;
}

static void copy_pv(struct pv *from, struct pv *to)
{
    int k;

    to->length = from->length;
    for (k=0;k<from->length;k++) {
        to->moves[k] = from->moves[k];
    }
}

static bool checkup(struct gamestate *pos)
{
    bool ponderhit;

    assert(!pos->abort);

    if (!tc_check_time(pos)) {
        pos->abort = true;
        return true;
    }
    if (engine_check_input(pos, &ponderhit)) {
        pos->abort = true;
    }
	if (ponderhit) {
		tc_start_clock(pos);
		tc_allocate_time(pos);
		pos->pondering = false;
	}

    return pos->abort;
}

static int quiescence(struct gamestate *pos, int depth, int alpha, int beta)
{
    int      score;
    int      see_score;
    int      best_score;
    uint32_t move;
    bool     found_move;
    bool     in_check;

    /* Update search statistics */
    if (depth < 0) {
        pos->nodes++;
        pos->qnodes++;
    }

    /* Check if the time is up or if we have received a new command */
    if (pos->abort || CHECKUP(pos->nodes)) {
        if (checkup(pos)) {
            return 0;
        }
    }

    /* Reset the search tree for this ply */
    pos->pv_table[pos->sply].length = 0;

    /* Check if we should considered the game as a draw */
    if (board_is_repetition(pos) || (pos->fifty >= 100)) {
        return 0;
    }

    /* Evaluate the position */
    score = eval_evaluate(pos);

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
    select_init_node(pos, depth, true, false);
    (void)hash_tt_lookup(pos, 0, alpha, beta, &move, &score);
    select_set_tt_move(pos, move);

    /* Search all moves */
    found_move = false;
    while (select_get_quiscence_move(pos, &move, &see_score)) {
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
        score = -quiescence(pos, depth-1, -beta, -alpha);
        board_unmake_move(pos);
        if (pos->abort) {
            return 0;
        }

        /* Check if we have found a better move */
        if (score > best_score) {
            best_score = score;
            if (score > alpha) {
                if (score >= beta) {
                    break;
                }
                alpha = score;
            }
        }
    }

    /*
     * In case the side to move is in check the all all moves are generated
     * so if no legal move was found then it must be checkmate.
     */
    return (in_check && !found_move)?-CHECKMATE+pos->sply:best_score;
}

static int search(struct gamestate *pos, int depth, int alpha, int beta,
                  bool try_null)
{
    int      score;
    int      tb_score;
    int      see_score;
    int      best_score;
    uint32_t move;
    uint32_t best_move;
    int      movenumber;
    bool     found_move;
    int      reduction;
    int      futility_pruning;
    bool     in_check;
    int      tt_flag;
    bool     found_pv;
    bool     pv_node;

    /* Set node type */
    pv_node = (beta-alpha) > 1;

    /* Update search statistics */
    pos->nodes++;

    /* Check if we have reached the full depth of the search */
    if (depth <= 0) {
        return quiescence(pos, 0, alpha, beta);
    }

    /* Check if the time is up or if we have received a new command */
    if (pos->abort || CHECKUP(pos->nodes)) {
        if (checkup(pos)) {
            return 0;
        }
    }

    /* Check if the selective depth should be updated */
    if (pos->sply > pos->seldepth) {
        pos->seldepth = pos->sply;
    }

    /* Reset the search tree for this ply */
    pos->pv_table[pos->sply].length = 0;

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
    select_init_node(pos, depth, false, false);

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
    select_set_tt_move(pos, move);

    /* Probe tablebases */
    if (pos->probe_wdl && (BITCOUNT(pos->bb_all) <= pos->tb_men)) {
        if (probe_wdl_tables(pos, &tb_score)) {
            return tb_score;
        }
    }

    /*
     * Null move pruning. If the opponent can't beat beta even when given
     * a free move then there is no point doing a full search. However
     * some care has to be taken since the idea will fail in so-called
     * zugzwang positions (positions where all moves makes the position worse).
     */
    if (try_null && !in_check && ((depth-NULLMOVE_REDUCTION-1) > 0) &&
        board_has_non_pawn(pos, pos->stm)) {
        board_make_null_move(pos);
        score = -search(pos, depth-1-NULLMOVE_REDUCTION, -beta, -beta+1, false);
        board_unmake_null_move(pos);
        if (pos->abort) {
            return 0;
        }
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
     * Evaluate the position in order to get a score
     * to use for pruning decisions.
     */
    score = eval_evaluate(pos);

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
            return quiescence(pos, 0, alpha, beta);
        }
        depth--;
    }

    /*
     * Decide if futility pruning should be tried for this node. The
     * basic idea is that if the current static evaluation plus a margin
     * is less than alpha then this position is probably lost so there is
     * no point searching further.
     */
    futility_pruning = false;
    if ((depth <= FUTILITY_DEPTH) &&
        !in_check &&
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
    while (select_get_move(pos, &move, &see_score)) {
        if (!board_make_move(pos, move)) {
            continue;
        }
        movenumber++;
        found_move = true;

        /*
         * If the futility pruning flag is set then prune
         * all moves except very tactical ones. Also make
         * sure to search at least one move.
         */
        if (futility_pruning &&
            (movenumber > 1) &&
            !ISCAPTURE(move) &&
            !ISENPASSANT(move) &&
            !ISPROMOTION(move) &&
            !board_in_check(pos, pos->stm)) {
            board_unmake_move(pos);
            continue;
        }

        /*
         * LMR (Late Move Reduction). With good move ordering later moves
         * are unlikely to be good. Therefore search them to a reduced
         * depth. Exceptions are made for tactical moves, like captures and
         * promotions.
         */
        reduction = ((movenumber > 3) && (depth > 3) &&
                     (!ISCAPTURE(move)) && (!ISPROMOTION(move)) &&
                     (!in_check) && (!board_in_check(pos, pos->stm)))?1:0;
        if ((reduction > 0) && (movenumber > 6)) {
            reduction++;
        }

        /* Recursivly search the move */
        if (!found_pv) {
            /*
             * Perform a full search until a pv move is found. Usually
             * this is the first move.
             */
            score = -search(pos, depth-1, -beta, -alpha, true);
        } else {
            /* Perform a reduced depth search with a zero window */
            score = -search(pos, depth-1-reduction, -alpha-1, -alpha, true);

            /* Re-search with full depth if the move improved alpha */
            if ((score > alpha) && (reduction > 0) && !pos->abort) {
                score = -search(pos, depth-1, -alpha-1, -alpha, true);
            }

            /*
             * Re-search with full depth and a full window if alpha was
             * improved. If this is not a pv node then the full window
             * is actually a null window so there is no need to re-search.
             */
            if (pv_node && (score > alpha) && !pos->abort) {
                score = -search(pos, depth-1, -beta, -alpha, true);
            }
        }
        board_unmake_move(pos);
        if (pos->abort) {
            return 0;
        }

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
                    add_killer_move(pos, move, see_score);
                    tt_flag = TT_BETA;
                    break;
                }

                /*
                 * Update the lower bound with the new score. Also
                 * update the principle variation with our new best move.
                 */
                tt_flag = TT_EXACT;
                alpha = score;
                update_pv(pos, move);
                update_history_table(pos, move, depth);
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

static int search_root(struct gamestate *pos, int depth, int alpha, int beta)
{
    int      score;
    int      see_score;
    int      best_score;
    uint32_t move;
    uint32_t best_move;
    int      tt_flag;

    /* Reset the search tree for this ply */
    pos->pv_table[0].length = 0;

    /*
     * Initialize the move selector for this node. Also
     * initialize the best move found to the PV move.
     */
    select_init_node(pos, depth, false, true);
    (void)hash_tt_lookup(pos, depth, alpha, beta, &move, &score);
    select_set_tt_move(pos, move);
    best_move = move;
    pos->best_move = move;
	pos->ponder_move = NOMOVE;

    /* Update score for root moves */
    select_update_root_move_scores(pos);

    /* Search all moves */
    tt_flag = TT_ALPHA;
    best_score = -INFINITE_SCORE;
    pos->currmovenumber = 0;
    while (select_get_root_move(pos, &move, &see_score)) {
        /* Recursivly search the move */
        (void)board_make_move(pos, move);
        pos->currmovenumber++;
        pos->currmove = move;
        engine_send_move_info(pos);
        score = -search(pos, depth-1, -beta, -alpha, true);
        board_unmake_move(pos);
        if (pos->abort) {
            return 0;
        }

        /* Check if a new best move have been found */
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
                 * us reach this position. This means that we don't need to
                 * search this position further.
                 */
                if (score >= beta) {
                    add_killer_move(pos, move, see_score);
                    tt_flag = TT_BETA;
                    break;
                }

                /*
                 * Update the lower bound with the new score. Also
                 * update the principle variation with our new best move.
                 */
                tt_flag = TT_EXACT;
                alpha = score;
                update_pv(pos, move);
                engine_send_pv_info(pos, score);
                update_history_table(pos, move, depth);
            }

            /*
             * In case the search fails high don't update the
			 * returned best move. If the pv is longer than one
			 * then also update the ponder move. If there is no
			 * ponder move we have to overwrite the old one
			 * otherwise the ponder move may be illegal when there
			 * is a new move.
             */
            pos->best_move = move;
			pos->ponder_move = (pos->pv_table[0].length > 1)?
											pos->pv_table[0].moves[1]:NOMOVE;
        }
    }

    /* Store the result for this node in the transposition table */
    hash_tt_store(pos, best_move, depth, best_score, tt_flag);

    return best_score;
}

void search_reset_data(struct gamestate *pos)
{
    int k;
    int l;
    int m;

    pos->root_moves.nmoves = 0;

    pos->exit_on_mate = true;
    pos->silent = false;

    pos->sd = MAX_SEARCH_DEPTH;
    pos->nodes = 0;
    pos->qnodes = 0;
    pos->depth = 0;
    pos->seldepth = 0;
    pos->currmovenumber = 0;
    pos->currmove = 0;

    for (k=0;k<MAX_PLY;k++) {
        pos->killer_table[k][0] = NOMOVE;
        pos->killer_table[k][1] = NOMOVE;
    }

    for (k=0;k<NSIDES;k++) {
        for (l=0;l<NSQUARES;l++) {
            for (m=0;m<NSQUARES;m++) {
                pos->history_table[k][l][m] = 0;
            }
        }
    }
}

uint32_t search_find_best_move(struct gamestate *pos, bool pondering,
                               uint32_t *ponder_move)
{
    int       score;
    int       alpha = -INFINITE_SCORE;
    int       beta = INFINITE_SCORE;
    int       depth = 1;
    uint32_t  best_move = NOMOVE;
    int       awindex = 0;
    int       bwindex = 0;
    struct pv pv;
	bool      ponderhit;
	bool      analysis;

    assert(valid_board(pos));
    assert(ponder_move != NULL);

	/* Initialize ponder move */
	*ponder_move = NOMOVE;

	/*
	 * Try to guess if the search is part of a
	 * game or if it is for analysis.
	 */
	analysis = (pos->tc_type == TC_INFINITE) || (pos->root_moves.nmoves > 0);

    /* Try to find a move in the opening book */
    if (pos->use_own_book && pos->in_book) {
        best_move = polybook_probe(pos);
        if (best_move != NOMOVE) {
            return best_move;
        }
        pos->in_book = false;
    }

	/*
	 * Allocate time for the search and start the clock. In
	 * pondering mode the clock is started when the ponderhit
	 * command is received.
	 */
	if (!pondering) {
		tc_start_clock(pos);
		tc_allocate_time(pos);
	}

    /* Prepare for search */
    pos->probe_wdl = pos->use_tablebases;
    pos->root_in_tb = false;
    pos->root_tb_score = 0;
    pos->pondering = pondering;
    pos->ponder_move = NOMOVE;
    pos->sply = 0;
    pos->abort = false;
    pos->resolving_root_fail = false;
    hash_tt_age_table(pos);

    /* Probe tablebases for the root position */
    if (pos->use_tablebases && (BITCOUNT(pos->bb_all) <= pos->tb_men)) {
        pos->root_in_tb = probe_dtz_tables(pos, &pos->root_tb_score);
        pos->probe_wdl = !pos->root_in_tb;
    }

    /* If no root moves are specified then search all moves */
    if (pos->root_moves.nmoves == 0) {
        gen_legal_moves(pos, &pos->root_moves);
        assert(pos->root_moves.nmoves > 0);
    }
    best_move = pos->root_moves.moves[0];

    /* Main iterative deepening loop */
    while (tc_new_iteration(pos) && (depth <= pos->sd)) {
        /*
         * If there is only one legal move then there is no
         * need to do a search. Instead save the time for later.
         */
        if (pos->root_moves.nmoves == 1 && !pos->pondering && !analysis &&
            !pos->root_in_tb) {
            return pos->root_moves.moves[0];
        }

		/* Search */
        pos->depth = depth;
        alpha = MAX(alpha, -INFINITE_SCORE);
        beta = MIN(beta, INFINITE_SCORE);
        score = search_root(pos, depth, alpha, beta);

        /* Check if the search was interrupted for some reason */
        if (pos->abort) {
            assert(pos->best_move != NOMOVE);
			best_move = pos->best_move;
			*ponder_move = pos->ponder_move;
            break;
        }

        /*
         * If the score is outside of the alpha/beta bounds then
         * increase the window and re-search.
         */
        if (score <= alpha) {
            awindex++;
            alpha = score - aspiration_window[awindex];
            pos->resolving_root_fail = true;
            continue;
        }
        if (score >= beta) {
            bwindex++;
            beta = score + aspiration_window[bwindex];
            pos->resolving_root_fail = true;
            continue;
        }
        pos->resolving_root_fail = false;
        best_move = pos->best_move;
		*ponder_move = pos->ponder_move;

        /*
         * Check if the score indicates a known win in
         * which case there is no point in searching any
         * further.
         */
        if (pos->exit_on_mate && !pos->pondering) {
            if ((score > KNOWN_WIN) || (score < (-KNOWN_WIN))) {
                break;
            }
        }

        /*
         * Setup the next iteration. There is not much to gain
         * from having an aspiration window for the first few
         * iterations so an infinite window is used to start with.
         */
        copy_pv(&pos->pv_table[0], &pv);
        hash_tt_insert_pv(pos, &pv);
        depth++;
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

    /*
     * In some rare cases the search may reach the maximum depth. If this
     * happens while the engine is pondering then wait until a ponderhit
     * command is received so that the bestmove command is not sent too
     * early.
     */
	while (pos->pondering && !pos->abort) {
		sleep_ms(100);
		if (engine_check_input(pos, &ponderhit)) {
			pos->abort = true;
		}
		if (ponderhit) {
			pos->pondering = false;
		}
	}

    assert(valid_move(best_move));
    return best_move;
}

int search_get_quiscence_score(struct gamestate *pos)
{
    assert(valid_board(pos));

    search_reset_data(pos);
    pos->pondering = false;
    pos->ponder_move = NOMOVE;
    pos->sply = 0;
    pos->abort = false;
    pos->resolving_root_fail = false;

    tc_configure_time_control(pos, TC_INFINITE, 0, 0, 0);

    return quiescence(pos, 0, -INFINITE_SCORE, INFINITE_SCORE);
}
