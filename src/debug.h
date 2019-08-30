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
#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
#include <stdarg.h>

#include "chess.h"

/* The highest (most verbose) log level */
#define LOG_HIGHEST_LEVEL 2

/* Logging macros */
#define LOG_INFO1(...) dbg_log_info(1, __VA_ARGS__)
#define LOG_INFO2(...) dbg_log_info(2, __VA_ARGS__)

/*
 * Set the log level.
 *
 * @param level The log level.
 */
void dbg_set_log_level(int level);

/*
 * Get the log level.
 *
 * @return Returns the log level.
 */
int dbg_get_log_level(void);

/* Close the log file */
void dbg_log_close(void);

/*
 * Write a message to the log file.
 *
 * @param level The log level.
 * @param fmt The format argument.
 */
void dbg_log_info(int level, char *fmt, ...);

/*
 * Print a chess board.
 *
 * @param pos The position to print.
 */
void dbg_print_board(struct position *pos);

/*
 * Print a bitboard.
 *
 * @param bb The bitboard to print.
 */
void dbg_print_bitboard(uint64_t bb);

/*
 * Print a move.
 *
 * @param move The move to print.
 */
void dbg_print_move(uint32_t move);

/*
 * Print a movelist.
 *
 * @param list The move list to print.
 */
void dbg_print_movelist(struct movelist *list);

/*
 * Interactivly browse the trasposition table.
 *
 * @param pos The board structure.
 */
void dbg_browse_transposition_table(struct position *pos);

#endif
