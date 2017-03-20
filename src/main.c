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
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include "utils.h"
#include "chess.h"
#include "board.h"
#include "bitboard.h"
#include "debug.h"
#include "movegen.h"
#include "engine.h"
#include "polybook.h"
#include "config.h"
#include "test.h"
#include "eval.h"

/* The maximum length of a line in the configuration file */
#define CFG_MAX_LINE_LENGTH 1024

/* Configration values */
static int cfg_default_hash_size = DEFAULT_MAIN_HASH_SIZE;
static int cfg_log_level = 0;

static void cleanup(void)
{
    dbg_log_close();
}

static void read_config_file(void)
{
	FILE *fp;
	char buffer[CFG_MAX_LINE_LENGTH];
	char *line;
	int  int_val;

	/* Initialise */
    fp = fopen(CONFIGFILE_NAME, "r");
	if (fp == NULL) {
		return;
    }

	/* Parse the file line by line */
	line = fgets(buffer, CFG_MAX_LINE_LENGTH, fp);
	while (line != NULL) {
	    if (sscanf(line, "HASH_SIZE=%d", &int_val) == 1) {
		    if (int_val > MAX_MAIN_HASH_SIZE) {
	            cfg_default_hash_size = MAX_MAIN_HASH_SIZE;
            } else if (int_val < MIN_MAIN_HASH_SIZE) {
			    cfg_default_hash_size = MIN_MAIN_HASH_SIZE;
            } else {
			    cfg_default_hash_size = int_val;
            }
	    } else if (sscanf(line, "LOG_LEVEL=%d", &int_val) == 1) {
			cfg_log_level = int_val;
        }

        /* Next line */
		line = fgets(buffer, CFG_MAX_LINE_LENGTH, fp);
    }

	/* Clean up */
	fclose(fp);
}

int main(int argc, char *argv[])
{
    struct gamestate *pos;

    /* Register a clean up function */
    atexit(cleanup);

    /* Turn off buffering for I/O */
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Read configuration file */
    read_config_file();

    /* Initialize components */
    dbg_log_init(cfg_log_level);
    chess_data_init();
    bb_init();
    eval_reset();
    polybook_open(BOOKFILE_NAME);

    /* Handle specific benchmark command line option */
    if ((argc == 2) && !strncmp(argv[1], "-b", 2)) {
        test_run_benchmark();
        return 0;
    }

    /* Create game state */
    pos = create_game_state(cfg_default_hash_size);
    if (pos == NULL) {
        return 1;
    }
    pos->default_hash_size = cfg_default_hash_size;

    /* Enter the main engine loop */
    engine_loop(pos);

    /* Destroy game state */
    polybook_close();
    destroy_game_state(pos);

    return 0;
}
