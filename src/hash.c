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
#include "smp.h"
#include "config.h"
#include "engine.h"

/*
 * Since a move only uses 22 out of 32 bits the
 * upper 10 bits are used to store the date.
 */
#define GETMOVE(v)      ((v)&0x003FFFFF)
#define GETDATE(v)      (((v)&0xFFC00000)>>22)
#define MOVEDATE(m, d)  ((m)|((d)<<22))

/* Main transposition table */
static struct tt_bucket *transposition_table = NULL;
static int tt_size_in_mb = 0;
static uint64_t tt_size = 0ULL;
static uint8_t tt_date = 0;

static uint64_t largest_power_of_2(uint64_t size, int item_size)
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

static void allocate_nnue_cache(struct search_worker *worker, int size)
{
    worker->nnue_cache_size = largest_power_of_2(size,
                                                sizeof(struct nnue_cache_item));
    worker->nnue_cache = aligned_malloc(CACHE_LINE_SIZE,
                        worker->nnue_cache_size*sizeof(struct nnue_cache_item));
    assert(worker->nnue_cache != NULL);
}

int hash_tt_max_size(void)
{
	return is64bit()?MAX_MAIN_HASH_SIZE_64BIT:MAX_MAIN_HASH_SIZE_32BIT;
}

int hash_tt_size(void)
{
    return tt_size_in_mb;
}

void hash_tt_create_table(int size)
{
    assert((size >= MIN_MAIN_HASH_SIZE) && (size <= hash_tt_max_size()));
	
    hash_tt_destroy_table();

    tt_size_in_mb = size;
    allocate_tt(size);
    hash_tt_clear_table();
}

void hash_tt_destroy_table(void)
{
    aligned_free(transposition_table);
    transposition_table = NULL;
    tt_size_in_mb = 0;
    tt_size = 0ULL;
    tt_date = 0;
}

void hash_tt_clear_table(void)
{
    assert(transposition_table != NULL);

    parallel_memset(transposition_table, 0, tt_size*sizeof(struct tt_bucket),
                    smp_number_of_workers());
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
         * depth or if the item have an older date.
         */
        if (pos->key == item->key) {
            if ((depth >= item->depth) || (tt_date != GETDATE(item->move))) {
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
        age = tt_date - GETDATE(item->move);
        item_score = (256 - age - 1) + item->depth*256;

        /* Remeber the item with the worst score */
        if (item_score < worst_score) {
            worst_score = item_score;
            worst_item = item;
        }
    }
    assert(worst_item != NULL);

    /* Replace the worst item */
    worst_item->key = pos->key;
    worst_item->move = MOVEDATE(move, tt_date);
    worst_item->score = (int16_t)score;
    worst_item->depth = depth;
    worst_item->type = type;
}

bool hash_tt_lookup(struct position *pos, struct tt_item *item)
{
    uint64_t         idx;
    struct tt_bucket *bucket;
    struct tt_item   *tmp;
    int              k;

    assert(valid_position(pos));
    assert(item != NULL);

    if (transposition_table == NULL) {
        return false;
    }

    /* Find the correct bucket */
    idx = (uint64_t)(pos->key&(tt_size-1));
    bucket = &transposition_table[idx];

    /*
     * Find the first item, if any, that have
     * the same key as the current position.
     */
    for (k=0;k<TT_BUCKET_SIZE;k++) {
        tmp = &bucket->items[k];
        if (pos->key == tmp->key) {
            memcpy(item, tmp, sizeof(struct tt_item));

            /* Mask of the date from the move */
            item->move = GETMOVE(item->move);
            return true;
        }
    }

    return false;
}

/* Transposition table usage is estimated based on the first 1000 buckets */
int hash_tt_usage(void)
{
    struct tt_bucket *bucket;
    int k;
    int idx;
    int nused;

    nused = 0;
    for (k=0;k<1000;k++) {
        bucket = &transposition_table[k];
        for (idx=0;idx<TT_BUCKET_SIZE;idx++) {
            if (GETDATE(bucket->items[idx].move) == tt_date) {
                nused++;
            }
        }
    }

    return nused/TT_BUCKET_SIZE;
}

void hash_nnue_create_table(struct search_worker *worker, int size)
{
    assert(size >= 0);

    hash_nnue_destroy_table(worker);

    allocate_nnue_cache(worker, size);
    hash_nnue_clear_table(worker);
}

void hash_nnue_destroy_table(struct search_worker *worker)
{
    aligned_free(worker->nnue_cache);
    worker->nnue_cache = NULL;
    worker->nnue_cache_size = 0;
}

void hash_nnue_clear_table(struct search_worker *worker)
{
    assert(worker != NULL);
    assert(worker->nnue_cache != NULL);

    memset(worker->nnue_cache, 0,
           worker->nnue_cache_size*sizeof(struct nnue_cache_item));
}

void hash_nnue_store(struct search_worker *worker, int score)
{
    struct position *pos;
    uint32_t        idx;

    assert(valid_position(&worker->pos));

    pos = &worker->pos;

    if (worker->nnue_cache == NULL) {
        return;
    }

    /* Find the correct position in the table */
    idx = (uint32_t)(pos->key&(worker->nnue_cache_size-1));

    /*
     * Insert the item in the table. An always-replace strategy
     * is used in case the position is already taken.
     */
    worker->nnue_cache[idx].score = score;
    worker->nnue_cache[idx].key = pos->key;
}

bool hash_nnue_lookup(struct search_worker *worker, int *score)
{
    struct position *pos;
    uint32_t        idx;
    bool            found;

    assert(valid_position(&worker->pos));
    assert(score != NULL);

    pos = &worker->pos;

    if (worker->nnue_cache == NULL) {
        return false;
    }

    /*
     * Find the correct position in the table and check
     * if it contains an item for this position.
     */
    found = false;
    idx = (uint32_t)(pos->key&(worker->nnue_cache_size-1));
    if (worker->nnue_cache[idx].key == pos->key) {
        found = true;
        *score = worker->nnue_cache[idx].score;
    }

    return found;
}

void hash_prefetch(struct search_worker *worker)
{
    (void)worker;

    PREFETCH_ADDRESS(&transposition_table[worker->pos.key&(tt_size-1)]);
    PREFETCH_ADDRESS(&worker->nnue_cache[worker->pos.key&(worker->nnue_cache_size-1)]);
}
