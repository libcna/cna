// SPDX-License-Identifier: MS-PL

/* plan_binding.md CBIND-040A: the handle lifetime and buffer boundary contract, measured.
 *
 * Every other pure-C test proves what a route *does*. This one proves what the ABI does when a
 * caller gets it wrong -- stale handles, handles of the wrong kind, a child destroyed after its
 * parent, a buffer one byte too small -- and it does so at volume, because a registry defect that
 * shows up once in ten thousand slots is exactly the kind a single-shot test misses.
 *
 * Handles are treated as the documentation requires a caller to treat them: compared only with
 * zero and with each other, never decoded. The bit-level generation case is proved where the bits
 * are visible, in tests/cpp/HandleRegistryTest.cpp; here a *stale* handle plays that role, which
 * is the form a real caller's mistake actually takes.
 */

#include <CNA/C/cna.h>

#include <stdint.h>
#include <string.h>
#include <threads.h>

/* Large enough that the registry must reuse slots many times over, small enough that the
   quadratic uniqueness comparison below stays instant even under a sanitizer. */
#define CYCLE_COUNT 4096U

/* The high-volume pass is about sustained churn rather than the recorded handles. */
#define CHURN_COUNT 20000U

typedef struct ThreadProbe {
    CNA_CurveHandle curve;
    CNA_Result read_result;
    CNA_Result destroy_result;
    CNA_Result clone_result;
    CNA_CurveHandle clone;
} ThreadProbe;

static int touch_curve_from_another_thread(void* context)
{
    ThreadProbe* const probe = (ThreadProbe*)context;
    CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;
    probe->clone = CNA_INVALID_HANDLE;
    probe->read_result = cna_curve_get_pre_loop(probe->curve, &loop_type);
    probe->clone_result = cna_curve_clone(probe->curve, &probe->clone);
    probe->destroy_result = cna_curve_destroy(probe->curve);
    return 0;
}

static void count_event(void* const context)
{
    int* const seen = (int*)context;
    ++(*seen);
}

/* Every route that refuses must also leave a matching thread-local diagnostic behind, because a C
   caller has nothing else to report. */
static int last_error_is(const CNA_Result result, const CNA_ErrorCategory category)
{
    CNA_ErrorInfo info;
    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    if (cna_error_get_last_info(&info) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return info.result == result && info.category == category && info.message_byte_length != 0U;
}

/* A destroyed handle must never come back to life, no matter how many slots have been reused since.
   The handles are kept and re-tested at the end, so a slot recycled two thousand times over still
   has to refuse the handle that once named it. */
static int check_stale_handles(void)
{
    static CNA_CurveHandle issued[CYCLE_COUNT];
    uint32_t index = 0U;
    uint32_t other = 0U;

    for (index = 0U; index < CYCLE_COUNT; ++index) {
        CNA_CurveHandle curve = CNA_INVALID_HANDLE;
        CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;
        if (cna_curve_create(&curve) != CNA_RESULT_SUCCESS || curve == CNA_INVALID_HANDLE) {
            return 11;
        }
        if (cna_curve_get_pre_loop(curve, &loop_type) != CNA_RESULT_SUCCESS) {
            return 12;
        }
        if (cna_curve_destroy(curve) != CNA_RESULT_SUCCESS) {
            return 13;
        }
        /* Idempotence-detecting, not silently idempotent. */
        if (cna_curve_destroy(curve) != CNA_RESULT_INVALID_HANDLE ||
            !last_error_is(CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_HANDLE)) {
            return 14;
        }
        if (cna_curve_get_pre_loop(curve, &loop_type) != CNA_RESULT_INVALID_HANDLE) {
            return 15;
        }
        issued[index] = curve;
    }

    /* No handle value may ever be issued twice: that, not the bit layout, is the property a caller
       depends on when it compares two handles. */
    for (index = 0U; index < CYCLE_COUNT; ++index) {
        for (other = index + 1U; other < CYCLE_COUNT; ++other) {
            if (issued[index] == issued[other]) {
                return 16;
            }
        }
    }

    for (index = 0U; index < CYCLE_COUNT; ++index) {
        CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;
        CNA_CurveHandle clone = CNA_INVALID_HANDLE;
        if (cna_curve_get_pre_loop(issued[index], &loop_type) != CNA_RESULT_INVALID_HANDLE ||
            cna_curve_destroy(issued[index]) != CNA_RESULT_INVALID_HANDLE ||
            cna_curve_clone(issued[index], &clone) != CNA_RESULT_INVALID_HANDLE ||
            clone != CNA_INVALID_HANDLE) {
            return 17;
        }
    }
    return 0;
}

/* A handle the registry never issued, and a live handle of the wrong family, must both be refused
   the same way -- and refused before anything touches a C++ object. */
static int check_invalid_handles(void)
{
    static const CNA_Handle never_issued[] = {
        CNA_INVALID_HANDLE,
        UINT64_C(1),
        UINT64_C(2),
        UINT64_C(0x7fffffff),
        UINT64_C(0x100000000),
        UINT64_MAX
    };
    size_t index = 0U;
    CNA_CurveHandle curve = CNA_INVALID_HANDLE;
    CNA_Texture2DInfo texture_info;
    CNA_CurveKeyCollectionHandle keys = CNA_INVALID_HANDLE;
    uint64_t count = 0U;

    for (index = 0U; index < sizeof(never_issued) / sizeof(never_issued[0]); ++index) {
        CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;
        if (cna_curve_get_pre_loop(never_issued[index], &loop_type) != CNA_RESULT_INVALID_HANDLE ||
            cna_curve_destroy(never_issued[index]) != CNA_RESULT_INVALID_HANDLE) {
            return 21;
        }
    }

    if (cna_curve_create(&curve) != CNA_RESULT_SUCCESS) {
        return 22;
    }
    /* A live handle carried into another family's route is refused as a handle failure, not
       misread as that family's object. */
    memset(&texture_info, 0, sizeof(texture_info));
    texture_info.struct_size = (uint32_t)sizeof(texture_info);
    texture_info.struct_version = UINT32_C(1);
    if (cna_texture2d_get_info(curve, &texture_info) != CNA_RESULT_INVALID_HANDLE ||
        !last_error_is(CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_HANDLE) ||
        texture_info.width != 0U || texture_info.height != 0U) {
        return 23;
    }
    if (cna_curve_key_collection_get_count(curve, &count) != CNA_RESULT_INVALID_HANDLE) {
        return 24;
    }

    /* A null output is an argument failure, which is a different category from a bad handle; the
       two must not collapse into one another. */
    if (cna_curve_get_keys(curve, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        !last_error_is(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT)) {
        return 25;
    }

    if (cna_curve_get_keys(curve, &keys) != CNA_RESULT_SUCCESS ||
        keys == CNA_INVALID_HANDLE || keys == curve) {
        return 26;
    }
    /* The view is a handle of its own kind: the curve's handle does not stand in for it, nor it
       for the curve's. */
    if (cna_curve_key_collection_get_count(curve, &count) != CNA_RESULT_INVALID_HANDLE) {
        return 27;
    }
    {
        CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;
        if (cna_curve_get_pre_loop(keys, &loop_type) != CNA_RESULT_INVALID_HANDLE) {
            return 28;
        }
    }
    if (cna_curve_key_collection_destroy(keys) != CNA_RESULT_SUCCESS ||
        cna_curve_key_collection_destroy(keys) != CNA_RESULT_INVALID_HANDLE) {
        return 29;
    }
    /* Destroying the view does not destroy the curve it viewed. */
    if (cna_curve_destroy(curve) != CNA_RESULT_SUCCESS) {
        return 30;
    }
    return 0;
}

/* Handles are thread-affine: the thread that created one is the only thread that may use or
   destroy it, and the refusal has to arrive as a result rather than as undefined behavior. */
static int check_thread_affinity(void)
{
    ThreadProbe probe;
    thrd_t thread;
    CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;

    memset(&probe, 0, sizeof(probe));
    if (cna_curve_create(&probe.curve) != CNA_RESULT_SUCCESS) {
        return 31;
    }
    if (thrd_create(&thread, touch_curve_from_another_thread, &probe) != thrd_success) {
        return 32;
    }
    if (thrd_join(thread, 0) != thrd_success) {
        return 33;
    }
    if (probe.read_result != CNA_RESULT_THREAD ||
        probe.clone_result != CNA_RESULT_THREAD ||
        probe.destroy_result != CNA_RESULT_THREAD ||
        probe.clone != CNA_INVALID_HANDLE) {
        return 34;
    }
    /* The refusal must have left the handle untouched, not half-released. */
    if (cna_curve_get_pre_loop(probe.curve, &loop_type) != CNA_RESULT_SUCCESS ||
        cna_curve_destroy(probe.curve) != CNA_RESULT_SUCCESS) {
        return 35;
    }
    return 0;
}

/* A copy route answers two questions -- how many bytes are needed, and here they are -- and the
   boundary between them is where a C consumer's buffer bugs live. The sweep walks every capacity
   from zero past the exact length and checks three things at each step: the required count is
   always reported, a refusal writes nothing at all, and a success writes exactly the bytes and no
   terminator. */
static int check_buffer_boundaries(const CNA_Handle game)
{
    char destination[64];
    uint64_t required = UINT64_MAX;
    uint64_t capacity = 0U;
    uint64_t index = 0U;
    char expected[64];

    if (cna_game_get_type_name_size(game, &required) != CNA_RESULT_SUCCESS ||
        required == 0U || required + 2U > sizeof(destination)) {
        return 61;
    }
    if (cna_game_copy_type_name(game, expected, sizeof(expected), &required) !=
        CNA_RESULT_SUCCESS) {
        return 62;
    }

    for (capacity = 0U; capacity <= required + 2U; ++capacity) {
        uint64_t reported = UINT64_MAX;
        CNA_Result result = CNA_RESULT_SUCCESS;
        memset(destination, '#', sizeof(destination));
        result = cna_game_copy_type_name(game, destination, capacity, &reported);
        if (reported != required) {
            return 63;
        }
        if (capacity < required) {
            if (result != CNA_RESULT_BUFFER_TOO_SMALL) {
                return 64;
            }
            /* No partial write: not one byte of the destination may have moved. */
            for (index = 0U; index < sizeof(destination); ++index) {
                if (destination[index] != '#') {
                    return 65;
                }
            }
            continue;
        }
        if (result != CNA_RESULT_SUCCESS ||
            memcmp(destination, expected, (size_t)required) != 0) {
            return 66;
        }
        /* Counted bytes, not a C string: the byte after the text is the caller's, untouched. */
        if (destination[required] != '#') {
            return 67;
        }
    }

    /* A null destination is legal only for a pure size query. */
    if (cna_game_copy_type_name(game, 0, 0U, &required) != CNA_RESULT_BUFFER_TOO_SMALL ||
        required == 0U ||
        cna_game_copy_type_name(game, 0, sizeof(destination), &required) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_game_copy_type_name(game, destination, sizeof(destination), 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 68;
    }
    return 0;
}

/* The same boundary rules hold for the diagnostic itself -- and an error query must never overwrite
   the diagnostic it is being used to read. */
static int check_error_message_boundaries(void)
{
    char message[256];
    uint64_t required = 0U;
    uint64_t reported = UINT64_MAX;
    uint64_t index = 0U;
    CNA_CurveLoopType loop_type = CNA_CURVE_LOOP_CONSTANT;

    if (cna_curve_get_pre_loop(UINT64_C(1), &loop_type) != CNA_RESULT_INVALID_HANDLE) {
        return 41;
    }
    if (cna_error_get_last_message_size(&required) != CNA_RESULT_SUCCESS || required == 0U ||
        required >= sizeof(message)) {
        return 42;
    }
    memset(message, '#', sizeof(message));
    if (cna_error_copy_last_message(message, required - 1U, &reported) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        reported != required) {
        return 43;
    }
    for (index = 0U; index < sizeof(message); ++index) {
        if (message[index] != '#') {
            return 44;
        }
    }
    if (cna_error_copy_last_message(message, sizeof(message), &reported) != CNA_RESULT_SUCCESS ||
        reported != required || message[required] != '#') {
        return 45;
    }
    /* Reading the diagnostic three times must answer the same thing three times. */
    if (!last_error_is(CNA_RESULT_INVALID_HANDLE, CNA_ERROR_CATEGORY_HANDLE) ||
        cna_error_get_last_message_size(&reported) != CNA_RESULT_SUCCESS ||
        reported != required) {
        return 46;
    }
    return 0;
}

/* Shutdown order is a contract, not a hope: a game refuses to go while a child handle the caller
   owns is still alive, and says so with a state failure rather than leaving the child dangling.
   The child is created inside a callback, because that is the only place the graphics device may
   be borrowed -- and it deliberately outlives the callback, which is what makes the guard matter. */
static CNA_Result create_child_resource(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    CNA_Handle* const sprite_batch = (CNA_Handle*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    (void)game_time;
    (void)out_error;
    if (*sprite_batch != CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    if (cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    return cna_sprite_batch_create(graphics_device, sprite_batch);
}

static int check_shutdown_order(const CNA_Handle game, const CNA_Handle sprite_batch)
{
    CNA_Bool fixed = CNA_FALSE;

    if (sprite_batch == CNA_INVALID_HANDLE) {
        return 71;
    }
    if (cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        !last_error_is(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE)) {
        return 72;
    }
    /* The refused destruction must have changed nothing: both handles still answer. */
    if (cna_game_get_is_fixed_time_step(game, &fixed) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_end(sprite_batch) != CNA_RESULT_INVALID_STATE) {
        return 73;
    }
    if (cna_sprite_batch_destroy(sprite_batch) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_destroy(sprite_batch) != CNA_RESULT_INVALID_HANDLE) {
        return 74;
    }
    return 0;
}

/* An event registration is the one child handle a game does *not* refuse to be destroyed around,
   because a subscriber has to be able to observe the game's own disposal. That makes the teardown
   order the caller's to get wrong, so the ABI has to survive the wrong one: the registration names a
   handler collection inside the game, and unsubscribing after the game is gone must detach nothing
   rather than reach into freed memory. Found by this test before it was true -- a heap use-after-free
   in ~GameRegistration, reported by the sanitized tree. */
static int check_registration_outliving_its_game(void)
{
    CNA_GameCreateInfo create_info;
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle disposed = CNA_INVALID_HANDLE;
    CNA_GameEventRegistrationHandle window_moved = CNA_INVALID_HANDLE;
    int disposed_seen = 0;
    static const char title[] = "CBIND-040A teardown";

    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_TRUE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title.data = title;
    create_info.window_title.byte_length = sizeof(title) - 1U;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS) {
        return 91;
    }
    if (cna_game_subscribe(game, CNA_GAME_EVENT_DISPOSED, count_event, &disposed_seen, &disposed) !=
            CNA_RESULT_SUCCESS ||
        cna_game_window_subscribe(
            game,
            CNA_GAME_WINDOW_EVENT_CLIENT_SIZE_CHANGED,
            count_event,
            &disposed_seen,
            &window_moved) != CNA_RESULT_SUCCESS) {
        cna_game_destroy(game);
        return 92;
    }
    /* Live registrations do not stop the game from being destroyed. */
    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 93;
    }
    /* The subscriber observed the disposal it subscribed to; the invalidation happens after. */
    if (disposed_seen != 1) {
        return 94;
    }
    /* Both handles are still the caller's to release, and releasing them detaches nothing. */
    if (cna_game_unsubscribe(disposed) != CNA_RESULT_SUCCESS ||
        cna_game_unsubscribe(disposed) != CNA_RESULT_INVALID_HANDLE ||
        cna_game_unsubscribe(window_moved) != CNA_RESULT_SUCCESS) {
        return 95;
    }
    return 0;
}

/* Sustained churn, which is where a registry that grows without bound or forgets to release an
   object shows itself. Under the sanitized tree this is also the leak check: twenty thousand
   objects created and destroyed must leave nothing behind. */
static int check_high_volume(void)
{
    uint32_t index = 0U;
    CNA_CurveHandle previous = CNA_INVALID_HANDLE;

    for (index = 0U; index < CHURN_COUNT; ++index) {
        CNA_CurveHandle curve = CNA_INVALID_HANDLE;
        CNA_CurveKeyCollectionHandle keys = CNA_INVALID_HANDLE;
        CNA_CurveKey key;
        if (cna_curve_create(&curve) != CNA_RESULT_SUCCESS || curve == previous ||
            cna_curve_get_keys(curve, &keys) != CNA_RESULT_SUCCESS ||
            cna_curve_key_init_position_value((float)index, (float)index, &key) !=
                CNA_RESULT_SUCCESS ||
            cna_curve_key_collection_add(keys, key) != CNA_RESULT_SUCCESS) {
            return 51;
        }
        /* Child first, then parent: the order this ABI requires everywhere. */
        if (cna_curve_key_collection_destroy(keys) != CNA_RESULT_SUCCESS ||
            cna_curve_destroy(curve) != CNA_RESULT_SUCCESS) {
            return 52;
        }
        previous = curve;
    }
    return 0;
}

int main(void)
{
    CNA_GameCreateInfo create_info;
    CNA_GameCallbacks callbacks;
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Handle sprite_batch = CNA_INVALID_HANDLE;
    static const char title[] = "CBIND-040A stress";
    int step = 0;

    step = check_stale_handles();
    if (step != 0) {
        return step;
    }
    step = check_invalid_handles();
    if (step != 0) {
        return step;
    }
    step = check_thread_affinity();
    if (step != 0) {
        return step;
    }
    step = check_error_message_boundaries();
    if (step != 0) {
        return step;
    }
    step = check_high_volume();
    if (step != 0) {
        return step;
    }

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.update = create_child_resource;
    callbacks.context = &sprite_batch;

    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_TRUE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title.data = title;
    create_info.window_title.byte_length = sizeof(title) - 1U;
    create_info.callbacks = &callbacks;
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        game == CNA_INVALID_HANDLE) {
        return 81;
    }
    /* One frame is what creates the graphics device and, through the update callback, the child
       resource the shutdown-order check needs. */
    if (cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS) {
        cna_game_destroy(game);
        return 82;
    }

    step = check_buffer_boundaries(game);
    if (step == 0) {
        step = check_shutdown_order(game, sprite_batch);
    }
    if (step != 0) {
        cna_sprite_batch_destroy(sprite_batch);
        cna_game_destroy(game);
        return step;
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_HANDLE) {
        return 83;
    }

    return check_registration_outliving_its_game();
}
