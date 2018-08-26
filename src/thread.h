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
#ifndef THREAD_H
#define THREAD_H

#ifdef WINDOWS
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <stdbool.h>

/* Portable multi-threading primitives */
#ifdef WINDOWS
typedef HANDLE thread_t;
typedef LPTHREAD_START_ROUTINE thread_func_t;
typedef CRITICAL_SECTION mutex_t;
typedef HANDLE event_t;
#else
typedef pthread_t thread_t;
typedef void* (*thread_func_t)(void*);;  
typedef pthread_mutex_t mutex_t;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            is_set;
} event_t;
#endif

/*
 * Create and start a new thread.
 *
 * @param thread Variable where the thread handle is stored.
 * @param func The start function of the thread.
 * @param data Data passed to the thread function.
 */
void thread_create(thread_t *thread, thread_func_t func, void *data);

/*
 * Wait for a thread to exit.
 *
 * @param thread The thread to wait for.
 */
void thread_join(thread_t *thread);

/*
 * Initialize a mutex.
 *
 * @param mutex The mutex to initialize.
 */
void mutex_init(mutex_t *mutex);

/*
 * Destroy a mutex.
 *
 * @param mutex The mutex to destroy.
 */
void mutex_destroy(mutex_t *mutex);

/*
 * Lock a mutex.
 *
 * @param mutex The mutex to lock.
 */
void mutex_lock(mutex_t *mutex);

/*
 * Unlock a mutex.
 *
 * @param mutex The mutex to unlock.
 */
void mutex_unlock(mutex_t *mutex);

/*
 * Initialize an event.
 *
 * @param event The event to initialize.
 */
void event_init(event_t *event);

/*
 * Destroy an event.
 *
 * @param event The event to destroy.
 */
void event_destroy(event_t *event);

/*
 * Set an event.
 *
 * @param event The event to set.
 */
void event_set(event_t *event);

/*
 * Reset an event.
 *
 * @param event The event to reset.
 */
void event_reset(event_t *event);

/*
 * Wait for an event.
 *
 * @param event The event to wait for.
 */
void event_wait(event_t *event);

#endif
