// SPDX-License-Identifier: MS-PL

/*
 * hello_cna -- the whole CNA C ABI in one file a newcomer can copy.
 *
 * This is not a test. It is the program somebody writes first, and it is deliberately written the
 * way one should write it rather than the shortest way it could be written: every call is checked,
 * every failure prints the diagnostic the ABI left behind, and the teardown order is the documented
 * one. It builds against an **installed** CNA -- `find_package(CNA CONFIG)` and one target,
 * `CNA::CApi` -- so it also proves that the package this project installs is usable from outside
 * its own source tree.
 *
 * It runs without a display: the graphics work is guarded by the capability queries, so on a
 * backend that cannot draw it reports that and still exits cleanly. That is the point of the
 * capability queries, and the reason a renderer's *name* is never the thing to branch on.
 *
 * What it demonstrates, in the order it does it:
 *
 *   1. checking the ABI version before anything else;
 *   2. creating a game with C callbacks, and what a callback may and may not do;
 *   3. borrowing the graphics device, which is legal only inside a callback;
 *   4. asking what the renderer can do instead of assuming;
 *   5. the count-then-copy idiom every string in this ABI uses;
 *   6. reading the error diagnostic after a deliberate failure;
 *   7. creating a texture, uploading pixels and drawing them;
 *   8. shutting down in the documented order -- children first, then the game.
 */

#include <CNA/C/cna.h>

#include <stdio.h>
#include <string.h>

/* Everything in this program happens on one thread: the thread that created the game. Handles in
   this ABI are thread-affine, and a handle used from another thread answers CNA_RESULT_THREAD
   rather than misbehaving. See docs/c-api/CALLBACKS_AND_THREADING.md. */

typedef struct DemoState {
    int frames_drawn;
    int can_draw;
    CNA_Handle texture;
    CNA_Handle sprite_batch;
    char renderer_name[128];
} DemoState;

/* Every fallible route returns a CNA_Result, and the failing thread keeps a structured diagnostic
   for the last one. Printing it is what turns "it returned 3" into something actionable. */
static int failed(const char* const what, const CNA_Result result)
{
    CNA_ErrorInfo info;
    char message[512];
    uint64_t needed = 0U;

    if (result == CNA_RESULT_SUCCESS) {
        return 0;
    }

    fprintf(stderr, "%s failed with result %u", what, (unsigned)result);

    memset(&info, 0, sizeof(info));
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    if (cna_error_get_last_info(&info) == CNA_RESULT_SUCCESS) {
        fprintf(stderr, " (category %u)", (unsigned)info.category);
    }

    /* The count/copy idiom: ask for the size, then copy into a buffer of your own. The count never
       includes a terminator, and a buffer that is too small is refused **without a partial write**,
       so nothing is ever half-copied into your memory. */
    if (cna_error_copy_last_message(message, sizeof(message) - 1U, &needed) ==
        CNA_RESULT_SUCCESS) {
        message[needed] = '\0';
        fprintf(stderr, ": %s", message);
    }
    fprintf(stderr, "\n");
    return 1;
}

/* Runs once per frame, on the game's thread, with a game handle borrowed for the call. The handle
   must not be stored: it is valid until this function returns. */
static CNA_Result on_update(
    const CNA_Handle game,
    const CNA_GameTime* const game_time,
    void* const context,
    CNA_CallbackError* const out_error)
{
    DemoState* const state = (DemoState*)context;
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    CNA_RendererInfo renderer_info;
    CNA_Bool supports_three_d = CNA_FALSE;
    CNA_Result result = CNA_RESULT_SUCCESS;
    uint64_t name_bytes = 0U;

    (void)game_time;
    (void)out_error;

    if (state->frames_drawn != 0) {
        return CNA_RESULT_SUCCESS;
    }

    /* The graphics device may be borrowed only from inside a lifecycle callback, and the handle it
       hands back is borrowed too -- never released by the caller. */
    result = cna_game_get_graphics_device(game, &graphics_device);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }

    memset(&renderer_info, 0, sizeof(renderer_info));
    renderer_info.struct_size = (uint32_t)sizeof(renderer_info);
    renderer_info.struct_version = UINT32_C(1);
    result = cna_graphics_device_get_renderer_info(graphics_device, &renderer_info);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }

    /* Count, then copy. The reported count excludes any terminator, so leave room for one if you
       want a C string out of it. */
    result = cna_graphics_device_copy_renderer_name(
        graphics_device, state->renderer_name, sizeof(state->renderer_name) - 1U, &name_bytes);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }
    state->renderer_name[name_bytes] = '\0';

    /* Ask what this backend can do rather than guessing from its name. An enumerated renderer
       identity is not a support claim: two builds carrying the same identity can answer these
       differently, so the capability query is the thing to branch on -- never the identity. */
    result = cna_graphics_device_supports_capability(
        graphics_device, CNA_GRAPHICS_CAPABILITY_THREE_D, &supports_three_d);
    if (result != CNA_RESULT_SUCCESS) {
        return result;
    }

    printf("renderer: %s (max texture %u, 3D %s)\n",
        state->renderer_name,
        (unsigned)renderer_info.max_texture_dimension,
        supports_three_d == CNA_TRUE ? "supported" : "unavailable");

    /* Two-dimensional drawing has no capability identity of its own, which is the other half of the
       same lesson: where there is nothing to ask, **attempt the operation and handle its answer**.
       A backend that will not do this returns CNA_RESULT_NOT_SUPPORTED, which is an answer, not a
       failure -- so it is reported and the program carries on. */
    {
        CNA_Texture2DCreateInfo texture_info;
        CNA_SpriteBatchBeginInfo begin_info;
        CNA_SpriteCommand command;
        static const CNA_Color pixels[4] = {
            {UINT8_C(255), UINT8_C(0), UINT8_C(0), UINT8_C(255)},
            {UINT8_C(0), UINT8_C(255), UINT8_C(0), UINT8_C(255)},
            {UINT8_C(0), UINT8_C(0), UINT8_C(255), UINT8_C(255)},
            {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(255)}
        };

        /* Every extensible input struct starts with its own size and version. Zero it, then set
           both -- CNA reads only the fields your struct_size says you know about, which is how a
           future CNA can add fields without breaking this program. */
        memset(&texture_info, 0, sizeof(texture_info));
        texture_info.struct_size = (uint32_t)sizeof(texture_info);
        texture_info.struct_version = UINT32_C(1);
        texture_info.width = UINT32_C(2);
        texture_info.height = UINT32_C(2);
        texture_info.mip_map = CNA_FALSE;
        texture_info.format = CNA_SURFACE_FORMAT_COLOR;

        result = cna_texture2d_create(graphics_device, &texture_info, &state->texture);
        if (result == CNA_RESULT_NOT_SUPPORTED) {
            state->frames_drawn = 1;
            return CNA_RESULT_SUCCESS;
        }
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = cna_texture2d_set_data_rgba8(state->texture, pixels, UINT64_C(4));
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = cna_sprite_batch_create(graphics_device, &state->sprite_batch);
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }

        memset(&begin_info, 0, sizeof(begin_info));
        begin_info.struct_size = (uint32_t)sizeof(begin_info);
        begin_info.struct_version = UINT32_C(1);
        begin_info.sort_mode = CNA_SPRITE_SORT_MODE_DEFERRED;

        memset(&command, 0, sizeof(command));
        command.struct_size = (uint32_t)sizeof(command);
        command.struct_version = UINT32_C(1);
        command.texture = state->texture;
        command.destination.x = 0;
        command.destination.y = 0;
        command.destination.width = 64;
        command.destination.height = 64;
        command.source.x = 0;
        command.source.y = 0;
        command.source.width = 2;
        command.source.height = 2;
        command.color.r = UINT8_C(255);
        command.color.g = UINT8_C(255);
        command.color.b = UINT8_C(255);
        command.color.a = UINT8_C(255);

        result = cna_sprite_batch_begin(state->sprite_batch, &begin_info);
        if (result == CNA_RESULT_NOT_SUPPORTED) {
            state->frames_drawn = 1;
            return CNA_RESULT_SUCCESS;
        }
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        result = cna_sprite_batch_submit_many(state->sprite_batch, &command, UINT64_C(1));
        if (result == CNA_RESULT_SUCCESS) {
            result = cna_sprite_batch_end(state->sprite_batch);
        } else {
            (void)cna_sprite_batch_end(state->sprite_batch);
        }
        if (result == CNA_RESULT_NOT_SUPPORTED) {
            state->frames_drawn = 1;
            return CNA_RESULT_SUCCESS;
        }
        if (result != CNA_RESULT_SUCCESS) {
            return result;
        }
        state->can_draw = 1;
    }

    state->frames_drawn = 1;
    /* Returning anything but success stops the game, and the enclosing C call reports
       CNA_RESULT_CALLBACK. Never throw, never longjmp out of a callback. */
    return CNA_RESULT_SUCCESS;
}

int main(void)
{
    DemoState state;
    CNA_GameCreateInfo create_info;
    CNA_GameCallbacks callbacks;
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Result result = CNA_RESULT_SUCCESS;
    const uint32_t runtime_abi = cna_get_abi_version();
    static const char title[] = "hello_cna";
    char type_name[64];
    uint64_t type_name_bytes = 0U;

    /* 1. The version check comes first, because it decides whether anything else in this file is
          meaningful. A different major is never compatible; a lower minor lacks routes this
          program may have been compiled against. */
    if ((runtime_abi >> 16U) != CNA_ABI_VERSION_MAJOR) {
        fprintf(stderr,
            "CNA ABI major mismatch: built against %u, found %u\n",
            (unsigned)CNA_ABI_VERSION_MAJOR,
            (unsigned)(runtime_abi >> 16U));
        return 1;
    }
    if (runtime_abi < CNA_ABI_VERSION) {
        fprintf(stderr,
            "CNA runtime ABI %u is older than the headers this program was built against (%u)\n",
            (unsigned)runtime_abi,
            (unsigned)CNA_ABI_VERSION);
        return 1;
    }
    printf("CNA C ABI %u.%u.%u\n",
        (unsigned)(runtime_abi >> 16U),
        (unsigned)((runtime_abi >> 8U) & 0xFFU),
        (unsigned)(runtime_abi & 0xFFU));

    memset(&state, 0, sizeof(state));
    state.texture = CNA_INVALID_HANDLE;
    state.sprite_batch = CNA_INVALID_HANDLE;

    /* 2. One active game per process. The callback table is copied during creation, so it does not
          have to outlive this call -- the context pointer does. */
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.struct_size = (uint32_t)sizeof(callbacks);
    callbacks.struct_version = UINT32_C(1);
    callbacks.update = on_update;
    callbacks.context = &state;

    memset(&create_info, 0, sizeof(create_info));
    create_info.struct_size = (uint32_t)sizeof(create_info);
    create_info.struct_version = UINT32_C(1);
    create_info.is_fixed_time_step = CNA_TRUE;
    create_info.target_elapsed_time_ticks = INT64_C(166667);
    create_info.window_title.data = title;
    create_info.window_title.byte_length = sizeof(title) - 1U;
    create_info.callbacks = &callbacks;

    result = cna_game_create(&create_info, &game);
    if (failed("cna_game_create", result)) {
        return 1;
    }

    /* 3. One frame is enough for this program; a real game calls cna_game_run and lets CNA own the
          loop until cna_game_request_exit. */
    result = cna_game_run_one_frame(game);
    if (failed("cna_game_run_one_frame", result)) {
        (void)cna_sprite_batch_destroy(state.sprite_batch);
        (void)cna_texture2d_destroy(state.texture);
        (void)cna_game_destroy(game);
        return 1;
    }

    /* 4. The same count/copy idiom, this time for a value that is always there. */
    result = cna_game_copy_type_name(game, type_name, sizeof(type_name) - 1U, &type_name_bytes);
    if (failed("cna_game_copy_type_name", result)) {
        type_name_bytes = 0U;
    }
    type_name[type_name_bytes] = '\0';
    printf("game type: %s\n", type_name);

    /* 5. Two deliberate mistakes, because the ABI tells them apart and so should you. A null
          output is an *argument* failure -- you passed the wrong kind of thing. A handle that was
          never issued, or that you already destroyed, is a *handle* failure -- the thing it named
          is not there. Nothing is touched or corrupted in either case, and the diagnostic says
          which happened. Note the order: arguments are checked before handles, so a call that gets
          both wrong reports the argument. */
    {
        CNA_Bool active = CNA_FALSE;
        printf("two deliberate mistakes, and how the ABI explains itself:\n  ");
        (void)failed(
            "cna_game_get_is_active(live handle, null output)",
            cna_game_get_is_active(game, NULL));
        printf("  ");
        (void)failed(
            "cna_game_get_is_active(handle that was never issued)",
            cna_game_get_is_active(UINT64_C(0xDEADBEEF), &active));
    }

    /* 6. Teardown, in the documented order: every owned child first, then the game. A game refuses
          to be destroyed while an owned resource is alive, so getting this wrong is a diagnosable
          error rather than a crash -- but getting it right is the caller's job. */
    if (state.sprite_batch != CNA_INVALID_HANDLE) {
        result = cna_sprite_batch_destroy(state.sprite_batch);
        if (failed("cna_sprite_batch_destroy", result)) {
            return 1;
        }
    }
    if (state.texture != CNA_INVALID_HANDLE) {
        result = cna_texture2d_destroy(state.texture);
        if (failed("cna_texture2d_destroy", result)) {
            return 1;
        }
    }
    result = cna_game_destroy(game);
    if (failed("cna_game_destroy", result)) {
        return 1;
    }

    printf("hello_cna finished: %d frame(s), %s\n",
        state.frames_drawn,
        state.can_draw ? "drew a sprite" : "drew nothing, by capability");
    return 0;
}
