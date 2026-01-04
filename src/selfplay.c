/*
 * Marvin - an UCI/XBoard compatible chess engin
 * Copyright (C) 2023 Martin Danielsson
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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>

#include "selfplay.h"
#include "position.h"
#include "bitboard.h"
#include "key.h"
#include "data.h"
#include "hash.h"
#include "smp.h"
#include "search.h"
#include "timectl.h"
#include "engine.h"
#include "validation.h"
#include "movegen.h"
#include "fen.h"

#define BATCH_SIZE 10000
#define EVAL_LIMIT 10000

#define RANDOM_PLIES 16
#define MIN_DRAW_PLY 80
#define MAX_GAME_PLY 400
#define DRAW_SCORE 10
#define DRAW_COUNT 10
#define EVAL_LIMIT 10000

#define SFEN_BIN_SIZE 40

struct packed_sfen {
    uint8_t  position[32];
    int16_t  stm_score;
    uint16_t move;
    uint16_t ply;
    int8_t   stm_result;
    uint8_t  padding;
};

/* Table containing Huffman encoding of each piece type */
static struct {
    uint8_t code;       /* Code for the piece */
    uint8_t nbits;      /* The number of bits in the code */
    uint8_t piece_type; /* The type of piece */
} sfen_huffman_table[] = {
    {0b0000, 1, NO_PIECE}, /* No piece */
    {0b0001, 4, PAWN},     /* Pawn */
    {0b0011, 4, KNIGHT},   /* Knight */
    {0b0101, 4, BISHOP},   /* Bishop */
    {0b0111, 4, ROOK},     /* Rook */
    {0b1001, 4, QUEEN},    /* Queen */
};

enum output_format {
    SFEN
};

struct position_data {
    uint8_t  board[NSQUARES];
    uint8_t  black_king_sq;
    uint8_t  white_king_sq;
    uint8_t  ep_sq;
    int      castle;
    int      fifty;
    int      fullmove;
    uint8_t  stm;
    uint32_t move;
    int16_t  stm_score;
    uint16_t ply;
    int8_t   stm_result;
};

static uint8_t sfen_encode_bit(uint8_t *buf, uint8_t cursor, uint8_t value)
{
    if (value != 0) {
        buf[cursor/8] |= (1 << (cursor&0x7));
    }

    return cursor + 1;
}

static uint8_t sfen_encode_bits(uint8_t *buf, uint8_t cursor, uint8_t value,
                                uint8_t nbits)
{
    uint8_t k;

    for (k=0;k<nbits;k++) {
        cursor = sfen_encode_bit(buf, cursor, value&(1 << k));
    }

    return cursor;
}

static uint8_t sfen_encode_piece(uint8_t *buf, uint8_t cursor, int piece)
{
    int value = VALUE(piece);
    int color = COLOR(piece);

    if (piece == NO_PIECE) {
        cursor = sfen_encode_bits(buf, cursor, sfen_huffman_table[0].code,
                                  sfen_huffman_table[0].nbits);
    } else {
        cursor = sfen_encode_bits(buf, cursor,
                                  sfen_huffman_table[value/2+1].code,
                                  sfen_huffman_table[value/2+1].nbits);
        cursor = sfen_encode_bit(buf, cursor, color);
    }

    return cursor;
}

static void sfen_encode_position(struct position_data *data, uint8_t *buf)
{
    uint8_t cursor = 0;
    int     file;
    int     rank;
    int     piece;

    /* Encode side to move */
    cursor = sfen_encode_bit(buf, cursor, data->stm);

    /* Encode king positions */
    cursor = sfen_encode_bits(buf, cursor, data->white_king_sq, 6);
    cursor = sfen_encode_bits(buf, cursor, data->black_king_sq, 6);

    /* Encode piece positions */
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        for (file=FILE_A;file<=FILE_H;file++) {
            piece = data->board[SQUARE(file, rank)];
            if ((piece == WHITE_KING) || (piece == BLACK_KING)) {
                continue;
            }
            cursor = sfen_encode_piece(buf, cursor, piece);
        }
    }

    /* Encode castling availability */
    cursor = sfen_encode_bit(buf, cursor, (data->castle&WHITE_KINGSIDE) != 0);
    cursor = sfen_encode_bit(buf, cursor, (data->castle&WHITE_QUEENSIDE) != 0);
    cursor = sfen_encode_bit(buf, cursor, (data->castle&BLACK_KINGSIDE) != 0);
    cursor = sfen_encode_bit(buf, cursor, (data->castle&BLACK_QUEENSIDE) != 0);

    /* Encode en-passant square */
    if  (data->ep_sq == NO_SQUARE) {
        cursor = sfen_encode_bit(buf, cursor, 0);
    } else {
        cursor = sfen_encode_bit(buf, cursor, 1);
        cursor = sfen_encode_bits(buf, cursor, data->ep_sq, 6);
    }

    /*
     * Encode fifty-move counter. To keep compatibility with Stockfish
     * only 6 bits are stored now. The last bit is stored at the end.
     */
    cursor = sfen_encode_bits(buf, cursor, data->fifty, 6);

    /* Encode move counter */
    cursor = sfen_encode_bits(buf, cursor, data->fullmove, 8);
    cursor = sfen_encode_bits(buf, cursor, data->fullmove >> 8, 8);

    /* Encode upper bit of the fifty-move counter */
    cursor = sfen_encode_bit(buf, cursor, (data->fifty >> 6) & 1);
}

static uint16_t sfen_encode_move(uint32_t move)
{
    uint16_t data = 0;
    int      to = TO(move);
    int      from = FROM(move);

    data |= to;
    data |= (from << 6);
    if (ISPROMOTION(move)) {
        data |= ((VALUE(PROMOTION(move))/2 - 1) << 12);
        data |= (1 << 14);
    } else if (ISENPASSANT(move)) {
        data |= (2 << 14);
    } else if (ISKINGSIDECASTLE(move) || ISQUEENSIDECASTLE(move)) {
        data |= (3 << 14);
    }

    return data;
}


static void write_sfen_data(FILE *fp, struct position_data *data)
{
    struct packed_sfen sfen;

    memset(&sfen, 0, sizeof(struct packed_sfen));
    sfen_encode_position(data, sfen.position);
    sfen.stm_score = data->stm_score;
    sfen.move = sfen_encode_move(data->move);
    sfen.ply = data->ply;
    sfen.stm_result = data->stm_result;
    sfen.padding = 0xFF;

    fwrite(&sfen, SFEN_BIN_SIZE, 1, fp);
}

static void write_position_data(FILE *fp, struct position_data *batch, int npos,
                                enum output_format format)
{
    int k;

    for (k=0;k<npos;k++) {
        switch (format) {
        case SFEN:
            write_sfen_data(fp, &batch[k]);
            break;
        default:
            assert(false);
        }
    }

    fflush(fp);
}

static void fill_position_data(struct position *pos, struct position_data *data,
                               uint32_t move, int stm_score)
{
    memcpy(data->board, pos->pieces, NSQUARES);
    data->white_king_sq = LSB(pos->bb_pieces[WHITE_KING]);
    data->black_king_sq = LSB(pos->bb_pieces[BLACK_KING]);
    data->ep_sq = pos->ep_sq;
    data->castle = pos->castle;
    data->fifty = pos->fifty;
    data->fullmove = pos->fullmove;
    data->stm = pos->stm;
    data->move = move;
    data->stm_score = stm_score;
    data->ply = pos->ply;
    data->stm_result = (pos->stm == WHITE)?1:-1;
}

static void play_random_moves(struct position *pos, int nmoves)
{
    int             k;
    int             index;
    struct movelist list;

    for (k=0;k<nmoves;k++) {
        gen_legal_moves(pos, &list);
        index = rand()%list.size;
        pos_make_move(pos, list.moves[index]);
        if (pos_get_game_result(pos) != RESULT_UNDETERMINED) {
            break;
        }
    }
}

static void setup_start_position(struct position *pos, float frc_prob)
{
    float prob;
    int   id;

    prob = (float)rand()/(float)RAND_MAX;
    if (prob < frc_prob) {
        id = rand()%960;
        if (pos_setup_from_fen(pos, fen_get_frc_start_position(id))) {
            pos->key = key_update_castling(pos->key, pos->castle, 0);
            pos->castle = 0;
            return;
        }
    }
            
    pos_setup_start_position(pos);
}

static int play_game(FILE *fp, struct engine *engine, int pos_left,
                     float frc_prob, enum output_format format)
{
    struct position      *pos = &engine->pos;
    struct position_data batch[MAX_GAME_PLY];
    int                  npos = 0;
    uint32_t             move;
    int                  stm_score;
    int                  white_score;
    int                  white_result = 0;
    int                  draw_count = 0;
    int                  k;

    /* Prepare for a new game */
    memset(batch, 0, sizeof(struct position_data)*MAX_GAME_PLY);
    smp_newgame();

    /* Setup start position and play some random opening moves */
    setup_start_position(&engine->pos, frc_prob);
    play_random_moves(&engine->pos, RANDOM_PLIES);
    if (pos_get_game_result(pos) != RESULT_UNDETERMINED) {
        return 0;
    }

    /* Play game */
    while (pos_get_game_result(pos) == RESULT_UNDETERMINED) {
        /* Search the position */
        move = search_position(engine, false, NULL, &stm_score);

        /* Skip non-quiet moves */
        if (ISTACTICAL(move) ||
            pos_in_check(pos, pos->stm) ||
            pos_move_gives_check(pos, move)) {
            pos_make_move(pos, move);
            continue;
        }

        /* Check if the score exceeds the eval limit */
        if (abs(stm_score) >= EVAL_LIMIT) {
            white_score = (pos->stm == WHITE)?stm_score:-stm_score;
            white_result = (white_score > 0)?1:-1;
            break;
        }

        /*
         * Store position data and the result of the search. The game
         * result is filled in later.
         */
        fill_position_data(pos, &batch[npos++], move, stm_score);

        /* Check ply limit */
        if (pos->ply >= MAX_GAME_PLY) {
            white_result = 0;
            break;
        }

        /* Draw adjudication */
        if (pos->ply > MIN_DRAW_PLY) {
            if (abs(stm_score) <= DRAW_SCORE) {
                draw_count++;
            } else {
                draw_count = 0;
            }
            if (draw_count >= DRAW_COUNT) {
                white_result = 0;
                break;
            }
        }

        /* Play move */
        pos_make_move(pos, move);
    }

    /* Set game result */
    switch (pos_get_game_result(pos)) {
    case RESULT_CHECKMATE:
        white_result = (pos->stm == BLACK)?1:-1;
        break;
    case RESULT_STALEMATE:
    case RESULT_DRAW_BY_RULE:
        white_result = 0;
        break;
    case RESULT_UNDETERMINED:
        break;
    }
    for (k=0;k<npos;k++) {
        batch[k].stm_result *= white_result;
    }

    /* Write positions to file */
    if (npos > pos_left) {
        npos = pos_left;
    }
    write_position_data(fp, batch, npos, format);

    return npos;
}

static int play_games(char *output, int depth, int npositions, double frc_prob,
                      enum output_format format)
{
    FILE          *outfp = NULL;
    struct engine *engine = NULL;
    int           ngenerated;

    /* Open output file */
    outfp = fopen(output, "ab");
    if (outfp == NULL) {
        printf("Error: failed to open output file, %s\n", output);
        return 1;
    }

    /* Setup engine */
    hash_tt_destroy_table();
    hash_tt_create_table(DEFAULT_MAIN_HASH_SIZE);
    smp_destroy_workers();
    smp_create_workers(1);
    tc_configure_time_control(0, 0, 0, TC_INFINITE_TIME);
    engine = engine_create();
    engine->sd = depth;
    engine->move_filter.size = 0;
    engine->exit_on_mate = true;

    /* Play games to generate moves */
    ngenerated = 0;
    while (ngenerated < npositions) {
        /* Play game */
        ngenerated += play_game(outfp, engine, npositions-ngenerated, frc_prob,
                                format);
        
        /* Clear the transposition table */
        hash_tt_clear_table();
    }

    /* Close the output file */
    fclose(outfp);

    /* Destroy the engine*/
    engine_destroy(engine);

    return 0;
}

static void selfplay_usage(void)
{
    printf("marvin --selfplay <options>\n");
    printf("Options:\n");
    printf("\t--output (-o) <file>\n");
    printf("\t--depth (-d) <file>\n");
    printf("\t--npositions (-n) <int>\n");
    printf("\t--seed (-s) <int>\n");
    printf("\t--frc-prob (-f) <float>\n");
    printf("\t--format (-r) [sfen]\n");
    printf("\t--help (-h) <int>\n");
}

int selfplay_run(int argc, char *argv[])
{
    int                iter;
    char               *output_file = NULL;
    int                depth = 8;
    int64_t            npositions = -1;
    int                seed = time(NULL);
    double             frc_prob = 0.0;
    enum output_format format = SFEN;

    /* Parse command line options */
    iter = 2;
    while (iter < argc) {
        if ((MATCH(argv[iter], "-o") || MATCH(argv[iter], "--output")) &&
                   ((iter+1) < argc)) {
            iter++;
            output_file = argv[iter];
        } else if ((MATCH(argv[iter], "-d") ||
                    MATCH(argv[iter], "--depth")) &&
                   ((iter+1) < argc)) {
            iter++;
            depth = atoi(argv[iter]);
        } else if ((MATCH(argv[iter], "-n") ||
                    MATCH(argv[iter], "--npositions")) &&
                   ((iter+1) < argc)) {
            iter++;
            npositions = atoi(argv[iter]);
        } else if ((MATCH(argv[iter], "-s") ||
                    MATCH(argv[iter], "--seed")) &&
                   ((iter+1) < argc)) {
            iter++;
            seed = atoi(argv[iter]);
        } else if ((MATCH(argv[iter], "-f") ||
                    MATCH(argv[iter], "--frc-prob")) &&
                   ((iter+1) < argc)) {
            iter++;
            frc_prob = atof(argv[iter]);
        } else if ((MATCH(argv[iter], "-r") ||
                    MATCH(argv[iter], "--format")) &&
                   ((iter+1) < argc)) {
            iter++;
            if (MATCH(argv[iter], "sfen")) {
                format = SFEN;
            } else {
                printf("Error: unknown output format, %s\n", argv[iter]);
                selfplay_usage();
                return 1;
            }
        } else if (MATCH(argv[iter], "-h") || MATCH(argv[iter], "--help")) {
            selfplay_usage();
            return 0;
        } else {
            printf("Error: unknown argument, %s\n", argv[iter]);
            selfplay_usage();
            return 1;
        }

        iter++;
    }

    /* Validate options */
    if (!output_file ||
        (depth <= 0) || (depth >= MAX_SEARCH_DEPTH) ||
        (npositions <= 0) || (frc_prob < 0.0) || (frc_prob >= 1.0)) {
        printf("Error: invalid options\n");
        selfplay_usage();
        return 1;
    }

    /* Initialize random number generator */
    srand(seed);

    return play_games(output_file, depth, npositions, frc_prob, format);
}
