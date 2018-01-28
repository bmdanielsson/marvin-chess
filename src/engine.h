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
#ifndef ENGINE_H
#define ENGINE_H

#include <stdbool.h>

#include "chess.h"

/* Enum for different chess protocols */
enum protocol {
    PROTOCOL_UNSPECIFIED,
    PROTOCOL_UCI,
    PROTOCOL_XBOARD
};

/* Global engine variables */
extern enum protocol engine_protocol;
extern char engine_syzygy_path[1024];
extern int engine_default_hash_size;
extern int engine_default_num_threads;

/*
 * The main engine loop.
 *
 * @param state The game state object.
 */
void engine_loop(struct gamestate *state);

/*
 * Read a new command.
 *
 * @return Returns the read command. The returned pointer shoiuld not be freed.
 */
char* engine_read_command(void);

/*
 * Write a command.
 *
 * @param format. The command format string.
 */
void engine_write_command(char *format, ...);

/*
 * Set a pending command to execute when the search finishes.
 *
 * @param cmd The command to set.
 */
void engine_set_pending_command(char *cmd);

/*
 * Get any pending command.
 *
 * @return Returns the pending command, or NULL if there is none.
 */
char* engine_get_pending_command(void);

/* Clear any pending command */
void engine_clear_pending_command(void);

/*
 * Function called during search to check if input has arrived.
 *
 * @param worker The worker.
 * @return Returns true if the current search should be stopped.
 */
bool engine_check_input(struct search_worker *worker);

/*
 * Function used to wait until input arrives.
 *
 * @param worker The worker.
 * @return Returns true if the current search should be stopped.
 */
bool engine_wait_for_input(struct search_worker *worker);

/*
 * Send information about the principle variation.
 *
 * @param worker The worker
 * @param pv The pv.
 * @param depth The depth.
 * @param seldepth The selctive depth.
 * @param score The PV score.
 * @param nodes The number of searched nodes.
 */
void engine_send_pv_info(struct search_worker *worker, struct pv *pv, int depth,
                         int seldepth, int score, uint32_t nodes);

/*
 * Send information about the move currently being searched.
 *
 * @param worker The worker.
 */
void engine_send_move_info(struct search_worker *worker);

#endif
