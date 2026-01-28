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

#include "types.h"

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
 * Get the size of the transposition table size.
 *
 * @return Returns the size (in MB).
 */
int hash_tt_size(void);

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
 */
void hash_tt_store(struct position *pos, uint32_t move, int depth, int score,
                   int type);

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
 * Prefetch hash table entries for a specific position.
 *
 * @param worker The worker.
 */
void hash_prefetch(struct search_worker *worker);

#endif
