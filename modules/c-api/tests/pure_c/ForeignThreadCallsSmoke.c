// SPDX-License-Identifier: MS-PL
// CBIND-141: cna_game_set_foreign_thread_calls_ext, another thread's calls on the game thread's
// handles run on the game thread. CBIND-152: cna_game_run_foreign_thread_calls_ext runs them while
// the game thread waits for that thread inside a callback.

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "CnaTestThreads.h"

typedef struct Shared {
    CNA_Handle game;
    CNA_Handle texture;
    int updates;
    int phase;
    thrd_t worker;
    atomic_int worker_done;
    CNA_Result refused;
    CNA_Result read;
    CNA_Color pixels[4];
    CNA_Result too_small;
    CNA_ErrorInfo too_small_info;
    CNA_Result pumped_read;
    CNA_Color pumped_pixels[4];
    int failed;
} Shared;

static const CNA_Color Expected[4] = {
    {255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {9, 8, 7, 255},
};

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

static CNA_Result read_texture_info(const CNA_Handle texture)
{
    CNA_Texture2DInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    return cna_texture2d_get_info(texture, &info);
}

/* Off: the texture is the game thread's, so this thread is refused, as it always was. */
static int refused_worker(void* const argument)
{
    Shared* const shared = (Shared*)argument;
    shared->refused = read_texture_info(shared->texture);
    atomic_store(&shared->worker_done, 1);
    return 0;
}

/* On: the same calls wait for the game thread and come back with its answers. */
static int served_worker(void* const argument)
{
    Shared* const shared = (Shared*)argument;
    uint64_t count = 0U;
    shared->read = cna_texture2d_get_data_rgba8(shared->texture, shared->pixels, 4U, &count);
    if (shared->read == CNA_RESULT_SUCCESS && count != 4U) {
        shared->read = CNA_RESULT_INTERNAL;
    }
    /* A served call that fails leaves this thread the error record the game thread wrote. */
    shared->too_small = cna_texture2d_get_data_rgba8(shared->texture, shared->pixels, 1U, &count);
    memset(&shared->too_small_info, 0, sizeof(shared->too_small_info));
    shared->too_small_info.struct_size = (uint32_t)sizeof(shared->too_small_info);
    shared->too_small_info.struct_version = UINT32_C(1);
    if (cna_error_get_last_info(&shared->too_small_info) != CNA_RESULT_SUCCESS) {
        shared->too_small_info.result = CNA_RESULT_INTERNAL;
    }
    atomic_store(&shared->worker_done, 1);
    return 0;
}

/* The loading-screen shape: the game thread joins this one from its update. */
static int pumped_worker(void* const argument)
{
    Shared* const shared = (Shared*)argument;
    uint64_t count = 0U;
    shared->pumped_read = cna_texture2d_get_data_rgba8(shared->texture, shared->pumped_pixels, 4U, &count);
    if (shared->pumped_read == CNA_RESULT_SUCCESS && count != 4U) {
        shared->pumped_read = CNA_RESULT_INTERNAL;
    }
    atomic_store(&shared->worker_done, 1);
    return 0;
}

/* Waits for the worker as a binding's game thread does, running its calls meanwhile. Without
   cna_game_run_foreign_thread_calls_ext this wait never ends: the worker's call waits for the game
   thread's next update, and the game thread waits for the worker. */
static int join_while_running_calls(Shared* const shared)
{
    const struct timespec millisecond = {0, 1000000L};
    for (int waited = 0; !atomic_load(&shared->worker_done); ++waited) {
        if (waited > 10000 || cna_game_run_foreign_thread_calls_ext(shared->game) != CNA_RESULT_SUCCESS) {
            return 0;
        }
        thrd_sleep(&millisecond, NULL);
    }
    return thrd_join(shared->worker, 0) == thrd_success;
}

static CNA_Result on_load_content(const CNA_Handle game, const CNA_GameTime* const game_time,
                                  void* const context, CNA_CallbackError* const out_error)
{
    Shared* const shared = (Shared*)context;
    CNA_Handle device = CNA_INVALID_HANDLE;
    CNA_Texture2DCreateInfo create_info;
    (void)game_time;
    (void)out_error;
    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.width = 2U;
    create_info.height = 2U;
    create_info.mip_map = CNA_FALSE;
    create_info.format = CNA_SURFACE_FORMAT_COLOR;
    if (cna_game_get_graphics_device(game, &device) != CNA_RESULT_SUCCESS ||
        cna_texture2d_create(device, &create_info, &shared->texture) != CNA_RESULT_SUCCESS ||
        cna_texture2d_set_data_rgba8(shared->texture, Expected, 4U) != CNA_RESULT_SUCCESS) {
        shared->failed = 1;
    }
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_update(const CNA_Handle game, const CNA_GameTime* const game_time,
                            void* const context, CNA_CallbackError* const out_error)
{
    Shared* const shared = (Shared*)context;
    (void)game_time;
    (void)out_error;
    ++shared->updates;
    if (shared->failed || shared->updates > 600) {
        shared->failed = 1;
        return cna_game_request_exit(game);
    }

    switch (shared->phase) {
        case 0:
            /* The refused call returns at once, so the game thread may wait for it. */
            atomic_store(&shared->worker_done, 0);
            if (thrd_create(&shared->worker, refused_worker, shared) != thrd_success ||
                thrd_join(shared->worker, 0) != thrd_success ||
                cna_game_set_foreign_thread_calls_ext(game, CNA_TRUE) != CNA_RESULT_SUCCESS) {
                shared->failed = 1;
                break;
            }
            atomic_store(&shared->worker_done, 0);
            if (thrd_create(&shared->worker, served_worker, shared) != thrd_success) {
                shared->failed = 1;
                break;
            }
            shared->phase = 1;
            break;
        case 1:
            /* The worker's calls run at the start of this thread's updates and draws; it must not
               be waited for until it is done. */
            if (atomic_load(&shared->worker_done)) {
                (void)thrd_join(shared->worker, 0);
                atomic_store(&shared->worker_done, 0);
                if (thrd_create(&shared->worker, pumped_worker, shared) != thrd_success ||
                    !join_while_running_calls(shared)) {
                    shared->failed = 1;
                    break;
                }
                shared->phase = 2;
                return cna_game_request_exit(game);
            }
            break;
        default:
            break;
    }
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    Shared shared;
    CNA_GameCallbacks callbacks;
    CNA_GameCreateInfo create_info;
    CNA_Handle game = CNA_INVALID_HANDLE;

    memset(&shared, 0, sizeof(shared));
    atomic_init(&shared.worker_done, 0);
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.load_content = on_load_content;
    callbacks.update = on_update;
    callbacks.context = &shared;
    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_FALSE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title = view("C API foreign-thread calls smoke");
    create_info.callbacks = &callbacks;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(1);
    }
    shared.game = game;
    if (cna_game_set_foreign_thread_calls_ext(game, (CNA_Bool)2) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_run_foreign_thread_calls_ext(CNA_INVALID_HANDLE) == CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(2);
    }

    if (cna_game_run(game) != CNA_RESULT_SUCCESS || shared.failed || shared.phase != 2) {
        fprintf(stderr, "failed=%d phase=%d updates=%d\n", shared.failed, shared.phase, shared.updates);
        return CNA_TEST_FAIL(3);
    }
    if (shared.refused != CNA_RESULT_THREAD) {
        fprintf(stderr, "refused=%u\n", (unsigned)shared.refused);
        return CNA_TEST_FAIL(4);
    }
    if (shared.read != CNA_RESULT_SUCCESS || memcmp(shared.pixels, Expected, sizeof(Expected)) != 0) {
        fprintf(stderr, "read=%u\n", (unsigned)shared.read);
        return CNA_TEST_FAIL(5);
    }
    if (shared.too_small != CNA_RESULT_BUFFER_TOO_SMALL ||
        shared.too_small_info.result != CNA_RESULT_BUFFER_TOO_SMALL) {
        fprintf(stderr, "too_small=%u info=%u\n", (unsigned)shared.too_small,
                (unsigned)shared.too_small_info.result);
        return CNA_TEST_FAIL(6);
    }

    if (shared.pumped_read != CNA_RESULT_SUCCESS ||
        memcmp(shared.pumped_pixels, Expected, sizeof(Expected)) != 0) {
        fprintf(stderr, "pumped_read=%u\n", (unsigned)shared.pumped_read);
        return CNA_TEST_FAIL(11);
    }

    /* Off again: the game thread's handles refuse other threads, as by default, and running the
       queue does nothing. */
    if (cna_game_set_foreign_thread_calls_ext(game, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_game_run_foreign_thread_calls_ext(game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(7);
    }
    atomic_store(&shared.worker_done, 0);
    shared.refused = CNA_RESULT_SUCCESS;
    if (thrd_create(&shared.worker, refused_worker, &shared) != thrd_success ||
        thrd_join(shared.worker, 0) != thrd_success || shared.refused != CNA_RESULT_THREAD) {
        return CNA_TEST_FAIL(8);
    }

    if (cna_texture2d_destroy(shared.texture) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(9);
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : CNA_TEST_FAIL(10);
}
