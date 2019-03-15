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
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <inttypes.h>

#include "uci.h"
#include "config.h"
#include "board.h"
#include "debug.h"
#include "eval.h"
#include "timectl.h"
#include "search.h"
#include "utils.h"
#include "hash.h"
#include "engine.h"
#include "validation.h"
#include "tbprobe.h"
#include "smp.h"

/* Different UCI modes */
static bool ponder_mode = false;
static bool own_book_mode = true;
static bool tablebase_mode = false;

static void uci_cmd_go(char *cmd, struct gamestate *state)
{
    char     *iter;
    int      movetime = 0;
    int      wtime = 0;
    int      btime = 0;
    int      winc = 0;
    int      binc = 0;
    int      movestogo = 0;
    char     best_movestr[6];
    char     ponder_movestr[6];
    bool     infinite = false;
    int      depth = 0;
    bool     in_movelist = false;
    char     *temp;
    bool     ponder = false;
    uint32_t move;

    /* Start the clock */
    tc_start_clock();

    /* Prepare for the search */
    search_reset_data(state);

    /*
     * Extract parameters. If an invalid parameter is
     * encountered the entire command is skipped. Unsupported
     * parameters are ignored.
     */
    iter = strchr(cmd, ' ');
    while ((iter != NULL) && (*iter != '\0')) {
        iter = skip_whitespace(iter);
        if (!strncmp(iter, "wtime", 5)) {
            if (sscanf(iter, "wtime %d", &wtime) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "btime", 5)) {
            if (sscanf(iter, "btime %d", &btime) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "winc", 4)) {
            if (sscanf(iter, "winc %d", &winc) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "binc", 4)) {
            if (sscanf(iter, "binc %d", &binc) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "movestogo", 9)) {
            if (sscanf(iter, "movestogo %d", &movestogo) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "movetime", 8)) {
            if (sscanf(iter, "movetime %d", &movetime) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "depth", 5)) {
            if (sscanf(iter, "depth %d", &depth) != 1) {
                return;
            }
            if ((depth >= MAX_SEARCH_DEPTH) || (depth <= 0)) {
                state->sd = MAX_SEARCH_DEPTH;
            } else {
                state->sd = depth;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "infinite", 8)) {
            infinite = true;
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "ponder", 6)) {
            ponder = true;
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (!strncmp(iter, "searchmoves", 11)) {
            iter = strchr(iter, ' ');
            in_movelist = true;
        } else if (in_movelist) {
            temp = strchr(iter, ' ');
            if (temp != NULL) {
                *temp = '\0';
            }
            move = str2move(iter, &state->pos);
            if (move != NOMOVE) {
                if (board_make_move(&state->pos, move)) {
                    board_unmake_move(&state->pos);
                    state->root_moves.moves[state->root_moves.nmoves] = move;
                    state->root_moves.nmoves++;
                }
            }
            if (temp != NULL) {
                *temp = ' ';
            }
            iter = temp;
        }
    }

    /* Set the correct time control */
    if (infinite) {
        tc_configure_time_control(TC_INFINITE, 0, 0, 0);
        state->exit_on_mate = false;
        state->in_book = false;
    } else if (movetime > 0) {
        tc_configure_time_control(TC_FIXED_TIME, movetime, 0, 0);
    } else if ((winc > 0) && (state->pos.stm == WHITE)) {
        tc_configure_time_control(TC_FISCHER, wtime, winc, 0);
    } else if ((binc > 0) && (state->pos.stm == BLACK)) {
        tc_configure_time_control(TC_FISCHER, btime, binc, 0);
    } else if ((movestogo > 0) && (state->pos.stm == WHITE)) {
        tc_configure_time_control(TC_TOURNAMENT, wtime, 0, movestogo);
    } else if ((movestogo > 0) && (state->pos.stm == BLACK)) {
        tc_configure_time_control(TC_TOURNAMENT, btime, 0, movestogo);
    } else if ((state->pos.stm == WHITE) && (wtime > 0)) {
        tc_configure_time_control(TC_SUDDEN_DEATH, wtime, 0, 0);
    } else if ((state->pos.stm == BLACK) && (btime > 0)) {
        tc_configure_time_control(TC_SUDDEN_DEATH, btime, 0, 0);
    } else {
        tc_configure_time_control(TC_INFINITE, 0, 0, 0);
        state->exit_on_mate = false;
        state->in_book = false;
    }

    /* Search the position for a move */
    smp_search(state, ponder && ponder_mode, own_book_mode, tablebase_mode);

    /* Send the best move */
    move2str(state->best_move, best_movestr);
    if (ponder_mode && (state->ponder_move != NOMOVE)) {
        move2str(state->ponder_move, ponder_movestr);
        engine_write_command("bestmove %s ponder %s", best_movestr,
                             ponder_movestr);
    } else {
        engine_write_command("bestmove %s", best_movestr);
    }
    tc_stop_clock();
}

static void uci_cmd_isready(void)
{
    engine_write_command("readyok");
}

static void uci_cmd_position(char *cmd, struct gamestate *state)
{
    char     *iter;
    char     *moves;
    char     *movestr;
    uint32_t move;

    /* Find the first parameter */
    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        /* Invalid command, set start position and return */
        board_start_position(&state->pos);
        return;
    }
    iter = skip_whitespace(iter);

    /* Find the beinning of the moves section if there is one */
    moves = strstr(cmd, "moves");

    /* Check if the parameter is fen or startpos */
    if (!strncmp(iter, "startpos", 8)) {
        board_start_position(&state->pos);
    } else if (!strncmp(iter, "fen", 3)) {
        /* Find beginning of FEN string */
        iter += strlen("fen");
        iter = skip_whitespace(iter);

        /* Find end of FEN string and temporarily add a '\0' */
        if (moves != NULL) {
            *(moves-1) = '\0';
        }

        /* Setup the position */
        if (!board_setup_from_fen(&state->pos, iter)) {
            /* Failed to setup position */
            board_start_position(&state->pos);
            return;
        }

        /* Remove the '\0' added before */
        if (moves != NULL) {
            *(moves-1) = ' ';
        }
    } else {
        /* Invalid command, set start position and return */
        board_start_position(&state->pos);
        return;
    }

    /* Execute all moves in the moves section */
    if (moves != NULL) {
        iter = moves + strlen("moves");
        iter = skip_whitespace(iter);

        /* Some GUIs sends an empty moves section in some cases */
        if (*iter == '\0') {
            return;
        }

        /* Play all moves on the internal board */
        while (iter != NULL) {
            movestr = skip_whitespace(iter);
            move = str2move(movestr, &state->pos);
            if (!board_make_move(&state->pos, move)) {
                /* Illegal move */
                return;
            }
            iter = strchr(movestr, ' ');
        }
    }
}

static void uci_cmd_setoption(char *cmd, struct gamestate *state)
{
    char *iter;
    int  value;

    /*
     * Handle options. Options that are not
     * recognized are ignored.
     */
    iter = strstr(cmd, "name");
    while (iter != NULL) {
        iter += 4;
        iter = skip_whitespace(iter);

        if (!strncmp(iter, "Hash", 4)) {
            iter += 4;
            iter = skip_whitespace(iter);
            if (sscanf(iter, "value %d", &value) == 1) {
                if (value > hash_tt_max_size()) {
                    value = hash_tt_max_size();
                } else if (value < MIN_MAIN_HASH_SIZE) {
                    value = MIN_MAIN_HASH_SIZE;
                }
                hash_tt_create_table(value);
            }
        } else if (!strncmp(iter, "Clear Hash", 10)) {
            hash_tt_clear_table();
        } else if (!strncmp(iter, "OwnBook", 7)) {
            iter = strstr(iter, "value");
            iter += strlen("value");
            iter = skip_whitespace(iter);
            if (!strncmp(iter, "false", 5)) {
                own_book_mode = false;
                state->in_book = false;
            } else if (!strncmp(iter, "true", 4)) {
                own_book_mode = true;
                state->in_book = true;
            }
        } else if (!strncmp(iter, "Ponder", 6)) {
            iter = strstr(iter, "value");
            iter += strlen("value");
            iter = skip_whitespace(iter);
            if (!strncmp(iter, "false", 5)) {
                ponder_mode = false;
            } else if (!strncmp(iter, "true", 4)) {
                ponder_mode = true;
            }
        } else if (!strncmp(iter, "SyzygyPath", 10) && !tablebase_mode) {
            iter = strstr(iter, "value");
            iter += strlen("value");
            iter = skip_whitespace(iter);

            strncpy(engine_syzygy_path, iter, MAX_PATH_LENGTH);
            tb_init(engine_syzygy_path);
            tablebase_mode = TB_LARGEST > 0;
        } else if (!strncmp(iter, "Threads", 7)) {
            iter += 7;
            iter = skip_whitespace(iter);
            if (sscanf(iter, "value %d", &value) == 1) {
                if (value > MAX_WORKERS) {
                    value = MAX_WORKERS;
                } else if (value < 1) {
                    value = 1;
                }
                smp_destroy_workers();
                smp_create_workers(value);
            }
        }
        iter = strstr(iter, "name");
    }
}

static void uci_cmd_uci(struct gamestate *state)
{
    engine_protocol = PROTOCOL_UCI;

    tablebase_mode = TB_LARGEST > 0;

    state->silent = false;
    state->in_book = true;

    engine_write_command("id name %s %s", APP_NAME, APP_VERSION);
    engine_write_command("id author %s", APP_AUTHOR);
    engine_write_command("option name Hash type spin default %d min %d max %d",
                         engine_default_hash_size, MIN_MAIN_HASH_SIZE,
						 hash_tt_max_size());
    engine_write_command("option name Clear Hash type button");
    engine_write_command("option name OwnBook type check default true");
    engine_write_command("option name Ponder type check default false");
    engine_write_command("option name SyzygyPath type string default %s",
                         engine_syzygy_path[0] != '\0'?
                                                engine_syzygy_path:"<empty>");
    engine_write_command(
                        "option name Threads type spin default %d min 1 max %d",
                        engine_default_num_threads, MAX_WORKERS);
    engine_write_command("uciok");
}

static void uci_cmd_ucinewgame(struct gamestate *state)
{
    hash_tt_clear_table();
    if (own_book_mode) {
        state->in_book = true;
    }
}

bool uci_handle_command(struct gamestate *state, char *cmd, bool *stop)
{
    assert(cmd != NULL);
    assert(stop != NULL);

    *stop = false;

    if (!strncmp(cmd, "debug", 5)) {
        /* Ignore */
    } else if (!strncmp(cmd, "go", 2)) {
        /* Both UCI and Xboard protocol has a go command */
        if (engine_protocol == PROTOCOL_UCI) {
            uci_cmd_go(cmd, state);
        } else {
            return false;
        }
    } else if (!strncmp(cmd, "isready", 7)) {
        uci_cmd_isready();
    } else if (!strncmp(cmd, "position", 8)) {
        uci_cmd_position(cmd, state);
    } else if (!strncmp(cmd, "setoption", 9)) {
        uci_cmd_setoption(cmd, state);
	} else if (!strncmp(cmd, "stop", 4)) {
		/* Ignore */
    } else if (!strncmp(cmd, "uci", 3) && (strlen(cmd) == 3)) {
        uci_cmd_uci(state);
    } else if (!strncmp(cmd, "ucinewgame", 10) && (strlen(cmd) == 10)) {
        uci_cmd_ucinewgame(state);
    } else if (!strncmp(cmd, "quit", 4)) {
        /* Both UCI and Xboard protocol has a quit command */
        if (engine_protocol == PROTOCOL_UCI) {
            *stop = true;
        } else {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

bool uci_check_input(struct search_worker *worker)
{
    char *cmd;
    bool stop = false;

    /* Read command */
    cmd = engine_read_command();
    if (cmd == NULL) {
        /* The GUI exited unexpectedly */
        return false;
    }

    /* Process command */
    if (!strncmp(cmd, "isready", 7)) {
        uci_cmd_isready();
    } else if(!strncmp(cmd, "ponderhit", 9)) {
        tc_allocate_time();
        worker->state->pondering = false;
    } else if (!strncmp(cmd, "stop", 4)) {
        worker->state->pondering = false;
        stop = true;
    }

    return stop;
}

void uci_send_pv_info(struct search_worker *worker, struct pv *pv, int depth,
                      int seldepth, int score)
{
    char     movestr[6];
    char     buffer[1024];
    int      msec;
    int      nps;
    int      k;
    uint64_t tbhits;
    uint64_t nodes;

    /* Get the currently searched time */
    msec = (int)tc_elapsed_time();
    nodes = smp_nodes();
    nps = (msec > 0)?(nodes/msec)*1000:0;
    tbhits = worker->state->root_in_tb?1:smp_tbhits();

    /* Adjust score in case the root position was found in tablebases */
    if (worker->state->root_in_tb) {
        score = ((score > FORCED_MATE) || (score < (-FORCED_MATE)))?
                                            score:worker->state->root_tb_score;
    }

    /* Build command */
    sprintf(buffer, "info depth %d seldepth %d nodes %"PRIu64" time %d nps %d "
            "tbhits %"PRIu64" hashfull %d score cp %d pv", depth, seldepth,
            nodes, msec, nps, tbhits, hash_tt_usage(), score);
    for (k=0;k<pv->length;k++) {
        strcat(buffer, " ");
        move2str(pv->moves[k], movestr);
        strcat(buffer, movestr);
    }

    /* Write command */
    engine_write_command(buffer);
}

void uci_send_move_info(struct search_worker *worker)
{
    char movestr[6];
    int  msec;

    /* Get the currently searched time */
    msec = (int)tc_elapsed_time();
    if (msec < 1000) {
        /* Wait 1s before starting to send move info to avoid traffic */
        return;
    }

    /* Send command */
    move2str(worker->currmove, movestr);
    engine_write_command("info depth %d currmove %s currmovenumber %d",
                         worker->depth, movestr, worker->currmovenumber);
}
