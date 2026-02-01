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
#ifndef SIMD_H
#define SIMD_H

#include <stdint.h>

/*
 * SIMD implementation of a fully connected layer with one output and a
 * SCReLU activation function.
 * 
 * @param inputs The layer input.
 * @param weights Layer weights.
 * @return Returns the output.
 */
int32_t simd_fully_connected(int16_t *inputs, int16_t *weights);

/*
 * SIMD implementation of an add operation.
 *
 * @param inputs Input values.
 * @param outputs Output values.
 */
void simd_add(int16_t *inputs, int16_t *outputs);

/*
 * SIMD implementation of a sub operation.
 *
 * @param inputs Input values.
 * @param outputs Output values.
 */
void simd_sub(int16_t *inputs, int16_t *output);

/*
 * SIMD implementation of a fused add + sub operation.
 *
 * @param input The input.
 * @param output The output.
 * @param add The add weights.
 * @output sub The sub weights.
 */
void simd_add_sub(int16_t* input, int16_t *output, int16_t *add, int16_t *sub);

/*
 * SIMD implementation of a fused add + sub + sub operation.
 *
 * @param input The input.
 * @param output The output.
 * @param add The add weights.
 * @output sub1 The first sub weights.
 * @output sub2 The second sub weights.
 */
void simd_add_sub2(int16_t* input, int16_t *output, int16_t *add, int16_t *sub1,
                   int16_t *sub2);

/*
 * SIMD implementation of a fused add + add + sub + sub operation.
 *
 * @param input The input.
 * @param output The output.
 * @param add1 The first add weights.
 * @param add2 The second add weights.
 * @output sub1 The first sub weights.
 * @output sub2 The second sub weights.
 */
void simd_add2_sub2(int16_t* input, int16_t *output, int16_t *add1,
                    int16_t *add2, int16_t *sub1, int16_t *sub2);

#endif
