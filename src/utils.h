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
#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Utility macros */
#define MIN(a, b)       ((a) < (b))?(a):(b)
#define MAX(a, b)       ((a) > (b))?(a):(b)
#define CLAMP(x, a, b)  MAX((a), MIN((x), (b)))

/*
 * Calculate the number of bits that are set in a 64-bit value.
 *
 * @param v The value to count the number of bits for.
 * @return Returns the number of bits that are set.
 */
int pop_count(uint64_t v);

/*
 * Find the least significant bit.
 *
 * @param v The value to scan. At least one bit must be set for the
 *           function to work.
 * @return Returns the index (0..63) of the least significant bit.
 */
int bitscan_forward(uint64_t v);

/*
 * Return the index of a set bit from a value a then clear it.
 *
 * @param v The value to pop a bit from.
 * @return Returns the index of the popped bit.
 */
int pop_bit(uint64_t *v);

/*
 * Get the current time in a portable way.
 *
 * @return Returns the current time in milliseconds.
 */
time_t get_current_time(void);

/*
 * Get the PID of the calling process.
 *
 * @return Return the PID of the calling process.
 */
int get_current_pid(void);

/*
 * Function to check for input in a portable way.
 *
 * @return Returns true if there is a command waiting.
 */
bool poll_input(void);

/*
 * Sleep for a specified number of milliseconds.
 *
 * @param ms The number of milliseconds to sleep.
 */
void sleep_ms(int ms);

/*
 * Read a 16-bit unsigned integer in big endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint16_t read_uint16(uint8_t *buffer);

/*
 * Read a 32-bit unsigned integer in big endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint32_t read_uint32(uint8_t *buffer);

/*
 * Reads a 64-bit unsigned integer from a file in big-endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint64_t read_uint64(uint8_t *buffer);

/*
 * Remove leading white space from a string.
 *
 * @param str The string to remove white spec from.
 * @return Returns a pointer to the first non-space character in the string.
 */
char* skip_whitespace(char *str);

#endif
