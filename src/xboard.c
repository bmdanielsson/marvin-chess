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
#include <inttypes.h>

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
#include "smp.h"

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
static bool ponder_mode = false;
static bool tablebase_mode = false;

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
static bool infinite_time = true;
static bool fixed_time = false;

/* Pondering variables */
static uint32_t pondering_on = NOMOVE;

/* Forward declarations */
static void xboard_cmd_bk(struct gamestate *pos);
static void xboard_cmd_new(struct gamestate *pos);
static void xboard_cmd_setboard(char *cmd, struct gamestate *pos);
static void xboard_cmd_undo(struct gamestate *pos);
static void xboard_cmd_usermove(char *cmd, struct gamestate *pos,
                                bool engine_move);

static void update_moves_to_time_control(struct gamestate *state)
{
    int moves_in_tc;

    if (infinite_time || fixed_time || (moves_per_time_control == 0)) {
        return;
    }

    moves_in_tc = state->pos.fullmove%moves_per_time_control;
    if (state->pos.fullmove == 1) {
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

static void write_result(struct gamestate *state, enum game_result result)
{
	switch (result) {
	case RESULT_CHECKMATE:
		if (state->pos.stm == WHITE) {
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

static bool is_three_fold_repetition(struct position *pos)
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
static enum game_result is_game_over(struct position *pos)
{
    struct movelist list;

	/* Check for checkmate and stalemate */
    gen_legal_moves(pos, &list);
    if (list.size == 0) {
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

static void make_engine_move(struct gamestate *state)
{
    uint32_t         best_move;
    char             best_movestr[MAX_MOVESTR_LENGTH];
    uint32_t         ponder_move;
    enum game_result result;
    bool             ponder;
    int              flags;

    /* Start the clock */
    if (!tc_is_clock_running()) {
        tc_start_clock();
    }

    /* Set default search parameters */
    state->exit_on_mate = true;
    state->silent = false;
    ponder = false;
    pondering_on = NOMOVE;
    flags = 0;
    if (infinite_time) {
        flags = TC_INFINITE_TIME;
    } else if (fixed_time) {
        flags = TC_FIXED_TIME|TC_TIME_LIMIT;
    } else if ((engine_time_left > 0) || (engine_time_increment > 0)) {
        flags = TC_TIME_LIMIT;
    }
    if (moves_to_time_control > 0) {
        flags |= TC_REGULAR;
    }
    if (search_depth_limit < MAX_SEARCH_DEPTH) {
        flags |= TC_DEPTH_LIMIT;
    }

    while (true) {
        /* Set time control */
        state->sd = search_depth_limit;
        update_moves_to_time_control(state);
        tc_configure_time_control(engine_time_left, engine_time_increment,
                                  moves_to_time_control, flags);

        /* Search the position to find the best move */
        best_move = smp_search(state, ponder_mode && ponder, true,
                               tablebase_mode, &ponder_move);

        /*
         * If the search finishes while the engine is pondering
         * then it was pondering on the wrong move. Exit the loop
         * in order to handle the user move and restart the search.
         */
        if (pondering_on != NOMOVE) {
            board_unmake_move(&state->pos);
            break;
        }

        /* Make move */
        (void)board_make_move(&state->pos, best_move);

        /* Send move */
        move2str(best_move, best_movestr);
        engine_write_command("move %s", best_movestr);
		tc_stop_clock();

        /* Check if the game is over */
        result = is_game_over(&state->pos);
        if (result != RESULT_UNDETERMINED) {
            write_result(state, result);
            game_over = true;
            break;
        }

        /* Check if a ponder search should be started */
        if (ponder_mode && (ponder_move != NOMOVE)) {
            /*
             * Make the pondering move. If the move causes
             * the game to finish then cancel pondering.
            */
            (void)board_make_move(&state->pos, ponder_move);
            if (is_game_over(&state->pos) != RESULT_UNDETERMINED) {
                board_unmake_move(&state->pos);
                break;
            }

            ponder = true;
            pondering_on = ponder_move;
            tc_start_clock();
        } else {
            break;
        }
    }
}

static void xboard_cmd_analyze(struct gamestate *state)
{
    char *cmd;

    analyze_mode = true;
    tc_start_clock();

    while (true) {
        /* Set default search parameters */
        state->sd = MAX_SEARCH_DEPTH;
        state->silent = false;
        state->exit_on_mate = false;
        engine_clear_pending_command();
        tc_configure_time_control(0, 0, 0, TC_INFINITE_TIME);

        /* Search until told otherwise */
        (void)smp_search(state, false, false, tablebase_mode, NULL);

        /* Exit analyze mode if there is no pending command */
        cmd = engine_get_pending_command();
        if (cmd == NULL) {
            break;
        }

        /* Process command */
        if(MATCH(cmd, "bk")) {
            xboard_cmd_bk(state);
        } else if (MATCH(cmd, "new")) {
            xboard_cmd_new(state);
        } else if (MATCH(cmd, "setboard")) {
            xboard_cmd_setboard(cmd, state);
        } else if (MATCH(cmd, "undo")) {
            xboard_cmd_undo(state);
        } else if (MATCH(cmd, "usermove")) {
            xboard_cmd_usermove(cmd, state, false);
        }
    }

    tc_stop_clock();
    analyze_mode = false;
}

static void xboard_cmd_bk(struct gamestate *state)
{
    struct book_entry *entries;
    int               nentries;
    int               k;
    char              movestr[MAX_MOVESTR_LENGTH];
    int               sum;

    /* Find all book moves for this position */
    entries = polybook_get_entries(&state->pos, &nentries);
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

static void xboard_cmd_cores(char *cmd)
{
    int ncores;

    if (sscanf(cmd, "cores %d", &ncores) == 1) {
        if (ncores > MAX_WORKERS) {
            ncores = MAX_WORKERS;
        } else if (ncores < 1) {
            ncores = 1;
        }
        smp_destroy_workers();
        smp_create_workers(ncores);
    } else {
        engine_write_command("Error (malformed command): %s", cmd);
    }
}

static void xboard_cmd_easy(void)
{
    ponder_mode = false;
}

static void xboard_cmd_exit(void)
{
    analyze_mode = false;
}

static void xboard_cmd_egtpath(char *cmd)
{
    char *iter;

    if (tablebase_mode) {
        return;
    }

    iter = strstr(cmd, "syzygy");
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }
    iter += strlen("syzygy");
    iter = skip_whitespace(iter);

    strncpy(engine_syzygy_path, iter, MAX_PATH_LENGTH);
    tb_init(engine_syzygy_path);
    tablebase_mode = TB_LARGEST > 0;
}

static void xboard_cmd_force(void)
{
    force_mode = true;
}

static void xboard_cmd_go(struct gamestate *state)
{
    engine_side = state->pos.stm;
    force_mode = false;
    if (!game_over) {
        make_engine_move(state);
    }
}

static void xboard_cmd_hard(void)
{
    ponder_mode = true;
}

static void xboard_cmd_level(char *cmd)
{
	int   min;
	int   sec;
	float sec_f;
	char  *endptr;
	char  *iter;
    int   movestogo;
    int   increment;
    int   time_left;

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

    /* Set time control variables */
    infinite_time = false;
    fixed_time = false;
    moves_per_time_control = movestogo;
    moves_to_time_control = movestogo;
    engine_time_left = time_left;
    engine_time_increment = increment;
}

static void xboard_cmd_memory(char *cmd)
{
    int size;

    if (sscanf(cmd, "memory %d", &size) == 1) {
        if (size > hash_tt_max_size()) {
            size = hash_tt_max_size();
        } else if (size < MIN_MAIN_HASH_SIZE) {
            size = MIN_MAIN_HASH_SIZE;
        }
        hash_tt_create_table(size);
    } else {
        engine_write_command("Error (malformed command): %s", cmd);
    }
}

static void xboard_cmd_new(struct gamestate *state)
{
    board_start_position(&state->pos);
    hash_tt_clear_table();
    smp_newgame();

    search_depth_limit = MAX_SEARCH_DEPTH;
    engine_side = BLACK;
    analyze_mode = false;
    force_mode = false;
    game_over = false;

    state->exit_on_mate = true;
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

static void xboard_cmd_playother(struct gamestate *state)
{
    force_mode = false;
    engine_side = FLIP_COLOR(state->pos.stm);
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
	engine_write_command("feature variants=\"normal,fischerandom\"");
	engine_write_command("feature colors=0");
	engine_write_command("feature name=1");
	engine_write_command("feature nps=0");
	engine_write_command("feature memory=1");
	engine_write_command("feature smp=1");
	engine_write_command("feature egt=\"syzygy\"");
    engine_write_command("feature done=1");
}

static void xboard_cmd_remove(struct gamestate *state)
{
    if (state->pos.ply >= 2) {
        board_unmake_move(&state->pos);
        board_unmake_move(&state->pos);
    }

    game_over = is_game_over(&state->pos) != RESULT_UNDETERMINED;
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

static void xboard_cmd_setboard(char *cmd, struct gamestate *state)
{
    char *iter;

    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }

    if (!board_setup_from_fen(&state->pos, iter+1)) {
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
    infinite_time = false;
    fixed_time = true;
    moves_per_time_control = 0;
    moves_to_time_control = 0;
    engine_time_left = time*1000;
    engine_time_increment = 0;
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

static void xboard_cmd_undo(struct gamestate *state)
{
    if (force_mode || analyze_mode) {
        if (state->pos.ply >= 1) {
            board_unmake_move(&state->pos);
        }
    } else {
        engine_write_command("Error (command not legal now): undo");
        return;
    }

    game_over = is_game_over(&state->pos) != RESULT_UNDETERMINED;
}

static void xboard_cmd_usermove(char *cmd, struct gamestate *state,
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
    move = str2move(iter+1, &state->pos);
    if (move == NOMOVE) {
        engine_write_command("Illegal move: %s", cmd);
        return;
    }

    /* Make the move */
    if (!board_make_move(&state->pos, move)) {
        engine_write_command("Illegal move: %s", cmd);
        return;
    }

    /* Check if the game is over */
    result = is_game_over(&state->pos);
    if (result != RESULT_UNDETERMINED) {
        write_result(state, result);
        game_over = true;
		return;
    }

    /* Find a move to make and send it to the GUI */
    if (engine_move) {
        make_engine_move(state);
    }
}

static void xboard_cmd_variant(char *cmd)
{
    char *iter;
    char *variant;

    iter = strchr(cmd, ' ');
    if (iter == NULL) {
        engine_write_command("Error (malformed command): %s", cmd);
        return;
    }
    variant = iter+1;

    if (MATCH(variant, "normal")) {
        engine_variant = VARIANT_STANDARD;
    } else if (MATCH(variant, "fischerandom")) {
        engine_variant = VARIANT_FRC;
    } else {
        engine_write_command("Error (malformed command): %s", cmd);
    }
}

static void xboard_cmd_xboard(struct gamestate *state)
{
    engine_protocol = PROTOCOL_XBOARD;
    engine_variant = VARIANT_STANDARD;

    ponder_mode = false;
    tablebase_mode = TB_LARGEST > 0;
    analyze_mode = false;
    force_mode = false;
    post_mode = false;
    game_over = false;

    state->silent = false;
    state->move_filter.size = 0;
}

bool xboard_handle_command(struct gamestate *state, char *cmd, bool *stop)
{
    assert(cmd != NULL);
    assert(stop != NULL);

    *stop = false;

    if (MATCH(cmd, "?")) {
        /* Ignore */
    } else if (MATCH(cmd, "accepted")) {
        /* Ignore */
    } else if (MATCH(cmd, "analyze")) {
        xboard_cmd_analyze(state);
    } else if (MATCH(cmd, "bk")) {
        xboard_cmd_bk(state);
    } else if (MATCH(cmd, "computer")) {
        /* Ignore */
    } else if (MATCH(cmd, "cores")) {
        xboard_cmd_cores(cmd);
    } else if (MATCH(cmd, "easy")) {
        xboard_cmd_easy();
    } else if (MATCH(cmd, "exit")) {
        xboard_cmd_exit();
    } else if (MATCH(cmd, "egtpath")) {
        xboard_cmd_egtpath(cmd);
    } else if (MATCH(cmd, "force")) {
        xboard_cmd_force();
    } else if (MATCH(cmd, "go")) {
        xboard_cmd_go(state);
    } else if (MATCH(cmd, "hard")) {
        xboard_cmd_hard();
    } else if (MATCH(cmd, "level")) {
        xboard_cmd_level(cmd);
    } else if (MATCH(cmd, "memory")) {
        xboard_cmd_memory(cmd);
    } else if (MATCH(cmd, "name")) {
        /* Ignore */
    } else if (MATCH(cmd, "new")) {
        xboard_cmd_new(state);
    } else if (MATCH(cmd, "nopost")) {
        xboard_cmd_nopost();
    } else if (MATCH(cmd, "otim")) {
        /* Ignore */
    } else if (MATCH(cmd, "ping")) {
        xboard_cmd_ping(cmd);
    } else if (MATCH(cmd, "playother")) {
        xboard_cmd_playother(state);
    } else if (MATCH(cmd, "post")) {
        xboard_cmd_post();
    } else if (MATCH(cmd, "protover")) {
        xboard_cmd_protover();
    } else if (MATCH(cmd, "quit")) {
        *stop = true;
    } else if (MATCH(cmd, "rating")) {
        /* Ignore */
    } else if (MATCH(cmd, "random")) {
        /* Ignore */
    } else if (MATCH(cmd, "rejected")) {
        /* Ignore */
    } else if (MATCH(cmd, "remove")) {
        xboard_cmd_remove(state);
    } else if (MATCH(cmd, "result")) {
        /* Ignore */
    } else if (MATCH(cmd, "sd")) {
        xboard_cmd_sd(cmd);
    } else if (MATCH(cmd, "setboard")) {
        xboard_cmd_setboard(cmd, state);
    } else if (MATCH(cmd, "st")) {
        xboard_cmd_st(cmd);
    } else if (MATCH(cmd, "time")) {
        xboard_cmd_time(cmd);
    } else if (MATCH(cmd, "undo")) {
        xboard_cmd_undo(state);
    } else if (MATCH(cmd, "usermove")) {
        xboard_cmd_usermove(cmd, state, !force_mode);
    } else if (MATCH(cmd, "variant")) {
        xboard_cmd_variant(cmd);
    } else if (MATCH(cmd, "xboard")) {
        xboard_cmd_xboard(state);
    } else {
        if (engine_protocol == PROTOCOL_XBOARD) {
            engine_write_command("Error (unknown command): %s", cmd);
        }
        return false;
    }

    return true;
}

bool xboard_check_input(struct search_worker *worker)
{
    char *cmd;
    bool stop = false;
    char movestr[MAX_MOVESTR_LENGTH];
    char *iter;

    /* Read command */
    cmd = engine_read_command();
    if (cmd == NULL) {
        /* The GUI exited unexpectedly */
        return false;
    }

    /* Process command */
    if(MATCH(cmd, "cores")) {
        engine_set_pending_command(cmd);
        if (worker->state->pondering) {
            stop = true;
        }
    } else if (MATCH(cmd, "?") ||
               MATCH(cmd, "exit")) {
        stop = true;
    } else if (MATCH(cmd, "easy")) {
        xboard_cmd_easy();
    } else if (MATCH(cmd, "hard")) {
        xboard_cmd_hard();
    } else if (MATCH(cmd, "nopost")) {
        xboard_cmd_nopost();
    } else if (MATCH(cmd, "otim")) {
        /* Ignore */
    } else if (MATCH(cmd, "ping")) {
        xboard_cmd_ping(cmd);
    } else if (MATCH(cmd, "post")) {
        xboard_cmd_post();
    } else if (MATCH(cmd, "time")) {
        xboard_cmd_time(cmd);
        if (worker->state->pondering) {
            tc_update_time(engine_time_left);
        }
    } else if (MATCH(cmd, "usermove")) {
        if (!worker->state->pondering) {
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
                pondering_on = NOMOVE;
            } else {
                engine_set_pending_command(cmd);
                stop = true;
                tc_start_clock();
            }
            tc_allocate_time();
            worker->state->pondering = false;
        }
    } else if (MATCH(cmd, "bk") ||
               MATCH(cmd, "force") ||
               MATCH(cmd, "new") ||
               MATCH(cmd, "quit") ||
               MATCH(cmd, "setboard") ||
               MATCH(cmd, "undo")) {
        engine_set_pending_command(cmd);
        stop = true;
    }

    return stop;
}

void xboard_send_pv_info(struct gamestate *state, struct pvinfo *pvinfo)
{
    char            buffer[1024];
    int             k;
    char            movestr[MAX_MOVESTR_LENGTH];
    uint32_t        msec;
    struct movelist *pv;
    int             score;

    /* Only display thinking in post mode */
    if (!post_mode) {
        return;
    }

    /* Adjust score in case the root position was found in tablebases */
    score = pvinfo->score;
    if (state->root_in_tb) {
        score = ((score > FORCED_MATE) || (score < (-FORCED_MATE)))?
                                                    score:state->root_tb_score;
    }

    /* Display thinking according to the current output mode */
    msec = tc_elapsed_time();
    sprintf(buffer, "%3d %6d %7d %9"PRIu64"", pvinfo->depth,
            score, msec/10, smp_nodes());
    pv = &pvinfo->pv;
    for (k=0;k<pv->size;k++) {
        strcat(buffer, " ");
        move2str(pv->moves[k], movestr);
        strcat(buffer, movestr);
    }
    engine_write_command(buffer);
}
