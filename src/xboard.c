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

#include "xboard.h"
#include "config.h"
#include "utils.h"
#include "chess.h"
#include "board.h"
#include "search.h"
#include "hash.h"
#include "fen.h"
#include "validation.h"
#include "engine.h"
#include "movegen.h"
#include "eval.h"
#include "timectl.h"
#include "polybook.h"
#include "debug.h"
#include "tbprobe.h"

/* Possible game results */
enum game_result {
    RESULT_UNDETERMINED,
    RESULT_CHECKMATE,
    RESULT_STALEMATE,
    RESULT_DRAW_BY_RULE
};

/* Different Xboard modes */
static bool analyze_mode = false;
static bool force_mode = false;
static bool post_mode = false;

/* The side that the engine is playing */
static int engine_side = BLACK;

/* Flag indicating if the game is over */
static bool game_over = false;

/* Time control variables */
static int moves_per_time_control = 0;
static int moves_to_time_control = 0;
static int engine_time_left = 0;
static int engine_time_increment = 0;
static int search_depth_limit = MAX_SEARCH_DEPTH;
enum timectl_type time_control_type = TC_INFINITE;

/* Pondering variables */
static uint32_t pondering_on = NOMOVE;

/* Forward declarations */
static void xboard_cmd_bk(struct gamestate *pos);
static void xboard_cmd_new(struct gamestate *pos);
static void xboard_cmd_setboard(char *cmd, struct gamestate *pos);
static void xboard_cmd_undo(struct gamestate *pos);
static void xboard_cmd_usermove(char *cmd, struct gamestate *pos,
                                bool engine_move);

static void update_moves_to_time_control(struct gamestate *pos)
{
    int moves_in_tc;

    if (pos->tc_type != TC_TOURNAMENT) {
        return;
    }

    moves_in_tc = pos->fullmove%moves_per_time_control;
    if (pos->fullmove == 1) {
        /*
         * This is the first move of the game so the number of
         * moves to go have already been set.
         */
    } else if (moves_in_tc == 1) {
        /*
         * This is the first move of a new time control so reset
         * the number of moves to go.
         */
        moves_to_time_control = moves_per_time_control;
    } else if (moves_in_tc == 0) {
        moves_to_time_control = 1;
    } else {
        moves_to_time_control = moves_per_time_control - moves_in_tc + 1;
    }
}

static void write_result(struct gamestate *pos, enum game_result result)
{
	switch (result) {
	case RESULT_CHECKMATE:
		if (pos->stm == WHITE) {
			engine_write_command("0-1 {Black mates}");
        } else {
			engine_write_command("1-0 {White mates}");
        }
		break;
	case RESULT_STALEMATE:
		engine_write_command("1/2-1/2 {Stalemate}");
		break;
	case RESULT_DRAW_BY_RULE:
		engine_write_command("1/2-1/2 {Draw by rule}");
		break;
	default:
		break;
	}
}

static void write_search_status(struct gamestate *pos)
{
	time_t   now;
	uint32_t msec;
	char     movestr[6];
    int      moves_left;

	now = get_current_time();
	msec = now - pos->search_start;
	move2str(pos->currmove, movestr);
    moves_left = pos->root_moves.nmoves - pos->currmovenumber;
	engine_write_command("stat01: %d %d %d %d %d %s\n", msec/10, pos->nodes,
		                 pos->depth, moves_left, pos->root_moves.nmoves,
                         movestr);
}

static bool is_three_fold_repetition(struct gamestate *pos)
{
    int idx;
    int nreps;

    /*
     * Pawn moves and captures are irreversible and so there is no need to
     * to check older positions for repetitions. Since the fifty counter
     * already keeps track of this to handle the fifty move rule this
     * counter can be used here as well.
     *
     * Also there is no need to consider position where the other side is to
     * move so only check every other position in the history.
     */
    nreps = 1;
    idx = pos->ply - 2;
    while ((idx >= 0) && (idx >= (pos->ply - pos->fifty)) && (nreps < 3)) {
        if (pos->history[idx].key == pos->key) {
            nreps++;
        }
        idx -= 2;
    }

    return nreps >= 3;
}

/*
 * Check if the last move resulted in a position that ends the game.
 * For each possible move, test if it result in a legal position.
 * If at least one move result in a legal position,
 * then the game is not over. If there is no legal move
 * and the player is in check then there is a checkmate,
 * otherwise it is a stalemate. If it is not checkmate or stalemate,
 * test for draw.
 */
static enum game_result is_game_over(struct gamestate *pos)
{
    struct movelist list;

	/* Check for checkmate and stalemate */
    gen_legal_moves(pos, &list);
    if (list.nmoves == 0) {
        return board_in_check(pos, pos->stm)?RESULT_CHECKMATE:RESULT_STALEMATE;
    }

	/* Check for draw by rul by rule */
    if ((pos->fifty > 100) ||
        is_three_fold_repetition(pos) ||
        eval_is_material_draw(pos)) {
		return RESULT_DRAW_BY_RULE;
    }

    return RESULT_UNDETERMINED;
}

static void make_engine_move(struct gamestate *pos)
{
    uint32_t         best_move;
    char             best_movestr[6];
    uint32_t         ponder_move;
    enum game_result result;
    bool             ponder;

    /* Prepare for search */
    search_reset_data(pos);
    ponder = false;
    pondering_on = NOMOVE;

    while (true) {
        /* Set time control */
        pos->sd = search_depth_limit;
        update_moves_to_time_control(pos);
        tc_configure_time_control(pos, time_control_type, engine_time_left,
                                  engine_time_increment, moves_to_time_control);

        /* Search the position for a move */
        best_move = search_find_best_move(pos, pos->ponder && ponder,
                                          &ponder_move);

        /*
         * If the search finishes while the engine is pondering
         * then it was pondering on the wrong move. Exit the loop
         * in order to handle the user move and restart the search.
         */
        if (pondering_on != NOMOVE) {
            board_unmake_move(pos);
            break;
        }

        /* Make move */
        (void)board_make_move(pos, best_move);

        /* Send move */
        move2str(best_move, best_movestr);
        engine_write_command("move %s", best_movestr);

        /* Check if the game is over */
        result = is_game_over(pos);
        if (result != RESULT_UNDETERMINED) {
            write_result(pos, result);
            game_over = true;
            break;
        }

        /* Check if a ponder search should be started */
        if (pos->ponder && (ponder_move != NOMOVE)) {
			/*
			 * Make the pondering move. If the move causes
			 * the game to finish then cancel pondering.
			 */
			(void)board_make_move(pos, ponder_move);
			if (is_game_over(pos) != RESULT_UNDETERMINED) {
				board_unmake_move(pos);
				break;
			}

			search_reset_data(pos);
            ponder = true;
            pondering_on = ponder_move;
        } else {
            break;
        }
    }
}

static void xboard_cmd_analyze(struct gamestate *pos)
{
    uint32_t ponder_move;
    char     *cmd;

    analyze_mode = true;

    while (true) {
        /* Prepare for search */
        search_reset_data(pos);
        pos->exit_on_mate = false;
        pos->in_book = false;
        engine_clear_pending_command();
        tc_configure_time_control(pos, TC_INFINITE, 0, 0, 0);

        /* Until told otherwise */
        (void)search_find_best_move(pos, false, &ponder_move);

        /* Exit analyze mode if there is no pending command */
        cmd = engine_get_pending_command();
        if (cmd == NULL) {
            break;
        }

        /* Process command */
        if(!strncmp(cmd, "bk", 2)) {
            xboard_cmd_bk(pos);
        } else if (!strncmp(cmd, "new", 3)) {
            xboard_cmd_new(pos);
        } else if (!strncmp(cmd, "setboard", 8)) {
            xboard_cmd_setboard(cmd, pos);
        } else if (!strncmp(cmd, "undo", 4)) {
            xboard_cmd_undo(pos);
        } else if (!strncmp(cmd, "usermove", 8)) {
            xboard_cmd_usermove(cmd, pos, false);
        }
    }

    analyze_mode = false;
}

static void xboard_cmd_bk(struct gamestate *pos)
{
    struct book_entry *entries;
    int               nentries;
    int               k;
    char              movestr[16];
    int               sum;

    /* Find all book moves for this position */
    entries = polybook_get_entries(pos, &nentries);
    if ((entries == NULL) || (nentries == 0)) {
        engine_write_command(" No book moves found");
        engine_write_command("");
        free(entries);
        return;
    }

    sum = 0;
    for (k=0;k<nentries;k++) {
        sum += entries[k].weight;
    }

    for (k=0;k<nentries;k++) {
        move2str(entries[k].move, movestr);
        engine_write_command(" %s %.0f%%", movestr,
                             ((float)entries[k].weight/(float)sum)*100.0f);
    }
    engine_write_command("");

    free(entries);
}

static void xboard_cmd_easy(struct gamestate *pos)
{
    pos->ponder = false;
}

static void xboard_cmd_exit(void)
{
    analyze_mode = false;
}

static void xboard_cmd_egtpath(char *cmd, struct gamestate *pos)
{
    char *iter;

    if (pos->use_tablebases) {
        return;
    }

    iter = strstr(cmd, "syzygy");
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }
    iter += strlen("syzygy");
    iter = skip_whitespace(iter);

    strncpy(syzygy_path, iter, sizeof(syzygy_path));
    if (tb_init(syzygy_path) && (TB_LARGEST > 0)) {
        pos->tb_men = TB_LARGEST;
        pos->use_tablebases = pos->tb_men > 0;
    }
}

static void xboard_cmd_force(void)
{
    force_mode = true;
}

static void xboard_cmd_go(struct gamestate *pos)
{
    engine_side = pos->stm;
    force_mode = false;
    if (!game_over) {
        make_engine_move(pos);
    }
}

static void xboard_cmd_hard(struct gamestate *pos)
{
    pos->ponder = true;
}

static void xboard_cmd_hint(struct gamestate *pos)
{
    uint32_t move;
    uint32_t ponder_move;
    char     movestr[6];

    /* Check the opening book */
    move = polybook_probe(pos);

    /*
     * If there is no move in the opening book try to
     * find a move to suggest through other means.
     */
    if (move == NOMOVE) {
        /* Check if there is a move in the priciple variation */
        if (pos->ponder_move != NOMOVE) {
            move = pos->ponder_move;
        } else {
		    /*
             * If all else fails do a shallow search to
             * find a resonably good move.
             */
            search_reset_data(pos);
            tc_configure_time_control(pos, TC_INFINITE, 0, 0, 0);
		    pos->sd = 6;
            pos->silent = true;
            move = search_find_best_move(pos, false, &ponder_move);
            pos->silent = false;
        }
	}

    /* Send hint */
    move2str(move, movestr);
    engine_write_command("Hint: %s", movestr);
}

static void xboard_cmd_level(char *cmd)
{
	int               min;
	int               sec;
	float             sec_f;
	char              *endptr;
	char              *iter;
    enum timectl_type tc_type;
    int               movestogo;
    int               increment;
    int               time_left;

	/* Extract MPS */
	min = 0;
	sec = 0;
	iter = strchr(cmd, ' ');
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }
    movestogo = strtol(iter+1, &endptr, 10);
	if (*endptr != ' ') {
        engine_write_command("Error (malformed command): %s", cmd);
		return;
    }

	/* Extract BASE */
	min = 0;
	sec = 0;
	iter = endptr + 1;
	min = strtol(iter, &endptr, 10);
	if ((*endptr != ' ') && (*endptr != ':')) {
        engine_write_command("Error (malformed command): %s", cmd);
		return;
    }
	if (*endptr == ':') {
		iter = endptr + 1;
		sec = strtol(iter, &endptr, 10);
		if (*endptr != ' ') {
            engine_write_command("Error (malformed command): %s", cmd);
			return;
        }
	}
	time_left = (sec + min*60)*1000;

	/* Extract INC */
	sec = 0;
	iter = endptr + 1;
    if (strchr(iter, '.') == NULL) {
	    sec = strtol(iter, &endptr, 10);
	    if (*endptr != '\0') {
            engine_write_command("Error (malformed command): %s", cmd);
		    return;
        }
	    increment = sec*1000;
    } else {
	    sec_f = strtof(iter, &endptr);
	    if (*endptr != '\0') {
            engine_write_command("Error (malformed command): %s", cmd);
		    return;
        }
	    increment = (int)(sec_f*1000.0f);
    }

	/* Figure out the clock style */
	if (movestogo != 0) {
		tc_type = TC_TOURNAMENT;
	} else if (increment != 0) {
		tc_type = TC_FISCHER;
		movestogo = 0;
	} else {
		tc_type = TC_SUDDEN_DEATH;
		movestogo = 0;
	}

    /* Set time control variables */
    moves_per_time_control = movestogo;
    moves_to_time_control = movestogo;
    engine_time_left = time_left;
    engine_time_increment = increment;
    time_control_type = tc_type;
}

static void xboard_cmd_memory(char *cmd, struct gamestate *pos)
{
    int size;

    if (sscanf(cmd, "memory %d", &size) == 1) {
        if (size > MAX_MAIN_HASH_SIZE) {
            size = MAX_MAIN_HASH_SIZE;
        } else if (size < MIN_MAIN_HASH_SIZE) {
            size = MIN_MAIN_HASH_SIZE;
        }
        hash_tt_create_table(pos, size);
    } else {
        engine_write_command("Error (malformed command): %s", cmd);
    }
}

static void xboard_cmd_new(struct gamestate *pos)
{
    reset_game_state(pos);
    search_depth_limit = MAX_SEARCH_DEPTH;
    engine_side = BLACK;
    analyze_mode = false;
    force_mode = false;
    game_over = false;
    pos->exit_on_mate = true;
    pos->in_book = true;
}

static void xboard_cmd_nopost(void)
{
    post_mode = false;
}

static void xboard_cmd_ping(char *cmd)
{
    int id;

    if (sscanf(cmd, "ping %d", &id) != 1) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }

    engine_write_command("pong %d", id);
}

static void xboard_cmd_playother(struct gamestate *pos)
{
    force_mode = false;
    engine_side = FLIP_COLOR(pos->stm);
}

static void xboard_cmd_post(void)
{
    post_mode = true;
}

static void xboard_cmd_protover(void)
{
	engine_write_command("feature ping=1");
	engine_write_command("feature setboard=1");
    engine_write_command("feature playother=1");
    engine_write_command("feature usermove=1");
	engine_write_command("feature draw=0");
	engine_write_command("feature sigint=0");
	engine_write_command("feature sigterm=0");
	engine_write_command("feature myname=\"%s %s\"", APP_NAME, APP_VERSION);
	engine_write_command("feature variants=\"normal\"");
	engine_write_command("feature colors=0");
	engine_write_command("feature name=0");
	engine_write_command("feature nps=0");
	engine_write_command("feature memory=1");
	engine_write_command("feature egt=\"syzygy\"");
    engine_write_command("feature done=1");
}

static void xboard_cmd_remove(struct gamestate *pos)
{
    if (pos->ply >= 2) {
        board_unmake_move(pos);
        board_unmake_move(pos);
    }

    game_over = is_game_over(pos) != RESULT_UNDETERMINED;
}

static void xboard_cmd_sd(char *cmd)
{
    int depth;

    if (sscanf(cmd, "sd %d", &depth) != 1) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }

    search_depth_limit = depth;
}

static void xboard_cmd_setboard(char *cmd, struct gamestate *pos)
{
    char *iter;

    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }

    if (!board_setup_from_fen(pos, iter+1)) {
        engine_write_command("tellusererror Illegal position");
    }
}

static void xboard_cmd_st(char *cmd)
{
    int time;

    if (sscanf(cmd, "st %d", &time) != 1) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }

    /* Set time control variables */
    moves_per_time_control = 0;
    moves_to_time_control = 0;
    engine_time_left = time*1000;
    engine_time_increment = 0;
    time_control_type = TC_FIXED_TIME;
}

static void xboard_cmd_time(char *cmd)
{
    int time;

    if (sscanf(cmd, "time %d", &time) != 1) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }

    engine_time_left = time*10;
}

static void xboard_cmd_undo(struct gamestate *pos)
{
    if (force_mode || analyze_mode) {
        if (pos->ply >= 1) {
            board_unmake_move(pos);
        }
    } else {
        engine_write_command("Error (command not legal now): undo");
        return;
    }

    game_over = is_game_over(pos) != RESULT_UNDETERMINED;
}

static void xboard_cmd_usermove(char *cmd, struct gamestate *pos,
                                bool engine_move)
{
    char             *iter;
    uint32_t         move;
    enum game_result result;

    /* Extract the move from the command */
    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }
    move = str2move(iter+1, pos);
    if (move == NOMOVE) {
        engine_write_command("Illegal move: %s", cmd);
        return;
    }

    /* Make the move */
    if (!board_make_move(pos, move)) {
        engine_write_command("Illegal move: %s", cmd);
        return;
    }

    /* Check if the game is over */
    result = is_game_over(pos);
    if (result != RESULT_UNDETERMINED) {
        write_result(pos, result);
        game_over = true;
		return;
    }

    /* Find a move to make and send it to the GUI */
    if (engine_move) {
        make_engine_move(pos);
    }
}

static void xboard_cmd_xboard(struct gamestate *pos)
{
    pos->protocol = PROTOCOL_XBOARD;
    analyze_mode = false;
    force_mode = false;
    post_mode = false;
    pos->silent = false;
    pos->use_own_book = true;
    pos->in_book = true;
    pos->ponder = false;
    game_over = false;
    pos->sd = MAX_SEARCH_DEPTH;
}

bool xboard_handle_command(struct gamestate *pos, char *cmd, bool *stop)
{
    assert(valid_board(pos));
    assert(cmd != NULL);
    assert(stop != NULL);

    *stop = false;

    if (!strncmp(cmd, "?", 1)) {
        /* Ignore */
    } else if (!strncmp(cmd, "accepted", 8)) {
        /* Ignore */
    } else if (!strncmp(cmd, "analyze", 7)) {
        xboard_cmd_analyze(pos);
    } else if (!strncmp(cmd, "bk", 2)) {
        xboard_cmd_bk(pos);
    } else if (!strncmp(cmd, "computer", 8)) {
        /* Ignore */
    } else if (!strncmp(cmd, "easy", 4)) {
        xboard_cmd_easy(pos);
    } else if (!strncmp(cmd, "exit", 4)) {
        xboard_cmd_exit();
    } else if (!strncmp(cmd, "egtpath", 7)) {
        xboard_cmd_egtpath(cmd, pos);
    } else if (!strncmp(cmd, "force", 5)) {
        xboard_cmd_force();
    } else if (!strncmp(cmd, "go", 2)) {
        xboard_cmd_go(pos);
    } else if (!strncmp(cmd, "hard", 4)) {
        xboard_cmd_hard(pos);
    } else if (!strncmp(cmd, "hint", 4)) {
        xboard_cmd_hint(pos);
    } else if (!strncmp(cmd, "level", 5)) {
        xboard_cmd_level(cmd);
    } else if (!strncmp(cmd, "memory", 6)) {
        xboard_cmd_memory(cmd, pos);
    } else if (!strncmp(cmd, "new", 3)) {
        xboard_cmd_new(pos);
    } else if (!strncmp(cmd, "nopost", 6)) {
        xboard_cmd_nopost();
    } else if (!strncmp(cmd, "otim", 4)) {
        /* Ignore */
    } else if (!strncmp(cmd, "ping", 4)) {
        xboard_cmd_ping(cmd);
    } else if (!strncmp(cmd, "playother", 9)) {
        xboard_cmd_playother(pos);
    } else if (!strncmp(cmd, "post", 4)) {
        xboard_cmd_post();
    } else if (!strncmp(cmd, "protover", 8)) {
        xboard_cmd_protover();
    } else if (!strncmp(cmd, "quit", 4)) {
        *stop = true;
    } else if (!strncmp(cmd, "random", 6)) {
        /* Ignore */
    } else if (!strncmp(cmd, "rejected", 8)) {
        /* Ignore */
    } else if (!strncmp(cmd, "remove", 6)) {
        xboard_cmd_remove(pos);
    } else if (!strncmp(cmd, "result", 5)) {
        /* Ignore */
    } else if (!strncmp(cmd, "sd", 2)) {
        xboard_cmd_sd(cmd);
    } else if (!strncmp(cmd, "setboard", 8)) {
        xboard_cmd_setboard(cmd, pos);
    } else if (!strncmp(cmd, "st", 2)) {
        xboard_cmd_st(cmd);
    } else if (!strncmp(cmd, "time", 4)) {
        xboard_cmd_time(cmd);
    } else if (!strncmp(cmd, "undo", 4)) {
        xboard_cmd_undo(pos);
    } else if (!strncmp(cmd, "usermove", 8)) {
        xboard_cmd_usermove(cmd, pos, !force_mode);
    } else if (!strncmp(cmd, "xboard", 5)) {
        xboard_cmd_xboard(pos);
    } else {
        if (pos->protocol == PROTOCOL_XBOARD) {
            engine_write_command("Error (unknown command): %s", cmd);
        }
        return false;
    }

    return true;
}

bool xboard_check_input(struct gamestate *pos, bool *ponderhit)
{
    char             *cmd;
    bool             stop = false;
    char             movestr[6];
    char             *iter;

    assert(valid_board(pos));
    assert(ponderhit != NULL);

    *ponderhit = false;

    /* Read command */
    cmd = engine_read_command();
    if (cmd == NULL) {
        /* The GUI exited unexpectidely */
        return false;
    }

    /* Process command */
    if (!strncmp(cmd, ".", 1)) {
        write_search_status(pos);
    } else if (!strncmp(cmd, "?", 1) ||
               !strncmp(cmd, "exit", 4)) {
        stop = true;
    } else if(!strncmp(cmd, "hint", 4)) {
        /*
         * This only makes sense if the engine is in analyze mode
         * so send the current best move as a hint.
         */
        move2str(pos->best_move, movestr);
        engine_write_command("Hint: %s", movestr);
    } else if (!strncmp(cmd, "easy", 4)) {
        xboard_cmd_easy(pos);
    } else if (!strncmp(cmd, "hard", 4)) {
        xboard_cmd_hard(pos);
    } else if (!strncmp(cmd, "nopost", 6)) {
        xboard_cmd_nopost();
    } else if (!strncmp(cmd, "otim", 4)) {
        /* Ignore */
    } else if (!strncmp(cmd, "ping", 4)) {
        xboard_cmd_ping(cmd);
    } else if (!strncmp(cmd, "post", 4)) {
        xboard_cmd_post();
    } else if (!strncmp(cmd, "time", 4)) {
        xboard_cmd_time(cmd);
        if (pos->pondering) {
            pos->time_left = engine_time_left;
        }
    } else if (!strncmp(cmd, "usermove", 8)) {
        if (!pos->pondering) {
            engine_set_pending_command(cmd);
            stop = true;
        } else {
            /*
             * Check if the move made is the same move that
             * the engine is pondering on.
             */
            iter = strchr(cmd, ' ');
            if (iter == NULL) {
                engine_write_command("Error (malformed command): %s", cmd);
                return false;
            }
            iter++;
            move2str(pondering_on, movestr);
            if (!strcmp(movestr, iter) && (strlen(movestr) == strlen(iter))) {
                *ponderhit = true;
                pondering_on = NOMOVE;
            } else {
                engine_set_pending_command(cmd);
                stop = true;
                pos->pondering = false;
            }
        }
    } else if (!strncmp(cmd, "bk", 2) ||
               !strncmp(cmd, "force", 5) ||
               !strncmp(cmd, "new", 3) ||
               !strncmp(cmd, "quit", 4) ||
               !strncmp(cmd, "setboard", 8) ||
               !strncmp(cmd, "undo", 4)) {
        engine_set_pending_command(cmd);
        stop = true;
    }

    return stop;
}

void xboard_send_pv_info(struct gamestate *pos, int score)
{
	char     buffer[1024];
	int      k;
	char     movestr[6];
	time_t   now;
	uint32_t msec;

    assert(valid_board(pos));

	/* Only display thinking in post mode */
	if (!post_mode) {
		return;
    }

    /* Adjust score in case the root position was found in tablebases */
    if (pos->root_in_tb) {
        score = ((score > FORCED_MATE) || (score < (-FORCED_MATE)))?
                                                    score:pos->root_tb_score;
    }

	/* Display thinking according to the current output mode */
	now = get_current_time();
	msec = now - pos->search_start;
	sprintf(buffer, "%3d %6d %7d %9d", pos->depth, score, msec/10, pos->nodes);
	for (k=0;k<pos->pv_table[0].length;k++) {
		strcat(buffer, " ");
        move2str(pos->pv_table[0].moves[k], movestr);
		strcat(buffer, movestr);
	}
	engine_write_command(buffer);
}
