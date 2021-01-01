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
#ifndef HASH_H
#define HASH_H

#include <stdint.h>

#include "chess.h"

/* Different flags for transposition table entries */
enum {
    TT_EXACT,
    TT_BETA,
    TT_ALPHA
};

/*
 * Get the maximum transposition table size.
 *
 * @return Returns the maximum size (in MB).
 */
int hash_tt_max_size(void);

/*
 * Create the main transposition table.
 *
 * @param size The amount of memory to use for the table (in MB).
 */
void hash_tt_create_table(int size);

/*
 * Destroy the main transposition table.
 */
void hash_tt_destroy_table(void);

/*
 * Clear the main transposition table.
 */
void hash_tt_clear_table(void);

/*
 * Increase the age of the main transposition table.
 */
void hash_tt_age_table(void);

/*
 * Store a new position in the main transposition table.
 *
 * @param pos The board structure.
 * @param move The best move found.
 * @param depth The depth to which the position was searched.
 * @param score The score for the position.
 * @param type The type of the score.
 * @param eval_score The static evaluation of the position.
 */
void hash_tt_store(struct position *pos, uint32_t move, int depth, int score,
                   int type, int eval_score);

/*
 * Lookup the current position in the main transposition table.
 *
 * @param pos The board structure.
 * @param item Location to store the transposition table item at.
 * @return Returns true if an entry was found.
 */
bool hash_tt_lookup(struct position *pos, struct tt_item *item);

/*
 * Get the transposition table usage.
 *
 * @return Returns how many permill of the transposition table that is used.
 */
int hash_tt_usage(void);

/*
 * Create the pawn transposition table.
 *
 * @param worker The worker.
 * @param size The amount of memory to use for the table (in MB).
 */
void hash_pawntt_create_table(struct search_worker *worker, int size);

/*
 * Destroy the pawn transposition table.
 *
 * @param worker The worker.
 */
void hash_pawntt_destroy_table(struct search_worker *worker);

/*
 * Clear the pawn transposition table.
 *
 * @param worker The worker.
 */
void hash_pawntt_clear_table(struct search_worker *worker);

/*
 * Initialize an item to store in the pawn transposition table.
 *
 * @param item The item to initialize.
 */
void hash_pawntt_init_item(struct pawntt_item *item);

/*
 * Store a new position in the pawn transposition table.
 *
 * @param worker The worker.
 * @param item The item to store. Note that the pawn key field is ignored,
 *             the key in the board structure is always used.
 */
void hash_pawntt_store(struct search_worker *worker, struct pawntt_item *item);

/*
 * Lookup the current position in the pawn transposition table.
 *
 * @param worker The worker.
 * @param item Location where the found item is stored.
 * @return Returns true if the position was found, false otherwise.
 */
bool hash_pawntt_lookup(struct search_worker *worker, struct pawntt_item *item);

/*
 * Create the NNUE cache.
 *
 * @param worker The worker.
 * @param size The amount of memory to use for the cache (in MB).
 */
void hash_nnue_create_table(struct search_worker *worker, int size);

/*
 * Destroy the NNUE cache.
 *
 * @param worker The worker.
 */
void hash_nnue_destroy_table(struct search_worker *worker);

/*
 * Clear the NNUE cache.
 *
 * @param worker The worker.
 */
void hash_nnue_clear_table(struct search_worker *worker);

/*
 * Store a new score in the NNUE cache.
 *
 * @param worker The worker.
 * @param score The score to store.
 */
void hash_nnue_store(struct search_worker *worker, int score);

/*
 * Lookup the current position in the NNUE cache.
 *
 * @param worker The worker.
 * @param score Location to store the score at.
 * @return Returns true if a score was found in the table, false otherwise.
 */
bool hash_nnue_lookup(struct search_worker *worker, int *score);

/*
 * Prefetch hash table entries for a specific position.
 *
 * @param worker The worker.
 */
void hash_prefetch(struct search_worker *worker);

#endif
