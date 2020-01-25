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
#include "thread.h"

#ifdef WINDOWS
void thread_create(thread_t *thread, thread_func_t func, void *data)
{
    *thread = CreateThread(NULL, 0, func, data, 0, NULL);
}

void thread_join(thread_t *thread)
{
    WaitForSingleObject(*thread, INFINITE);
	CloseHandle(*thread);
}

void mutex_init(mutex_t *mutex)
{
    InitializeCriticalSection(mutex);
}

void mutex_destroy(mutex_t *mutex)
{
    DeleteCriticalSection(mutex);
}

void mutex_lock(mutex_t *mutex)
{
    EnterCriticalSection(mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    LeaveCriticalSection(mutex);
}

void event_init(event_t *event)
{
    *event = CreateEvent(NULL, FALSE, FALSE, NULL);
	ResetEvent(*event);
}

void event_destroy(event_t *event)
{
    CloseHandle(*event);
}

void event_set(event_t *event)
{
    SetEvent(*event);
}

void event_reset(event_t *event)
{
    ResetEvent(*event);
}

void event_wait(event_t *event)
{
    WaitForSingleObject(*event, INFINITE);
}
#else
void thread_create(thread_t *thread, thread_func_t func, void *data)
{
    (void)pthread_create(thread, NULL, func, data);
}

void thread_join(thread_t *thread)
{
    (void)pthread_join(*thread, NULL);
}

void mutex_init(mutex_t *mutex)
{
    (void)pthread_mutex_init(mutex, NULL);
}

void mutex_destroy(mutex_t *mutex)
{
    (void)pthread_mutex_destroy(mutex);
}

void mutex_lock(mutex_t *mutex)
{
    (void)pthread_mutex_lock(mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    (void)pthread_mutex_unlock(mutex);
}

void event_init(event_t *event)
{
    pthread_mutex_init(&event->mutex, NULL);
    pthread_cond_init(&event->cond, NULL);
    event->is_set = false;
}

void event_destroy(event_t *event)
{
    pthread_mutex_destroy(&event->mutex);
    pthread_cond_destroy(&event->cond);
    event->is_set = false;
}

void event_set(event_t *event)
{
    pthread_mutex_lock(&event->mutex);
    event->is_set = true;
    pthread_cond_signal(&event->cond);
    pthread_mutex_unlock(&event->mutex);
}

void event_reset(event_t *event)
{
    pthread_mutex_lock(&event->mutex);
    event->is_set = false;
    pthread_mutex_unlock(&event->mutex);
}

void event_wait(event_t *event)
{
    pthread_mutex_lock(&event->mutex);
    while (!event->is_set) {
        pthread_cond_wait(&event->cond, &event->mutex);
    }
    event->is_set = false;
    pthread_mutex_unlock(&event->mutex);
}
#endif
