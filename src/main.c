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
#include <limits.h>

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
#include "tbprobe.h"
#include "smp.h"
#include "hash.h"
#include "see.h"
#include "search.h"
#include "nnue.h"

/* The maximum length of a line in the configuration file */
#define CFG_MAX_LINE_LENGTH 1024

/* Configration values */
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
            engine_default_hash_size = CLAMP(int_val, MIN_MAIN_HASH_SIZE,
                                             hash_tt_max_size());
        } else if (sscanf(line, "LOG_LEVEL=%d", &int_val) == 1) {
            dbg_set_log_level(int_val);
        } else if (sscanf(line, "SYZYGY_PATH=%s", engine_syzygy_path) == 1) {
            tb_init(engine_syzygy_path);
        } else if (sscanf(line, "NUM_THREADS=%d", &int_val) == 1) {
            engine_default_num_threads = CLAMP(int_val, 1, MAX_WORKERS);
        }

        /* Next line */
		line = fgets(buffer, CFG_MAX_LINE_LENGTH, fp);
    }

	/* Clean up */
	fclose(fp);
}

static void print_version(void)
{
    printf("%s %s (%s)\n", APP_NAME, APP_VERSION, APP_ARCH);
    printf("%s\n", APP_AUTHOR);
}

int main(int argc, char *argv[])
{
    struct gamestate *state;

    /* Register a clean up function */
    atexit(cleanup);

    /* Turn off buffering for I/O */
    setbuf(stdout, NULL);
    setbuf(stdin, NULL);

    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Setup the default NNUE net */
    strcpy(engine_eval_file, NETFILE_NAME);
    nnue_init();
    engine_loaded_net = nnue_load_net(NULL);
    engine_using_nnue = engine_loaded_net;

    /* Read configuration file */
    read_config_file();

    /* Initialize components */
    chess_data_init();
    bb_init();
    search_init();
    polybook_open(BOOKFILE_NAME);

    /* Setup SMP */
    smp_init();
    smp_create_workers(engine_default_num_threads);

    /* Setup main transposition table */
    hash_tt_create_table(engine_default_hash_size);

    /* Handle command line options */
    if ((argc == 2) &&
        (!strncmp(argv[1], "-b", 2) || !strncmp(argv[1], "--bench", 6))) {
        test_run_benchmark();
        return 0;
    } else if ((argc == 2) &&
               (!strncmp(argv[1], "-v", 2) ||
                !strncmp(argv[1], "--version", 9))) {
        print_version();
        return 0;
    }

    /* Create game state */
    state = create_game_state();
    if (state == NULL) {
        return 1;
    }

    /* Enter the main engine loop */
    engine_loop(state);

    /* Clean up */
    polybook_close();
    destroy_game_state(state);
    smp_destroy_workers();
    smp_destroy();
    nnue_destroy();

    return 0;
}
