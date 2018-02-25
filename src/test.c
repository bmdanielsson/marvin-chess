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
#include <inttypes.h>

#include "test.h"
#include "config.h"
#include "utils.h"
#include "chess.h"
#include "board.h"
#include "search.h"
#include "hash.h"
#include "fen.h"
#include "movegen.h"
#include "validation.h"
#include "moveselect.h"
#include "engine.h"
#include "timectl.h"
#include "smp.h"

/* Positions used for benchmark */
struct benchmark_positions {
    /* The FEN string of the position */
    char *fen;
    /* The depth to search the position to */
    int depth;
};

/* Benchmark positions */
struct benchmark_positions positions[] = {
    {"3r1k2/4npp1/1ppr3p/p6P/P2PPPP1/1NR5/5K2/2R5 w - - 0 1", 17},
    {"rnbqkb1r/p3pppp/1p6/2ppP3/3N4/2P5/PPP1QPPP/R1B1KB1R w KQkq - 0 1", 15},
    {"4b3/p3kp2/6p1/3pP2p/2pP1P2/4K1P1/P3N2P/8 w - - 0 1", 20},
    {"r3r1k1/ppqb1ppp/8/4p1NQ/8/2P5/PP3PPP/R3R1K1 b - - 0 1", 16},
    {"2r2rk1/1bqnbpp1/1p1ppn1p/pP6/N1P1P3/P2B1N1P/1B2QPP1/R2R2K1 b - - 0 1", 16},
    {"r1bqk2r/pp2bppp/2p5/3pP3/P2Q1P2/2N1B3/1PP3PP/R4RK1 b kq - 0 1", 16}
};

static void perft(struct position *pos, int depth, uint32_t *nleafs)
{
    struct movelist list;
    int             k;

    /* Check if its time to stop */
    if (depth == 0) {
        (*nleafs)++;
        return;
    }

    /* Search all moves */
    gen_moves(pos, &list);
    for (k=0;k<list.nmoves;k++) {
        if (!board_make_move(pos, list.moves[k])) {
            continue;
        }
        perft(pos, depth-1, nleafs);
        board_unmake_move(pos);
    }
}

void test_run_perft(struct position *pos, int depth)
{
    uint32_t nleafs;
    char     fenstr[FEN_MAX_LENGTH];

    assert(valid_position(pos));
    assert(depth > 0);

    fen_build_string(pos, fenstr);

    nleafs = 0;
    perft(pos, depth, &nleafs);
    printf("Nodes: %u\n", nleafs);
}

void test_run_divide(struct position *pos, int depth)
{
    struct movelist list;
    uint32_t        nleafs;
    uint32_t        ntotal;
    int             k;
    char            movestr[6];
    char            fenstr[FEN_MAX_LENGTH];

    assert(valid_position(pos));
    assert(depth > 0);

    fen_build_string(pos, fenstr);

    ntotal = 0;
    gen_moves(pos, &list);
    for (k=0;k<list.nmoves;k++) {
        if (!board_make_move(pos, list.moves[k])) {
            continue;
        }
        nleafs = 0;
        perft(pos, depth-1, &nleafs);
        ntotal += nleafs;
        move2str(list.moves[k], movestr);
        printf("%s %u\n", movestr, nleafs);
        board_unmake_move(pos);
    }

    printf("Moves: %d\n", list.nmoves);
    printf("Leafs: %u\n", ntotal);
}

void test_run_benchmark(void)
{
    struct gamestate *state;
    int              k;
    int              npos;
    uint64_t         nodes;
    time_t           start;
    time_t           total;

    hash_tt_destroy_table();
    hash_tt_create_table(DEFAULT_MAIN_HASH_SIZE);

    state = create_game_state();
    nodes = 0ULL;
    start = get_current_time();
    npos = sizeof(positions)/sizeof(struct benchmark_positions);
    for (k=0;k<npos;k++) {
        board_setup_from_fen(&state->pos, positions[k].fen);
        search_reset_data(state);
        tc_configure_time_control(TC_INFINITE, 0, 0, 0);
        state->sd = positions[k].depth;
        state->silent = true;

        smp_search(state, false, false, false);
        nodes += smp_nodes();

        printf("#");
    }
    printf("\n");

    total = get_current_time() - start;

    printf("Total time: %.2fs\n", total/1000.0);
    printf("Total number of nodes: %"PRIu64"\n", nodes);
    printf("Speed: %.2fkN/s\n", ((double)nodes)/(total/1000.0)/1000);

    destroy_game_state(state);
}
