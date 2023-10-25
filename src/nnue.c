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

#define INCBIN_PREFIX
#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#include "incbin.h"
INCBIN(nnue_net, NETFILE_NAME);

/* NNUE quantization parameters */
#define OUTPUT_SCALE 16.0f

/* Definition of the network architcechure */
#define NET_VERSION 0x00000008
#define NET_HEADER_SIZE 4
static int model_layer_sizes[NNUE_NUM_LAYERS] =
                                        {NNUE_TRANSFORMER_SIZE*2, 8, 16, 1};

/*
 * Struct holding information about a layer in the network. The size
 * may be padded in order to better match SIMD instructions. The model_size
 * field holds the actual, unpadedd, size as defined in the model.
 */
struct layer {
    union {
        int8_t  *i8;
        int16_t *i16;
    }                   weights;
    union {
        int32_t *i32;
        int16_t *i16;
    }                   biases;
};

/* Struct for holding data during a forward pass */
struct net_data {
    alignas(64) int32_t intermediate[NNUE_TRANSFORMER_SIZE*2];
    alignas(64) uint8_t output[NNUE_TRANSFORMER_SIZE*2];
};

/* The network */
static struct layer layers[NNUE_NUM_LAYERS];
static int layer_sizes[NNUE_NUM_LAYERS];

static int feature_index(int sq, int piece, int side)
{
    int piece_index;

    /*
     * For black the board should be flipped so squares must be
     * updated accordingly. Additionally the piece index is
     * side-to-move relative so for black the indexing order has
     * to be swapped.
     */
    if (side == BLACK) {
        sq = MIRROR(sq);
        piece_index = FLIP_COLOR(piece)*NSQUARES;
    } else {
        piece_index = piece*NSQUARES;
    }

    return sq + piece_index;
}

static void accumulator_propagate(struct position *pos, bool update_stm)
{
    int16_t *prev_data;
    int16_t *data;
    int     side;

    for (side=0;side<NSIDES;side++) {
        if (update_stm || (side != pos->stm)) {
            prev_data =
                    &pos->eval_stack[pos->height-1].accumulator.data[side][0];
            data = &pos->eval_stack[pos->height].accumulator.data[side][0];
            simd_copy(prev_data, data, layer_sizes[0]/2);
        }
    }
}

static void accumulator_add(struct position *pos, bool update_stm,
                            int piece, int sq)
{
    struct nnue_accumulator *acc;
    int16_t                 *data;
    int                     k;
    uint32_t                index;
    uint32_t                offset;
    int                     side;
    int                     king_sq;

    acc = &pos->eval_stack[pos->height].accumulator;
    data = &acc->data[side][0];

    /* Apply required updates to the accumulator */
    for (k=0;k<acc->nupdates;k++) {
        index = feature_index(acc->updates[k].sq, acc->updates[k].piece, side);
        offset = (layer_sizes[0]/2)*index;
        if (acc->updates[k].add) {
            simd_add(&layers[0].weights.i16[offset], data, layer_sizes[0]/2);
        }
    }
}

static void accumulator_remove(struct position *pos, bool update_stm,
                               int piece, int sq)
{
    struct nnue_accumulator *acc;
    int16_t                 *data;
    uint32_t                index;
    uint32_t                offset;
    int                     side;
    int                     king_sq;

    acc = &pos->eval_stack[pos->height].accumulator;
    for (side=0;side<NSIDES;side++) {
        if (update_stm || (side != pos->stm)) {
            data = &acc->data[side][0];
            king_sq = LSB(pos->bb_pieces[side+KING]);
            index = feature_index(sq, piece, king_sq, side);
            offset = (layer_sizes[0]/2)*index;
            simd_sub(&layers[0].weights.i16[offset], data, layer_sizes[0]/2);
        }
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
    simd_copy(layers[0].biases.i16, data, layer_sizes[0]/2);

    /* Update the accumulator based on each piece */
    bb = pos->bb_all;
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        index = feature_index(sq, pos->pieces[sq], side);
        offset = (layer_sizes[0]/2)*index;
        simd_add(&layers[0].weights.i16[offset], data, layer_sizes[0]/2);
    }
}

static bool accumulator_refresh_required(struct position *pos, int side)
{
    /*
     * If there is no worker associated with the position then
     * the engine is not searching so it doesn't matter if a
     * full update is done.
     */
    if (pos->worker == NULL) {
        return true;
    }

    /*
     * If the state of the previous position is not valid
     * then a full refresh is required.
     */
    if ((pos->height == 0) ||
        !pos->eval_stack[pos->height-1].accumulator.up2date) {
        return true;
    }

    /*
     * If the king for this side has moved then all feature
     * indeces are invalid and a refresh is required.
     */
    return pos->eval_stack[pos->height].accumulator.refresh[side];
}

static void transformer_forward(struct position *pos, struct net_data *data)
{
    int      side;
    uint32_t size;
    uint8_t  *dest;
    int16_t  *half;

    /*
     * Check if the accumulator is up to date. If it's
     * not then it has to be updated.
     */
    if (!pos->eval_stack[pos->height].accumulator.up2date) {
        for (side=0;side<NSIDES;side++) {
            if (accumulator_refresh_required(pos, side)) {
                accumulator_refresh(pos, side);
            }
        }

        /* Mark the accumulator as up to date */
        pos->eval_stack[pos->height].accumulator.up2date = true;
    }

    /*
     * Combine the two halves to form the inputs to the network. The
     * values are clamped to be in the range [0, 127].
     */
    size = layer_sizes[0]/2;
    dest = &data->output[0];
    half = pos->eval_stack[pos->height].accumulator.data[pos->stm];
    simd_clamp(half, dest, size);
    half = pos->eval_stack[pos->height].accumulator.data[FLIP_COLOR(pos->stm)];
    simd_clamp(half, dest+size, size);
}

static void fc_layer_forward(int idx, struct net_data *data, bool output)
{
    simd_fc_forward(data->output, data->intermediate, layer_sizes[idx-1],
                    model_layer_sizes[idx], layers[idx].biases.i32,
                    layers[idx].weights.i8);
    if (!output) {
        simd_scale_and_clamp(data->intermediate, data->output,
                             layer_sizes[idx]);
    }
}

static void network_forward(struct position *pos, struct net_data *data)
{
    int k;

    transformer_forward(pos, data);
    for (k=1;k<NNUE_NUM_LAYERS-1;k++) {
        fc_layer_forward(k, data, false);
    }
    fc_layer_forward(k, data, true);
}

static bool parse_header(uint8_t **data)
{
    uint8_t  *iter = *data;
    uint32_t version;

    version = read_uint32_le(iter);
    iter += 4;

    *data = iter;

    return version == NET_VERSION;
}

static uint8_t* read_transformer(uint8_t *iter, struct layer *layer)
{
    int k;
    int half;
    int model_half;

    /* Weights and biases are padded with zeroes if necessary */
    model_half = model_layer_sizes[0]/2;
    half = layer_sizes[0]/2;
    for (k=0;k<model_half;k++,iter+=2) {
        layer->biases.i16[k] = read_uint16_le(iter);
    }
    for (;k<half;k++) {
        layer->biases.i16[k] = 0;
    }
    for (k=0;k<model_half*NNUE_NUM_INPUT_FEATURES;k++,iter+=2) {
        layer->weights.i16[k] = read_uint16_le(iter);
    }
    for (;k<half*NNUE_NUM_INPUT_FEATURES;k++) {
        layer->weights.i16[k] = 0;
    }

    return iter;
}

static uint8_t* read_fc_layer(uint8_t *iter, int idx, struct layer *layer)
{
    int k;
    int l;

    /* Weights and biases are padded with zeroes if necessary */
    for (k=0;k<model_layer_sizes[idx];k++,iter+=4) {
        layer->biases.i32[k] = read_uint32_le(iter);
    }
    for (;k<layer_sizes[idx];k++) {
        layer->biases.i32[k] = 0;
    }
    for (k=0;k<model_layer_sizes[idx];k++) {
        for (l=0;l<model_layer_sizes[idx-1];l++,iter++) {
            layer->weights.i8[k*layer_sizes[idx-1]+l] = (int8_t)*iter;
        }
        for (;l<layer_sizes[idx-1];l++) {
            layer->weights.i8[k*layer_sizes[idx-1]+l] = 0;
        }
    }

    return iter;
}

static uint8_t* read_output_layer(uint8_t *iter, int idx, struct layer *layer)
{
    int k;
    int l;

    /* Note that the size of the output layer is not padded */
    for (k=0;k<model_layer_sizes[idx];k++,iter+=4) {
        layer->biases.i32[k] = read_uint32_le(iter);
    }
    for (k=0;k<model_layer_sizes[idx];k++) {
        for (l=0;l<model_layer_sizes[idx-1];l++,iter++) {
            layer->weights.i8[k*layer_sizes[idx-1]+l] = (int8_t)*iter;
        }
        for (;l<layer_sizes[idx-1];l++) {
            layer->weights.i8[k*layer_sizes[idx-1]+l] = 0;
        }
    }

    return iter;
}

static bool parse_network(uint8_t **data)
{
    uint8_t *iter = *data;
    int   k;

    /* Read biases and weights for the transformer */
    iter = read_transformer(iter, &layers[0]);

    /* Read biases and weights for each hidden layer */
    for (k=1;k<NNUE_NUM_LAYERS-1;k++) {
        iter = read_fc_layer(iter, k, &layers[k]);
    }

    /* Read biases and weights for the output layer */
    iter = read_output_layer(iter, k, &layers[k]);

    *data = iter;

    return true;
}

static uint32_t calculate_net_size(void)
{
    uint32_t size = 0;
    int      k;

    size += NET_HEADER_SIZE;

    size += NNUE_TRANSFORMER_SIZE*sizeof(int16_t);
    size += NNUE_TRANSFORMER_SIZE*NNUE_NUM_INPUT_FEATURES*sizeof(int16_t);

    for (k=1;k<NNUE_NUM_LAYERS;k++) {
        size += model_layer_sizes[k]*sizeof(int32_t);
        size += model_layer_sizes[k]*model_layer_sizes[k-1]*sizeof(int8_t);
    }

    return size;
}

void nnue_init(void)
{
    int k;

    /* Allocate space for layers */
    layer_sizes[0] = simd_pad_size(NNUE_TRANSFORMER_SIZE)*2;
    layers[0].weights.i16 = aligned_malloc(64,
                    layer_sizes[0]*NNUE_NUM_INPUT_FEATURES*sizeof(int16_t));
    layers[0].biases.i16 = aligned_malloc(64, layer_sizes[0]*sizeof(int16_t));
    for (k=1;k<NNUE_NUM_LAYERS-1;k++) {
        layer_sizes[k] = simd_pad_size(model_layer_sizes[k]);
        layers[k].weights.i8 = aligned_malloc(64,
                                layer_sizes[k]*layer_sizes[k-1]*sizeof(int8_t));
        layers[k].biases.i32 = aligned_malloc(64,
                                layer_sizes[k]*sizeof(int32_t));
    }
    layer_sizes[k] = model_layer_sizes[k]; /* Output layer is not padded */
    layers[k].weights.i8 = aligned_malloc(64,
                            layer_sizes[k]*layer_sizes[k-1]*sizeof(int8_t));
    layers[k].biases.i32 = aligned_malloc(64,
                            layer_sizes[k]*sizeof(int32_t));
}

void nnue_destroy(void)
{
    int k;

    aligned_free(layers[0].weights.i16);
    aligned_free(layers[0].biases.i16);
    for (k=1;k<NNUE_NUM_LAYERS;k++) {
        aligned_free(layers[k].weights.i8);
        aligned_free(layers[k].biases.i32);
    }
}

void nnue_reset_accumulator(struct position *pos)
{
    int k;

    for (k=0;k<MAX_PLY;k++) {
        pos->eval_stack[k].accumulator.up2date = false;
    }
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

    /* Parse network header */
    iter = data;
    if (!parse_header(&iter)) {
        ret = false;
        goto exit;
    }

    /* Parse network */
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
    struct net_data data;

    network_forward(pos, &data);
    return data.intermediate[0]/OUTPUT_SCALE;
}

void nnue_make_move(struct position *pos, uint32_t move)
{
    struct nnue_accumulator *acc;
    int                     from = FROM(move);
    int                     to = TO(move);
    int                     promotion = PROMOTION(move);
    int                     capture = pos->pieces[to];
    int                     piece = pos->pieces[from];
    bool                    update_stm = (piece != (pos->stm+KING));

    assert(pos != NULL);

    if (pos->worker == NULL) {
        return;
    }

    /*
     * If the state of the previous position is not valid
     * then a full refresh is required.
     */
    acc = &pos->eval_stack[pos->height].accumulator;
    if ((pos->height == 0) ||
        !pos->eval_stack[pos->height-1].accumulator.up2date) {
        acc->up2date = false;
        return;
    }

    acc->refresh[WHITE] = piece == WHITE_KING;
    acc->refresh[BLACK] = piece == BLACK_KING;
    acc->up2date = !acc->refresh[WHITE] && !acc->refresh[BLACK];

    /* Copy accumulator data from the previous ply */
    accumulator_propagate(pos, update_stm);

    if (ISKINGSIDECASTLE(move)) {
        accumulator_remove(pos, false, ROOK + pos->stm, to);
        accumulator_add(pos, false, ROOK + pos->stm,
                        KINGCASTLE_KINGMOVE(to) - 1);
    } else if (ISQUEENSIDECASTLE(move)) {
        accumulator_remove(pos, false, ROOK + pos->stm, to);
        accumulator_add(pos, false, ROOK + pos->stm,
                        QUEENCASTLE_KINGMOVE(to) + 1);
    } else if (ISENPASSANT(move)) {
        accumulator_remove(pos, true, piece, from);
        accumulator_add(pos, true, piece, to);
        accumulator_remove(pos, true, PAWN + FLIP_COLOR(pos->stm),
                           (pos->stm == WHITE)?to-8:to+8);
    } else {
        if (VALUE(piece) != KING) {
            accumulator_remove(pos, update_stm, piece, from);
        }
        if (ISCAPTURE(move)) {
            accumulator_remove(pos, update_stm, capture, to);
        }
        if (VALUE(piece) != KING) {
            accumulator_add(pos, update_stm, ISPROMOTION(move)?promotion:piece,
                            to);
        }
    }
}

void nnue_make_null_move(struct position *pos)
{
    assert(pos != NULL);

    if (pos->worker == NULL) {
        return;
    }

    if ((pos->height > 0) &&
        pos->eval_stack[pos->height-1].accumulator.up2date) {
        pos->eval_stack[pos->height].accumulator =
                                    pos->eval_stack[pos->height-1].accumulator;
    } else {
        pos->eval_stack[pos->height].accumulator.up2date = false;
    }
}
