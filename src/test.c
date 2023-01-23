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

/* Depth to search the benchmark positions to */
#define BENCH_DEPTH 17

/* Benchmark positions */
static char *positions[] = {
    "r4rk1/pp3ppp/2npb3/2p5/P1B1Pb1q/2PPN3/1P3R1P/R1BQ2K1 w - - 0 1",
    "1rb2rk1/p1n1q1b1/1p3nNp/2p2p2/2P5/B1N1P1P1/P1Q2PBP/3RR1K1 b - - 0 1",
    "r1bqkn1Q/p3pp2/3p2p1/7p/4P2P/4BP1N/P1P3P1/R3KB1R b KQq - 0 1",
    "r1bqk2r/p1p1bnpp/5p2/1N1PpP2/7N/3BB3/PP3PPP/R2Q1RK1 b kq - 0 1",
    "r1b1kb1r/pp3ppp/2n1p1n1/4P3/2pp4/5NB1/P1q2PPP/RN2Q1K1 w kq - 0 1",
    "r1bq1rk1/p1bn1ppp/2p1pn2/1p3Q2/2BP4/P1N1PN2/1P1B1PPP/R4RK1 w - - 0 1",
    "r1b4Q/ppp1kp1p/6p1/3q2N1/1n1P3P/8/PP2PPP1/R3KB1R w KQ - 0 1",
    "rnb2b1r/1pk2ppp/p3pn2/2p5/N3P3/5P2/P1P3PP/1R2KBNR b K - 0 1",
    "3nk2r/rp1b2pp/pR3p2/3P4/5Q2/3B1N2/5PPP/5RK1 b k - 0 1",
    "2b1rbk1/1p1n1pp1/3B3p/6q1/2B1P3/2N2P1P/R2Q2P1/6K1 b - - 0 1",
    "5rk1/4n3/p6Q/1p1ppP2/1q4n1/1P3N1P/5P2/R5K1 w - - 0 1",
    "r2k4/3n1p2/2p2b1p/p2q4/P7/5NP1/1P1N1P2/2K1R3 b - - 0 1",
    "r7/p2nBkp1/bp4p1/2p5/1bp5/1P4P1/P3PP1P/qN3K1R w - - 0 1",
    "1r3N1k/q4R1p/6p1/2p3P1/p1B1p2Q/2P1B3/P1P4P/6K1 b - - 0 1",
    "q1b1k1nr/3p1ppp/p3p3/1p2P3/8/1P6/P1PQ1PPP/R3K2R w KQk - 0 1",
    "r2r2k1/1pqb1ppp/2n1p3/2b5/p7/P1PB2P1/3BQP1P/R1NR2K1 w - - 0 1",
    "r2qkb1r/4n1pp/R1pp1p2/1Q2p1B1/3PP3/1P3b1P/1PP2PP1/1N3RK1 w kq - 0 1",
    "q7/3r2pk/p3p2p/1p2Pp2/1n1B1P2/8/5P1P/1BQR2K1 w - - 0 1",
    "2k4r/pbpnq3/1p6/7r/2P1P3/P3Q1p1/1PB1KP2/R2R4 b - - 0 1",
    "4r3/2p3bk/pP5p/2P3p1/4p1b1/4B1P1/1P1RPP2/3R2K1 b - - 0 1",
    "rn1q1rk1/4ppbp/1p4p1/2p1N3/3Pb3/2P5/P3BPPP/2BQ1RK1 w - - 0 1",
    "1rr3k1/ppp2p1p/3pp1p1/2nPn1B1/2P1P3/2P2B1P/P3QPP1/1R3RK1 b - - 0 1",
    "r3k1r1/p4p2/np2N1n1/3pP2Q/q4P2/P1p5/2P3PP/R1B2RK1 w q - 0 1",
    "4rrk1/q6p/3p2p1/2p5/1pNbbn1B/1P6/3Q2PP/2R1R2K w - - 0 1",
    "3r4/4k3/8/5p1R/8/1b2PB2/1P6/4K3 b - - 0 1",
    "1R6/7p/4k1pB/p1Ppn3/3K3P/8/r7/8 w - - 0 1",
    "8/5kp1/p4n1p/3pK3/1B6/8/8/8 w - - 0 1",
    "8/8/1B3k2/4p2p/2p1K2P/8/8/8 b - - 0 1",
    "5Rb1/6P1/2n5/1p2k1K1/p7/P7/1P6/8 b - - 0 1",
    "8/8/7k/1R6/1p5r/4KP2/8/8 b - - 0 1",
    "7k/8/4r2p/6pP/3Rp1K1/4P1P1/8/8 b - - 0 1",
    "2K5/r6k/7p/4N3/5P2/8/8/8 b - - 0 1"
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
    for (k=0;k<list.size;k++) {
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
    char            movestr[MAX_MOVESTR_LENGTH];
    char            fenstr[FEN_MAX_LENGTH];

    assert(valid_position(pos));
    assert(depth > 0);

    fen_build_string(pos, fenstr);

    ntotal = 0;
    gen_moves(pos, &list);
    for (k=0;k<list.size;k++) {
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

    printf("Moves: %d\n", list.size);
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
    int              nworkers;
    int              tt_size;

    printf("%s %s (%s)\n", APP_NAME, APP_VERSION, APP_ARCH);
    if (engine_using_nnue) {
        printf("Using NNUE evaluation with %s\n", engine_eval_file);
    } else {
        printf("Using classic evaluation\n");
    }

    nworkers = smp_number_of_workers();
    tt_size = hash_tt_size();
    hash_tt_destroy_table();
    hash_tt_create_table(DEFAULT_MAIN_HASH_SIZE);
    smp_destroy_workers();
    smp_create_workers(1);

    state = create_game_state();
    nodes = 0ULL;
    total = 0;
    npos = sizeof(positions)/sizeof(char*);
    for (k=0;k<npos;k++) {
        board_setup_from_fen(&state->pos, positions[k]);
        tc_configure_time_control(0, 0, 0, TC_INFINITE_TIME);
        smp_newgame();
        state->sd = BENCH_DEPTH;
        state->silent = true;
        state->move_filter.size = 0;
        state->exit_on_mate = true;

        start = get_current_time();
        (void)smp_search(state, false, false, NULL);
        total += (get_current_time() - start);
        nodes += smp_nodes();

        printf("#");
    }
    printf("\n");

    printf("Total time: %.2fs\n", total/1000.0);
    printf("Total number of nodes: %"PRIu64"\n", nodes);
    printf("Speed: %.2fkN/s\n", ((double)nodes)/(total/1000.0)/1000);

    destroy_game_state(state);

    hash_tt_destroy_table();
    hash_tt_create_table(tt_size);
    smp_destroy_workers();
    smp_create_workers(nworkers);
}
