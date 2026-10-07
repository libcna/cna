/* SPDX-License-Identifier: MS-PL */

/*
 * plans/plan_apple_m4.md AM4-072 -- C11 threads for the pure-C suites on every C library.
 *
 * The suites start a second thread to call the C API from where it must refuse, and use C11's
 * <threads.h> to do it. Apple's C library has no <threads.h> at all (the compiler advertises
 * __STDC_NO_THREADS__), so 41 suites could not compile on macOS or iOS. Where the header exists
 * this file is exactly that header; elsewhere it maps the part the suites use -- thrd_t,
 * thrd_create, thrd_join, thrd_sleep and the thrd_* results -- onto POSIX threads with C11's
 * meaning.
 */

#ifndef CNA_C_API_TEST_THREADS_H
#define CNA_C_API_TEST_THREADS_H

#if !defined(__STDC_NO_THREADS__) || defined(_WIN32)

#include <threads.h>

#else

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

typedef pthread_t thrd_t;
typedef int (*thrd_start_t)(void*);

enum
{
    thrd_success = 0,
    thrd_nomem = 1,
    thrd_timedout = 2,
    thrd_busy = 3,
    thrd_error = 4
};

/** @brief What one new thread runs, carried to it through pthread's single argument. */
typedef struct CnaTestThreadStart
{
    thrd_start_t function;
    void* argument;
} CnaTestThreadStart;

static inline void* cna_test_thread_entry(void* const opaque)
{
    const CnaTestThreadStart start = *(const CnaTestThreadStart*)opaque;
    free(opaque);
    return (void*)(intptr_t)start.function(start.argument);
}

/**
 * @brief C11 thrd_create: starts @p function(@p argument) on a new thread.
 *
 * @param thread Receives the new thread.
 * @param function The thread's body; its result is what thrd_join reports.
 * @param argument Passed to @p function.
 * @return thrd_success, thrd_nomem when the start record cannot be allocated, else thrd_error.
 */
static inline int thrd_create(thrd_t* const thread, const thrd_start_t function, void* const argument)
{
    CnaTestThreadStart* const start = (CnaTestThreadStart*)malloc(sizeof *start);
    if (start == NULL)
        return thrd_nomem;
    start->function = function;
    start->argument = argument;
    const int created = pthread_create(thread, NULL, cna_test_thread_entry, start);
    if (created == 0)
        return thrd_success;
    free(start);
    return created == EAGAIN ? thrd_nomem : thrd_error;
}

/**
 * @brief C11 thrd_join: waits for @p thread and reports its result.
 *
 * @param thread A thread started by thrd_create and not yet joined.
 * @param result Receives the thread function's return value; may be null.
 * @return thrd_success or thrd_error.
 */
static inline int thrd_join(const thrd_t thread, int* const result)
{
    void* value = NULL;
    if (pthread_join(thread, &value) != 0)
        return thrd_error;
    if (result != NULL)
        *result = (int)(intptr_t)value;
    return thrd_success;
}

/**
 * @brief C11 thrd_sleep: suspends the calling thread for @p duration.
 *
 * @param duration How long to sleep.
 * @param remaining Receives the unslept time when a signal interrupts; may be null.
 * @return 0 after the full duration, -1 when interrupted, another negative value on error.
 */
static inline int thrd_sleep(const struct timespec* const duration, struct timespec* const remaining)
{
    if (nanosleep(duration, remaining) == 0)
        return 0;
    return errno == EINTR ? -1 : -2;
}

#endif

#endif
