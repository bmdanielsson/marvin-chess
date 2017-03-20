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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

#include "hash.h"
#include "validation.h"
#include "search.h"
#include "board.h"
#include "config.h"

static void allocate_tt(struct gamestate *pos, int size)
{
    pos->tt_size = (size*1024*1024)/sizeof(struct tt_bucket);
    pos->tt_table = malloc(pos->tt_size*sizeof(struct tt_bucket));
    if (pos->tt_table == NULL) {
        pos->tt_size = (MIN_MAIN_HASH_SIZE*1024*1024)/sizeof(struct tt_bucket);
        pos->tt_table = malloc(pos->tt_size*sizeof(struct tt_bucket));
    }
    assert(pos->tt_table != NULL);
}

static void allocate_pawntt(struct gamestate *pos, int size)
{
    pos->pawntt_size = (size*1024*1024)/sizeof(struct pawntt_item);
    pos->pawntt_table = malloc(pos->pawntt_size*sizeof(struct pawntt_item));
    assert(pos->tt_table != NULL);
}

static bool check_tt_cutoff(struct gamestate *pos, struct tt_item *item,
                            int depth, int alpha, int beta, int *score)
{
    int  adj_score;
    bool cutoff;

    /* Adjust mate scores */
    adj_score = item->score;
    if (adj_score > FORCED_MATE) {
        adj_score -= pos->sply;
    } else if (adj_score < -FORCED_MATE) {
        adj_score += pos->sply;
    }
    *score = adj_score;

    cutoff = false;
    if (item->depth >= depth) {
        switch (item->type) {
        case TT_EXACT:
            cutoff = true;
            break;
        case TT_ALPHA:
            if (adj_score <= alpha) {
                *score = alpha;
                cutoff = true;
            }
            break;
        case TT_BETA:
            if (adj_score >= beta) {
                *score = beta;
                cutoff = true;
            }
            break;
        default:
            cutoff = false;
            break;
        }
    }

    return cutoff;
}

void hash_tt_create_table(struct gamestate *pos, int size)
{
    assert((size >= MIN_MAIN_HASH_SIZE) && (size <= MAX_MAIN_HASH_SIZE));

    hash_tt_destroy_table(pos);

    allocate_tt(pos, size);
    hash_tt_clear_table(pos);
}

void hash_tt_destroy_table(struct gamestate *pos)
{
    free(pos->tt_table);
    pos->tt_table = NULL;
    pos->tt_size = 0;
    pos->date = 0;
    pos->tt_used = 0;
}

void hash_tt_clear_table(struct gamestate *pos)
{
    assert(pos != NULL);
    assert(pos->tt_table != NULL);

    memset(pos->tt_table, 0, pos->tt_size*sizeof(struct tt_bucket));
    pos->tt_used = 0;
}

void hash_tt_age_table(struct gamestate *pos)
{
    assert(pos != NULL);
    assert(pos->tt_table != NULL);

    pos->date++;
}

void hash_tt_store(struct gamestate *pos, uint32_t move, int depth, int score,
                   int type)
{
    uint32_t         idx;
    struct tt_bucket *bucket;
    struct tt_item   *item;
    struct tt_item   *worst_item;
    int              item_score;
    int              worst_score;
    int              k;
    uint8_t          age;

    assert(valid_board(pos));
    assert(valid_move(move));
    assert((score > -INFINITE_SCORE) && (score < INFINITE_SCORE));

    if (pos->tt_table == NULL) {
        return;
    }

    /*
     * Mate scores are dependent on search depth so if nothing is done
     * they will be incorrect if the position is found at a different
     * depth. Therefore the scores are adjusted so that they are stored
     * as mate-in-n from the _current_ position instead of from the root
     * of the search tree. Based on this a correct mate score can be
     * calculated when retrieving the entry.
     *
     * Additionally only store mate scores as TT_EXACT entries, not
     * as boundaries. The reason is that the score have taken on a
     * different meaning in these cases since the mate was actually
     * found in a different part of the tree.
     */
    if (score > FORCED_MATE) {
        if (type != TT_EXACT) {
            return;
        }
        score += pos->sply;
    } else if (score < -FORCED_MATE) {
        if (type != TT_EXACT) {
            return;
        }
        score -= pos->sply;
    }
    assert((score <= CHECKMATE) && (score >= (-CHECKMATE)));

    /* Find the correct bucket */
    idx = (uint32_t)(pos->key%pos->tt_size);
    bucket = &pos->tt_table[idx];

    /*
     * Iterate over all items and find the best
     * location to store this position at.
     */
    worst_item = NULL;
    worst_score = INT_MAX;
    for (k=0;k<TT_BUCKET_SIZE;k++) {
        item = &bucket->items[k];

        /*
         * If the same position is already stored then
         * replace it if the new search is to a greater
         * depth or if the item have an older date. Only
         * exception is if when inserting a PV move, in
         * which case it is inserted it unless the existing
         * item has the same move.
         */
        if (pos->key == item->key) {
            if (type == TT_PV) {
                if (move != item->move) {
                    worst_item = item;
                    break;
                } else {
                    return;
                }
            } else if ((depth >= item->depth) || (pos->date != item->date)) {
                worst_item = item;
                break;
            }

            /*
             * The stored position is more valuable so skip
             * the current position.
             */
            return;
        } else if (item->key == 0ULL) {
            worst_item = item;
            break;
        }

        /*
         * Calculate a score for the item. The main idea is to
         * prefer searches to a higher depth and to prefer
         * newer searches before older ones.
         */
        age = pos->date - item->date;
        item_score = (256 - age - 1) + item->depth*256;

        /* Remeber the item with the worst score */
        if (item_score < worst_score) {
            worst_score = item_score;
            worst_item = item;
        }
    }
    assert(worst_item != NULL);

    /* Replace the worst item */
    if (worst_item->key == 0ULL) {
        pos->tt_used++;
    }
    worst_item->key = pos->key;
    worst_item->move = move;
    worst_item->score = score;
    worst_item->depth = depth;
    worst_item->type = type;
    worst_item->date = pos->date;
}

bool hash_tt_lookup(struct gamestate *pos, int depth, int alpha, int beta,
                    uint32_t *move, int *score)
{
    uint32_t         idx;
    struct tt_bucket *bucket;
    struct tt_item   *item;
    bool             cutoff;
    int              k;

    assert(valid_board(pos));
    assert(move != NULL);
    assert(score != NULL);

    if (pos->tt_table == NULL) {
        *move = NOMOVE;
        *score = 0;
        return false;
    }

    /* Find the correct bucket */
    idx = (uint32_t)(pos->key%pos->tt_size);
    bucket = &pos->tt_table[idx];

    /*
     * Find the first item, if any, that have
     * the same key as the current position
     * and is good enough to cause a cutoff.
     */
    *move = NOMOVE;
    *score = 0;
    cutoff = false;
    for (k=0;k<TT_BUCKET_SIZE;k++) {
        item = &bucket->items[k];

        if (pos->key == item->key) {
            *move = item->move;
            cutoff = check_tt_cutoff(pos, item, depth, alpha, beta, score);
            break;
        }
    }

    return cutoff;
}

void hash_tt_insert_pv(struct gamestate *pos, struct pv *pv)
{
    int      k;
    uint32_t move;

    assert(valid_board(pos));

    if (pos->tt_table == NULL) {
        return;
    }

    for (k=0;k<pv->length;k++) {
        move = pv->moves[k];
        hash_tt_store(pos, move, 0, 0, TT_PV);
        board_make_move(pos, move);
    }

    for (;k>0;k--) {
        board_unmake_move(pos);
    }
}

void hash_pawntt_create_table(struct gamestate *pos, int size)
{
    assert(size >= 0);

    hash_pawntt_destroy_table(pos);

    allocate_pawntt(pos, size);
    hash_pawntt_clear_table(pos);
}

void hash_pawntt_destroy_table(struct gamestate *pos)
{
    free(pos->pawntt_table);
    pos->pawntt_table = NULL;
    pos->pawntt_size = 0;
}

void hash_pawntt_clear_table(struct gamestate *pos)
{
    assert(pos != NULL);
    assert(pos->pawntt_table != NULL);

    memset(pos->pawntt_table, 0, pos->pawntt_size*sizeof(struct pawntt_item));
}

void hash_pawntt_init_item(struct pawntt_item *item)
{
    assert(item != NULL);

    memset(item, 0, sizeof(struct pawntt_item));
    item->used = true;
}

void hash_pawntt_store(struct gamestate *pos, struct pawntt_item *item)
{
    uint32_t idx;

    assert(valid_board(pos));
    assert(item != NULL);

    if (pos->pawntt_table == NULL) {
        return;
    }

    /* Find the correct position in the table */
    idx = (uint32_t)(pos->pawnkey%pos->pawntt_size);

    /*
     * Insert the item in the table. An always-replace strategy
     * is used in case the position is already taken.
     */
    pos->pawntt_table[idx] = *item;
    pos->pawntt_table[idx].pawnkey = pos->pawnkey;
}

bool hash_pawntt_lookup(struct gamestate *pos, struct pawntt_item *item)
{
    uint32_t idx;
    bool     found;

    assert(valid_board(pos));
    assert(item != NULL);

    if (pos->pawntt_table == NULL) {
        return false;
    }

    /*
     * Find the correct position in the table and check
     * if it contains an item for this position.
     */
    found = false;
    idx = (uint32_t)(pos->pawnkey%pos->pawntt_size);
    if (pos->pawntt_table[idx].pawnkey == pos->pawnkey) {
        found = true;
        *item = pos->pawntt_table[idx];
    }

    return found && item->used;
}
