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
#ifdef USE_SSE2
#include <emmintrin.h>
#endif

#include "nnue.h"
#include "utils.h"
#include "bitboard.h"

/* Parameters for validating the network file */
#define NET_SIZE 21022697
#define NET_VERSION 0x7AF32F16
#define NET_HEADER_HASH 0x3E5AA6EE
#define NET_TRANSFORMER_HASH 0x5D69D7B8
#define NET_NETWORK_HASH 0x63337156
#define NET_ARCHITECTURE_SIZE 177
#define NET_ARCHITECTURE "Features=HalfKP(Friend)[41024->256x2],"\
                         "Network=AffineTransform[1<-32](ClippedReLU[32]"\
                         "(AffineTransform[32<-32](ClippedReLU[32]"\
                         "(AffineTransform[32<-512](InputSlice[512(0:512)])))))"

/* Parameters describing the network architecture */
#define HALF_DIMS 256
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
static int16_t hidden1_weights[HIDDEN_LAYER_SIZE*FEATURE_OUT_DIMS];
static int16_t hidden2_weights[HIDDEN_LAYER_SIZE*HIDDEN_LAYER_SIZE];
static int16_t output_weights[HIDDEN_LAYER_SIZE];
static int32_t hidden1_biases[HIDDEN_LAYER_SIZE];
static int32_t hidden2_biases[HIDDEN_LAYER_SIZE];
static int32_t output_biases[OUTPUT_LAYER_SIZE];
static alignas(64) int16_t feature_biases[HALF_DIMS];
static alignas(64) int16_t feature_weights[HALF_DIMS*FEATURE_IN_DIMS];

/*
 * Struct holding the data of all the layer
 * calculations in a forward propagation pass.
 */
struct net_data {
    alignas(64) int16_t input[FEATURE_OUT_DIMS];
    int32_t hidden1_values[HIDDEN_LAYER_SIZE];
    int32_t hidden2_values[HIDDEN_LAYER_SIZE];
    int16_t hidden1_output[HIDDEN_LAYER_SIZE];
    int16_t hidden2_output[HIDDEN_LAYER_SIZE];
    int32_t output;
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

static void find_active_features(struct position *pos, int side,
                                 struct feature_list *list)
{
    int      king_sq;
    int      sq;
    int      piece;
    uint32_t index;
    uint64_t bb;

    /* Initialize */
    list->size = 0;

    /*
     * Find the location of the king. For black
     * the board is rotated 180 degrees.
     */
    king_sq = LSB(pos->bb_pieces[side+KING]);
    if (side == BLACK) {
        king_sq = SQUARE(7-FILENR(king_sq), 7-RANKNR(king_sq));
    }

    /* Construct a bitboard of all pieces excluding the two kings */
    bb = pos->bb_all&(~(pos->bb_pieces[WHITE_KING]|pos->bb_pieces[BLACK_KING]));

    /* Construct a king/piece index for each piece and add it to the list */
    while (bb != 0ULL) {
        sq = POPBIT(&bb);
        piece = pos->pieces[sq];

        /*
         * Calculate the input feature index for this piece and add it
         * to the list. For black the board is rotated 180 degrees.
         */
        if (side == BLACK) {
            sq = SQUARE(7-FILENR(sq), 7-RANKNR(sq));
        }
        index = sq + piece2index[side][piece] + (KING*NSQUARES+1)*king_sq;
        list->features[list->size++] = index;
    }
}

static void layer_propagate(int16_t *input, int32_t *output, int ninputs,
                            int noutputs, int32_t *biases, int16_t *weights)
{
    int k;
    int l;

    /*
     * Perform neuron calculations. Multiply each input with the
     * corresponding weight, summarize and add the bias.
     */
    for (k=0;k<noutputs;k++) {
        output[k] = biases[k];
        for (l=0;l<ninputs;l++) {
            output[k] += (input[l]*weights[k*ninputs+l]);
        }
    }
}

static void layer_activate(int32_t *input, int16_t *output, uint32_t ndims)
{
    uint32_t k;

    /* Apply activation function */
    for (k=0;k<ndims;k++) {
        output[k] = CLAMP((input[k]>>6), 0, 127);
    }
}

#ifdef USE_SSE2
/*
 * The SIMD code in this function is taken from Cfish,
 * https://github.com/syzygy1/Cfish
 */
static void transformer_propagate(struct position *pos, struct net_data *data)
{
    struct feature_list active_features[NSIDES];
    alignas(64) int16_t features[NSIDES][HALF_DIMS];
    int                 nchunks = HALF_DIMS/8;
    int                 side;
    int                 ft;
    int                 k;

    /* Find active features for each half */
    find_active_features(pos, WHITE, &active_features[0]);
    find_active_features(pos, BLACK, &active_features[1]);

    /* Process the neurons in each half */
    for (side=0;side<NSIDES;side++) {
        __m128i *biases_vec = (__m128i*)&feature_biases[0];
        __m128i *feature_vec = (__m128i*)&features[side][0];

        /* Add bias */
        for (k=0;k<nchunks;k++) {
            feature_vec[k] = biases_vec[k];
        }

        /* Summarize the weights for all active features */
        for (ft=0;ft<active_features[side].size;ft++) {
            uint32_t index = active_features[side].features[ft];
            __m128i *column = (__m128i*)&feature_weights[HALF_DIMS*index];

            for (k=0;k<nchunks;k++) {
                feature_vec[k] = _mm_add_epi16(feature_vec[k], column[k]);
            }
        }
    }

    /*
     * Combine the two halves to form the inputs to the network. The
     * values are clamped to be in the range [0, 127].
     */
    __m128i k0x7f80 = _mm_set1_epi16(0x7f80);
    __m128i k0x0080 = _mm_set1_epi16(0x0080);
    __m128i k0x8000 = _mm_set1_epi16(-0x8000);
    int     perspectives[NSIDES] = {pos->stm, FLIP_COLOR(pos->stm)};

    for (side=0;side<NSIDES;side++) {
        __m128i *out = (__m128i*)&data->input[HALF_DIMS*side];

        for (k=0;k<nchunks;k++) {
            __m128i sum = ((__m128i*)&features[perspectives[side]])[k];
            __m128i t1 = _mm_adds_epi16(sum, k0x7f80);
            __m128i t2 = _mm_add_epi16(t1, k0x0080);
            out[k] = _mm_subs_epu16(t2, k0x8000);
        }
    }
}
#else
static void transformer_propagate(struct position *pos, struct net_data *data)
{
    struct feature_list active_features[NSIDES];
    int16_t             features[NSIDES][HALF_DIMS];
    int                 perspectives[NSIDES];
    int                 side;
    int                 k;
    int                 l;
    uint32_t            index;
    uint32_t            offset;
    int16_t             value;

    /* Find active features for each half */
    find_active_features(pos, WHITE, &active_features[0]);
    find_active_features(pos, BLACK, &active_features[1]);

    /* Process the neurons in each half */
    for (side=0;side<NSIDES;side++) {
        /* Add biases */
        for (k=0;k<HALF_DIMS;k++) {
            features[side][k] = feature_biases[k];
        }

        /* Summarize the weights for all active features */
        for (k=0;k<active_features[side].size;k++) {
            index = active_features[side].features[k];
            offset = HALF_DIMS*index;
            for (l=0;l<HALF_DIMS;l++) {
                features[side][l] += feature_weights[offset+l];
            }
        }
    }

    /*
     * Combine the two halves to form the inputs to the network. The
     * values are clamped to be in the range [0, 127].
     */
    perspectives[0] = pos->stm;
    perspectives[1] = FLIP_COLOR(pos->stm);
    for (side=0;side<NSIDES;side++) {
        offset = HALF_DIMS*side;
        for (k=0;k<HALF_DIMS;k++) {
            value = features[perspectives[side]][k];
            data->input[offset+k] = CLAMP(value, 0, 127);
        }
    }
}
#endif

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
    uint32_t hash;
    uint32_t size;
    char     architecture[NET_ARCHITECTURE_SIZE+1] = {0};

    version = read_uint32_le(iter);
    hash = read_uint32_le(iter+4);
    size = read_uint32_le(iter+8);
    if (size > NET_ARCHITECTURE_SIZE) {
        return false;
    }
    memcpy(architecture, iter+12, size);

    *data = iter + 12 + size;

    return (version == NET_VERSION) &&
           (hash == NET_HEADER_HASH) &&
           (size == NET_ARCHITECTURE_SIZE) &&
           !strcmp(architecture, NET_ARCHITECTURE);
}

static bool parse_transformer(uint8_t **data)
{
    uint8_t  *iter = *data;
    uint32_t hash;
    int      k;

    /* Read hash */
    hash = read_uint32_le(iter);
    if (hash != NET_TRANSFORMER_HASH) {
        return false;
    }
    iter += 4;

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
    uint8_t  *iter = *data;
    uint32_t hash;
    int8_t   temp;
    int      k;

    /* Read hash */
    hash = read_uint32_le(iter);
    if (hash != NET_NETWORK_HASH) {
        return false;
    }
    iter += 4;

    /* Read biases and weights of the first hidden layer */
    for (k=0;k<HIDDEN_LAYER_SIZE;k++,iter+=4) {
        hidden1_biases[k] = read_uint32_le(iter);
    }
    for (k=0;k<HIDDEN_LAYER_SIZE*FEATURE_OUT_DIMS;k++) {
        temp = *iter;
        hidden1_weights[k] = temp;
        iter++;
    }

    /* Read biases and weights of the second hidden layer */
    for (k=0;k<HIDDEN_LAYER_SIZE;k++,iter+=4) {
        hidden2_biases[k] = read_uint32_le(iter);
    }
    for (k=0;k<HIDDEN_LAYER_SIZE*HIDDEN_LAYER_SIZE;k++) {
        temp = *iter;
        hidden2_weights[k] = temp;
        iter++;
    }

    /* Read biases and weights of the output layer */
    for (k=0;k<OUTPUT_LAYER_SIZE;k++,iter+=4) {
        output_biases[k] = read_uint32_le(iter);
    }
    for (k=0;k<HIDDEN_LAYER_SIZE*OUTPUT_LAYER_SIZE;k++) {
        temp = *iter;
        output_weights[k] = temp;
        iter++;
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
