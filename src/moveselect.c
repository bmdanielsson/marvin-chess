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

#include "moveselect.h"
#include "movegen.h"
#include "validation.h"
#include "see.h"
#include "hash.h"
#include "eval.h"
#include "board.h"

/*
 * Different move generation phases.
 */
enum {
    /* Normal search */
    PHASE_TT,
    PHASE_GEN_CAPS,
    PHASE_GOOD_CAPS,
    PHASE_KILLER1,
    PHASE_KILLER2,
    PHASE_GEN_MOVES,
    PHASE_MOVES,
    PHASE_ADD_BAD_CAPS,
    PHASE_BAD_CAPS,
    /* Quiscence search */
    PHASE_GEN_QUISCENCE,
    PHASE_QUISCENCE,
    /* Check evasions */
    PHASE_GEN_EVASIONS,
    PHASE_EVASIONS
};

/* Base scores for move ordering */
#define BASE_SCORE_TT           6*BASE_SCORE_DELTA
#define BASE_SCORE_GOOD_CAPS    5*BASE_SCORE_DELTA
#define BASE_SCORE_KILLER1      4*BASE_SCORE_DELTA
#define BASE_SCORE_KILLER2      3*BASE_SCORE_DELTA
#define BASE_SCORE_NORMAL       2*BASE_SCORE_DELTA
#define BASE_SCORE_BAD_CAPS     BASE_SCORE_DELTA

static int calculate_see_score(struct position *pos, uint32_t move)
{
    /*
     * En passant captures can never loose material (from the
     * perspective of SEE), at worst the opponent just recaptures
     * in which case material is even. Too simplify assume that
     * on average we gain 1/2 of a pawn. This will cause en passant
     * captures to be sorted after captures that gain material but
     * before captures that trade evenly.
     */
    if (ISCAPTURE(move)) {
        return see_calculate_score(pos, move);
    } else if (ISENPASSANT(move)) {
        return see_material[PAWN+FLIP_COLOR(pos->stm)]/2;
    }
    return 0;
}

static void assign_root_score(struct search_worker *worker,
                              struct moveselect *ms, int idx)
{
    int             from;
    int             to;
    uint32_t        move;
    struct moveinfo *info;
    struct position *pos;

    pos = &worker->pos;
    info = &ms->moveinfo[idx];
    move = info->move;
    from = FROM(move);
    to = TO(move);

    /*
     * Assign a score to each move. Normal moves are scored based
     * on history heuristic and capture moves based on SEE.
     */
    if (move == ms->ttmove) {
        info->score = BASE_SCORE_TT;
    } else if ((ISCAPTURE(move) || ISENPASSANT(move)) && (info->see >= 0)) {
        info->score = info->see + BASE_SCORE_GOOD_CAPS;
    } else if (move == worker->killer_table[pos->sply][0]) {
        info->score = BASE_SCORE_KILLER1;
    } else if (move == worker->killer_table[pos->sply][1]) {
        info->score = BASE_SCORE_KILLER2;
    } else if (!(ISCAPTURE(move) || ISENPASSANT(move))) {
        info->score =
                    worker->history_table[pos->stm][from][to] + BASE_SCORE_NORMAL;
    } else {
        info->score = info->see + BASE_SCORE_BAD_CAPS;
    }
}

static void assign_score(struct search_worker *worker, struct moveselect *ms,
                         struct movelist *list, int iter)
{
    int             from;
    int             to;
    int             see;
    uint32_t        move;
    struct moveinfo *info;
    struct position *pos;

    pos = &worker->pos;

    /*
     * Moves in the transposition table and killer moves are
     * handled separately and should not be considered here.
     */
    move = list->moves[iter];
    if ((move == ms->ttmove) ||
        (move == worker->killer_table[pos->sply][0]) ||
        (move == worker->killer_table[pos->sply][1])) {
        return;
    }

    /* Calculate SEE score */
    see = calculate_see_score(pos, move);

    /*
     * If the SEE score is positive (normal moves or good captures) then
     * the move is appended to the moveinfo list. If the SEE score is
     * negative (bad captures) the the move is added to be bead capture list.
     */
    if (see >= 0) {
        info = &ms->moveinfo[ms->nmoves];
        ms->nmoves++;
    } else {
        info = &ms->badcapinfo[ms->nbadcaps];
        ms->nbadcaps++;
    }
    info->move = move;
    info->see = see;

    /*
     * Assign a score to each move. Normal moves are scored based
     * on history heuristic and capture moves based on SEE. The
     * transposition table move have already been searched so it's
     * no need to check for it here.
     */
    from = FROM(move);
    to = TO(move);
    if ((ISCAPTURE(move) || ISENPASSANT(move)) && (info->see >= 0)) {
        info->score = info->see + BASE_SCORE_GOOD_CAPS;
    } else if (!(ISCAPTURE(move) || ISENPASSANT(move))) {
        info->score =
                worker->history_table[pos->stm][from][to] + BASE_SCORE_NORMAL;
    } else {
        info->score = info->see + BASE_SCORE_BAD_CAPS;
    }
}

static void assign_quiscence_score(struct search_worker *worker,
                                   struct moveselect *ms, struct movelist *list,
                                   int iter)
{
    int             from;
    int             to;
    uint32_t        move;
    struct moveinfo *info;
    struct position *pos;

    pos = &worker->pos;

    /*
     * Copy the move to the move info list. The move from
     * the transposition table is skipped since it has
     * already been searched.
     */
    move = list->moves[iter];
    info = &ms->moveinfo[ms->nmoves];
    ms->nmoves++;
    info->move = move;

    /* Calculate a SEE score for all captures */
    info->see = calculate_see_score(pos, move);

    /*
     * Assign a score to each move. Normal moves are scored based
     * on history heuristic and capture moves based on SEE.
     */
    from = FROM(move);
    to = TO(move);
    if (move == ms->ttmove) {
        info->score = BASE_SCORE_TT;
    } else if ((ISCAPTURE(move) || ISENPASSANT(move)) && (info->see >= 0)) {
        info->score = info->see + BASE_SCORE_GOOD_CAPS;
    } else if (move == worker->killer_table[pos->sply][0]) {
        info->score = BASE_SCORE_KILLER1;
    } else if (move == worker->killer_table[pos->sply][1]) {
        info->score = BASE_SCORE_KILLER2;
    } else if (!(ISCAPTURE(move) || ISENPASSANT(move))) {
        info->score =
                worker->history_table[pos->stm][from][to] + BASE_SCORE_NORMAL;
    } else {
        info->score = info->see + BASE_SCORE_BAD_CAPS;
    }
}

static void assign_evasion_score(struct search_worker *worker,
                                 struct moveselect *ms, struct movelist *list,
                                 int iter)
{
    int             from;
    int             to;
    uint32_t        move;
    struct moveinfo *info;
    struct position *pos;

    pos = &worker->pos;

    /*
     * Copy the move to the move info list. The move from
     * the transposition table is skipped since it has
     * already been searched.
     */
    move = list->moves[iter];
    info = &ms->moveinfo[ms->nmoves];
    ms->nmoves++;
    info->move = move;

    /* Calculate a SEE score for all captures */
    info->see = calculate_see_score(pos, move);

    /*
     * Assign a score to each move. Normal moves are scored based
     * on history heuristic and capture moves based on SEE.
     */
    from = FROM(move);
    to = TO(move);
    if (move == ms->ttmove) {
        info->score = BASE_SCORE_TT;
    } else if ((ISCAPTURE(move) || ISENPASSANT(move)) && (info->see >= 0)) {
        info->score = info->see + BASE_SCORE_GOOD_CAPS;
    } else if (move == worker->killer_table[pos->sply][0]) {
        info->score = BASE_SCORE_KILLER1;
    } else if (move == worker->killer_table[pos->sply][1]) {
        info->score = BASE_SCORE_KILLER2;
    } else if (!(ISCAPTURE(move) || ISENPASSANT(move))) {
        info->score =
                worker->history_table[pos->stm][from][to] + BASE_SCORE_NORMAL;
    } else {
        info->score = info->see + BASE_SCORE_BAD_CAPS;
    }
}

static uint32_t select_move(struct moveselect *ms, int *see_score)
{
    int             iter;
    int             best;
    int             start;
    struct moveinfo temp;
    struct moveinfo *info_iter;
    struct moveinfo *info_best;

    /* Check if there are moves available */
    if (ms->idx >= ms->nmoves) {
        return NOMOVE;
    }

    /* Try the moves in order of their score */
    start = ms->idx;
    iter = start + 1;
    best = start;
    info_iter = &ms->moveinfo[iter];
    info_best = &ms->moveinfo[best];
    while (iter < ms->nmoves) {
        if (info_iter->score > info_best->score) {
            best = iter;
            info_best = &ms->moveinfo[best];
        }
        iter++;
        info_iter++;
    }

    /* Swap moves */
    if (best != start) {
        temp = *info_best;
        ms->moveinfo[best] = ms->moveinfo[start];
        ms->moveinfo[start] = temp;
    }

    if (see_score != NULL) {
        *see_score = ms->moveinfo[start].see;
    }
    return ms->moveinfo[start].move;
}

void select_init_node(struct search_worker *worker, bool qnode, bool root,
                      bool in_check)
{
    struct moveselect *ms;
    uint32_t          move;
    int               k;
    struct position *pos;

    pos = &worker->pos;
    ms = &worker->ppms[pos->sply];
    if (qnode) {
        ms->phase = PHASE_GEN_QUISCENCE;
    } else {
        ms->phase = !in_check?PHASE_TT:PHASE_GEN_EVASIONS;
    }
    ms->qnode = qnode;
    ms->root = root;
    ms->ttmove = NOMOVE;
    ms->idx = 0;
    if (!root) {
        ms->nmoves = 0;
        ms->nbadcaps = 0;
    }

    /*
     * The move info list at the root is reused for each iteration
     * so it should be setup only during the first iteration.
     */
    if (root && (ms->nmoves == 0)) {
        ms->nmoves = worker->root_moves.nmoves;
        for (k=0;k<worker->root_moves.nmoves;k++) {
            move = worker->root_moves.moves[k];
            ms->moveinfo[k].move = move;
            ms->moveinfo[k].score = 0;
            ms->moveinfo[k].see = calculate_see_score(pos, move);
        }
    }
}

void select_set_tt_move(struct search_worker *worker, uint32_t move)
{
    struct moveselect *ms;
    struct position *pos;

    pos = &worker->pos;

    ms = &worker->ppms[pos->sply];
    if ((move == NOMOVE) || !board_is_move_pseudo_legal(pos, move)) {
        return;
    }

    ms->ttmove = move;
}

bool select_get_root_move(struct search_worker *worker, uint32_t *move,
                          int *see_score)
{
    struct moveselect *ms;

    assert(move != NULL);
    assert(worker->pos.sply == 0);

    /* Select the next move to search */
    ms = &worker->ppms[0];
    *move = select_move(ms, see_score);
    ms->idx++;
    return *move != NOMOVE;
}

bool select_get_move(struct search_worker *worker, uint32_t *move,
                     int *see_score)
{
    struct moveselect *ms;
    struct movelist   list;
    uint32_t          killer;
    int               k;
    struct position *pos;

    assert(move != NULL);
    assert(worker->pos.sply > 0);

    pos = &worker->pos;
    ms = &worker->ppms[pos->sply];
    assert(!ms->root);

    switch (ms->phase) {
    case PHASE_TT:
        ms->phase++;
        if (ms->ttmove != NOMOVE) {
            if (see_score != NULL) {
                *see_score = calculate_see_score(pos, ms->ttmove);
            }
            *move = ms->ttmove;
            return true;
        }
        /* Fall through */
    case PHASE_GEN_CAPS:
        /* Generate all possible captures for this position */
        list.nmoves = 0;
        gen_capture_moves(pos, &list);
        for (k=0;k<list.nmoves;k++) {
            assign_score(worker, ms, &list, k);
        }
        ms->phase++;
        ms->idx = 0;
        /* Fall through */
    case PHASE_GOOD_CAPS:
        if (ms->idx < ms->nmoves) {
            break;
        }
        ms->phase++;
        /* Fall through */
    case PHASE_KILLER1:
        ms->phase++;
        killer = worker->killer_table[pos->sply][0];
        if ((killer != NOMOVE) && (killer != ms->ttmove) &&
            board_is_move_pseudo_legal(pos, killer)) {
            if (see_score != NULL) {
                *see_score = calculate_see_score(pos, killer);
            }
            *move = killer;
            return true;
        }
        /* Fall through */
    case PHASE_KILLER2:
        ms->phase++;
        killer = worker->killer_table[pos->sply][1];
        if ((killer != NOMOVE) && (killer != ms->ttmove) &&
            (killer != worker->killer_table[pos->sply][0]) &&
            board_is_move_pseudo_legal(pos, killer)) {
            if (see_score != NULL) {
                *see_score = calculate_see_score(pos, killer);
            }
            *move = killer;
            return true;
        }
        /* Fall through */
    case PHASE_GEN_MOVES:
        /* Generate all possible moves for this position */
        list.nmoves = 0;
        gen_promotion_moves(pos, &list);
        gen_normal_moves(pos, &list);
        for (k=0;k<list.nmoves;k++) {
            assign_score(worker, ms, &list, k);
        }
        ms->phase++;
        /* Fall through */
    case PHASE_MOVES:
        if (ms->idx < ms->nmoves) {
            break;
        }
        ms->phase++;
        /* Fall through */
    case PHASE_ADD_BAD_CAPS:
        for (k=0;k<ms->nbadcaps;k++) {
            ms->moveinfo[ms->nmoves] = ms->badcapinfo[k];
            ms->nmoves++;
        }
        ms->phase++;
        /* Fall through */
    case PHASE_BAD_CAPS:
        if (ms->idx < ms->nmoves) {
            break;
        }
        ms->phase++;
        return false;
    case PHASE_GEN_EVASIONS:
        /* Generate check evasions for this position */
        gen_check_evasions(pos, &list);
        for (k=0;k<list.nmoves;k++) {
            assign_evasion_score(worker, ms, &list, k);
        }
        ms->phase++;
        ms->idx = 0;
        /* Fall through */
    case PHASE_EVASIONS:
        if (ms->idx < ms->nmoves) {
            break;
        }
        ms->phase++;
        /* Fall through */
    default:
        /* All moves have been searched */
        return false;
    }

    /* Select the next move to search */
    *move = select_move(ms, see_score);
    ms->idx++;

    return *move != NOMOVE;
}

bool select_get_quiscence_move(struct search_worker *worker, uint32_t *move,
                               int *see_score)
{
    struct moveselect *ms;
    struct movelist   list;
    int               k;
    struct position   *pos;

    assert(move != NULL);

    pos = &worker->pos;
    ms = &worker->ppms[pos->sply];
    assert(!ms->root);

    switch (ms->phase) {
    case PHASE_GEN_QUISCENCE:
        /* Generate quiscence moves for this position */
        gen_quiscence_moves(pos, &list, false);
        for (k=0;k<list.nmoves;k++) {
            assign_quiscence_score(worker, ms, &list, k);
        }
        ms->phase++;
        ms->idx = 0;
        /* Fall through */
    case PHASE_QUISCENCE:
        if (ms->idx < ms->nmoves) {
            break;
        }
        ms->phase++;
        /* Fall through */
    default:
        /* All moves have been searched */
        return false;
    }

    /* Select the next move to search */
    *move = select_move(ms, see_score);
    ms->idx++;

    return *move != NOMOVE;
}

void select_update_root_move_scores(struct search_worker *worker)
{
    struct moveselect *ms;
    int               k;

    assert(worker->pos.sply == 0);

    ms = &worker->ppms[0];
    for (k=0;k<ms->nmoves;k++) {
        assign_root_score(worker, ms, k);
    }
}

