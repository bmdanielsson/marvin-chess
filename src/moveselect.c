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
#include "history.h"

/*
 * Different move generation phases.
 */
enum {
    PHASE_TT,
    PHASE_GEN_TACTICAL,
    PHASE_GOOD_TACTICAL,
    PHASE_KILLER1,
    PHASE_KILLER2,
    PHASE_COUNTER,
    PHASE_GEN_MOVES,
    PHASE_MOVES,
    PHASE_ADD_BAD_TACTICAL,
    PHASE_BAD_TACTICAL,
};

/*
 * Table of MVV/LVA scores indexed by [victim, attacker]. For instance
 * for QxP index by mvvlva_table[P][Q].
 */
static int mvvlva_table[NPIECES][NPIECES] = {
    {160, 160, 150, 150, 140, 140, 130, 130, 120, 120, 110, 110},
    {160, 160, 150, 150, 140, 140, 130, 130, 120, 120, 110, 110},
    {260, 260, 250, 250, 240, 240, 230, 230, 220, 220, 210, 210},
    {260, 260, 250, 250, 240, 240, 230, 230, 220, 220, 210, 210},
    {360, 360, 350, 350, 340, 340, 330, 330, 320, 320, 310, 310},
    {360, 360, 350, 350, 340, 340, 330, 330, 320, 320, 310, 310},
    {460, 460, 450, 450, 440, 440, 430, 430, 420, 420, 410, 410},
    {460, 460, 450, 450, 440, 440, 430, 430, 420, 420, 410, 410},
    {560, 560, 550, 550, 540, 540, 530, 530, 520, 520, 510, 510},
    {560, 560, 550, 550, 540, 540, 530, 530, 520, 520, 510, 510},
    {660, 660, 650, 650, 640, 640, 630, 630, 620, 620, 610, 610},
    {660, 660, 650, 650, 640, 640, 630, 630, 620, 620, 610, 610}
};

static int mvvlva(struct position *pos, uint32_t move)
{
    assert(ISCAPTURE(move) || ISENPASSANT(move));

    if (ISCAPTURE(move)) {
        return mvvlva_table[pos->pieces[TO(move)]][pos->pieces[FROM(move)]];
    } else if (ISENPASSANT(move)) {
        return mvvlva_table[PAWN+pos->stm][PAWN+pos->stm];
    }
    return 0;
}

static void add_move(struct search_worker *worker, struct moveselector *ms,
                     struct movelist *list, int iter)
{
    int             from;
    int             to;
    uint32_t        move;
    struct moveinfo *info;
    struct position *pos;
    bool            gez;

    pos = &worker->pos;

    /*
     * Moves in the transposition table, killer moves and counter moves
     * are handled separately and should not be considered here.
     */
    move = list->moves[iter];
    if ((move == ms->ttmove) ||
        (move == ms->killer1) ||
        (move == ms->killer2) ||
        (move == ms->counter)) {
        return;
    }

    /*
     * If the SEE score is positive (normal moves or good captures) then
     * the move is added to the moveinfo list. If the SEE score is
     * negative (bad captures) the the move is added to be bad capture list.
     */
    gez = (ISCAPTURE(move) || ISENPASSANT(move))?see_ge(pos, move, 0):true;
    if ((ISCAPTURE(move) || ISENPASSANT(move)) && !gez) {
        info = &ms->moveinfo[MAX_MOVES+ms->nbadcaps];
        ms->nbadcaps++;
    } else {
        info = &ms->moveinfo[ms->last_idx];
        ms->last_idx++;
    }
    info->move = move;

    /* Assign a score to the move */
    from = FROM(move);
    to = TO(move);
    if ((ISCAPTURE(move) || ISENPASSANT(move)) && gez) {
        info->score = mvvlva(pos, move);
    } else if (!(ISCAPTURE(move) || ISENPASSANT(move))) {
        info->score = worker->history_table[pos->pieces[from]][to];
    } else {
        info->score = mvvlva(pos, move);
    }
}

static uint32_t select_move(struct moveselector *ms)
{
    int             iter;
    int             best;
    int             start;
    struct moveinfo temp;
    struct moveinfo *info_iter;
    struct moveinfo *info_best;

    /* Check if there are moves available */
    if (ms->idx >= ms->last_idx) {
        return NOMOVE;
    }

    /* Try the moves in order of their score */
    start = ms->idx;
    iter = start + 1;
    best = start;
    info_iter = &ms->moveinfo[iter];
    info_best = &ms->moveinfo[best];
    while (iter < ms->last_idx) {
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

    return ms->moveinfo[start].move;
}

void select_init_node(struct search_worker *worker, bool tactical_only,
                      bool in_check, uint32_t ttmove)
{
    struct moveselector *ms;
    uint32_t            prev_move;
    int                 prev_to;
    struct position     *pos;

    pos = &worker->pos;
    ms = &worker->ppms[pos->sply];
    ms->phase = PHASE_TT;
    ms->tactical_only = tactical_only;
    ms->underpromote = !tactical_only;
    if ((ttmove == NOMOVE) || !board_is_move_pseudo_legal(pos, ttmove)) {
        ms->ttmove = NOMOVE;
    } else if (ms->tactical_only && !in_check && !ISTACTICAL(ms->ttmove)) {
        ms->ttmove = NOMOVE;
    } else {
        ms->ttmove = ttmove;
    }
    ms->in_check = in_check;
    ms->idx = 0;
    ms->last_idx = 0;
    ms->nbadcaps = 0;
    ms->killer1 = worker->killer_table[pos->sply][0];
    ms->killer2 = worker->killer_table[pos->sply][1];
    prev_move = pos->history[pos->ply-1].move;
    if (!ISNULLMOVE(prev_move)) {
        prev_to = TO(prev_move);
        ms->counter =
                worker->countermove_table[pos->pieces[prev_to]][prev_to];
    } else {
        ms->counter = NOMOVE;
    }
}

bool select_get_move(struct search_worker *worker, uint32_t *move)
{
    struct moveselector *ms;
    struct movelist     list;
    uint32_t            killer;
    uint32_t            counter;
    int                 k;
    struct position     *pos;

    assert(move != NULL);

    pos = &worker->pos;
    ms = &worker->ppms[pos->sply];

    switch (ms->phase) {
    case PHASE_TT:
        ms->phase++;
        if (ms->ttmove != NOMOVE) {
            *move = ms->ttmove;
            return true;
        }
        /* Fall through */
    case PHASE_GEN_TACTICAL:
        /* Generate all possible captures for this position */
        list.nmoves = 0;
        if (ms->in_check) {
            gen_check_evasion_captures(pos, &list);
        } else {
            gen_capture_moves(pos, &list);
            gen_promotion_moves(pos, &list, ms->underpromote);
        }
        for (k=0;k<list.nmoves;k++) {
            add_move(worker, ms, &list, k);
        }
        ms->phase++;
        ms->idx = 0;
        /* Fall through */
    case PHASE_GOOD_TACTICAL:
        if (ms->idx < ms->last_idx) {
            break;
        }
        if (ms->tactical_only && !ms->in_check) {
            break;
        }
        ms->phase++;
        /* Fall through */
    case PHASE_KILLER1:
        ms->phase++;
        killer = ms->killer1;
        if ((killer != NOMOVE) && (killer != ms->ttmove) &&
            board_is_move_pseudo_legal(pos, killer)) {
            *move = killer;
            return true;
        }
        /* Fall through */
    case PHASE_KILLER2:
        ms->phase++;
        killer = ms->killer2;
        if ((killer != NOMOVE) && (killer != ms->ttmove) &&
            (killer != ms->killer1) &&
            board_is_move_pseudo_legal(pos, killer)) {
            *move = killer;
            return true;
        }
        /* Fall through */
    case PHASE_COUNTER:
        ms->phase++;
        counter = ms->counter;
        if ((counter != NOMOVE) && (counter != ms->ttmove) &&
            (counter != ms->killer1) && (counter != ms->killer2) &&
            board_is_move_pseudo_legal(pos, counter)) {
            *move = counter;
            return true;
        }
        /* Fall through */
    case PHASE_GEN_MOVES:
        /* Generate all possible moves for this position */
        list.nmoves = 0;
        if (ms->in_check) {
            gen_check_evasion_moves(pos, &list);
        } else {
            gen_quiet_moves(pos, &list);
        }
        for (k=0;k<list.nmoves;k++) {
            add_move(worker, ms, &list, k);
        }
        ms->phase++;
        /* Fall through */
    case PHASE_MOVES:
        if (ms->idx < ms->last_idx) {
            break;
        }
        ms->phase++;
        /* Fall through */
    case PHASE_ADD_BAD_TACTICAL:
        ms->last_idx = MAX_MOVES + ms->nbadcaps;
        ms->idx = MAX_MOVES;
        ms->phase++;
        /* Fall through */
    case PHASE_BAD_TACTICAL:
        if (ms->idx < ms->last_idx) {
            break;
        }
        ms->phase++;
        /* Fall through */
    default:
        /* All moves have been searched */
        *move = NOMOVE;
        return false;
    }

    /* Select the next move to search */
    *move = select_move(ms);
    ms->idx++;

    return *move != NOMOVE;
}

bool select_is_bad_capture_phase(struct search_worker *worker)
{
    struct moveselector *ms;
    struct position     *pos;

    pos = &worker->pos;
    ms = &worker->ppms[pos->sply];

    return ms->phase == PHASE_BAD_TACTICAL;
}
