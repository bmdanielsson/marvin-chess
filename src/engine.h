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
 * @param state The board structure.
 * @param ponderhit Location to store if a ponderhit command was received.
 * @return Returns true if the current search should be stopped.
 */
bool engine_check_input(struct gamestate *state, bool *ponderhit);

/*
 * Send information about the principle variation.
 *
 * @param state The board structure.
 * @param score The PV score.
 */
void engine_send_pv_info(struct gamestate *state, int score);

/*
 * Send information about the move currently being searched.
 *
 * @param state The board structure.
 */
void engine_send_move_info(struct gamestate *state);

#endif
