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
#ifndef CONFIG_H
#define CONFIG_H

/* Version information */
#define APP_NAME "Marvin"
#define APP_VERSION "4.0.0-a13"
#define APP_AUTHOR "Martin Danielsson"

/* The name of the log file */
#define LOGFILE_NAME "marvin.log"

/*
 * The size of the main hash tables (in MB). This value
 * can be configured at runtime by using the UCI Hash option.
 */
#define DEFAULT_MAIN_HASH_SIZE 32
#define MIN_MAIN_HASH_SIZE 1
#define MAX_MAIN_HASH_SIZE_32BIT 1024
#define MAX_MAIN_HASH_SIZE_64BIT 131072

/* The size to use for the pawn hash tables (in MB) */
#define PAWN_HASH_SIZE 2

/* The maximum number of supported worker threads */
#define MAX_WORKERS 512

/* The maximum number of MultiPV lines that can be reported */
#define MAX_MULTIPV_LINES 32

/* The name of the opening book file */
#define BOOKFILE_NAME "book.bin"

/* The name of the configuration file */
#define CONFIGFILE_NAME "marvin.ini"

#endif
