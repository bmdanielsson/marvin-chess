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

#ifdef USE_SSE
static int32_t hsum_4x32(__m128i v)
{
    v = _mm_add_epi32(v, _mm_srli_si128(v, 8));
    v = _mm_add_epi32(v, _mm_srli_si128(v, 4));
    return _mm_cvtsi128_si32(v);
}

void simd_linear_forward(uint8_t *input, int32_t *output, int ninputs,
                         int noutputs, int32_t *biases, int8_t *weights)
{
    int k;
    int l;
    int niterations = ninputs/16;

    __m128i c1 = _mm_set1_epi16(1);

    for (k=0;k<noutputs;k++) {
        output[k] = biases[k];

        __m128i vsum = _mm_setzero_si128();
        __m128i *p1 = (__m128i*)input;
        __m128i *p2 = (__m128i*)&weights[k*ninputs];

        for (l=0;l<niterations;l++) {
            __m128i v1 = _mm_load_si128(p1);
            __m128i v2 = _mm_load_si128(p2);

            __m128i temp = _mm_maddubs_epi16(v1, v2);
            temp = _mm_madd_epi16(temp, c1);
            vsum = _mm_add_epi32(vsum, temp);

            p1++;
            p2++;
        }

        output[k] += hsum_4x32(vsum);
    }
}
#endif

#ifdef USE_AVX2
static int32_t hsum_8x32(__m256i v)
{
    v = _mm256_add_epi32(v, _mm256_srli_si256(v, 8));
    v = _mm256_add_epi32(v, _mm256_srli_si256(v, 4));
    return _mm256_extract_epi32(v, 0) + _mm256_extract_epi32(v, 4);
}

void simd_linear_forward(uint8_t *input, int32_t *output, int ninputs,
                         int noutputs, int32_t *biases, int8_t *weights)
{
    int k;
    int l;
    int niterations = ninputs/32;

    __m256i c1 = _mm256_set1_epi16(1);

    for (k=0;k<noutputs;k++) {
        output[k] = biases[k];

        __m256i vsum = _mm256_setzero_si256();
        __m256i *pi = (__m256i*)input;
        __m256i *pw = (__m256i*)&weights[k*ninputs];

        for (l=0;l<niterations;l++) {
            __m256i v1 = _mm256_load_si256(pi);
            __m256i v2 = _mm256_load_si256(pw);

            __m256i t1 = _mm256_maddubs_epi16(v1, v2);
            __m256i t2 = _mm256_madd_epi16(t1, c1);

            vsum = _mm256_add_epi32(vsum, t2);

            pi++;
            pw++;
        }

        output[k] += hsum_8x32(vsum);
    }
}

#endif
