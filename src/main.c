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
#include "types.h"
#include "position.h"
#include "bitboard.h"
#include "debug.h"
#include "movegen.h"
#include "engine.h"
#include "polybook.h"
#include "config.h"
#include "test.h"
#include "smp.h"
#include "hash.h"
#include "see.h"
#include "search.h"
#include "nnue.h"
#include "data.h"
#include "sfen.h"

static void cleanup(void)
{
    dbg_log_close();
}

static void print_version(void)
{
    printf("%s %s (%s)\n", APP_NAME, APP_VERSION, APP_ARCH);
    printf("%s\n", APP_AUTHOR);
    printf("\n");
    if (!engine_using_nnue || !engine_loaded_net) {
        printf("Using classic evaluation\n");
    } else {
        printf("Using NNUE evaluation with %s\n", engine_eval_file);
    }
}

int main(int argc, char *argv[])
{
    struct engine *engine;

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
    engine_read_config_file(CONFIGFILE_NAME);

    /* Initialize components */
    data_init();
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
        (MATCH(argv[1], "-b") || MATCH(argv[1], "--bench"))) {
        test_run_benchmark();
        return 0;
    } else if ((argc == 2) &&
               (MATCH(argv[1], "-v") || MATCH(argv[1], "--version"))) {
        print_version();
        return 0;
    } else if ((argc >= 2) && (MATCH(argv[1], "--generate"))) {
        return sfen_generate(argc, argv);
    }

    /* Create engine */
    engine = engine_create();
    if (engine == NULL) {
        return 1;
    }

    /* Enter the main engine loop */
    engine_loop(engine);

    /* Clean up */
    polybook_close();
    engine_destroy(engine);
    hash_tt_destroy_table();
    smp_destroy_workers();
    smp_destroy();
    nnue_destroy();

    return 0;
}
