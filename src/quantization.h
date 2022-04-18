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
#ifndef QUANTIZATION_H
#define QUANTIZATION_H

#include <stdint.h>

#define NNUE2SCORE                  600.0f
#define MAX_QUANTIZED_ACTIVATION    127.0f
#define WEIGHT_SCALE_BITS           6
#define OUTPUT_SCALE                16.0f

/*
 * Quantize a halfkx layer weight
 *
 * @param v The value to quantize.
 * @return Return the quantized value.
 */
int16_t quant_halfkx_weight(float v);

/*
 * Quantize a halfkx layer bias.
 *
 * @param v The value to quantize.
 * @return Return the quantized value.
 */
int16_t quant_halfkx_bias(float v);

/*
 * Quantize a hidden layer wieght.
 *
 * @param v The value to quantize.
 * @return Return the quantized value.
 */
int8_t quant_hidden_weight(float v);

/*
 * Quantize a hidden layer bias.
 *
 * @param v The value to quantize.
 * @return Return the quantized value.
 */
int32_t quant_hidden_bias(float v);

/*
 * Quantize an output layer weight.
 *
 * @param v The value to quantize.
 * @return Return the quantized value.
 */
int8_t quant_output_weight(float v);

/*
 * Quantize an output layer bias.
 *
 * @param v The value to quantize.
 * @return Return the quantized value.
 */
int32_t quant_output_bias(float v);

#endif
