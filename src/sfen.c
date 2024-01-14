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

#include "sfen.h" 
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
} huffman_table[] = {
    {0b0000, 1, NO_PIECE}, /* No piece */
    {0b0001, 4, PAWN},     /* Pawn */
    {0b0011, 4, KNIGHT},   /* Knight */
    {0b0101, 4, BISHOP},   /* Bishop */
    {0b0111, 4, ROOK},     /* Rook */
    {0b1001, 4, QUEEN},    /* Queen */
};

static uint8_t encode_bit(uint8_t *data, uint8_t cursor, uint8_t value)
{
    if (value != 0) {
        data[cursor/8] |= (1 << (cursor&0x7));
    }

    return cursor + 1;
}

static int read_bit(uint8_t *data, int *cursor)
{
    int b = (data[*cursor/8] >> (*cursor&7))&1;
    (*cursor)++;

    return b;
}

static uint8_t encode_bits(uint8_t *data, uint8_t cursor, uint8_t value,
                           uint8_t nbits)
{
    uint8_t k;

    for (k=0;k<nbits;k++) {
        cursor = encode_bit(data, cursor, value&(1 << k));
    }

    return cursor;
}

static int read_bits(uint8_t *data, int *cursor, int nbits)
{
    int k;
    int result = 0;

    for (k=0;k<nbits;k++) {
        result |= read_bit(data, cursor)?(1 << k):0;
    }

    return result;
}

static uint8_t encode_piece(uint8_t *data, uint8_t cursor, int piece)
{
    int value = VALUE(piece);
    int color = COLOR(piece);

    if (piece == NO_PIECE) {
        cursor = encode_bits(data, cursor, huffman_table[0].code,
                             huffman_table[0].nbits);
    } else {
        cursor = encode_bits(data, cursor, huffman_table[value/2+1].code,
                             huffman_table[value/2+1].nbits);
        cursor = encode_bit(data, cursor, color);
    }

    return cursor;
}

static int read_piece(uint8_t *data, int *cursor)
{
    int  color = WHITE;
    int  code = 0;
    int  nbits = 0;
    bool found = false;
    int  k;

    while (!found) {
        code |= read_bit(data, cursor) << nbits;
        nbits++;

        for (k=0;k<6;k++) {
            if ((huffman_table[k].code == code) &&
                (huffman_table[k].nbits == nbits)) {
                found = true;
                break;
            }
        }
    }

    if (huffman_table[k].piece_type == NO_PIECE) {
        return NO_PIECE;
    }

    color = read_bit(data, cursor);

    return color + huffman_table[k].piece_type;
}

static void encode_position(struct position *pos, uint8_t *data)
{
    uint8_t cursor = 0;
    int     file;
    int     rank;
    int     piece;

    /* Encode side to move */
    cursor = encode_bit(data, cursor, pos->stm);

    /* Encode king positions */
    cursor = encode_bits(data, cursor, LSB(pos->bb_pieces[WHITE_KING]), 6);
    cursor = encode_bits(data, cursor, LSB(pos->bb_pieces[BLACK_KING]), 6);

    /* Encode piece positions */
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        for (file=FILE_A;file<=FILE_H;file++) {
            piece = pos->pieces[SQUARE(file, rank)];
            if ((piece == WHITE_KING) || (piece == BLACK_KING)) {
                continue;
            }
            cursor = encode_piece(data, cursor, piece);
        }
    }

    /* Encode castling availability */
    cursor = encode_bit(data, cursor, (pos->castle&WHITE_KINGSIDE) != 0);
    cursor = encode_bit(data, cursor, (pos->castle&WHITE_QUEENSIDE) != 0);
    cursor = encode_bit(data, cursor, (pos->castle&BLACK_KINGSIDE) != 0);
    cursor = encode_bit(data, cursor, (pos->castle&BLACK_QUEENSIDE) != 0);

    /* Encode en-passant square */
    if  (pos->ep_sq == NO_SQUARE) {
        cursor = encode_bit(data, cursor, 0);
    } else {
        cursor = encode_bit(data, cursor, 1);
        cursor = encode_bits(data, cursor, pos->ep_sq, 6);
    }

    /*
     * Encode fifty-move counter. To keep compatibility with Stockfish
     * only 6 bits are stored now. The last bit is stored at the end.
     */
    cursor = encode_bits(data, cursor, pos->fifty, 6);

    /* Encode move counter */
    cursor = encode_bits(data, cursor, pos->fullmove, 8);
    cursor = encode_bits(data, cursor, pos->fullmove >> 8, 8);

    /* Encode upper bit of the fifty-move counter */
    cursor = encode_bit(data, cursor, (pos->fifty >> 6) & 1);
}

static void add_piece(struct position *pos, int piece, int sq)
{
    pos->pieces[sq] = piece;
    SETBIT(pos->bb_pieces[piece], sq);
    SETBIT(pos->bb_sides[COLOR(piece)], sq);
    SETBIT(pos->bb_all, sq);
    pos->key = key_set_piece(pos->key, piece, sq);
}

static void position_from_sfen(uint8_t *data, struct position *pos)
{
    int cursor = 0;
    int sq;
    int rank;
    int file;
    int piece;
    int fifty;
    int fullmove;

    /* Initialize position */
    pos_reset(pos);

    /* The side to move */
    pos->stm = read_bit(data, &cursor);
    pos->key = key_set_side(pos->key, pos->stm);

    /* King positions */
    sq = read_bits(data, &cursor, 6);
    add_piece(pos, WHITE_KING, sq);
    sq = read_bits(data, &cursor, 6);
    add_piece(pos, BLACK_KING, sq);

    /* Piece positions */
    for (rank=RANK_8;rank>=RANK_1;rank--) {
        for (file=FILE_A;file<=FILE_H;file++) {
            sq = SQUARE(file, rank);
            if (pos->pieces[sq] != NO_PIECE) {
                continue;
            }

            piece = read_piece(data, &cursor);
            if (piece != NO_PIECE) {
                add_piece(pos, piece, sq);
            }
        }
    }

    /* Castling */
    if (read_bit(data, &cursor) == 1) {
        pos->castle |= WHITE_KINGSIDE;
    }
    if (read_bit(data, &cursor) == 1) {
        pos->castle |= WHITE_QUEENSIDE;
    }
    if (read_bit(data, &cursor) == 1) {
        pos->castle |= BLACK_KINGSIDE;
    }
    if (read_bit(data, &cursor) == 1) {
        pos->castle |= BLACK_QUEENSIDE;
    }
    pos->key = key_set_castling(pos->key, pos->castle);

    /* En-passant square */
    if (read_bit(data, &cursor) == 1) {
        pos->ep_sq = read_bits(data, &cursor, 6);
        pos->key = key_set_ep_square(pos->key, pos->ep_sq);
    }

    /* 50-move counter, lower 6 bits */
    fifty = read_bits(data, &cursor, 6);

    /* Fullmove counter */
    fullmove = read_bits(data, &cursor, 8);
    fullmove |= (read_bits(data, &cursor, 8) << 8);
    pos->fullmove = fullmove;

    /* 50-move counter, upper 1 bit */
    fifty |= (read_bit(data, &cursor) << 6);
    pos->fifty = fifty;
}

static uint16_t encode_move(uint32_t move)
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
                     float frc_prob)
{
    struct position    *pos = &engine->pos;
    struct packed_sfen batch[MAX_GAME_PLY];
    int                npos = 0;
    uint32_t           move;
    int                stm_score;
    int                white_score;
    int                white_result = 0;
    int                draw_count = 0;
    int                k;

    /* Prepare for a new game */
    memset(batch, 0, sizeof(struct packed_sfen)*MAX_GAME_PLY);
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
         * Encode the position and the result of the search. The game
         * result is filled in later.
         */
        encode_position(pos, batch[npos].position);
        batch[npos].stm_score = stm_score;
        batch[npos].move = encode_move(move);
        batch[npos].ply = pos->ply;
        batch[npos].stm_result = (pos->stm == WHITE)?1:-1;
        batch[npos].padding = 0xFF;
        npos++;

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

    /* Write sfen positions to file */
    if (npos > pos_left) {
        npos = pos_left;
    }
    fwrite(batch, SFEN_BIN_SIZE, npos, fp);
    fflush(fp);

    return npos;
}

static int generate(char *output, int depth, int npositions, double frc_prob)
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
        ngenerated += play_game(outfp, engine, npositions-ngenerated, frc_prob);
        
        /* Clear the transposition table */
        hash_tt_clear_table();
    }

    /* Close the output file */
    fclose(outfp);

    /* Destroy the engine*/
    engine_destroy(engine);

    return 0;
}

static int rescore(char *input, char *output, int depth, int npositions,
                   int64_t offset)
{
    int                k;
    int                count;
    int                ret = 0;
    FILE               *infp = NULL;
    FILE               *outfp = NULL;
    uint64_t           size;
    uint32_t           nentries;
    int                nscored;
    int                batch_size;
    struct packed_sfen batch_data[BATCH_SIZE];
    struct engine      *engine = NULL;
    int                score;

    /* Open input and output files */
    infp = fopen(input, "rb");
    if (infp == NULL) {
        printf("Error: failed to open input file, %s\n", input);
        ret = 1;
        goto done;
    }
    outfp = fopen(output, "ab");
    if (outfp == NULL) {
        printf("Error: failed to open output file, %s\n", output);
        ret = 1;
        goto done;
    }

    /* Get the number of entries in the input file */
    size = get_file_size(input);
    if ((size%SFEN_BIN_SIZE) != 0ULL) {
        printf("Error: invalid input size %"PRIu64"\n", size);
        ret = 1;
        goto done;
    }
    nentries = size/SFEN_BIN_SIZE;
    if (npositions < 0) {
        npositions = nentries;
    }
    if ((uint32_t)offset >= nentries) {
        printf("Error: invalid offset %d %d\n", (int)offset, nentries);
        ret = 1;
        goto done;
    }
    if (((uint32_t)npositions+(uint32_t)offset) > nentries) {
        printf("Error: invalid number of positions\n");
        ret = 1;
        goto done;
    }

    /* Seek to the correct position in the input file */
    if (set_file_position(infp, offset*SFEN_BIN_SIZE, SEEK_SET) < 0) {
        printf("Error: seek failed\n");
        ret = 1;
        goto done;
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

    /* Rescore positions */
    nscored = 0;
    batch_size = BATCH_SIZE;
    while (nscored < npositions) {
        /* Read batch */
        if ((nscored+BATCH_SIZE) <= npositions) {
           batch_size = BATCH_SIZE;
        } else {
           batch_size = npositions - nscored;
        }
        count = (int)fread(batch_data, SFEN_BIN_SIZE, batch_size, infp);
        if (count != batch_size) {
            printf("Error: failed to read data\n");
            ret = 1;
            goto done;
        }

        /* Analyse all positions in the batch */
        for (k=0;k<batch_size;k++) {
            position_from_sfen(batch_data[k].position, &engine->pos);
            assert(valid_position(&engine->pos));
            assert(engine->pos.key == key_generate(&engine->pos));
            smp_newgame();
            (void)search_position(engine, false, NULL, &score);
            if ((score > -EVAL_LIMIT) && (score < EVAL_LIMIT)) {
                /*
                 * The training code doesn't care about the actual move so
                 * don't bother updating it.
                 */
                batch_data[k].stm_score = (int16_t)score;
            }
        }

        /* Write the batch to the output file */
        count = (int)fwrite(batch_data, SFEN_BIN_SIZE, batch_size, outfp);
        if (count != batch_size) {
            printf("Error: failed to write data\n");
            ret = 1;
            goto done;
        }
        nscored += batch_size;
        fflush(outfp);

        /* Clear the transposition table */
        hash_tt_clear_table();
    }

done:
    if (engine != NULL) {
        engine_destroy(engine);
    }
    if (infp) {
        fclose(infp);
    }
    if (outfp) {
        fclose(outfp);
    }

    return ret;
}

static void generate_usage(void)
{
    printf("marvin --generate <options>\n");
    printf("Options:\n");
    printf("\t--output (-o) <file>\n");
    printf("\t--depth (-d) <file>\n");
    printf("\t--npositions (-n) <int>\n");
    printf("\t--seed (-d) <int>\n");
    printf("\t--frc-prob (-f) <float>\n");
    printf("\t--help (-h) <int>\n");
}

static void rescore_usage(void)
{
    printf("marvin --rescore <options>\n");
    printf("Options:\n");
    printf("\t--input (-i) <file>\n");
    printf("\t--output (-o) <file>\n");
    printf("\t--depth (-d) <file>\n");
    printf("\t--npositions (-n) <int>\n");
    printf("\t--offset (-f) <int>\n");
    printf("\t--help (-h) <int>\n");
}

int sfen_generate(int argc, char *argv[])
{
    int     iter;
    char    *output_file = NULL;
    int     depth = 8;
    int64_t npositions = -1;
    int     seed = time(NULL);
    double  frc_prob = 0.0;

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
        } else if (MATCH(argv[iter], "-h") || MATCH(argv[iter], "--help")) {
            generate_usage();
            return 0;
        } else {
            printf("Error: unknown argument, %s\n", argv[iter]);
            generate_usage();
            return 1;
        }

        iter++;
    }

    /* Validate options */
    if (!output_file ||
        (depth <= 0) || (depth >= MAX_SEARCH_DEPTH) ||
        (npositions <= 0) || (frc_prob < 0.0) || (frc_prob >= 1.0)) {
        printf("Error: invalid options\n");
        generate_usage();
        return 1;
    }

    /* Initialize random number generator */
    srand(seed);

    return generate(output_file, depth, npositions, frc_prob);
}

int sfen_rescore(int argc, char *argv[])
{
    int      iter;
    char     *input_file = NULL;
    char     *output_file = NULL;
    int      depth = 8;
    int      offset = 0;
    int64_t  npositions = -1;

    /* Parse command line options */
    iter = 2;
    while (iter < argc) {
        if ((MATCH(argv[iter], "-i") || MATCH(argv[iter], "--input")) &&
            ((iter+1) < argc)) {
            iter++;
            input_file = argv[iter];
        } else if ((MATCH(argv[iter], "-o") ||
                    MATCH(argv[iter], "--output")) &&
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
        } else if ((MATCH(argv[iter], "-f") ||
                    MATCH(argv[iter], "--offset")) &&
                   ((iter+1) < argc)) {
            iter++;
            offset = atoi(argv[iter]);
        } else if (MATCH(argv[iter], "-h") || MATCH(argv[iter], "--help")) {
            rescore_usage();
            return 0;
        } else {
            printf("Error: unknown argument, %s\n", argv[iter]);
            rescore_usage();
            return 1;
        }

        iter++;
    }

    /* Validate options */
    if (!input_file || !output_file ||
        (depth <= 0) || (depth >= MAX_SEARCH_DEPTH) ||
        (offset < 0) ||
        (npositions == 0)) {
        printf("Error: invalid options\n");
        rescore_usage();
        return 1;
    }

    return rescore(input_file, output_file, depth, npositions, offset);
}
