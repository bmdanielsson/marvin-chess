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

/* Struct holding information about a layer in the network */
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

/* Definition of the network architcechure */
#define NET_VERSION 0x00000002
#define NET_HEADER_SIZE 4
#define NUM_INPUT_FEATURES 64*64*10
#define MAX_ACTIVE_FEATURES 30
#define ACTIVATION_SCALE_BITS 6
#define OUTPUT_SCALE_FACTOR 16

/* Definition of the network */
#define NUM_LAYERS 4
#define HALFKX_LAYER_SIZE 128
static struct layer layers[NUM_LAYERS];
static int layer_sizes[NUM_LAYERS] = {HALFKX_LAYER_SIZE*2, 32, 32, 1};

/* Struct for holding data during a forward pass */
struct net_data {
    alignas(64) int32_t intermediate[HALFKX_LAYER_SIZE*2];
    alignas(64) uint8_t output[HALFKX_LAYER_SIZE*2];
};

/*
 * List of active features for one half. The features are the position
 * of all non-king pieces in relation to one of the two kings.
 */
struct feature_list {
    uint8_t  size;
    uint32_t features[MAX_ACTIVE_FEATURES];
};

/* Table mapping piece to piece index */
static uint32_t piece2index[NSIDES][NPIECES];

static int transform_square(int sq, int side)
{
    return (side == BLACK)?MIRROR(sq):sq;
}

static int calculate_feature_index(int sq, int piece, int king_sq, int side)
{
    sq = transform_square(sq, side);
    return sq + piece2index[side][piece] + KING*NSQUARES*king_sq;
}

static void find_active_features(struct position *pos, int side,
                                 struct feature_list *list)
{
    int      king_sq;
    int      sq;
    uint32_t index;
    uint64_t bb;

    /* Initialize */
    list->size = 0;

    /* Find the location of the king */
    king_sq = transform_square(LSB(pos->bb_pieces[side+KING]), side);

    /* Construct a bitboard of all pieces excluding the two kings */
    bb = pos->bb_all&(~(pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING]));

    /* Construct a king/piece index for each piece and add it to the list */
    while (bb != 0ULL) {
        sq = POPBIT(&bb);

        /*
         * Calculate the feature index for this
         * piece and add it to the list.
         */
        index = calculate_feature_index(sq, pos->pieces[sq], king_sq, side);
        list->features[list->size++] = index;
    }
}

static bool find_changed_features(struct position *pos, int side,
                                  struct feature_list *added,
                                  struct feature_list *removed)
{
    struct dirty_pieces *dp = &pos->eval_stack[pos->sply].dirty_pieces;
    int                 k;
    int                 king_sq;
    int                 piece;
    uint32_t            index;

    /* Initialize */
    added->size = 0;
    removed->size = 0;

    /*
     * Find the location of the king. For black
     * the board is rotated 180 degrees.
     */
    king_sq = transform_square(LSB(pos->bb_pieces[side+KING]), side);

    /* Loop over all dirty pieces and update feature lists */
    for (k=0;k<dp->ndirty;k++) {
        piece = dp->piece[k];

        /* Ignore the two kings */
        if (VALUE(piece) == KING) {
            continue;
        }

        /* Look for removed or added features */
        if (dp->from[k] != NO_SQUARE) {
            /*
             * Calculate the feature index for this piece and add it
             * to the list of removed features.
             */
            index = calculate_feature_index(dp->from[k], piece, king_sq, side);
            removed->features[removed->size++] = index;
        }
        if (dp->to[k] != NO_SQUARE) {
            /*
             * Calculate the feature index for this piece and add it
             * to the list of added pieces.
             */
            index = calculate_feature_index(dp->to[k], piece, king_sq, side);
            added->features[added->size++] = index;
        }
    }

    return false;
}

static void perform_full_update(struct position *pos, int side)
{
    struct feature_list active_features;
    uint32_t            offset;
    uint32_t            index;
    int16_t             *data;
    int                 k;

    /* Find all active features */
    find_active_features(pos, side, &active_features);

    /* Setup data pointer */
    data = &pos->eval_stack[pos->sply].state.data[side][0];

    /* Add biases */
    simd_copy(layers[0].biases.i16, data, HALFKX_LAYER_SIZE);

    /* Summarize the weights for all active features */
    for (k=0;k<active_features.size;k++) {
        index = active_features.features[k];
        offset = HALFKX_LAYER_SIZE*index;
        simd_add(&layers[0].weights.i16[offset], data, HALFKX_LAYER_SIZE);
    }
}

static void perform_incremental_update(struct position *pos, int side)
{
    struct feature_list added;
    struct feature_list removed;
    uint32_t            offset;
    uint32_t            index;
    int                 k;
    int16_t             *data;
    int16_t             *prev_data;

    /* Find all changed features */
    find_changed_features(pos, side, &added, &removed);

    /* Setup data pointers */
    data = &pos->eval_stack[pos->sply].state.data[side][0];
    prev_data = &pos->eval_stack[pos->sply-1].state.data[side][0];

    /* Copy the state from previous position */
    simd_copy(prev_data, data, HALFKX_LAYER_SIZE);

    /* Subtract weights for removed features */
    for (k=0;k<removed.size;k++) {
        index = removed.features[k];
        offset = HALFKX_LAYER_SIZE*index;
        simd_sub(&layers[0].weights.i16[offset], data, HALFKX_LAYER_SIZE);
    }

    /* Add weights for added features */
    for (k=0;k<added.size;k++) {
        index = added.features[k];
        offset = HALFKX_LAYER_SIZE*index;
        simd_add(&layers[0].weights.i16[offset], data, HALFKX_LAYER_SIZE);
    }
}

static bool incremental_update_possible(struct position *pos, int side)
{
    /*
     * If there is no worker associated with the position then
     * the engine is not searching so it doesn't matter if a
     * full update is done.
     */
    if (pos->worker == NULL) {
        return false;
    }

    /*
     * If the state of the previous position is not valid
     * then a full refresh is required.
     */
    if ((pos->sply == 0) || !pos->eval_stack[pos->sply-1].state.valid) {
        return false;
    }

    /*
     * If the king for this side has moved then all feature
     * indeces are invalid and a refresh is required.
     */
    if (pos->eval_stack[pos->sply].dirty_pieces.piece[0] == (side + KING)) {
        return false;
    }

    return true;
}

static void halfkx_layer_forward(struct position *pos, struct net_data *data)
{
    int      perspectives[NSIDES];
    int      side;
    uint32_t offset;
    uint8_t  *temp;
    int16_t  *features;

    /*
     * Check if the state is up to date. If it's
     * not then it has to be updated.
     */
    if (!pos->eval_stack[pos->sply].state.valid) {
        for (side=0;side<NSIDES;side++) {
            if (incremental_update_possible(pos, side)) {
                perform_incremental_update(pos, side);
            } else {
                perform_full_update(pos, side);
            }
        }

        /* Mark the state as valid */
        pos->eval_stack[pos->sply].state.valid = true;
    }

    /*
     * Combine the two halves to form the inputs to the network. The
     * values are clamped to be in the range [0, 127].
     */
    perspectives[0] = pos->stm;
    perspectives[1] = FLIP_COLOR(pos->stm);
    for (side=0;side<NSIDES;side++) {
        offset = HALFKX_LAYER_SIZE*side;
        temp = &data->output[offset];
        features = pos->eval_stack[pos->sply].state.data[perspectives[side]];
        simd_clamp(features, temp, HALFKX_LAYER_SIZE);
    }
}

static void linear_layer_forward(int idx, struct net_data *data, bool output)
{
    simd_linear_forward(data->output, data->intermediate, layer_sizes[idx-1],
                        layer_sizes[idx], layers[idx].biases.i32,
                        layers[idx].weights.i8);
    if (!output) {
        simd_scale_and_clamp(data->intermediate, data->output,
                             ACTIVATION_SCALE_BITS, layer_sizes[idx]);
    }
}

static void network_forward(struct position *pos, struct net_data *data)
{
    int k;

    halfkx_layer_forward(pos, data);
    for (k=1;k<NUM_LAYERS-1;k++) {
        linear_layer_forward(k, data, false);
    }
    linear_layer_forward(k, data, true);
}

static bool parse_header(uint8_t **data)
{
    uint8_t  *iter = *data;
    uint32_t version;

    /* Read version */
    version = read_uint32_le(iter);
    iter += 4;

    *data = iter;

    return version == NET_VERSION;
}

static bool parse_network(uint8_t **data)
{
    uint8_t *iter = *data;
    int     k;
    int     l;

    /* Read biases and weights for the HalfKX layer */
    for (k=0;k<HALFKX_LAYER_SIZE;k++,iter+=2) {
        layers[0].biases.i16[k] = read_uint16_le(iter);
    }
    for (k=0;k<HALFKX_LAYER_SIZE*NUM_INPUT_FEATURES;k++,iter+=2) {
        layers[0].weights.i16[k] = read_uint16_le(iter);
    }

    /* Read biases and weights for each linear layer */
    for (k=1;k<NUM_LAYERS;k++) {
        for (l=0;l<layer_sizes[k];l++,iter+=4) {
            layers[k].biases.i32[l] = read_uint32_le(iter);
        }
        for (l=0;l<layer_sizes[k]*layer_sizes[k-1];l++,iter++) {
            layers[k].weights.i8[l] = (int8_t)*iter;
        }
    }

    *data = iter;

    return true;
}

static uint32_t calculate_net_size(void)
{
    uint32_t size = 0;
    int      k;

    size += NET_HEADER_SIZE;

    size += HALFKX_LAYER_SIZE*sizeof(int16_t);
    size += HALFKX_LAYER_SIZE*NUM_INPUT_FEATURES*sizeof(int16_t);

    for (k=1;k<NUM_LAYERS;k++) {
        size += layer_sizes[k]*sizeof(int32_t);
        size += layer_sizes[k]*layer_sizes[k-1]*sizeof(int8_t);
    }

    return size;
}

void nnue_init(void)
{
    int piece;
    int k;

    /* Initialize piece index table */
    for (piece=0;piece<KING;piece+=2) {
        piece2index[WHITE][piece] = piece*NSQUARES;
        piece2index[WHITE][piece+1] = (piece+1)*NSQUARES;
        piece2index[BLACK][piece] = (piece+1)*NSQUARES;
        piece2index[BLACK][piece+1] = piece*NSQUARES;
    }

    /* Allocate space for layers */
    layers[0].weights.i16 = aligned_malloc(64,
                        HALFKX_LAYER_SIZE*NUM_INPUT_FEATURES*sizeof(int16_t));
    layers[0].biases.i16 = aligned_malloc(64,
                        HALFKX_LAYER_SIZE*sizeof(int16_t));
    for (k=1;k<NUM_LAYERS;k++) {
        layers[k].weights.i8 = aligned_malloc(64,
                              layer_sizes[k]*layer_sizes[k-1]*sizeof(int8_t));
        layers[k].biases.i32 = aligned_malloc(64,
                              layer_sizes[k]*sizeof(int32_t));
    }
}

void nnue_destroy(void)
{
    int k;

    aligned_free(layers[0].weights.i16);
    aligned_free(layers[0].biases.i16);
    for (k=1;k<NUM_LAYERS;k++) {
        aligned_free(layers[k].weights.i8);
        aligned_free(layers[k].biases.i32);
    }
}

void nnue_reset_state(struct position *pos)
{
    int k;

    for (k=0;k<MAX_PLY;k++) {
        pos->eval_stack[k].state.valid = false;
    }
}

bool nnue_load_net(char *path)
{
    uint32_t size;
    size_t   count;
    uint8_t  *data = NULL;
    uint8_t  *iter;
    FILE     *fh = NULL;
    bool     ret = true;

    assert(path != NULL);

    /* Read the complete file */
    size = get_file_size(path);
    if (size != calculate_net_size()) {
        ret = false;
        goto exit;
    }
    fh = fopen(path, "rb");
    if (fh == NULL) {
        ret = false;
        goto exit;
    }
    data = malloc(size);
    count = fread(data, 1, size, fh);
    if (count != size) {
        ret = false;
        goto exit;
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
    free(data);
    if (fh != NULL) {
        fclose(fh);
    }
    return ret;
}

int16_t nnue_evaluate(struct position *pos)
{
    struct net_data data;

    network_forward(pos, &data);
    return data.intermediate[0]/OUTPUT_SCALE_FACTOR;
}

void nnue_make_move(struct position *pos, uint32_t move)
{
    int                 from = FROM(move);
    int                 to = TO(move);
    int                 promotion = PROMOTION(move);
    int                 capture = pos->pieces[to];
    int                 piece = pos->pieces[from];
    struct dirty_pieces *dp;

    assert(pos != NULL);

    if (pos->worker == NULL) {
        return;
    }

    pos->eval_stack[pos->sply].state.valid = false;
    dp = &(pos->eval_stack[pos->sply].dirty_pieces);
    dp->ndirty = 1;

    if (ISKINGSIDECASTLE(move)) {
        dp->ndirty = 2;

        dp->piece[0] = KING + pos->stm;
        dp->from[0] = from;
        dp->to[0] = to;

        dp->piece[1] = ROOK + pos->stm;
        dp->from[1] = to + 1;
        dp->to[1] = to - 1;
    } else if (ISQUEENSIDECASTLE(move)) {
        dp->ndirty = 2;

        dp->piece[0] = KING + pos->stm;
        dp->from[0] = from;
        dp->to[0] = to;

        dp->piece[1] = ROOK + pos->stm;
        dp->from[1] = to - 2;
        dp->to[1] = to + 1;
    } else if (ISENPASSANT(move)) {
        dp->ndirty = 2;

        dp->piece[0] = piece;
        dp->from[0] = from;
        dp->to[0] = to;

        dp->piece[1] = PAWN + FLIP_COLOR(pos->stm);
        dp->from[1] = (pos->stm == WHITE)?to-8:to+8;
        dp->to[1] = NO_SQUARE;
    } else {
        dp->piece[0] = piece;
        dp->from[0] = from;
        dp->to[0] = to;

        if (ISCAPTURE(move)) {
            dp->ndirty = 2;
            dp->piece[1] = capture;
            dp->from[1] = to;
            dp->to[1] = NO_SQUARE;
        }
        if (ISPROMOTION(move)) {
            dp->to[0] = NO_SQUARE;
            dp->piece[dp->ndirty] = promotion;
            dp->from[dp->ndirty] = NO_SQUARE;
            dp->to[dp->ndirty] = to;
            dp->ndirty++;
        }
    }
}

void nnue_make_null_move(struct position *pos)
{
    assert(pos != NULL);

    if (pos->worker == NULL) {
        return;
    }

    if ((pos->sply > 0) && pos->eval_stack[pos->sply-1].state.valid) {
        pos->eval_stack[pos->sply].state = pos->eval_stack[pos->sply-1].state;
    } else {
        pos->eval_stack[pos->sply].state.valid = false;
    }
}
