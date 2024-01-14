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
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

#include "engine.h"
#include "uci.h"
#include "xboard.h"
#include "debug.h"
#include "test.h"
#include "polybook.h"
#include "config.h"
#include "eval.h"
#include "utils.h"
#include "types.h"
#include "timectl.h"
#include "thread.h"
#include "smp.h"
#include "position.h"
#include "hash.h"
#include "egtb.h"

/* The maximum length of a line in the configuration file */
#define CFG_MAX_LINE_LENGTH 1024

/* Size of the receive buffer */
#define RX_BUFFER_SIZE 4096

/* Size of the transmit buffer */
#define TX_BUFFER_SIZE 4096

/* Global engine variables */
enum protocol engine_protocol = PROTOCOL_UNSPECIFIED;
enum variant engine_variant = VARIANT_STANDARD;
char engine_syzygy_path[MAX_PATH_LENGTH+1] = {'\0'};
int engine_default_hash_size = DEFAULT_MAIN_HASH_SIZE;
int engine_default_num_threads = 1;
bool engine_using_nnue = false;
bool engine_loaded_net = false;
char engine_eval_file[MAX_PATH_LENGTH+1] = {'\0'};

/* Buffer used for receiving commands */
static char rx_buffer[RX_BUFFER_SIZE+1];

/* Buffer used for sending commands */
static char tx_buffer[TX_BUFFER_SIZE+1];

/*
 * Command received during search that should be
 * executed when the search finishes.
 */
static char pending_cmd_buffer[RX_BUFFER_SIZE+1];

/* Lock used to synchronize command output */
static mutex_t tx_lock;

/*
 * Custom command
 * Syntax: display
 */
static void cmd_display(struct engine *engine)
{
    dbg_print_board(&engine->pos);
}

/*
 * Custom command
 * Syntax: divide <depth>
 */
static void cmd_divide(char *cmd, struct engine *engine)
{
    int  depth;
    char *iter;

    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        return;
    }
    iter++;

    if (sscanf(iter, "%d", &depth) != 1) {
        return;
    }

    test_run_divide(&engine->pos, depth);
}

/*
 * Custom command
 * Syntax: eval
 */
static void cmd_eval(struct engine *engine)
{
    int nnue_score;
    int hce_score;

    hce_score = eval_evaluate(&engine->pos, true);
    if (engine_using_nnue && engine_loaded_net) {
        nnue_score = eval_evaluate(&engine->pos, false);
        printf("HCE: %d, NNUE: %d\n",
               engine->pos.stm == WHITE?hce_score:-hce_score,
               engine->pos.stm == WHITE?nnue_score:-nnue_score);
    } else {
        printf("HCE: %d\n", engine->pos.stm == WHITE?hce_score:-hce_score);
    }
}

/*
 * Custom command
 * Syntax: perft <depth>
 */
static void cmd_perft(char *cmd, struct engine *engine)
{
    int  depth;
    char *iter;

    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        return;
    }
    iter++;

    if (sscanf(iter, "%d", &depth) != 1) {
        return;
    }

    test_run_perft(&engine->pos, depth);
}

/*
 * Custom command
 * Syntax: bench
 */
static void cmd_bench(void)
{
    test_run_benchmark();
}

void engine_read_config_file(char *cfgfile)
{
    FILE *fp;
    char buffer[CFG_MAX_LINE_LENGTH];
    char *line;
    int  int_val;

    /* Initialise */
    fp = fopen(cfgfile, "r");
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
            egtb_init(engine_syzygy_path);
        } else if (sscanf(line, "NUM_THREADS=%d", &int_val) == 1) {
            engine_default_num_threads = CLAMP(int_val, 1, MAX_WORKERS);
        }

        /* Next line */
        line = fgets(buffer, CFG_MAX_LINE_LENGTH, fp);
    }

    /* Clean up */
    fclose(fp);
}

struct engine* engine_create(void)
{
    struct engine *engine;

    engine = aligned_malloc(64, sizeof(struct engine));
    if (engine == NULL) {
        return NULL;
    }
    memset(engine, 0, sizeof(struct engine));
    pos_reset(&engine->pos);
    pos_setup_start_position(&engine->pos);
    engine->multipv = 1;

    return engine;
}

void engine_destroy(struct engine *engine)
{
    assert(engine != NULL);

    aligned_free(engine);
}

void engine_loop(struct engine *engine)
{
    char *cmd;
    bool stop = false;
    bool handled = false;

    mutex_init(&tx_lock);

    /* Enter the main command loop */
    while (!stop) {
        if (strlen(pending_cmd_buffer) != 0) {
            strncpy(rx_buffer, pending_cmd_buffer, sizeof(rx_buffer));
            cmd = rx_buffer;
            pending_cmd_buffer[0] = '\0';
        } else {
            cmd = engine_read_command();
            if (cmd == NULL) {
                /* The GUI exited unexpectedly */
                break;
            }
        }

        /* Custom commands */
        handled = true;
        if (MATCH(cmd, "display")) {
            cmd_display(engine);
        } else if (MATCH(cmd, "divide")) {
            cmd_divide(cmd, engine);
        } else if (MATCH(cmd, "eval")) {
            cmd_eval(engine);
        } else if (MATCH(cmd, "perft")) {
            cmd_perft(cmd, engine);
        } else if (MATCH(cmd, "bench")) {
            cmd_bench();
        } else {
            handled = false;
        }

        /* Protocol commands */
        if (!handled) {
            handled = uci_handle_command(engine, cmd, &stop);
        }
        if (!handled) {
            handled = xboard_handle_command(engine, cmd, &stop);
        }
        if (!handled) {
            LOG_INFO1("Unknown command: %s\n", cmd);
        }
    }

    mutex_destroy(&tx_lock);
}

char* engine_read_command(void)
{
    char *iter;

    /* Read command from stdin */
    if (fgets(rx_buffer, RX_BUFFER_SIZE, stdin) == NULL) {
        return NULL;
    }

    /* Remove trailing white space */
    iter = &rx_buffer[strlen(&rx_buffer[0])-1];
    while ((iter > &rx_buffer[0]) && (isspace(*iter))) {
        *iter = '\0';
        iter--;
    }

    LOG_INFO2("==> %s\n", rx_buffer);

    return rx_buffer;
}

void engine_write_command(char *format, ...)
{
    va_list ap;

    assert(format != NULL);

    mutex_lock(&tx_lock);

    va_start(ap, format);
    vsprintf(tx_buffer, format, ap);
    printf("%s\n", tx_buffer);
    va_end(ap);

    mutex_unlock(&tx_lock);

    LOG_INFO2("<== %s\n", tx_buffer);
}

void engine_set_pending_command(char *cmd)
{
    assert(cmd != NULL);

    strncpy(pending_cmd_buffer, cmd, RX_BUFFER_SIZE);
}

char* engine_get_pending_command(void)
{
    return strlen(pending_cmd_buffer) > 0?pending_cmd_buffer:NULL;
}

void engine_clear_pending_command(void)
{
    pending_cmd_buffer[0] = '\0';
}

bool engine_check_input(struct search_worker *worker)
{
    if (!poll_input()) {
        return false;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        return uci_check_input(worker);
    } else {
        return xboard_check_input(worker);
    }

    return false;
}

bool engine_wait_for_input(struct search_worker *worker)
{
    if (engine_protocol == PROTOCOL_UCI) {
        return uci_check_input(worker);
    } else {
        return xboard_check_input(worker);
    }

    return false;
}

void engine_send_pv_info(struct engine *engine, struct pvinfo *pvinfo)
{
    if (engine_protocol == PROTOCOL_UNSPECIFIED) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        return uci_send_pv_info(engine, pvinfo);
    } else if (engine_protocol == PROTOCOL_XBOARD) {
        return xboard_send_pv_info(engine, pvinfo);
    }
}

void engine_send_bound_info(struct search_worker *worker, int score, bool lower)
{
    if (engine_protocol == PROTOCOL_UNSPECIFIED) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_bound_info(worker, score, lower);
    }
}

void engine_send_move_info(struct search_worker *worker, int movenumber,
                           uint32_t move)
{
    if (engine_protocol == PROTOCOL_UNSPECIFIED) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_move_info(worker, movenumber, move);
    }
}

void engine_send_multipv_info(struct search_worker *worker)
{
    if (engine_protocol == PROTOCOL_UNSPECIFIED) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_multipv_info(worker);
    }
}
