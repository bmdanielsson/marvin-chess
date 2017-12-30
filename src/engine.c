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

/* Size of the receive buffer */
#define RX_BUFFER_SIZE 4096

/* Size of the transmit buffer */
#define TX_BUFFER_SIZE 4096

/* Global engine variables */
enum protocol engine_protocol = PROTOCOL_UNSPECIFIED;
char engine_syzygy_path[1024] = {'\0'};
int engine_default_hash_size = DEFAULT_MAIN_HASH_SIZE;

/* Buffer used for receiving commands */
static char rx_buffer[RX_BUFFER_SIZE];

/* Buffer used for sending commands */
static char tx_buffer[TX_BUFFER_SIZE];

/*
 * Command received during search that should be
 * executed when the search finishes.
 */
static char pending_cmd_buffer[RX_BUFFER_SIZE];

/*
 * Custom command
 * Syntax: bench
 */
static void cmd_bench(void)
{
    test_run_benchmark();
}

/*
 * Custom command
 * Syntax: browse
 */
static void cmd_browse(struct gamestate *state)
{
    dbg_browse_transposition_table(&state->worker.pos);
}

/*
 * Custom command
 * Syntax: display
 */
static void cmd_display(struct gamestate *state)
{
    dbg_print_board(&state->worker.pos);
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

    test_run_divide(&state->worker.pos, depth);
}

/*
 * Custom command
 * Syntax: eval
 */
static void cmd_eval(struct gamestate *state)
{
    eval_display(&state->worker);
}

/*
 * Custom command
 * Syntax: info
 */
static void cmd_info(void)
{
    char *dummy;
    char str[256];
    bool is_64bit;

    is_64bit = sizeof(dummy) == 8;

    str[0] = '\0';
    sprintf(str, "%s %s (%s", APP_NAME, APP_VERSION,
            is_64bit?"64-bit":"32-bit");
#ifdef HAS_POPCNT
    strcat(str, ", popcnt");
#endif
#ifdef HAS_ALIGNED_MALLOC
    strcat(str, ", memalign");
#endif
#ifdef HAS_PREFETCH
    strcat(str, ", prefetch");
#endif
    strcat(str, ")");
    printf("%s\n", str);

    printf("%s\n", APP_AUTHOR);
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

    test_run_perft(&state->worker.pos, depth);
}

void engine_loop(struct gamestate *state)
{
    char *cmd;
    bool stop = false;
    bool handled = false;

    /* Enter the main command loop */
    while (!stop) {
        if (strlen(pending_cmd_buffer) != 0) {
            strncpy(rx_buffer, pending_cmd_buffer, sizeof(rx_buffer));
            cmd = rx_buffer;
            pending_cmd_buffer[0] = '\0';
        } else {
            cmd = engine_read_command();
            if (cmd == NULL) {
                /* The GUI exited unexpectidly */
                break;
            }
        }

        /* Custom commands */
        handled = true;
        if (!strncmp(cmd, "bench", 5)) {
            cmd_bench();
        } else if (!strncmp(cmd, "browse", 6)) {
            cmd_browse(state);
        } else if (!strncmp(cmd, "display", 7)) {
            cmd_display(state);
        } else if (!strncmp(cmd, "divide", 6)) {
            cmd_divide(cmd, state);
        } else if (!strncmp(cmd, "eval", 4)) {
            cmd_eval(state);
        } else if (!strncmp(cmd, "info", 4)) {
            cmd_info();
        } else if (!strncmp(cmd, "perft", 5)) {
            cmd_perft(cmd, state);
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

    va_start(ap, format);
    vsprintf(tx_buffer, format, ap);
    printf("%s\n", tx_buffer);
    va_end(ap);

    LOG_INFO2("<== %s\n", tx_buffer);
}

void engine_set_pending_command(char *cmd)
{
    assert(cmd != NULL);

    strncpy(pending_cmd_buffer, cmd, sizeof(pending_cmd_buffer));
}

char* engine_get_pending_command(void)
{
    return strlen(pending_cmd_buffer) > 0?pending_cmd_buffer:NULL;
}

void engine_clear_pending_command(void)
{
    pending_cmd_buffer[0] = '\0';
}

bool engine_check_input(struct gamestate *state, bool *ponderhit)
{
    *ponderhit = false;

    if (!poll_input()) {
        return false;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        return uci_check_input(ponderhit);
    } else {
        return xboard_check_input(state, ponderhit);
    }

    return false;
}

void engine_send_pv_info(struct gamestate *state, int score)
{
    char movestr[6];
    char buffer[1024];
    int  msec;
    int  k;
    int  nlines;

    if (state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        return uci_send_pv_info(state, score);
    } else if (engine_protocol == PROTOCOL_XBOARD) {
        return xboard_send_pv_info(state, score);
    }

    /* Get the currently searched time */
    msec = (int)tc_elapsed_time();

    printf("=> depth: %d, score: %d, time: %d, nodes: %d\n",
           state->worker.depth, score, msec, state->worker.nodes);
    buffer[0] = '\0';
    nlines = 1;
    strcat(buffer, "  ");
    for (k=0;k<state->worker.pv_table[0].length;k++) {
        strcat(buffer, " ");
        move2str(state->worker.pv_table[0].moves[k], movestr);
        strcat(buffer, movestr);
        if (((int)strlen(buffer) > (nlines*70)) &&
            ((k+1) < state->worker.pv_table[0].length)) {
            strcat(buffer, "\n  ");
            nlines++;
        }
    }
    printf("%s\n", buffer);
}

void engine_send_move_info(struct gamestate *state)
{
    if (state->silent) {
        return;
    }

    if (engine_protocol == PROTOCOL_UCI) {
        uci_send_move_info(state);
    }
}
