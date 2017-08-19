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
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#if defined(WINDOWS)
#include <windows.h>
#include <sys/timeb.h>
#include <process.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#endif

#include "utils.h"

#if HAS_POPCNT && __GNUC__
int pop_count (uint64_t v)
{
    return __builtin_popcountll(v);
}
#else
/*
 * This function is taken from https://chessprogramming.wikispaces.com/Population+Count.
 * Implemantation by Donald Knuth.
 */
const uint64_t k1 = 0x5555555555555555ULL;
const uint64_t k2 = 0x3333333333333333ULL;
const uint64_t k4 = 0x0f0f0f0f0f0f0f0fULL;
const uint64_t kf = 0x0101010101010101ULL;
int pop_count (uint64_t v)
{
    v =  v - ((v >> 1) & k1);
    v = (v & k2) + ((v >> 2) & k2);
    v = (v + (v >> 4)) & k4 ;
    v = (v * kf) >> 56;
    return (int) v;
}
#endif

#ifdef __GNUC__
int bitscan_forward(uint64_t v)
{
    assert(v != 0);

    return __builtin_ctzll(v);
}
#else
/*
 * This function is taken from https://chessprogramming.wikispaces.com/BitScan.
 * Implementation by Charles E. Leiserson, Harald Prokop and Keith H. Randall.
 */
const int index64[64] = {
    63,  0, 58,  1, 59, 47, 53,  2,
    60, 39, 48, 27, 54, 33, 42,  3,
    61, 51, 37, 40, 49, 18, 28, 20,
    55, 30, 34, 11, 43, 14, 22,  4,
    62, 57, 46, 52, 38, 26, 32, 41,
    50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16,  9, 12,
    44, 24, 15,  8, 23,  7,  6,  5
};
int bitscan_forward(uint64_t v)
{
    const uint64_t debruijn64 = 0x07EDD5E59A4E28C2ULL;

    assert(v != 0);

    return index64[((v&-v)*debruijn64)>>58];
}
#endif

int pop_bit(uint64_t *v)
{
    int index;

    assert(v != NULL);

    index = bitscan_forward(*v);
    *v &= ~(1ULL << index);
    return index;
}

time_t get_current_time(void)
{
#ifdef WINDOWS
    struct timeb tb;

    ftime(&tb);
    return tb.time*1000 + tb.millitm;
#else
    struct timeval tv;

    /*
     * The ftime function is marked as deprecated on Mac OS X and
     * Linux so gettimeofday is used instead.
     */
    gettimeofday(&tv, NULL);
    return tv.tv_sec*1000 + tv.tv_usec/1000;
#endif
}

int get_current_pid(void)
{
#ifdef WINDOWS
    return _getpid();
#else
    return getpid();
#endif
}

bool poll_input(void)
{
#ifdef WINDOWS
    HANDLE handle;
    DWORD  temp;

    handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        return FALSE;
    if (GetConsoleMode(handle, &temp)) {
        SetConsoleMode(handle, ENABLE_ECHO_INPUT|ENABLE_LINE_INPUT|
                       ENABLE_PROCESSED_INPUT);
        FlushConsoleInputBuffer(handle);
        if (!GetNumberOfConsoleInputEvents(handle, &temp))
            return FALSE;
        return (bool)(temp > 1);
    } else {
        if (!PeekNamedPipe(handle, NULL, 0, NULL, &temp, NULL)) {
            return FALSE;
        }
        return (bool)(temp != 0);
    }
#else
    fd_set         set;
    struct timeval tv;

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return (select(STDIN_FILENO+1, &set, NULL, NULL, &tv) > 0);
#endif
}

void sleep_ms(int ms)
{
#ifdef WINDOWS
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = ms/1000;
    ts.tv_nsec = (ms%1000)*1000000;
    nanosleep(&ts, NULL);
#endif
}

uint16_t read_uint16(uint8_t *buffer)
{
    uint16_t val;
    uint8_t  *p = (uint8_t*)&val;

#ifndef TARGET_BIG_ENDIAN
    p[1] = buffer[0];
    p[0] = buffer[1];
#else
    p[0] = buffer[0];
    p[1] = buffer[1];
#endif
    return val;
}

uint32_t read_uint32(uint8_t *buffer)
{
    uint32_t val;
    uint8_t  *p = (uint8_t*)&val;

#ifndef TARGET_BIG_ENDIAN
    p[3] = buffer[0];
    p[2] = buffer[1];
    p[1] = buffer[2];
    p[0] = buffer[3];
#else
    p[0] = buffer[0];
    p[1] = buffer[1];
    p[2] = buffer[2];
    p[3] = buffer[3];
#endif
    return val;
}

uint64_t read_uint64(uint8_t *buffer)
{
    uint64_t val;
    uint8_t  *p = (uint8_t*)&val;

#ifndef TARGET_BIG_ENDIAN
    p[7] = buffer[0];
    p[6] = buffer[1];
    p[5] = buffer[2];
    p[4] = buffer[3];
    p[3] = buffer[4];
    p[2] = buffer[5];
    p[1] = buffer[6];
    p[0] = buffer[7];
#else
    p[0] = buffer[0];
    p[1] = buffer[1];
    p[2] = buffer[2];
    p[3] = buffer[3];
    p[4] = buffer[4];
    p[5] = buffer[5];
    p[6] = buffer[6];
    p[7] = buffer[7];
#endif
    return val;
}

char* skip_whitespace(char *str)
{
    while (isspace(*str)) {
        str++;
    }
    return str;
}

void* aligned_malloc(int alignment, int size)
{
#ifdef HAS_ALIGNED_MALLOC
#ifdef WINDOWS
    return _aligned_malloc(size, alignment);
#else
    void *ptr;
    if (posix_memalign(&ptr, alignment, size)) {
        ptr = NULL;
    }
    return ptr;
#endif
#else
    (void)alignment;
    return malloc(size);
#endif
}

void aligned_free(void *ptr)
{
#ifdef HAS_ALIGNED_MALLOC
#ifdef WINDOWS
    _aligned_free(ptr);
#else
    free(ptr);
#endif
#else
    free(ptr);
#endif
}
