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

#include "simd.h"
#include "utils.h"
#include "quantization.h"

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

void simd_linear_forward(uint8_t *input, int32_t *output, int ninputs,
                         int noutputs, int32_t *biases, int8_t *weights)
{
#if defined(USE_AVX2)
    int k;
    int l;
    int niterations = ninputs/32;

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
    int k;
    int l;
    int niterations = ninputs/16;

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
#else
    int k;
    int l;

    for (k=0;k<noutputs;k++) {
        output[k] = biases[k];
        for (l=0;l<ninputs;l++) {
            output[k] += (input[l]*weights[k*ninputs+l]);
        }
    }
#endif
}

void simd_scale_and_clamp(int32_t *input, uint8_t *output, int shift,
                          int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/8;

    __m256i *pi = (__m256i*)input;
    __m256i *po = (__m256i*)output;

    __m256i c0 = _mm256_set1_epi8(0);
    __m256i idx = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);

    for (k=0;k<niterations/4;k++) {
        __m256i v1 = _mm256_load_si256(pi++);
        __m256i v2 = _mm256_load_si256(pi++);
        __m256i v16_1 = _mm256_packs_epi32(v1, v2);
        v16_1 = _mm256_srai_epi16(v16_1, shift);

        v1 = _mm256_loadu_si256(pi++);
        v2 = _mm256_loadu_si256(pi++);
        __m256i v16_2 = _mm256_packs_epi32(v1, v2);
        v16_2 = _mm256_srai_epi16(v16_2, shift);

        __m256i v8 = _mm256_packs_epi16(v16_1, v16_2);
        __m256i s = _mm256_permutevar8x32_epi32(v8, idx);
        s = _mm256_max_epi8(s, c0);
        _mm256_store_si256(po++, s);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/4;

    __m128i *pi = (__m128i*)input;
    __m128i *po = (__m128i*)output;

    __m128i c0 = _mm_set1_epi32(0);
    __m128i c127 = _mm_set1_epi32((int)MAX_QUANTIZED_ACTIVATION);

    for (k=0;k<niterations/4;k++) {
        __m128i v1 = _mm_load_si128(pi++);
        v1 = _mm_srai_epi32(v1, shift);
        v1 = _mm_max_epi32(v1, c0);
        v1 = _mm_min_epi32(v1, c127);

        __m128i v2 = _mm_load_si128(pi++);
        v2 = _mm_srai_epi32(v2, shift);
        v2 = _mm_max_epi32(v2, c0);
        v2 = _mm_min_epi32(v2, c127);

        __m128i o1 =  _mm_packs_epi32(v1, v2);
        v1 = _mm_load_si128(pi++);
        v1 = _mm_srai_epi32(v1, shift);
        v1 = _mm_max_epi32(v1, c0);
        v1 = _mm_min_epi32(v1, c127);

        v2 = _mm_load_si128(pi++);
        v2 = _mm_srai_epi32(v2, shift);
        v2 = _mm_max_epi32(v2, c0);
        v2 = _mm_min_epi32(v2, c127);

        __m128i o2 =  _mm_packs_epi32(v1, v2);
        __m128i o =  _mm_packs_epi16(o1, o2);
        _mm_store_si128(po++, o);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        output[k] = CLAMP((input[k]>>shift), 0, (int)MAX_QUANTIZED_ACTIVATION);
    }
#endif
}

void simd_clamp(int16_t *input, uint8_t *output, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pi = (__m256i*)input;
    __m256i *po = (__m256i*)output;

    __m256i c0 = _mm256_set1_epi8(0);

    for (k=0;k<niterations/2;k++) {
        __m256i v1 = _mm256_load_si256(pi++);
        __m256i v2 = _mm256_load_si256(pi++);
        __m256i v8 = _mm256_packs_epi16(v1, v2);
        __m256i s = _mm256_permute4x64_epi64(v8, 0xD8);
        s = _mm256_max_epi8(s, c0);
        _mm256_store_si256(po++, s);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pi = (__m128i*)input;
    __m128i *po = (__m128i*)output;

    __m128i c0 = _mm_set1_epi16(0);

    for (k=0;k<niterations/2;k++) {
        __m128i v1 = _mm_load_si128(pi++);
        __m128i v2 = _mm_load_si128(pi++);
        __m128i v8 = _mm_packs_epi16(v1, v2);
        __m128i s = _mm_max_epi8(v8, c0);
        _mm_store_si128(po++, s);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        output[k] = CLAMP(input[k], 0, (int)MAX_QUANTIZED_ACTIVATION);
    }
#endif
}

void simd_copy(int16_t *from, int16_t *to, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pf = (__m256i*)from;
    __m256i *pt = (__m256i*)to;

    for (k=0;k<niterations;k++) {
        pt[k] = _mm256_load_si256(pf++);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pf = (__m128i*)from;
    __m128i *pt = (__m128i*)to;

    for (k=0;k<niterations;k++) {
        pt[k] = _mm_load_si128(pf++);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        to[k] = from[k];
    }
#endif
}

void simd_add(int16_t *from, int16_t *to, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pf = (__m256i*)from;
    __m256i *pt = (__m256i*)to;

    for (k=0;k<niterations;k++) {
        pt[k] = _mm256_add_epi16(pf[k], pt[k]);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pf = (__m128i*)from;
    __m128i *pt = (__m128i*)to;

    for (k=0;k<niterations;k++) {
        pt[k] = _mm_add_epi16(pf[k], pt[k]);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        to[k] += from[k];
    }
#endif
}

void simd_sub(int16_t *from, int16_t *to, int nvalues)
{
#if defined(USE_AVX2)
    int k;
    int niterations = nvalues/16;

    __m256i *pf = (__m256i*)from;
    __m256i *pt = (__m256i*)to;

    for (k=0;k<niterations;k++) {
        pt[k] = _mm256_sub_epi16(pt[k], pf[k]);
    }
#elif defined(USE_SSE)
    int k;
    int niterations = nvalues/8;

    __m128i *pf = (__m128i*)from;
    __m128i *pt = (__m128i*)to;

    for (k=0;k<niterations;k++) {
        pt[k] = _mm_sub_epi16(pt[k], pf[k]);
    }
#else
    int k;

    for (k=0;k<nvalues;k++) {
        to[k] -= from[k];
    }
#endif
}
