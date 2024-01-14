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

#include "types.h"

/* Maximum length accepted for file paths */
#define MAX_PATH_LENGTH 1024

/* Enum for different chess protocols */
enum protocol {
    PROTOCOL_UNSPECIFIED,
    PROTOCOL_UCI,
    PROTOCOL_XBOARD
};

/* Enum for different chess variants */
enum variant {
    VARIANT_UNSPECIFIED,
    VARIANT_STANDARD,
    VARIANT_FRC
};

/* Global engine variables */
extern enum protocol engine_protocol;
extern enum variant engine_variant;
extern char engine_syzygy_path[MAX_PATH_LENGTH+1];
extern int engine_default_hash_size;
extern int engine_default_num_threads;
extern bool engine_using_nnue;
extern bool engine_loaded_net;
extern char engine_eval_file[MAX_PATH_LENGTH+1];

/*
 * Read and parse a config file.
 *
 * @param cfgfile The file to read.
 */
void engine_read_config_file(char *cfgfile);

/*
 * Create a new engine object.
 *
 * @return Returns the new engine object.
 */
struct engine* engine_create(void);

/*
 * Destroy an engine object.
 *
 * @param engine The object to destroy.
 */
void engine_destroy(struct engine *engine);

/*
 * The main engine loop.
 *
 * @param engine The engine object.
 */
void engine_loop(struct engine *engine);

/*
 * Read a new command.
 *
 * @return Returns the read command. The returned pointer should not be freed.
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
 * @param engine The engine.
 * @param pvinfo The pv.
 */
void engine_send_pv_info(struct engine *engine, struct pvinfo *pvinfo);

/*
 * Send information about score bound during search.
 *
 * @param worker The worker.
 * @param score The score.
 * @param lower Flag indicating a lower or upper bound.
 */
void engine_send_bound_info(struct search_worker *worker, int score,
                            bool lower);

/*
 * Send information about the move currently being searched.
 *
 * @param worker The worker.
 * @param movenumber The move number.
 * @param move The move.
 */
void engine_send_move_info(struct search_worker *worker, int movenumber,
                           uint32_t move);

/*
 * Send information the currently best lines.
 *
 * @param worker The worker.
 */
void engine_send_multipv_info(struct search_worker *worker);

#endif
