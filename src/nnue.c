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
/*
 * This file implements support for NNUE style neural networks. NNUE
 * was invented by Yu Nasu for use with shogi and adapted to chess
 * by Hisayori Noda.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>

#include "nnue.h"
#include "utils.h"
#include "bitboard.h"
#include "simd.h"
#include "data.h"
#include "hash.h"

#define INCBIN_PREFIX
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "incbin.h"
INCBIN(nnue_net, NETFILE_NAME);

/* Definition of the network architcechure */
static int layer_sizes[NNUE_NUM_LAYERS] = {NNUE_HIDDEN_LAYER_SIZE*2,
                                           NNUE_OUTPUT_LAYER_SIZE};

/* Struct holding information about a layer in the network */
struct layer {
    int16_t *weights;
    int16_t *biases;
};

/* Struct holding network layers */
struct net {
    struct layer hidden;
    struct layer output;
};

/* The network */
static struct net net;

/*
 * Table mapping Marvin piece definitions to Bullet piece definitions. Piece
 * values for black are flipped to be used when calculating feature indexes.
 */
static uint8_t bullet_piece_map[NSIDES][NPIECES] = {
    {0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11},
    {6, 0, 7, 1, 8, 2, 9, 3, 10, 4, 11, 5}
};

static int feature_index(int sq, int piece, int side)
{
    int piece_index;
    int bullet_piece;

    /*
     * Convert to Bullet piece representation. The lookup will also take
     * care of swapping indexing when it is black to move.
     */
    bullet_piece = bullet_piece_map[side][piece];

    /*
     * For black the board should be flipped so squares must be
     * updated accordingly.
     */
    piece_index = bullet_piece*NSQUARES;
    if (side == BLACK) {
        sq = MIRROR(sq);
    }

    return sq + piece_index;
}

static void accumulator_move(struct position *pos, uint32_t move)
{
    struct nnue_accumulator *src;
    struct nnue_accumulator *dest;
    int                     from = FROM(move);
    int                     to = TO(move);
    int                     side;
    int                     sub_piece;
    int                     add_piece;
    int                     add_off;
    int                     sub_off;

    /* Prepare operation parameters */
    sub_piece = pos->pieces[from];
    add_piece = ISPROMOTION(move)?PROMOTION(move):sub_piece;
    src = &pos->eval_stack[pos->height-1].accumulator;
    dest = &pos->eval_stack[pos->height].accumulator;

    /* Perform operation for both perspectives */
    for (side=0;side<NSIDES;side++) {
        sub_off = feature_index(from, sub_piece, side)*(layer_sizes[0]/2);
        add_off = feature_index(to, add_piece, side)*(layer_sizes[0]/2);

        simd_add_sub(&src->data[side][0], &dest->data[side][0],
                     &net.hidden.weights[add_off],
                     &net.hidden.weights[sub_off]);
    }
}

static void accumulator_capture(struct position *pos, uint32_t move)
{
    struct nnue_accumulator *src;
    struct nnue_accumulator *dest;
    int                     from = FROM(move);
    int                     to = TO(move);
    int                     side;
    int                     sub_piece1;
    int                     sub_piece2;
    int                     add_piece;
    int                     add_off;
    int                     sub_off1;
    int                     sub_off2;

    /* Prepare operation parameters */
    sub_piece1 = pos->pieces[from];
    sub_piece2 = pos->pieces[to];
    add_piece = ISPROMOTION(move)?PROMOTION(move):sub_piece1;
    src = &pos->eval_stack[pos->height-1].accumulator;
    dest = &pos->eval_stack[pos->height].accumulator;

    /* Perform operation for both perspectives */
    for (side=0;side<NSIDES;side++) {
        sub_off1 = feature_index(from, sub_piece1, side)*(layer_sizes[0]/2);
        sub_off2 = feature_index(to, sub_piece2, side)*(layer_sizes[0]/2);
        add_off = feature_index(to, add_piece, side)*(layer_sizes[0]/2);

        simd_add_sub2(&src->data[side][0], &dest->data[side][0],
                      &net.hidden.weights[add_off],
                      &net.hidden.weights[sub_off1],
                      &net.hidden.weights[sub_off2]);
    }
}

static void accumulator_en_passant(struct position *pos, uint32_t move)
{
    struct nnue_accumulator *src;
    struct nnue_accumulator *dest;
    int                     from = FROM(move);
    int                     to = TO(move);
    int                     side;
    int                     sub_piece1;
    int                     sub_piece2;
    int                     add_piece;
    int                     sub_sq2;
    int                     add_off;
    int                     sub_off1;
    int                     sub_off2;

    /* Prepare operation parameters */
    sub_piece1 = PAWN + pos->stm;
    sub_piece2 = PAWN + FLIP_COLOR(pos->stm);
    add_piece = sub_piece1;
    sub_sq2 = (pos->stm == WHITE)?to-8:to+8;
    src = &pos->eval_stack[pos->height-1].accumulator;
    dest = &pos->eval_stack[pos->height].accumulator;

    /* Perform operation for both perspectives */
    for (side=0;side<NSIDES;side++) {
        sub_off1 = feature_index(from, sub_piece1, side)*(layer_sizes[0]/2);
        sub_off2 = feature_index(sub_sq2, sub_piece2, side)*(layer_sizes[0]/2);
        add_off = feature_index(to, add_piece, side)*(layer_sizes[0]/2);

        simd_add_sub2(&src->data[side][0], &dest->data[side][0],
                      &net.hidden.weights[add_off],
                      &net.hidden.weights[sub_off1],
                      &net.hidden.weights[sub_off2]);
    }
}

static void accumulator_castling(struct position *pos, int king_from,
                                 int king_to,int rook_from, int rook_to)
{
    struct nnue_accumulator *src;
    struct nnue_accumulator *dest;
    int                     side;
    int                     rook = ROOK + pos->stm;
    int                     king = KING + pos->stm;
    int                     add_off1;
    int                     add_off2;
    int                     sub_off1;
    int                     sub_off2;

    /* Prepare operation parameters */
    src = &pos->eval_stack[pos->height-1].accumulator;
    dest = &pos->eval_stack[pos->height].accumulator;

    /* Perform operation for both perspectives */
    for (side=0;side<NSIDES;side++) {
        sub_off1 = feature_index(king_from, king, side)*(layer_sizes[0]/2);
        sub_off2 = feature_index(rook_from, rook, side)*(layer_sizes[0]/2);
        add_off1 = feature_index(king_to, king, side)*(layer_sizes[0]/2);
        add_off2 = feature_index(rook_to, rook, side)*(layer_sizes[0]/2);

        simd_add2_sub2(&src->data[side][0], &dest->data[side][0],
                      &net.hidden.weights[add_off1],
                      &net.hidden.weights[add_off2],
                      &net.hidden.weights[sub_off1],
                      &net.hidden.weights[sub_off2]);
    }
}

static void accumulator_copy(struct position *pos)
{
    int16_t *prev_data;
    int16_t *data;
    int     side;

    for (side=0;side<NSIDES;side++) {
        prev_data = &pos->eval_stack[pos->height-1].accumulator.data[side][0];
        data = &pos->eval_stack[pos->height].accumulator.data[side][0];
        memcpy(data, prev_data, sizeof(int16_t)*NNUE_HIDDEN_LAYER_SIZE);
    }
}

static void accumulator_refresh(struct position *pos, int side)
{
    int      sq;
    uint32_t index;
    uint32_t offset;
    int16_t  *data;
    uint64_t bb;

    /* Setup data pointer */
    data = &pos->eval_stack[pos->height].accumulator.data[side][0];

    /* Add biases */
    memcpy(data, net.hidden.biases, sizeof(int16_t)*NNUE_HIDDEN_LAYER_SIZE);

    /* Update the accumulator based on each piece */
    bb = pos->bb_all;
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        index = feature_index(sq, pos->pieces[sq], side);
        offset = (layer_sizes[0]/2)*index;
        simd_add(&net.hidden.weights[offset], data);
    }
}

static int32_t forward_half(struct position *pos, int side, int16_t *weights)
{
    int16_t *inputs;

    inputs = pos->eval_stack[pos->height].accumulator.data[side];
    return simd_fully_connected(inputs, weights);
}

static uint8_t* read_hidden_layer(uint8_t *iter, struct layer *layer)
{
    int k;
    int l;
    int half;

    half = layer_sizes[0]/2;
    for (k=0;k<NNUE_NUM_INPUT_FEATURES;k++) {
        for (l=0;l<half;l++,iter+=2) {
            layer->weights[k*half+l] = read_uint16_le(iter);
        }
    }
    for (k=0;k<half;k++,iter+=2) {
        layer->biases[k] = read_uint16_le(iter);
    }

    return iter;
}

static uint8_t* read_output_layer(uint8_t *iter, int idx, struct layer *layer)
{
    int k;
    int l;

    for (k=0;k<layer_sizes[idx-1];k++) {
        for (l=0;l<layer_sizes[idx];l++,iter+=2) {
            layer->weights[k*layer_sizes[idx]+l] = read_uint16_le(iter);
        }
    }
    for (k=0;k<layer_sizes[idx];k++,iter+=2) {
        layer->biases[k] = read_uint16_le(iter);
    }

    return iter;
}

static bool parse_network(uint8_t **data)
{
    uint8_t *iter = *data;

    /* Read biases and weights for the input layer */
    iter = read_hidden_layer(iter, &net.hidden);

    /* Read biases and weights for the output layer */
    iter = read_output_layer(iter, 1, &net.output);

    *data = iter;

    return true;
}

static uint32_t calculate_net_size(void)
{
    uint32_t size = 0;
    int      rem;

    /* Hidden layer */
    size += NNUE_HIDDEN_LAYER_SIZE*sizeof(int16_t);
    size += NNUE_HIDDEN_LAYER_SIZE*NNUE_NUM_INPUT_FEATURES*sizeof(int16_t);

    /* Output layer */
    size += layer_sizes[1]*sizeof(int16_t);
    size += layer_sizes[1]*layer_sizes[0]*sizeof(int16_t);

    /* Files are padded so that the size if a multiple of 64 */
    rem = size%64;
    if (rem > 0) {
        size += (64 - rem);
    }

    return size;
}

void nnue_init(void)
{
    /* Allocate space for layers */
    net.hidden.weights = aligned_malloc(64,
                    layer_sizes[0]*NNUE_NUM_INPUT_FEATURES*sizeof(int16_t));
    net.hidden.biases = aligned_malloc(64, layer_sizes[0]*sizeof(int16_t));
    net.output.weights = aligned_malloc(64,
                            layer_sizes[1]*layer_sizes[0]*sizeof(int16_t));
    net.output.biases = aligned_malloc(64,
                            layer_sizes[1]*sizeof(int16_t));
}

void nnue_destroy(void)
{
    aligned_free(net.hidden.weights);
    aligned_free(net.hidden.biases);
    aligned_free(net.output.weights);
    aligned_free(net.output.biases);
}

void nnue_refresh_accumulator(struct position *pos)
{
    accumulator_refresh(pos, WHITE);
    accumulator_refresh(pos, BLACK);
}

bool nnue_load_net(char *path)
{
    int32_t  size;
    int32_t  count;
    uint8_t  *data = NULL;
    uint8_t  *iter;
    FILE     *fh = NULL;
    bool     ret = true;

    /* If an external net is specified then read the complete file */
    if (path != NULL) {
        size = (int32_t)get_file_size(path);
        if (size < 0) {
            ret = false;
            goto exit;
        }
        if (size != (int32_t)calculate_net_size()) {
            ret = false;
            goto exit;
        }
        fh = fopen(path, "rb");
        if (fh == NULL) {
            ret = false;
            goto exit;
        }
        data = malloc(size);
        count = (int32_t)fread(data, 1, size, fh);
        if (count != size) {
            ret = false;
            goto exit;
        }
    } else {
        data = (uint8_t*)nnue_net_data;
        size = (int32_t)nnue_net_size;
        if (size != (int32_t)calculate_net_size()) {
            ret = false;
            goto exit;
        }
    }

    /* Parse network */
    iter = data;
    if (!parse_network(&iter)) {
        ret = false;
        goto exit;
    }

exit:
    if (path != NULL) {
        free(data);
        if (fh != NULL) {
            fclose(fh);
        }
    }
    return ret;
}

int16_t nnue_evaluate(struct position *pos)
{
    int32_t output = 0;

    /* Summarize the two accumulators */
    output += forward_half(pos, pos->stm, &net.output.weights[0]);
    output += forward_half(pos, FLIP_COLOR(pos->stm),
                           &net.output.weights[NNUE_HIDDEN_LAYER_SIZE]);

    /* Account for screlu */
    output /= NNUE_QUANT_QA;

    /* Add bias */
    output += net.output.biases[0];

    /* Apply scale factor */
    output *= NNUE_SCALE;

    /* Dequantize */
    output /= (NNUE_QUANT_QA*NNUE_QUANT_QB);

    return output;
}

void nnue_make_move(struct position *pos, uint32_t move)
{
    int from = FROM(move);
    int to = TO(move);

    assert(pos->height > 0);

    /* Only perform updates while searching */
    if (pos->worker == NULL) {
        return;
    }

    /* Update accumulator */
    if (ISKINGSIDECASTLE(move)) {
        accumulator_castling(pos, from, KINGCASTLE_KINGMOVE(to), to,
                             KINGCASTLE_KINGMOVE(to) - 1);
    } else if (ISQUEENSIDECASTLE(move)) {
        accumulator_castling(pos, from, QUEENCASTLE_KINGMOVE(to), to,
                             QUEENCASTLE_KINGMOVE(to) + 1);
    } else if (ISENPASSANT(move)) {
        accumulator_en_passant(pos, move);
    } else if (ISCAPTURE(move)) {
        accumulator_capture(pos, move);
    } else {
        accumulator_move(pos, move);
    }
}

void nnue_make_null_move(struct position *pos)
{
    assert(pos != NULL);

    accumulator_copy(pos);
}
