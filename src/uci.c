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

static void uci_cmd_go(char *cmd, struct gamestate *pos)
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
    uint32_t best_move;
    uint32_t ponder_move = NOMOVE;

    /* Prepare for the search */
    search_reset_data(pos);

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
                pos->sd = MAX_SEARCH_DEPTH;
            } else {
                pos->sd = depth;
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
            move = str2move(iter, pos);
            if (move != NOMOVE) {
                if (board_make_move(pos, move)) {
                    board_unmake_move(pos);
                    pos->root_moves.moves[pos->root_moves.nmoves] = move;
                    pos->root_moves.nmoves++;
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
        tc_configure_time_control(pos, TC_INFINITE, 0, 0, 0);
        pos->exit_on_mate = false;
        pos->in_book = false;
    } else if (movetime > 0) {
        tc_configure_time_control(pos, TC_FIXED_TIME, movetime, 0, 0);
    } else if ((winc > 0) && (pos->stm == WHITE)) {
        tc_configure_time_control(pos, TC_FISCHER, wtime, winc, 0);
    } else if ((binc > 0) && (pos->stm == BLACK)) {
        tc_configure_time_control(pos, TC_FISCHER, btime, binc, 0);
    } else if ((movestogo > 0) && (pos->stm == WHITE)) {
        tc_configure_time_control(pos, TC_TOURNAMENT, wtime, 0, movestogo);
    } else if ((movestogo > 0) && (pos->stm == BLACK)) {
        tc_configure_time_control(pos, TC_TOURNAMENT, btime, 0, movestogo);
    } else if ((pos->stm == WHITE) && (wtime > 0)) {
        tc_configure_time_control(pos, TC_SUDDEN_DEATH, wtime, 0, 0);
    } else if ((pos->stm == BLACK) && (btime > 0)) {
        tc_configure_time_control(pos, TC_SUDDEN_DEATH, btime, 0, 0);
    } else {
        tc_configure_time_control(pos, TC_INFINITE, 0, 0, 0);
        pos->exit_on_mate = false;
        pos->in_book = false;
    }

    /* Search the position for a move */
    best_move = search_find_best_move(pos, ponder && pos->ponder, &ponder_move);

    /* Send the best move */
    move2str(best_move, best_movestr);
    if (pos->ponder && (ponder_move != NOMOVE)) {
        move2str(ponder_move, ponder_movestr);
        engine_write_command("bestmove %s ponder %s", best_movestr,
                             ponder_movestr);
    } else {
        engine_write_command("bestmove %s", best_movestr);
    }
}

static void uci_cmd_isready(void)
{
    engine_write_command("readyok");
}

static void uci_cmd_position(char *cmd, struct gamestate *pos)
{
    char     *iter;
    char     *moves;
    char     *movestr;
    uint32_t move;

    /* Find the first parameter */
    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        /* Invalid command, set start position and return */
        board_start_position(pos);
        return;
    }
    iter = skip_whitespace(iter);

    /* Find the beinning of the moves section if there is one */
    moves = strstr(cmd, "moves");

    /* Check if the parameter is fen or startpos */
    if (!strncmp(iter, "startpos", 8)) {
        board_start_position(pos);
    } else if (!strncmp(iter, "fen", 3)) {
        /* Find beginning of FEN string */
        iter += strlen("fen");
        iter = skip_whitespace(iter);

        /* Find end of FEN string and temporarily add a '\0' */
        if (moves != NULL) {
            *(moves-1) = '\0';
        }

        /* Setup the position */
        if (!board_setup_from_fen(pos, iter)) {
            /* Failed to setup position */
            board_start_position(pos);
            return;
        }

        /* Remove the '\0' added before */
        if (moves != NULL) {
            *(moves-1) = ' ';
        }
    } else {
        /* Invalid command, set start position and return */
        board_start_position(pos);
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
            move = str2move(movestr, pos);
            if (!board_make_move(pos, move)) {
                /* Illegal move */
                return;
            }
            iter = strchr(movestr, ' ');
        }
    }
}

static void uci_cmd_setoption(char *cmd, struct gamestate *pos)
{
    char *iter;
    int size;

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
            if (sscanf(iter, "value %d", &size) == 1) {
                if (size > MAX_MAIN_HASH_SIZE) {
                    size = MAX_MAIN_HASH_SIZE;
                } else if (size < MIN_MAIN_HASH_SIZE) {
                    size = MIN_MAIN_HASH_SIZE;
                }
                hash_tt_create_table(pos, size);
            }
        } else if (!strncmp(iter, "OwnBook", 7)) {
            iter = strstr(iter, "value");
            iter += strlen("value");
            iter = skip_whitespace(iter);
            if (!strncmp(iter, "false", 5)) {
                pos->use_own_book = false;
                pos->in_book = false;
            } else if (!strncmp(iter, "true", 4)) {
                pos->use_own_book = true;
                pos->in_book = true;
            }
        } else if (!strncmp(iter, "Ponder", 6)) {
            iter = strstr(iter, "value");
            iter += strlen("value");
            iter = skip_whitespace(iter);
            if (!strncmp(iter, "false", 5)) {
                pos->ponder = false;
            } else if (!strncmp(iter, "true", 4)) {
                pos->ponder = true;
            }
        }

        iter = strstr(iter, "name");
    }
}

static void uci_cmd_uci(struct gamestate *pos)
{
    pos->protocol = PROTOCOL_UCI;
    pos->silent = false;
    pos->use_own_book = true;
    pos->in_book = true;
    pos->ponder = false;

    engine_write_command("id name %s %s", APP_NAME, APP_VERSION);
    engine_write_command("id author %s", APP_AUTHOR);
    engine_write_command("option name Hash type spin default %d min %d max %d",
                         pos->default_hash_size, MIN_MAIN_HASH_SIZE,
                         MAX_MAIN_HASH_SIZE);
    engine_write_command("option name OwnBook type check default true");
    engine_write_command("option name Ponder type check default false");
    engine_write_command("uciok");
}

static void uci_cmd_ucinewgame(struct gamestate *pos)
{
    hash_tt_clear_table(pos);
    hash_pawntt_clear_table(pos);
    if (pos->use_own_book) {
        pos->in_book = true;
    }
}

bool uci_handle_command(struct gamestate *pos, char *cmd, bool *stop)
{
    assert(valid_board(pos));
    assert(cmd != NULL);
    assert(stop != NULL);

    *stop = false;

    if (!strncmp(cmd, "debug", 5)) {
        /* Ignore */
    } else if (!strncmp(cmd, "go", 2)) {
        /* Both UCI and Xboard protocol has a go command */
        if (pos->protocol == PROTOCOL_UCI) {
            uci_cmd_go(cmd, pos);
        } else {
            return false;
        }
    } else if (!strncmp(cmd, "isready", 7)) {
        uci_cmd_isready();
    } else if (!strncmp(cmd, "position", 8)) {
        uci_cmd_position(cmd, pos);
    } else if (!strncmp(cmd, "setoption", 9)) {
        uci_cmd_setoption(cmd, pos);
	} else if (!strncmp(cmd, "stop", 4)) {
		/* Ignore */
    } else if (!strncmp(cmd, "uci", 3) && (strlen(cmd) == 3)) {
        uci_cmd_uci(pos);
    } else if (!strncmp(cmd, "ucinewgame", 10) && (strlen(cmd) == 10)) {
        uci_cmd_ucinewgame(pos);
    } else if (!strncmp(cmd, "quit", 4)) {
        /* Both UCI and Xboard protocol has a quit command */
        if (pos->protocol == PROTOCOL_UCI) {
            *stop = true;
        } else {
            return false;
        }
    } else {
        return false;
    }

    return true;
}

bool uci_check_input(bool *ponderhit)
{
    char *cmd;
    bool stop = false;

    assert(ponderhit != NULL);

    *ponderhit = false;

    /* Read command */
    cmd = engine_read_command();
    if (cmd == NULL) {
        /* The GUI exited unexpectidely */
        return false;
    }

    /* Process command */
    if (!strncmp(cmd, "isready", 7)) {
        uci_cmd_isready();
    } else if(!strncmp(cmd, "ponderhit", 9)) {
        *ponderhit = true;
    } else if (!strncmp(cmd, "stop", 4)) {
        stop = true;
    }

    return stop;
}

void uci_send_pv_info(struct gamestate *pos, int score)
{
    char   movestr[6];
    char   buffer[1024];
    time_t now;
    int    msec;
    int    nps;
    int    k;

    assert(valid_board(pos));

    /* Get the currently searched time */
    now = get_current_time();
    msec = (int)(now - pos->search_start);
    nps = (msec > 0)?(pos->nodes/msec)*1000:0;

    /* Build command */
    sprintf(buffer, "info depth %d seldepth %d nodes %d time %d nps %d "
            "score cp %d pv",
            pos->depth, pos->seldepth, pos->nodes, msec, nps, score);
    for (k=0;k<pos->pv_table[0].length;k++) {
        strcat(buffer, " ");
        move2str(pos->pv_table[0].moves[k], movestr);
        strcat(buffer, movestr);
    }

    /* Write command */
    engine_write_command(buffer);
}

void uci_send_move_info(struct gamestate *pos)
{
    char   movestr[6];
    time_t now;
    int    msec;
    int    size;

    assert(valid_board(pos));

    /* Get the currently searched time */
    now = get_current_time();
    msec = (int)(now - pos->search_start);
    if (msec < 1000) {
        /* Wait 1s before starting to send info to avoid traffic */
        return;
    }

    /* Send command */
    size = pos->tt_size*TT_BUCKET_SIZE;
    move2str(pos->currmove, movestr);
    engine_write_command("info depth %d seldepth %d nodes %d time %d nps %d "
                         "currmove %s currmovenumber %d hashfull %d",
                         pos->depth, pos->seldepth,  pos->nodes, msec,
                         (pos->nodes/msec)*1000, movestr, pos->currmovenumber,
                         (int)(((float)pos->tt_used/(float)(size))*100*10));
}
