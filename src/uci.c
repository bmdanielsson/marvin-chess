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
#include "nnue.h"

/* Different UCI modes */
static bool ponder_mode = false;
static bool own_book_mode = true;
static bool tablebase_mode = false;

/* Helper variable used for sorting pv lines */
static struct pvinfo sorted_mpv_lines[MAX_MULTIPV_LINES];

static void uci_cmd_go(char *cmd, struct gamestate *state)
{
    char     *iter;
    int      movetime = 0;
    int      moveinc = 0;
    int      wtime = 0;
    int      btime = 0;
    int      winc = 0;
    int      binc = 0;
    int      movestogo = 0;
    char     best_movestr[MAX_MOVESTR_LENGTH];
    char     ponder_movestr[MAX_MOVESTR_LENGTH];
    int      flags = 0;
    bool     infinite_time = false;
    bool     fixed_time = false;
    int      depth = 0;
    uint64_t nodes = 0ULL;
    bool     in_movelist = false;
    char     *temp;
    bool     ponder = false;
    uint32_t move;
    bool     skip_book = false;
    uint32_t best_move;
    uint32_t ponder_move;

    /* Start the clock */
    tc_start_clock();

    /* Set default search parameters */
    state->move_filter.size = 0;
    state->exit_on_mate = true;
    state->sd = MAX_SEARCH_DEPTH;

    /*
     * Extract parameters. If an invalid parameter is
     * encountered the entire command is skipped. Unsupported
     * parameters are ignored.
     */
    iter = strchr(cmd, ' ');
    while ((iter != NULL) && (*iter != '\0')) {
        iter = skip_whitespace(iter);
        if (MATCH(iter, "wtime")) {
            if (sscanf(iter, "wtime %d", &wtime) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= TC_TIME_LIMIT;
        } else if (MATCH(iter, "btime")) {
            if (sscanf(iter, "btime %d", &btime) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= TC_TIME_LIMIT;
        } else if (MATCH(iter, "winc")) {
            if (sscanf(iter, "winc %d", &winc) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= TC_TIME_LIMIT;
        } else if (MATCH(iter, "binc")) {
            if (sscanf(iter, "binc %d", &binc) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= TC_TIME_LIMIT;
        } else if (MATCH(iter, "movestogo")) {
            if (sscanf(iter, "movestogo %d", &movestogo) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= (TC_REGULAR|TC_TIME_LIMIT);
        } else if (MATCH(iter, "movetime")) {
            if (sscanf(iter, "movetime %d", &movetime) != 1) {
                return;
            }
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            fixed_time = true;
            flags |= (TC_FIXED_TIME|TC_TIME_LIMIT);
        } else if (MATCH(iter, "depth")) {
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
            flags |= TC_DEPTH_LIMIT;
        } else if (MATCH(iter, "nodes")) {
            if (sscanf(iter, "nodes %" SCNu64 "", &nodes) != 1) {
                return;
            }
            state->max_nodes = nodes;
            iter = strchr(iter, ' ');
            iter = skip_whitespace(iter);
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= TC_NODE_LIMIT;
        } else if (MATCH(iter, "infinite")) {
            infinite_time = true;
            iter = strchr(iter, ' ');
            in_movelist = false;
            flags |= TC_INFINITE_TIME;
        } else if (MATCH(iter, "ponder")) {
            ponder = true;
            iter = strchr(iter, ' ');
            in_movelist = false;
        } else if (MATCH(iter, "searchmoves")) {
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
                    state->move_filter.moves[state->move_filter.size] = move;
                    state->move_filter.size++;
                }
            }
            if (temp != NULL) {
                *temp = ' ';
            }
            iter = temp;
        }
    }

    /* Set the correct time control */
    if (infinite_time) {
        movetime = 0;
        moveinc = 0;
        movestogo = 0;
        state->exit_on_mate = false;
        skip_book = true;
    } else if (fixed_time) {
        moveinc = 0;
        movestogo = 0;
    } else {
        movetime = state->pos.stm == WHITE?wtime:btime;
        moveinc = state->pos.stm == WHITE?winc:binc;
    }
    tc_configure_time_control(movetime, moveinc, movestogo, flags);

    /* Search the position for a move */
    best_move = smp_search(state, ponder && ponder_mode,
                           own_book_mode && !skip_book, tablebase_mode,
                           &ponder_move);

    /* Send the best move */
    move2str(best_move, best_movestr);
    if (ponder_mode && (ponder_move != NOMOVE)) {
        move2str(ponder_move, ponder_movestr);
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
    if (MATCH(iter, "startpos")) {
        board_start_position(&state->pos);
    } else if (MATCH(iter, "fen")) {
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
    char *namestr;
    char *valuestr;
    int  value;

    /*
     * Handle options. Options that are not
     * recognized are ignored.
     */
    iter = strstr(cmd, "name");
    while (iter != NULL) {
        /* Find the option name and value */
        iter += 4;
        iter = skip_whitespace(iter);
        namestr = iter;
        iter = strstr(iter, "value");
        if (iter == NULL) {
            /* Skip invalid command */
            iter = strstr(namestr, "name");
            continue;
        }
        iter += strlen("value");
        iter = skip_whitespace(iter);
        valuestr = iter;

        /* Handle option */
        if (MATCH(namestr, "Hash")) {
            if (sscanf(valuestr, "%d", &value) == 1) {
                if (value > hash_tt_max_size()) {
                    value = hash_tt_max_size();
                } else if (value < MIN_MAIN_HASH_SIZE) {
                    value = MIN_MAIN_HASH_SIZE;
                }
                hash_tt_create_table(value);
            }
        } else if (MATCH(namestr, "OwnBook")) {
            if (MATCH(valuestr, "false")) {
                own_book_mode = false;
            } else if (MATCH(valuestr, "true")) {
                own_book_mode = true;
            }
        } else if (MATCH(namestr, "Ponder")) {
            if (MATCH(valuestr, "false")) {
                ponder_mode = false;
            } else if (MATCH(valuestr, "true")) {
                ponder_mode = true;
            }
        } else if (MATCH(namestr, "UCI_Chess960")) {
            if (MATCH(valuestr, "false")) {
                engine_variant = VARIANT_STANDARD;
            } else if (MATCH(valuestr, "true")) {
                engine_variant = VARIANT_FRC;
            }
        } else if (MATCH(namestr, "SyzygyPath")) {
            strncpy(engine_syzygy_path, valuestr, MAX_PATH_LENGTH);
            tb_init(engine_syzygy_path);
            tablebase_mode = TB_LARGEST > 0;
        } else if (MATCH(namestr, "Threads")) {
            if (sscanf(valuestr, "%d", &value) == 1) {
                if (value > MAX_WORKERS) {
                    value = MAX_WORKERS;
                } else if (value < 1) {
                    value = 1;
                }
                smp_destroy_workers();
                smp_create_workers(value);
            }
        } else if (MATCH(namestr, "MoveOverhead")) {
            if (sscanf(valuestr, "%d", &value) == 1) {
                if (value < MIN_MOVE_OVERHEAD) {
                    value = MIN_MOVE_OVERHEAD;
                } else if (value > MAX_MOVE_OVERHEAD) {
                    value = MAX_MOVE_OVERHEAD;
                }
                tc_set_move_overhead(value);
            }
        } else if (MATCH(namestr, "LogLevel")) {
            if (sscanf(valuestr, "%d", &value) == 1) {
                if (value > LOG_HIGHEST_LEVEL) {
                    value = LOG_HIGHEST_LEVEL;
                } else if (value < 0) {
                    value = 0;
                }
                dbg_set_log_level(value);
            }
        } else if (MATCH(namestr, "MultiPV")) {
            if (sscanf(valuestr, "%d", &value) == 1) {
                if (value > MAX_MULTIPV_LINES) {
                    value = MAX_MULTIPV_LINES;
                } else if (value < 1) {
                    value = 1;
                }
                state->multipv = value;
            }
        } else if (MATCH(namestr, "UseNNUE")) {
            if (MATCH(valuestr, "false")) {
                engine_using_nnue = false;
            } else if (MATCH(valuestr, "true")) {
                engine_using_nnue = true;
            }
        } else if (MATCH(namestr, "EvalFile")) {
            strncpy(engine_eval_file, valuestr, MAX_PATH_LENGTH);
            engine_loaded_net = nnue_load_net(engine_eval_file);
            if (!engine_loaded_net) {
                strcpy(engine_eval_file, NETFILE_NAME);
                engine_loaded_net = nnue_load_net(NULL);
            }
            engine_using_nnue = engine_loaded_net;
        }
        iter = strstr(iter, "name");
    }
}

static void uci_cmd_uci(struct gamestate *state)
{
    engine_protocol = PROTOCOL_UCI;
    engine_variant = VARIANT_STANDARD;

    tablebase_mode = TB_LARGEST > 0;

    state->silent = false;

    engine_write_command("id name %s %s", APP_NAME, APP_VERSION);
    engine_write_command("id author %s", APP_AUTHOR);
    engine_write_command("option name Hash type spin default %d min %d max %d",
                         engine_default_hash_size, MIN_MAIN_HASH_SIZE,
						 hash_tt_max_size());
    engine_write_command("option name OwnBook type check default true");
    engine_write_command("option name Ponder type check default false");
    engine_write_command("option name UCI_Chess960 type check default false");
    engine_write_command("option name SyzygyPath type string default %s",
                         engine_syzygy_path[0] != '\0'?
                                                engine_syzygy_path:"");
    engine_write_command(
                        "option name Threads type spin default %d min 1 max %d",
                        engine_default_num_threads, MAX_WORKERS);
    engine_write_command(
                        "option name MultiPV type spin default 1 min 1 max %d",
                        MAX_MULTIPV_LINES);
    engine_write_command(
                 "option name MoveOverhead type spin default %d min %d max %d",
                 DEFAULT_MOVE_OVERHEAD, MIN_MOVE_OVERHEAD, MAX_MOVE_OVERHEAD);
    engine_write_command(
                       "option name LogLevel type spin default %d min 0 max %d",
                        dbg_get_log_level(), LOG_HIGHEST_LEVEL);
    engine_write_command("option name UseNNUE type check default %s",
                         engine_using_nnue && engine_loaded_net?"true":"false");
    engine_write_command("option name EvalFile type string default ");
    engine_write_command("uciok");
}

static void uci_cmd_ucinewgame(void)
{
    hash_tt_clear_table();
    smp_newgame();
}

bool uci_handle_command(struct gamestate *state, char *cmd, bool *stop)
{
    assert(cmd != NULL);
    assert(stop != NULL);

    *stop = false;

    if (MATCH(cmd, "debug")) {
        /* Ignore */
    } else if (MATCH(cmd, "go")) {
        /* Both UCI and Xboard protocol has a go command */
        if (engine_protocol == PROTOCOL_UCI) {
            uci_cmd_go(cmd, state);
        } else {
            return false;
        }
    } else if (MATCH(cmd, "isready")) {
        uci_cmd_isready();
    } else if (MATCH(cmd, "position")) {
        uci_cmd_position(cmd, state);
    } else if (MATCH(cmd, "setoption")) {
        uci_cmd_setoption(cmd, state);
	} else if (MATCH(cmd, "stop")) {
		/* Ignore */
    } else if (MATCH(cmd, "uci") && (strlen(cmd) == 3)) {
        uci_cmd_uci(state);
    } else if (MATCH(cmd, "ucinewgame") && (strlen(cmd) == 10)) {
        uci_cmd_ucinewgame();
    } else if (MATCH(cmd, "quit")) {
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
    if (MATCH(cmd, "isready")) {
        uci_cmd_isready();
    } else if(MATCH(cmd, "ponderhit")) {
        tc_allocate_time();
        worker->state->pondering = false;
    } else if (MATCH(cmd, "stop")) {
        worker->state->pondering = false;
        stop = true;
    }

    return stop;
}

void uci_send_pv_info(struct gamestate *state, struct pvinfo *pvinfo)
{
    char     movestr[MAX_MOVESTR_LENGTH];
    char     buffer[1024];
    int      msec;
    int      nps;
    int      k;
    uint64_t tbhits;
    uint64_t nodes;
    int      score;

    /* Get information about the search */
    msec = (int)tc_elapsed_time();
    nodes = smp_nodes();
    nps = (msec > 0)?(nodes/msec)*1000:0;
    tbhits = state->root_in_tb?1:smp_tbhits();

    /* Adjust score in case the root position was found in tablebases */
    score = pvinfo->score;
    if (state->root_in_tb) {
        score = ((score > FORCED_MATE) || (score < (-FORCED_MATE)))?
                                                    score:state->root_tb_score;
    }

    /* Build command */
    sprintf(buffer, "info depth %d seldepth %d nodes %"PRIu64" time %d nps %d "
            "tbhits %"PRIu64" hashfull %d score cp %d pv",
            pvinfo->depth, pvinfo->seldepth,
            nodes, msec, nps, tbhits, hash_tt_usage(), score);
    for (k=0;k<pvinfo->pv.size;k++) {
        strcat(buffer, " ");
        move2str(pvinfo->pv.moves[k], movestr);
        strcat(buffer, movestr);
    }

    /* Write command */
    engine_write_command(buffer);
}

void uci_send_bound_info(struct search_worker *worker, int score, bool lower)
{
    char     buffer[1024];
    int      msec;
    int      nps;
    uint64_t tbhits;
    uint64_t nodes;

    /* Get information about the search */
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
            "tbhits %"PRIu64" hashfull %d score cp %d %s",
            worker->depth, worker->seldepth,
            nodes, msec, nps, tbhits, hash_tt_usage(),
            score, lower?"lowerbound":"upperbound");

    /* Write command */
    engine_write_command(buffer);
}

void uci_send_move_info(struct search_worker *worker, int movenumber,
                        uint32_t move)
{
    char movestr[MAX_MOVESTR_LENGTH];
    int  msec;

    /* Get the currently searched time */
    msec = (int)tc_elapsed_time();
    if (msec < 3000) {
        /* Wait some time before starting to send move info to avoid traffic */
        return;
    }

    /* Send command */
    move2str(move, movestr);
    engine_write_command("info depth %d currmove %s currmovenumber %d",
                         worker->depth, movestr, movenumber);
}

void uci_send_multipv_info(struct search_worker *worker)
{
    char          movestr[MAX_MOVESTR_LENGTH];
    char          buffer[1024];
    int           msec;
    int           nps;
    uint64_t      tbhits;
    uint64_t      nodes;
    int           ttusage;
    int           k;
    int           l;
    int           idx;
    int           score;
    struct pvinfo pv;

    /* Get information common for all lines */
    msec = (int)tc_elapsed_time();
    nodes = smp_nodes();
    nps = (msec > 0)?(nodes/msec)*1000:0;
    tbhits = worker->state->root_in_tb?1:smp_tbhits();
    ttusage = hash_tt_usage();

    /* Sort pv lines based on score */
    memcpy(sorted_mpv_lines, worker->mpv_lines, sizeof(sorted_mpv_lines));
    for (k=0;k<worker->multipv-1;k++) {
        score = sorted_mpv_lines[k].score;
        idx = k;
        for (l=k+1;l<worker->multipv;l++) {
            if (sorted_mpv_lines[k].score > score) {
                idx = l;
                score = sorted_mpv_lines[k].score;
            }
        }
        if (idx != k) {
            pv = sorted_mpv_lines[k];
            sorted_mpv_lines[k] = sorted_mpv_lines[idx];
            sorted_mpv_lines[k] = pv;
        }
    }

    /* Write one info command for each pv line */
    for (k=0;k<worker->multipv;k++) {
        if (sorted_mpv_lines[k].depth == 0) {
            continue;
        }
        sprintf(buffer, "info multipv %d depth %d seldepth %d nodes %"PRIu64" "
                "time %d nps %d tbhits %"PRIu64" hashfull %d score cp %d pv",
                k+1, sorted_mpv_lines[k].depth, sorted_mpv_lines[k].seldepth,
                nodes, msec, nps, tbhits, ttusage, sorted_mpv_lines[k].score);
        for (l=0;l<sorted_mpv_lines[k].pv.size;l++) {
            strcat(buffer, " ");
            move2str(sorted_mpv_lines[k].pv.moves[l], movestr);
            strcat(buffer, movestr);
        }

        engine_write_command(buffer);
    }
}
