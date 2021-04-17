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
#ifndef XBOARD_H
#define XBOARD_H

#include <stdbool.h>

#include "chess.h"

/*
 * Handle an Xboard command.
 *
 * @param state The game state object.
 * @param cmd The command to handle.
 * @param stop Flag set if the engine should stop after this command.
 * @return Returns true if the command was handled, false otherwise.
 */
bool xboard_handle_command(struct gamestate *state, char *cmd, bool *stop);

/*
 * Function called during search to check if input has arrived.
 *
 * @param worker The worker.
 * @return Returns true if the current search should be stopped.
 */
bool xboard_check_input(struct search_worker *worker);

/*
 * Send information about the principle variation.
 *
 * @param state The game state.
 * @param pvinfo The pv.
 */
void xboard_send_pv_info(struct gamestate *state, struct pvinfo *pvinfo);

#endif
