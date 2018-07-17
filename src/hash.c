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
#include <inttypes.h>

#include "hash.h"
#include "validation.h"
#include "search.h"
#include "board.h"
#include "utils.h"
#include "config.h"

/* Macros for managing the hash key stored in struct tt_item */
#define KEY_LOW(k)  ((uint32_t)((k)&0x00000000FFFFFFFF))
#define KEY_HIGH(k) ((uint32_t)(((k)>>32)&0x00000000FFFFFFFF))
#define KEY_EQUALS(k, i) \
                (k) == ((((uint64_t)(i)->key_high)<<32)|(uint64_t)(i)->key_low)
#define KEY_IS_ZERO(i) ((i)->key_low == 0ULL) && ((i)->key_high == 0ULL)

/* Main transposition table */
static struct tt_bucket *transposition_table = NULL;
static uint64_t tt_size = 0ULL;
static uint8_t tt_date = 0;

static int largest_power_of_2(uint64_t size, int item_size)
{
    uint64_t largest;
    uint64_t nitems;

    nitems = (size*1024ULL*1024ULL)/item_size;
    largest = 1ULL;
    while (largest <= nitems) {
        largest <<= 1ULL;
    }
    largest >>= 1ULL;

    return largest;
}

static void allocate_tt(int size)
{
    tt_size = largest_power_of_2(size, sizeof(struct tt_bucket));
    transposition_table = aligned_malloc(CACHE_LINE_SIZE,
                                         tt_size*sizeof(struct tt_bucket));
    if (transposition_table == NULL) {
        tt_size = largest_power_of_2(MIN_MAIN_HASH_SIZE,
                                     sizeof(struct tt_bucket));
        transposition_table = aligned_malloc(CACHE_LINE_SIZE,
                                             tt_size*sizeof(struct tt_bucket));
    }
    assert(transposition_table != NULL);
}

static void allocate_pawntt(struct search_worker *worker, int size)
{
    worker->pawntt_size = largest_power_of_2(size, sizeof(struct pawntt_item));
    worker->pawntt = aligned_malloc(CACHE_LINE_SIZE,
                                worker->pawntt_size*sizeof(struct pawntt_item));
    assert(worker->pawntt != NULL);
}

static bool check_tt_cutoff(struct position *pos, struct tt_item *item,
                            int depth, int alpha, int beta, int *score)
{
    int  adj_score;
    bool cutoff;

    /* Adjust mate scores */
    adj_score = item->score;
    if (adj_score > KNOWN_WIN) {
        adj_score -= pos->sply;
    } else if (adj_score < -KNOWN_WIN) {
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
                cutoff = true;
            }
            break;
        case TT_BETA:
            if (adj_score >= beta) {
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

int hash_tt_max_size(void)
{
	return is64bit()?MAX_MAIN_HASH_SIZE_64BIT:MAX_MAIN_HASH_SIZE_32BIT;
}

void hash_tt_create_table(int size)
{
	assert((size >= MIN_MAIN_HASH_SIZE) && (size <= hash_tt_max_size()));
	
    hash_tt_destroy_table();

    allocate_tt(size);
    hash_tt_clear_table();
}

void hash_tt_destroy_table(void)
{
    aligned_free(transposition_table);
    transposition_table = NULL;
    tt_size = 0ULL;
    tt_date = 0;
}

void hash_tt_clear_table(void)
{
    assert(transposition_table != NULL);

    memset(transposition_table, 0, tt_size*sizeof(struct tt_bucket));
}

void hash_tt_age_table(void)
{
    tt_date++;
}

void hash_tt_store(struct position *pos, uint32_t move, int depth, int score,
                   int type)
{
    uint64_t         idx;
    struct tt_bucket *bucket;
    struct tt_item   *item;
    struct tt_item   *worst_item;
    int              item_score;
    int              worst_score;
    int              k;
    uint8_t          age;

    assert(valid_position(pos));
    assert(valid_move(move));
    assert((score > -INFINITE_SCORE) && (score < INFINITE_SCORE));

    if (transposition_table == NULL) {
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
     *
     * The same reasoning also applies to tablebase wins/losses so
     * they are treated the same way.
     */
    if (score > KNOWN_WIN) {
        if (type != TT_EXACT) {
            return;
        }
        score += pos->sply;
    } else if (score < -KNOWN_WIN) {
        if (type != TT_EXACT) {
            return;
        }
        score -= pos->sply;
    }

    /* Find the correct bucket */
    idx = (uint64_t)(pos->key&(tt_size-1));
    bucket = &transposition_table[idx];

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
        if (KEY_EQUALS(pos->key, item)) {
            if (type == TT_PV) {
                if (move != item->move) {
                    worst_item = item;
                    break;
                } else {
                    return;
                }
            } else if ((depth >= item->depth) || (tt_date != item->date)) {
                worst_item = item;
                break;
            }

            /*
             * The stored position is more valuable so skip
             * the current position.
             */
            return;
        } else if (KEY_IS_ZERO(item)) {
            worst_item = item;
            break;
        }

        /*
         * Calculate a score for the item. The main idea is to
         * prefer searches to a higher depth and to prefer
         * newer searches before older ones.
         */
        age = tt_date - item->date;
        item_score = (256 - age - 1) + item->depth*256;

        /* Remeber the item with the worst score */
        if (item_score < worst_score) {
            worst_score = item_score;
            worst_item = item;
        }
    }
    assert(worst_item != NULL);

    /* Replace the worst item */
    worst_item->key_high = KEY_HIGH(pos->key);
    worst_item->key_low = KEY_LOW(pos->key);
    worst_item->move = move;
    worst_item->score = score;
    worst_item->depth = depth;
    worst_item->type = type;
    worst_item->date = tt_date;
}

bool hash_tt_lookup(struct position *pos, int depth, int alpha, int beta,
                    uint32_t *move, int *score, struct tt_item **item)
{
    uint64_t         idx;
    struct tt_bucket *bucket;
    struct tt_item   *tmp;
    bool             cutoff;
    int              k;

    assert(valid_position(pos));
    assert(move != NULL);
    assert(score != NULL);

    if (transposition_table == NULL) {
        *move = NOMOVE;
        *score = 0;
        return false;
    }

    /* Find the correct bucket */
    idx = (uint64_t)(pos->key&(tt_size-1));
    bucket = &transposition_table[idx];

    /*
     * Find the first item, if any, that have
     * the same key as the current position
     * and is good enough to cause a cutoff.
     */
    *move = NOMOVE;
    *score = 0;
    cutoff = false;
    for (k=0;k<TT_BUCKET_SIZE;k++) {
        tmp = &bucket->items[k];

        if (KEY_EQUALS(pos->key, tmp)) {
            *move = tmp->move;
            if (item != NULL) {
                *item = tmp;
            }
            cutoff = check_tt_cutoff(pos, tmp, depth, alpha, beta, score);
            break;
        }
    }

    return cutoff;
}

struct tt_item* hash_tt_lookup_raw(struct position *pos)
{
    uint64_t         idx;
    struct tt_bucket *bucket;
    struct tt_item   *item;
    int              k;

    assert(valid_position(pos));

    if (transposition_table == NULL) {
        return NULL;
    }

    idx = (uint64_t)(pos->key&(tt_size-1));
    bucket = &transposition_table[idx];
    for (k=0;k<TT_BUCKET_SIZE;k++) {
        item = &bucket->items[k];
        if (KEY_EQUALS(pos->key, item)) {
            return item;
        }
    }

    return NULL;
}

void hash_tt_insert_pv(struct position *pos, struct pv *pv)
{
    int      k;
    uint32_t move;

    assert(valid_position(pos));

    if (transposition_table == NULL) {
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

void hash_pawntt_create_table(struct search_worker *worker, int size)
{
    assert(size >= 0);

    hash_pawntt_destroy_table(worker);

    allocate_pawntt(worker, size);
    hash_pawntt_clear_table(worker);
}

void hash_pawntt_destroy_table(struct search_worker *worker)
{
    aligned_free(worker->pawntt);
    worker->pawntt = NULL;
    worker->pawntt_size = 0;
}

void hash_pawntt_clear_table(struct search_worker *worker)
{
    assert(worker != NULL);
    assert(worker->pawntt != NULL);

    memset(worker->pawntt, 0, worker->pawntt_size*sizeof(struct pawntt_item));
}

void hash_pawntt_init_item(struct pawntt_item *item)
{
    assert(item != NULL);

    memset(item, 0, sizeof(struct pawntt_item));
    item->used = true;
}

void hash_pawntt_store(struct search_worker *worker, struct pawntt_item *item)
{
    struct position *pos;
    uint32_t        idx;

    assert(valid_position(&worker->pos));
    assert(item != NULL);

    pos = &worker->pos;

    if (worker->pawntt == NULL) {
        return;
    }

    /* Find the correct position in the table */
    idx = (uint32_t)(pos->pawnkey&(worker->pawntt_size-1));

    /*
     * Insert the item in the table. An always-replace strategy
     * is used in case the position is already taken.
     */
    worker->pawntt[idx] = *item;
    worker->pawntt[idx].pawnkey = pos->pawnkey;
}

bool hash_pawntt_lookup(struct search_worker *worker, struct pawntt_item *item)
{
    struct position *pos;
    uint32_t        idx;
    bool            found;

    assert(valid_position(&worker->pos));
    assert(item != NULL);

    pos = &worker->pos;

    if (worker->pawntt == NULL) {
        return false;
    }

    /*
     * Find the correct position in the table and check
     * if it contains an item for this position.
     */
    found = false;
    idx = (uint32_t)(pos->pawnkey&(worker->pawntt_size-1));
    if (worker->pawntt[idx].pawnkey == pos->pawnkey) {
        found = true;
        *item = worker->pawntt[idx];
    }

    return found && item->used;
}

void hash_prefetch(struct search_worker *worker)
{
    PREFETCH_ADDRESS(&transposition_table[worker->pos.key&(tt_size-1)]);
    PREFETCH_ADDRESS(&worker->pawntt[worker->pos.pawnkey&(worker->pawntt_size-1)]);
}
