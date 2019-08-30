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
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "debug.h"
#include "bitboard.h"
#include "fen.h"
#include "utils.h"
#include "config.h"
#include "hash.h"
#include "movegen.h"
#include "board.h"
#include "thread.h"

/* Pointer to log file. */
static FILE *logfp = NULL;

/* The log level */
static int log_level = 0;

/* Lock for synchronizing log file writes */
static mutex_t log_lock;

static bool browse_display_move(struct position *pos, uint32_t move, int id,
                                bool pv, int current_score)
{
    struct tt_item item;
    char           movestr[6];

    move2str(move, movestr);

    if (pv) {
        printf("==> %3d. %5s", id+1, movestr);
    } else {
        printf("    %3d. %5s", id+1, movestr);
    }

    board_make_move(pos, move);

    if (!hash_tt_lookup(pos, &item)) {
        board_unmake_move(pos);
        if (pv) {
            printf("    %-5d\n", current_score);
        } else {
            printf("    ---\n");
        }
        return false;
    }

    switch (item.type) {
    case TT_EXACT:
        printf("     PV");
        break;
    case TT_BETA:
        printf("    Cut");
        break;
    case TT_ALPHA:
        printf("    All");
        break;
    default:
        break;
    }

    printf("    %5d   %5d\n", -1*item.score, item.depth);

    board_unmake_move(pos);

    return true;
}

static void browse_display_position(struct position *pos,
                                    struct movelist *list, bool *leafnodes)
{
    struct tt_item item;
    int            k;

    printf("\n");

    list->size = 0;
    if (!hash_tt_lookup(pos, &item)) {
        printf("Position not found\n");
        return;
    }

    gen_legal_moves(pos, list);

    dbg_print_board(pos);
    printf("\n");
    printf("Node: ");
    switch (item.type) {
    case TT_EXACT:
        printf("PV");
        break;
    case TT_BETA:
        printf("Cut");
        break;
    case TT_ALPHA:
        printf("All");
        break;
    default:
        break;
    }
    printf(", Score: %d, Depth: %d, Key: %"PRIu64"\n",
           item.score, item.depth, pos->key);
    printf("\n");

    for (k=0;k<list->size;k++) {
        leafnodes[k] = browse_display_move(pos, list->moves[k], k,
                                           list->moves[k]==item.move,
                                           item.score);
    }
    printf("\n");
}

void dbg_set_log_level(int level)
{
    char path[256];
    char *home;
    time_t t;

    assert(logfp == NULL);

    /* Set the log level */
    log_level = level;

    /* Check if a logfile has already been created */
    if (logfp != NULL) {
        return;
    }

    /* Don't create a log file if the log level is 0 */
    if (log_level == 0) {
        return;
    }

    /* Initialize lock */
    mutex_init(&log_lock);

    /*
     * Open the log file. First try to open the it in the current
     * working directory. If that fails use the users home directory.
     */
    logfp = fopen(LOGFILE_NAME, "a");
    if (logfp == NULL) {
        home = getenv("HOME");
        if (home != NULL) {
            snprintf(path, sizeof(path), "%s/%s", home, LOGFILE_NAME);
            logfp = fopen(path, "a");
        }
    }
    if (logfp != NULL) {
        setbuf(logfp, NULL);
    }

    /* Write a timestamp to the log */
    t = time(NULL);
    fprintf(logfp, "\n%s\n", ctime(&t));
}

int dbg_get_log_level(void)
{
    return log_level;
}

void dbg_log_close(void)
{
    if (logfp != NULL) {
        fclose(logfp);
        logfp = NULL;
        mutex_destroy(&log_lock);
    }
}

void dbg_log_info(int level, char *fmt, ...)
{
    va_list ap;

    assert(fmt != NULL);

    if ((logfp == NULL) || (level > log_level)) {
        return;
    }

    mutex_lock(&log_lock);

    va_start(ap, fmt);
    vfprintf(logfp, fmt, ap);
    va_end(ap);

    mutex_unlock(&log_lock);
}

void dbg_print_board(struct position *pos)
{
    char fenstr[FEN_MAX_LENGTH];
    int  rank;
    int  file;
    int  sq;
    int  piece;

    fen_build_string(pos, fenstr);
    printf("%s\n\n", fenstr);

    for (rank=RANK_8;rank>=RANK_1;rank--) {
        printf("%d  ", rank+1);
        for (file=FILE_A;file<=FILE_H;file++) {
            sq = SQUARE(file, rank);
            piece = pos->pieces[sq];
            printf("%3c", piece2char[piece]);
        }
        printf("\n");
    }
    printf("\n");
    printf("   ");
    for (file=FILE_A;file<=FILE_H;file++) {
        printf("%3c", 'a'+file);
    }
    printf("\n");
}

void dbg_print_bitboard(uint64_t bb)
{
    int rank;
    int file;
    int sq;

    for (rank=RANK_8;rank>=RANK_1;rank--) {
        printf("%d  ", rank+1);
        for (file=FILE_A;file<=FILE_H;file++) {
            sq = SQUARE(file, rank);
            printf("%3c", ISBITSET(bb, sq)?'x':'.');
        }
        printf("\n");
    }
    printf("\n");
    printf("   ");
    for (file=FILE_A;file<=FILE_H;file++) {
        printf("%3c", 'a'+file);
    }
    printf("\n");
}

void dbg_print_move(uint32_t move)
{
    char movestr[6];

    move2str(move, movestr);
    printf("%s\n", movestr);
}

void dbg_print_movelist(struct movelist *list)
{
    int  k;
    char movestr[6];

    for (k=0;k<list->size;k++) {
        if ((k != 0) && (k%10) == 0) {
            printf("\n");
        }
        move2str(list->moves[k], movestr);
        printf("%s ", movestr);
    }
    printf("\n");
}

void dbg_browse_transposition_table(struct position *pos)
{
    bool            stop;
    int             depth;
    char            buffer[10];
    int             id;
    struct movelist list;
    bool            leafnodes[MAX_MOVES];

    depth = 0;
    stop = false;
    while (!stop) {
        browse_display_position(pos, &list, leafnodes);

        if (list.size > 0) {
            printf("Move: 1-%d, q=quit, u=up\n", list.size);
        } else {
            printf("q=quit, u=up\n");
        }
        printf("Enter command: ");

        if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
            return;
        }

        if (!strncmp(buffer, "q", 1)) {
            stop = true;
        } else if (!strncmp(buffer, "u", 1)) {
            if (depth > 0) {
                depth--;
                board_unmake_move(pos);
            }
        } else {
            if ((sscanf(buffer, "%d", &id) == 1) && (id <= list.size) &&
                leafnodes[id-1]) {
                depth++;
                board_make_move(pos, list.moves[id-1]);
            }
        }
    }
}
