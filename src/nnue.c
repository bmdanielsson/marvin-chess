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

/* Parameters for validating the network file */
#define NET_VERSION 0x00000001
#define NET_SIZE \
    (                                                           \
    4                   +                                       \
    2*HALF_DIMS         + 2*HALF_DIMS*FEATURE_IN_DIMS         + \
    4*HIDDEN_LAYER_SIZE + HIDDEN_LAYER_SIZE*FEATURE_OUT_DIMS  + \
    4*HIDDEN_LAYER_SIZE + HIDDEN_LAYER_SIZE*HIDDEN_LAYER_SIZE + \
    4*OUTPUT_LAYER_SIZE + HIDDEN_LAYER_SIZE*OUTPUT_LAYER_SIZE   \
    )

/* Parameters describing the network architecture */
#define HALF_DIMS 128
#define FEATURE_IN_DIMS 64*(64*10 + 1)
#define FEATURE_OUT_DIMS HALF_DIMS*2
#define MAX_ACTIVE_FEATURES 30
#define HIDDEN_LAYER_SIZE 32
#define OUTPUT_LAYER_SIZE 1

/*
 * Parameters that makes up the net. A net consists of two parts,
 * a transformer and a network.
 *
 * The transformer is a combination of the input layer and the first
 * hidden layer in a traditional neural network. It consists of two
 * halves, one for each king. The halves use the same weights and biases.
 *
 * The network consists of a set of hidden layers and an output layer with
 * a single neuron.
 */
static alignas(64) int16_t hidden1_weights[HIDDEN_LAYER_SIZE*FEATURE_OUT_DIMS];
static alignas(64) int16_t hidden2_weights[HIDDEN_LAYER_SIZE*HIDDEN_LAYER_SIZE];
static alignas(64) int16_t output_weights[HIDDEN_LAYER_SIZE];
static alignas(64) int32_t hidden1_biases[HIDDEN_LAYER_SIZE];
static alignas(64) int32_t hidden2_biases[HIDDEN_LAYER_SIZE];
static alignas(64) int32_t output_biases[OUTPUT_LAYER_SIZE];
static alignas(64) int16_t feature_biases[HALF_DIMS];
static alignas(64) int16_t feature_weights[HALF_DIMS*FEATURE_IN_DIMS];

/*
 * Struct holding the data of all the layer
 * calculations in a forward propagation pass.
 */
struct net_data {
    alignas(64) int16_t input[FEATURE_OUT_DIMS];
    alignas(64) int32_t hidden1_values[HIDDEN_LAYER_SIZE];
    alignas(64) int32_t hidden2_values[HIDDEN_LAYER_SIZE];
    alignas(64) int16_t hidden1_output[HIDDEN_LAYER_SIZE];
    alignas(64) int16_t hidden2_output[HIDDEN_LAYER_SIZE];
    alignas(64) int32_t output;
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

static void layer_propagate(int16_t *input, int32_t *output, int ninputs,
                            int noutputs, int32_t *biases, int16_t *weights)
{
#ifdef USE_SIMD
    simd_layer_propagate(input, output, ninputs, noutputs, biases, weights);
#else
    int k = 0;
    int l = 0;

    /*
     * Perform neuron calculations. Multiply each input with the
     * corresponding weight, summarize and add the bias.
     */
    for (k=0;k<noutputs;k++) {
        output[k] = biases[k];
        #pragma omp simd aligned(weights,output,input:64)
        for (l=0;l<ninputs;l++) {
            output[k] += (input[l]*weights[k*ninputs+l]);
        }
    }
#endif
}

static void layer_activate(int32_t *input, int16_t *output, uint32_t ndims)
{
    uint32_t k = 0;

    /* Apply activation function */
    #pragma omp simd aligned(input,output:64)
    for (k=0;k<ndims;k++) {
        output[k] = CLAMP((input[k]>>6), 0, 127);
    }
}

static int transform_square(int sq, int side)
{
    /* For black the board is rotated 180 degrees */
    if (side == BLACK) {
        sq = SQUARE(7-FILENR(sq), 7-RANKNR(sq));
    }
    return sq;
}

static int calculate_feature_index(int sq, int piece, int king_sq, int side)
{
    sq = transform_square(sq, side);
    return sq + piece2index[side][piece] + (KING*NSQUARES+1)*king_sq;
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
    int                 k = 0;
    int                 l = 0;

    /* Find all active features */
    find_active_features(pos, side, &active_features);

    /* Setup data pointer */
    data = &pos->eval_stack[pos->sply].state.data[side][0];

    /* Add biases */
    #pragma omp simd aligned(data,feature_biases:64)
    for (k=0;k<HALF_DIMS;k++) {
        data[k] = feature_biases[k];
    }

    /* Summarize the weights for all active features */
    for (k=0;k<active_features.size;k++) {
        index = active_features.features[k];
        offset = HALF_DIMS*index;
        #pragma omp simd aligned(data,feature_weights:64)
        for (l=0;l<HALF_DIMS;l++) {
            data[l] += feature_weights[offset+l];
        }
    }
}

static void perform_incremental_update(struct position *pos, int side)
{
    struct feature_list added;
    struct feature_list removed;
    uint32_t            offset;
    uint32_t            index;
    int                 k = 0;
    int                 l = 0;
    int16_t             *data;
    int16_t             *prev_data;

    /* Find all changed features */
    find_changed_features(pos, side, &added, &removed);

    /* Setup data pointers */
    data = &pos->eval_stack[pos->sply].state.data[side][0];
    prev_data = &pos->eval_stack[pos->sply-1].state.data[side][0];

    /* Copy the state from previous position */
    #pragma omp simd aligned(data,prev_data:64)
    for (k=0;k<HALF_DIMS;k++) {
        data[k] = prev_data[k];
    }

    /* Subtract weights for removed features */
    for (k=0;k<removed.size;k++) {
        index = removed.features[k];
        offset = HALF_DIMS*index;
        #pragma omp simd aligned(data,feature_weights:64)
        for (l=0;l<HALF_DIMS;l++) {
            data[l] -= feature_weights[offset+l];
        }
    }

    /* Add weights for added features */
    for (k=0;k<added.size;k++) {
        index = added.features[k];
        offset = HALF_DIMS*index;
        #pragma omp simd aligned(data,feature_weights:64)
        for (l=0;l<HALF_DIMS;l++) {
            data[l] += feature_weights[offset+l];
        }
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

static void transformer_propagate(struct position *pos, struct net_data *data)
{
    int      perspectives[NSIDES];
    int      side = 0;
    int      k = 0;
    uint32_t offset;
    int16_t  *temp;
    int16_t  *features;
    int16_t  value;

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
        offset = HALF_DIMS*side;
        temp = &data->input[offset];
        features = pos->eval_stack[pos->sply].state.data[perspectives[side]];
        #pragma omp simd aligned(features,temp:64)
        for (k=0;k<HALF_DIMS;k++) {
            value = features[k];
            temp[k] = CLAMP(value, 0, 127);
        }
    }
}

static void network_propagate(struct net_data *data)
{
    /* First hidden layer */
    layer_propagate(data->input, data->hidden1_values, FEATURE_OUT_DIMS,
                    HIDDEN_LAYER_SIZE, hidden1_biases, hidden1_weights);
    layer_activate(data->hidden1_values, data->hidden1_output,
                   HIDDEN_LAYER_SIZE);

    /* Second hidden layer */
    layer_propagate(data->hidden1_output, data->hidden2_values,
                    HIDDEN_LAYER_SIZE, HIDDEN_LAYER_SIZE, hidden2_biases,
                    hidden2_weights);
    layer_activate(data->hidden2_values, data->hidden2_output,
                   HIDDEN_LAYER_SIZE);

    /* Output layer */
    layer_propagate(data->hidden2_output, &data->output, HIDDEN_LAYER_SIZE,
                    OUTPUT_LAYER_SIZE, output_biases, output_weights);
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

static bool parse_transformer(uint8_t **data)
{
    uint8_t  *iter = *data;
    int      k;

    /* Read biases and weights */
    for (k=0;k<HALF_DIMS;k++,iter+=2) {
        feature_biases[k] = read_uint16_le(iter);
    }
    for (k=0;k<HALF_DIMS*FEATURE_IN_DIMS;k++,iter+=2) {
        feature_weights[k] = read_uint16_le(iter);
    }

    *data = iter;

    return true;
}

static bool parse_network(uint8_t **data)
{
    uint8_t *iter = *data;
    int     k;

    /* Read biases and weights of the first hidden layer */
    for (k=0;k<HIDDEN_LAYER_SIZE;k++,iter+=4) {
        hidden1_biases[k] = read_uint32_le(iter);
    }
    for (k=0;k<HIDDEN_LAYER_SIZE*FEATURE_OUT_DIMS;k++,iter++) {
        hidden1_weights[k] = (int8_t)*iter;
    }

    /* Read biases and weights of the second hidden layer */
    for (k=0;k<HIDDEN_LAYER_SIZE;k++,iter+=4) {
        hidden2_biases[k] = read_uint32_le(iter);
    }
    for (k=0;k<HIDDEN_LAYER_SIZE*HIDDEN_LAYER_SIZE;k++,iter++) {
        hidden2_weights[k] = (int8_t)*iter;
    }

    /* Read biases and weights of the output layer */
    for (k=0;k<OUTPUT_LAYER_SIZE;k++,iter+=4) {
        output_biases[k] = read_uint32_le(iter);
    }
    for (k=0;k<HIDDEN_LAYER_SIZE*OUTPUT_LAYER_SIZE;k++,iter++) {
        output_weights[k] = (int8_t)*iter;
    }

    *data = iter;

    return true;
}

void nnue_init(void)
{
    int piece;

    for (piece=0;piece<KING;piece+=2) {
        piece2index[WHITE][piece] = piece*NSQUARES + 1;
        piece2index[WHITE][piece+1] = (piece+1)*NSQUARES + 1;
        piece2index[BLACK][piece] = (piece+1)*NSQUARES + 1;
        piece2index[BLACK][piece+1] = piece*NSQUARES + 1;
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
    if (size != NET_SIZE) {
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

    /* Parse header */
    iter = data;
    if (!parse_header(&iter)) {
        ret = false;
        goto exit;
    }

    /* Parse transformer section */
    if (!parse_transformer(&iter)) {
        ret = false;
        goto exit;
    }

    /* Parse network section */
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

    transformer_propagate(pos, &data);
    network_propagate(&data);

    return data.output/16;
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
