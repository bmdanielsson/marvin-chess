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
#include "chess.h"
#include "timectl.h"
#include "thread.h"
#include "smp.h"
#include "board.h"

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
 * Syntax: browse
 */
static void cmd_browse(struct gamestate *state)
{
    dbg_browse_transposition_table(&state->pos);
}

/*
 * Custom command
 * Syntax: display
 */
static void cmd_display(struct gamestate *state)
{
    dbg_print_board(&state->pos);
}

/*
 * Custom command
 * Syntax: divide <depth>
 */
static void cmd_divide(char *cmd, struct gamestate *state)
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

    test_run_divide(&state->pos, depth);
}

/*
 * Custom command
 * Syntax: eval
 */
static void cmd_eval(struct gamestate *state)
{
    int nnue_score;
    int hce_score;

    hce_score = eval_evaluate(&state->pos, true);
    if (engine_using_nnue && engine_loaded_net) {
        nnue_score = eval_evaluate(&state->pos, false);
        printf("HCE: %d, NNUE: %d\n",
               state->pos.stm == WHITE?hce_score:-hce_score,
               state->pos.stm == WHITE?nnue_score:-nnue_score);
    } else {
        printf("HCE: %d\n", state->pos.stm == WHITE?hce_score:-hce_score);
    }
}

/*
 * Custom command
 * Syntax: perft <depth>
 */
static void cmd_perft(char *cmd, struct gamestate *state)
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

    test_run_perft(&state->pos, depth);
}

/*
 * Custom command
 * Syntax: quiet
 */
static void cmd_quiet(struct gamestate *state)
{
    struct position pos;
    struct movelist pv;
    int             k;
    char            movestr[MAX_MOVESTR_LENGTH];

    pv.size = 0;
    pos = state->pos;
    board_quiet(&pos, &pv);

    printf("pv");
    for (k=0;k<pv.size;k++) {
        move2str(pv.moves[k], movestr);
        printf(" %s", movestr);
    }
    printf("\n");
}

/*
 * Custom command
 * Syntax: bench
 */
static void cmd_bench(void)
{
    test_run_benchmark();
}

void engine_loop(struct gamestate *state)
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
        if (MATCH(cmd, "browse")) {
            cmd_browse(state);
        } else if (MATCH(cmd, "display")) {
            cmd_display(state);
        } else if (MATCH(cmd, "divide")) {
            cmd_divide(cmd, state);
        } else if (MATCH(cmd, "eval")) {
            cmd_eval(state);
        } else if (MATCH(cmd, "perft")) {
            cmd_perft(cmd, state);
        } else if (MATCH(cmd, "quiet")) {
            cmd_quiet(state);
        } else if (MATCH(cmd, "bench")) {
            cmd_bench();
        } else {
            handled = false;
        }

        /* Protocol commands */
        if (!handled) {
            handled = uci_handle_command(state, cmd, &stop);
        }
        if (!handled) {
            handled = xboard_handle_command(state, cmd, &stop);
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

void engine_send_pv_info(struct gamestate *state, struct pvinfo *pvinfo)
{
    if (state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        return uci_send_pv_info(state, pvinfo);
    } else if (engine_protocol == PROTOCOL_XBOARD) {
        return xboard_send_pv_info(state, pvinfo);
    }
}

void engine_send_bound_info(struct search_worker *worker, int score, bool lower)
{
    if (worker->state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_bound_info(worker, score, lower);
    }
}

void engine_send_move_info(struct search_worker *worker, int movenumber,
                           uint32_t move)
{
    if (worker->state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_move_info(worker, movenumber, move);
    }
}

void engine_send_multipv_info(struct search_worker *worker)
{
    if (worker->state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_multipv_info(worker);
    }
}

void engine_send_eval_info(struct search_worker *worker)
{
    if (worker->state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_eval_info();
    }
}
