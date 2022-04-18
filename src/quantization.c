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
 * This file implements quantization for weights and biases in NNUE
 * networks. The current scheme is based on the scheme used by Stockfish.
 */
#include <math.h>

#include "quantization.h"
#include "utils.h"

#define HALFKX_WEIGHT_SCALE     MAX_QUANTIZED_ACTIVATION
#define HALFKX_BIAS_SCALE       MAX_QUANTIZED_ACTIVATION
#define HIDDEN_WEIGHT_SCALE     (1<<WEIGHT_SCALE_BITS)
#define HIDDEN_BIAS_SCALE       (1<<WEIGHT_SCALE_BITS)*MAX_QUANTIZED_ACTIVATION
#define OUTPUT_WEIGHT_SCALE     OUTPUT_SCALE*NNUE2SCORE/MAX_QUANTIZED_ACTIVATION
#define OUTPUT_BIAS_SCALE       OUTPUT_SCALE*NNUE2SCORE
#define MAX_WEIGHT              MAX_QUANTIZED_ACTIVATION/(1<<WEIGHT_SCALE_BITS)

int16_t quant_halfkx_weight(float v)
{
    return (int16_t)rintf(v*HALFKX_WEIGHT_SCALE);
}

int16_t quant_halfkx_bias(float v)
{
    return (int16_t)rintf(v*HALFKX_BIAS_SCALE);
}

int8_t quant_hidden_weight(float v)
{
    v = CLAMP(v, -1*MAX_WEIGHT, MAX_WEIGHT);
    return (int8_t)rintf(v*HIDDEN_WEIGHT_SCALE);
}

int32_t quant_hidden_bias(float v)
{
    return (int32_t)rintf(v*HIDDEN_BIAS_SCALE);
}

int8_t quant_output_weight(float v)
{
    v = CLAMP(v, -1*MAX_WEIGHT, MAX_WEIGHT);
    return (int8_t)rintf(v*OUTPUT_WEIGHT_SCALE);
}

int32_t quant_output_bias(float v)
{
    return (int32_t)rintf(v*OUTPUT_BIAS_SCALE);
}
