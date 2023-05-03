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
 * Calculate a padded size.
 *
 * @param size The actual size.
 * @return Return the size after padding.
 */
int simd_pad_size(int size);

/*
 * SIMD implementation of a forward pass of a fully connected layer.
 * 
 * @param input The layer input. 
 * @param output The layer output. 
 * @param ninputs The number of inputs. 
 * @param noutputs The number of outputs.
 * @param biases Layer biases.
 * @param weights Layer weights.
 */
void simd_fc_forward(uint8_t *input, int32_t *output, int ninputs,
                     int noutputs, int32_t *biases, int8_t *weights);


/*
 * SIMD implementation of a scale-and-clamp operation. Values are scaled down
 * and the clamped between 0 and 127.
 *
 * @param input Input values.
 * @param output Output values.
 * @param shift Value to scale with.
 * @param nvalues The number of values.
 */
void simd_scale_and_clamp(int32_t *input, uint8_t *output, int shift,
                          int nvalues);

/*
 * SIMD implementation of a clamp operation. Values and the clamped between 0
 * and 127.
 *
 * @param input Input values.
 * @param output Output values.
 * @param nvalues The number of values.
 */
void simd_clamp(int16_t *input, uint8_t *output, int nvalues);

/*
 * SIMD implementation of a copy operation.
 *
 * @param input Input values.
 * @param output Output values.
 * @param nvalues The number of values.
 */
void simd_copy(int16_t *input, int16_t *output, int nvalues);

/*
 * SIMD implementation of an add operation.
 *
 * @param input Input values.
 * @param output Output values.
 * @param nvalues The number of values.
 */
void simd_add(int16_t *input, int16_t *output, int nvalues);

/*
 * SIMD implementation of a sub operation.
 *
 * @param input Input values.
 * @param output Output values.
 * @param nvalues The number of values.
 */
void simd_sub(int16_t *input, int16_t *output, int nvalues);

#endif
