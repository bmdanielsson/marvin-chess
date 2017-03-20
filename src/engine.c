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

/* Size of the receive buffer */
#define RX_BUFFER_SIZE 4096

/* Size of the transmit buffer */
#define TX_BUFFER_SIZE 4096

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
 * Syntax: display
 */
static void cmd_display(struct gamestate *pos)
{
    dbg_print_board(pos);
}

/*
 * Custom command
 * Syntax: divide <depth>
 */
static void cmd_divide(char *cmd, struct gamestate *pos)
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

    test_run_divide(pos, depth);
}

/*
 * Custom command
 * Syntax: eval
 */
static void cmd_eval(struct gamestate *pos)
{
    eval_display(pos);
}

/*
 * Custom command
 * Syntax: info
 */
static void cmd_info(void)
{
    char *dummy;
    bool is_64bit;

    is_64bit = sizeof(dummy) == 8;

#ifdef HAS_POPCNT
    printf("%s %s (%s, popcnt)\n", APP_NAME, APP_VERSION,
           is_64bit?"64-bit":"32-bit");
#else
    printf("%s %s (%s)\n", APP_NAME, APP_VERSION,
           is_64bit?"64-bit":"32-bit");
#endif
    printf("%s\n", APP_AUTHOR);
}

/*
 * Custom command
 * Syntax: perft <depth>
 */
static void cmd_perft(char *cmd, struct gamestate *pos)
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

    test_run_perft(pos, depth);
}

void engine_loop(struct gamestate *pos)
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
        } else if (!strncmp(cmd, "display", 7)) {
            cmd_display(pos);
        } else if (!strncmp(cmd, "divide", 6)) {
            cmd_divide(cmd, pos);
        } else if (!strncmp(cmd, "eval", 4)) {
            cmd_eval(pos);
        } else if (!strncmp(cmd, "info", 4)) {
            cmd_info();
        } else if (!strncmp(cmd, "perft", 5)) {
            cmd_perft(cmd, pos);
        } else {
            handled = false;
        }

        /* Protocol commands */
        if (!handled) {
            handled = uci_handle_command(pos, cmd, &stop);
        }
        if (!handled) {
            handled = xboard_handle_command(pos, cmd, &stop);
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

bool engine_check_input(struct gamestate *pos, bool *ponderhit)
{
    *ponderhit = false;

    if (!poll_input()) {
        return false;
    }

    if (pos->protocol == PROTOCOL_UCI) {
        return uci_check_input(ponderhit);
    } else {
        return xboard_check_input(pos, ponderhit);
    }

    return false;
}

void engine_send_pv_info(struct gamestate *pos, int score)
{
    char   movestr[6];
    char   buffer[1024];
    time_t now;
    int    msec;
    int    k;
    int    nlines;

    if (pos->silent) {
        return;
    }

    if (pos->protocol == PROTOCOL_UCI) {
        return uci_send_pv_info(pos, score);
    } else if (pos->protocol == PROTOCOL_XBOARD) {
        return xboard_send_pv_info(pos, score);
    }

    /* Get the currently searched time */
    now = get_current_time();
    msec = (int)(now - pos->search_start);

    printf("=> depth: %d, score: %d, time: %d, nodes: %d\n",
           pos->depth, score, msec, pos->nodes);
    buffer[0] = '\0';
    nlines = 1;
    strcat(buffer, "  ");
    for (k=0;k<pos->pv_table[0].length;k++) {
        strcat(buffer, " ");
        move2str(pos->pv_table[0].moves[k], movestr);
        strcat(buffer, movestr);
        if (((int)strlen(buffer) > (nlines*70)) &&
            ((k+1) < pos->pv_table[0].length)) {
            strcat(buffer, "\n  ");
            nlines++;
        }
    }
    printf("%s\n", buffer);
}

void engine_send_move_info(struct gamestate *pos)
{
    if (pos->silent) {
        return;
    }

    if (pos->protocol == PROTOCOL_UCI) {
        uci_send_move_info(pos);
    }
}
