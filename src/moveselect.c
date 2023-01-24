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
#include "position.h"
#include "history.h"

/*
 * Different move generation phases.
 */
enum {
    PHASE_TT,
    PHASE_GEN_TACTICAL,
    PHASE_GOOD_TACTICAL,
    PHASE_KILLER,
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
    if (ISCAPTURE(move)) {
        return mvvlva_table[pos->pieces[TO(move)]][pos->pieces[FROM(move)]];
    } else if (ISENPASSANT(move)) {
        return mvvlva_table[PAWN+pos->stm][PAWN+pos->stm];
    }
    return 0;
}

static void add_moves(struct search_worker *worker, struct moveselector *ms,
                      struct movelist *list)
{
    uint32_t        move;
    struct moveinfo *info;
    struct position *pos = &worker->pos;
    int             k;

    for (k=0;k<list->size;k++) {
        move = list->moves[k];

        /*
         * Moves in the transposition table, killer moves and counter moves
         * are handled separately and should not be considered here.
         */
        if ((move == ms->ttmove) ||
            (move == ms->killer) ||
            (move == ms->counter)) {
            continue;
        }

        /*
         * If the SEE score is positive (normal moves or good captures) then
         * the move is added to the moveinfo list. If the SEE score is
         * negative (bad captures) then the move is added to the bad tacticals
         * list.
         */
        if (ISTACTICAL(move) && !see_ge(pos, move, 0)) {
            info = &ms->moveinfo[MAX_MOVES-1-ms->nbadtacticals];
            ms->nbadtacticals++;
        } else {
            info = &ms->moveinfo[ms->last_idx];
            ms->last_idx++;
        }
        info->move = move;

        /* Assign a score to the move */
        if (ISTACTICAL(move)) {
            info->score = mvvlva(pos, move);
        } else {
            info->score = history_get_score(worker, move);
        }
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

static bool get_move(struct moveselector *ms, struct search_worker *worker,
                     uint32_t *move)
{
    struct movelist list;
    uint32_t        killer;
    uint32_t        counter;
    struct position *pos = &worker->pos;

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
        list.size = 0;
        if (ms->in_check) {
            gen_check_evasion_tactical(pos, &list);
        } else {
            gen_capture_moves(pos, &list);
            gen_promotion_moves(pos, &list, ms->underpromote);
        }
        add_moves(worker, ms, &list);
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
    case PHASE_KILLER:
        ms->phase++;
        killer = ms->killer;
        if ((killer != NOMOVE) && (killer != ms->ttmove) &&
            pos_is_move_pseudo_legal(pos, killer)) {
            *move = killer;
            return true;
        }
        /* Fall through */
    case PHASE_COUNTER:
        ms->phase++;
        counter = ms->counter;
        if ((counter != NOMOVE) && (counter != ms->ttmove) &&
            (counter != ms->killer) &&
            pos_is_move_pseudo_legal(pos, counter)) {
            *move = counter;
            return true;
        }
        /* Fall through */
    case PHASE_GEN_MOVES:
        /* Generate all possible moves for this position */
        list.size = 0;
        if (ms->in_check) {
            gen_check_evasion_quiet(pos, &list);
        } else {
            gen_quiet_moves(pos, &list);
        }
        add_moves(worker, ms, &list);
        ms->phase++;
        /* Fall through */
    case PHASE_MOVES:
        if (ms->idx < ms->last_idx) {
            break;
        }
        ms->phase++;
        /* Fall through */
    case PHASE_ADD_BAD_TACTICAL:
        ms->last_idx = MAX_MOVES;
        ms->idx = MAX_MOVES - ms->nbadtacticals;
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

void select_init_node(struct moveselector *ms, struct search_worker *worker,
                      bool tactical_only, bool in_check, uint32_t ttmove)
{
    struct position *pos = &worker->pos;

    ms->phase = PHASE_TT;
    ms->tactical_only = tactical_only;
    ms->underpromote = !tactical_only;
    if ((ttmove == NOMOVE) || !pos_is_move_pseudo_legal(pos, ttmove)) {
        ms->ttmove = NOMOVE;
    } else if (tactical_only && !in_check && !ISTACTICAL(ttmove)) {
        ms->ttmove = NOMOVE;
    } else {
        ms->ttmove = ttmove;
    }
    ms->in_check = in_check;
    ms->idx = 0;
    ms->last_idx = 0;
    ms->nbadtacticals = 0;
    ms->killer = killer_get_move(worker);
    ms->counter = counter_get_move(worker);
}

bool select_get_move(struct moveselector *ms, struct search_worker *worker,
                     uint32_t *move)
{
    assert(move != NULL);

    return get_move(ms, worker, move);
}

bool select_is_bad_capture_phase(struct moveselector *ms)
{
    return ms->phase == PHASE_BAD_TACTICAL;
}
