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
#define MATCH(s1, s2)   !strncmp((s1), (s2), strlen(s2))

/* The cache line size */
#define CACHE_LINE_SIZE 64

/* Macro for prefetching the data at an address in to the cache */
#ifdef __GNUC__
#define PREFETCH_ADDRESS(a) __builtin_prefetch((a))
#else
#define PREFETCH_ADDRESS(a)
#endif

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
 *          function to work.
 * @return Returns the index (0..63) of the least significant bit.
 */
int bitscan_forward(uint64_t v);

/*
 * Find the most significant bit.
 *
 * @param v The value to scan. At least one bit must be set for the
 *          function to work.
 * @return Returns the index (0..63) of the most significant bit.
 */
int bitscan_reverse(uint64_t v);

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
 * Read a 16-bit unsigned integer in little endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint16_t read_uint16_le(uint8_t *buffer);

/*
 * Read a 16-bit unsigned integer in big endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint16_t read_uint16_be(uint8_t *buffer);

/*
 * Read a 32-bit unsigned integer in little endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint32_t read_uint32_le(uint8_t *buffer);

/*
 * Read a 32-bit unsigned integer in big endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint32_t read_uint32_be(uint8_t *buffer);

/*
 * Reads a 64-bit unsigned integer from a file in little endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint64_t read_uint64_le(uint8_t *buffer);

/*
 * Reads a 64-bit unsigned integer from a file in little endian format.
 *
 * @param buffer The buffer to read from.
 * @return Returns the read value in the native endian format.
 */
uint64_t read_uint64_be(uint8_t *buffer);

/*
 * Remove leading white space from a string.
 *
 * @param str The string to remove white spec from.
 * @return Returns a pointer to the first non-space character in the string.
 */
char* skip_whitespace(char *str);

/*
 * Allocate memory with a specific alignment.
 *
 * @param alignment The required alignment.
 * @param size The amount of memory to allocate.
 * @return Returns a pointer to the allocated memory.
 */
void* aligned_malloc(int alignment, uint64_t size);

/*
 * Free memory allocated with aligned_malloc.
 *
 * @param ptr Pointer to the memory to free.
 */
void aligned_free(void *ptr);

/*
 * Parallel version of memset.
 *
 * @param memory Pointer to the memory to set.
 * @param value The value to write
 * @param size The number of bytes to write.
 * @param nthreads The number of threads to use.
 */
void parallel_memset(void *memory, uint8_t value, size_t size, int nthreads);

/*
 * Check this is a 64-bit build.
 *
 * @return Returns true if this is a 64-bit build.
 */
bool is64bit(void);

/*
 * Get the size of a file in a portable way.
 *
 * @param file The file.
 * @return The size of the file, or 0xFFFFFFFF in case of error.
 */
uint32_t get_file_size(char *file);

#endif
