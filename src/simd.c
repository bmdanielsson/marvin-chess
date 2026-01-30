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
#include <stdlib.h>
#include <stdalign.h>
#ifdef USE_SSE
#include <emmintrin.h>
#include <tmmintrin.h>
#include <smmintrin.h>
#endif
#ifdef USE_AVX2
#include <immintrin.h>
#endif
#ifdef USE_NEON
#include <arm_neon.h>
#endif

#include "simd.h"
#include "utils.h"
#include "types.h"

#if defined(USE_AVX2)
#define NUM_REGS 16
#elif defined(USE_SSE)
#define NUM_REGS 8
#endif

#if defined(USE_AVX2)

static int32_t hsum_8x32(__m256i v)
{
    v = _mm256_add_epi32(v, _mm256_srli_si256(v, 8));
    v = _mm256_add_epi32(v, _mm256_srli_si256(v, 4));
    return _mm256_extract_epi32(v, 0) + _mm256_extract_epi32(v, 4);
}

int32_t simd_fully_connected(int16_t *inputs, int16_t *weights)
{
    int k;
    int niterations = NNUE_HIDDEN_LAYER_SIZE/NUM_REGS;

    __m256i vmin = _mm256_set1_epi16(0);
    __m256i vmax = _mm256_set1_epi16(NNUE_QUANT_QA);

    __m256i vsum = _mm256_setzero_si256();

    __m256i *pi = (__m256i*)inputs;
    __m256i *pw = (__m256i*)weights;

    for (k=0;k<niterations;k++,pi++,pw++) {
        __m256i vi = _mm256_load_si256(pi);
        __m256i vw = _mm256_load_si256(pw);

        __m256i vclamp = _mm256_max_epi16(vi, vmin);
        vclamp = _mm256_min_epi16(vclamp, vmax);

        __m256i vprod = _mm256_mullo_epi16(vclamp, vw);
        __m256i vres =  _mm256_madd_epi16(vprod, vclamp);

        vsum = _mm256_add_epi32(vsum, vres);
    }

    return hsum_8x32(vsum);
}

void simd_add(int16_t *inputs, int16_t *outputs)
{
    int k;
    int niterations = NNUE_HIDDEN_LAYER_SIZE/NUM_REGS;

    __m256i *pi = (__m256i*)inputs;
    __m256i *po = (__m256i*)outputs;

    for (k=0;k<niterations;k++) {
        po[k] = _mm256_add_epi16(po[k], pi[k]);
    }
}

void simd_sub(int16_t *inputs, int16_t *outputs)
{
    int k;
    int niterations = NNUE_HIDDEN_LAYER_SIZE/NUM_REGS;

    __m256i *pi = (__m256i*)inputs;
    __m256i *po = (__m256i*)outputs;

    for (k=0;k<niterations;k++) {
        po[k] = _mm256_sub_epi16(po[k], pi[k]);
    }
}

#elif defined(USE_SSE)

static int32_t hsum_4x32(__m128i v)
{
    v = _mm_add_epi32(v, _mm_srli_si128(v, 8));
    v = _mm_add_epi32(v, _mm_srli_si128(v, 4));
    return _mm_cvtsi128_si32(v);
}

int32_t simd_fully_connected(int16_t *inputs, int16_t *weights)
{
    int k;
    int niterations = NNUE_HIDDEN_LAYER_SIZE/NUM_REGS;

    __m128i vmin = _mm_set1_epi16(0);
    __m128i vmax = _mm_set1_epi16(NNUE_QUANT_QA);

    __m128i vsum = _mm_setzero_si128();

    __m128i *pi = (__m128i*)inputs;
    __m128i *pw = (__m128i*)weights;

    for (k=0;k<niterations;k++,pi++,pw++) {
        __m128i vi = _mm_load_si128(pi);
        __m128i vw = _mm_load_si128(pw);

        __m128i vclamp = _mm_max_epi16(vi, vmin);
        vclamp = _mm_min_epi16(vclamp, vmax);

        __m128i vprod = _mm_mullo_epi16(vclamp, vw);
        __m128i vres =  _mm_madd_epi16(vprod, vclamp);

        vsum = _mm_add_epi32(vsum, vres);
    }

    return hsum_4x32(vsum);
}

void simd_add(int16_t *inputs, int16_t *outputs)
{
    int k;
    int niterations = NNUE_HIDDEN_LAYER_SIZE/NUM_REGS;

    __m128i *pi = (__m128i*)inputs;
    __m128i *po = (__m128i*)outputs;

    for (k=0;k<niterations;k++) {
        po[k] = _mm_add_epi16(po[k], pi[k]);
    }
}

void simd_sub(int16_t *inputs, int16_t *outputs)
{
    int k;
    int niterations = NNUE_HIDDEN_LAYER_SIZE/NUM_REGS;

    __m128i *pi = (__m128i*)inputs;
    __m128i *po = (__m128i*)outputs;

    for (k=0;k<niterations;k++) {
        po[k] = _mm_sub_epi16(po[k], pi[k]);
    }
}

#else

static int32_t screlu(int16_t input)
{
    int16_t val;

    val = CLAMP(input, 0, NNUE_QUANT_QA);
    return val*val;
}

int32_t simd_fully_connected(int16_t *inputs, int16_t *weights)
{
    int32_t output = 0;
    int     k;

    for (k=0;k<NNUE_HIDDEN_LAYER_SIZE;k++) {
        output += screlu(inputs[k])*weights[k];
    }

    return output;
}

void simd_add(int16_t *inputs, int16_t *outputs)
{
    int k;

    for (k=0;k<NNUE_HIDDEN_LAYER_SIZE;k++) {
        outputs[k] += inputs[k];
    }
}

void simd_sub(int16_t *inputs, int16_t *outputs)
{
    int k;

    for (k=0;k<NNUE_HIDDEN_LAYER_SIZE;k++) {
        outputs[k] -= inputs[k];
    }
}
#endif
