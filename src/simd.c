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

#if defined(USE_AVX2)
#define MIN_SIZE 32
#elif defined(USE_SSE)
#define MIN_SIZE 16
#elif defined(USE_NEON)
#define MIN_SIZE 16
#else
#define MIN_SIZE 1
#endif

#define MAX_QUANTIZED_ACTIVATION 127.0f

#if defined(USE_AVX2) || defined(USE_SSE)
static int32_t hsum_4x32(__m128i v)
{
    v = _mm_add_epi32(v, _mm_srli_si128(v, 8));
    v = _mm_add_epi32(v, _mm_srli_si128(v, 4));
    return _mm_cvtsi128_si32(v);
}
#endif

#if defined(USE_AVX2)
static int32_t hsum_8x32(__m256i v)
{
    __m128i sum128 = _mm_add_epi32(_mm256_castsi256_si128(v),
                                   _mm256_extracti128_si256(v, 1));
    return hsum_4x32(sum128);
}
#endif

void simd_fc_forward(uint8_t *input, int32_t *output, int ninputs,
                     int noutputs, int32_t *biases, int8_t *weights)
{
    int k;
    int l;
    int niterations = ninputs/MIN_SIZE;

    assert((ninputs%MIN_SIZE) == 0);

#if defined(USE_AVX2)
    __m256i c1 = _mm256_set1_epi16(1);

    for (k=0;k<noutputs;k++) {
        __m256i *pi = (__m256i*)input;
        __m256i *pw = (__m256i*)&weights[k*ninputs];

        __m256i v1 = _mm256_load_si256(pi++);
        __m256i v2 = _mm256_load_si256(pw++);
        __m256i t1 = _mm256_maddubs_epi16(v1, v2);
        __m256i t2 = _mm256_madd_epi16(t1, c1);
        __m256i vsum = t2;

        for (l=1;l<niterations;l++) {
            v1 = _mm256_load_si256(pi++);
            v2 = _mm256_load_si256(pw++);
            t1 = _mm256_maddubs_epi16(v1, v2);
            t2 = _mm256_madd_epi16(t1, c1);
            vsum = _mm256_add_epi32(vsum, t2);
        }

        output[k] = hsum_8x32(vsum) + biases[k];
    }
#elif defined(USE_SSE)
    __m128i c1 = _mm_set1_epi16(1);

    for (k=0;k<noutputs;k++) {
        __m128i *pi = (__m128i*)input;
        __m128i *pw = (__m128i*)&weights[k*ninputs];

        __m128i v1 = _mm_load_si128(pi++);
        __m128i v2 = _mm_load_si128(pw++);
        __m128i temp = _mm_maddubs_epi16(v1, v2);
        __m128i vsum = _mm_madd_epi16(temp, c1);

        for (l=1;l<niterations;l++) {
            v1 = _mm_load_si128(pi++);
            v2 = _mm_load_si128(pw++);
            temp = _mm_maddubs_epi16(v1, v2);
            temp = _mm_madd_epi16(temp, c1);
            vsum = _mm_add_epi32(vsum, temp);
        }

        output[k] = hsum_4x32(vsum) + biases[k];
    }
#elif defined(USE_NEON)
    for (k=0;k<noutputs;k++) {
        int8x8_t *pi = (int8x8_t*)input;
        int8x8_t *pw = (int8x8_t*)&weights[k*ninputs];

        int16x8_t temp = vmull_s8(*(pi++), *(pw++));
        temp = vmlal_s8(temp, *(pi++), *(pw++));
        int32x4_t vsum = vpaddlq_s16(temp);

        for (l=1;l<niterations;l++) {
            temp = vmull_s8(*(pi++), *(pw++));
            temp = vmlal_s8(temp, *(pi++), *(pw++));
            vsum = vpadalq_s16(vsum, temp);
        }

        output[k] = vaddvq_s32(vsum) + biases[k];
    }
#else
    for (k=0;k<noutputs;k++) {
        output[k] = biases[k];
        for (l=0;l<niterations;l++) {
            output[k] += (input[l]*weights[k*niterations+l]);
        }
    }
#endif
}

void simd_clamp(int16_t *input, uint8_t *output, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/32;

    __m256i *pi = (__m256i*)input;
    __m256i *po = (__m256i*)output;

    __m256i min = _mm256_set1_epi8(0);

    for (k=0;k<niterations;k++) {
        __m256i v1 = _mm256_load_si256(pi++);
        __m256i v2 = _mm256_load_si256(pi++);
        __m256i v8 = _mm256_packs_epi16(v1, v2);
        __m256i s = _mm256_permute4x64_epi64(v8, 0xD8);
        s = _mm256_max_epi8(s, min);
        _mm256_store_si256(po++, s);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/16;

    __m128i *pi = (__m128i*)input;
    __m128i *po = (__m128i*)output;

    __m128i min = _mm_set1_epi16(0);

    for (k=0;k<niterations;k++) {
        __m128i v1 = _mm_load_si128(pi++);
        __m128i v2 = _mm_load_si128(pi++);
        __m128i v8 = _mm_packs_epi16(v1, v2);
        __m128i s = _mm_max_epi8(v8, min);
        _mm_store_si128(po++, s);
    }
#elif defined(USE_NEON)
    int k;
    int niterations = nvalues/16;

    int16x8_t *pi = (int16x8_t*)input;
    int8x16_t *po = (int8x16_t*)output;

    int16x8_t min = vmovq_n_s16(0);
    int16x8_t max = vmovq_n_s16((int16_t)MAX_QUANTIZED_ACTIVATION);

    for (k=0;k<niterations;k++) {
        int16x8_t lower = vminq_s16(*(pi++), max);
        lower = vmaxq_s16(lower, min);

        int16x8_t upper = vminq_s16(*(pi++), max);
        upper = vmaxq_s16(upper, min);

        *(po++) = vcombine_s8(vmovn_s16(lower), vmovn_s16(upper));
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        output[k] = CLAMP(input[k], 0, (int)MAX_QUANTIZED_ACTIVATION);
    }
#endif
}

void simd_copy(int16_t *input, int16_t *output, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pi = (__m256i*)input;
    __m256i *po = (__m256i*)output;

    for (k=0;k<niterations;k++) {
        po[k] = _mm256_load_si256(pi++);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pi = (__m128i*)input;
    __m128i *po = (__m128i*)output;

    for (k=0;k<niterations;k++) {
        po[k] = _mm_load_si128(pi++);
    }
#elif defined(USE_NEON)
    int k;
    int niterations = nvalues/8;

    int16x8_t *po = (int16x8_t*)output;

    for (k=0;k<niterations;k++) {
        po[k] = vld1q_s16(input);
        input += 8;
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        output[k] = input[k];
    }
#endif
}

void simd_add(int16_t *input, int16_t *output, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pi = (__m256i*)input;
    __m256i *po = (__m256i*)output;

    for (k=0;k<niterations;k++) {
        po[k] = _mm256_add_epi16(po[k], pi[k]);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pi = (__m128i*)input;
    __m128i *po = (__m128i*)output;

    for (k=0;k<niterations;k++) {
        po[k] = _mm_add_epi16(po[k], pi[k]);
    }
#elif defined(USE_NEON)
    int k;
    int niterations = nvalues/8;

    int16x8_t *pi = (int16x8_t*)input;
    int16x8_t *po = (int16x8_t*)output;

    for (k=0;k<niterations;k++) {
        po[k] = vaddq_s16(po[k], pi[k]);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        output[k] += input[k];
    }
#endif
}

void simd_sub(int16_t *input, int16_t *output, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pi = (__m256i*)input;
    __m256i *po = (__m256i*)output;

    for (k=0;k<niterations;k++) {
        po[k] = _mm256_sub_epi16(po[k], pi[k]);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pi = (__m128i*)input;
    __m128i *po = (__m128i*)output;

    for (k=0;k<niterations;k++) {
        po[k] = _mm_sub_epi16(po[k], pi[k]);
    }
#elif defined(USE_NEON)
    int k;
    int niterations = nvalues/8;

    int16x8_t *pi = (int16x8_t*)input;
    int16x8_t *po = (int16x8_t*)output;

    for (k=0;k<niterations;k++) {
        po[k] = vsubq_s16(po[k], pi[k]);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        output[k] -= input[k];
    }
#endif
}
