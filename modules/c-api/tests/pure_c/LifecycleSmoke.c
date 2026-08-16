// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <stdlib.h>
#include <string.h>
#include <threads.h>

typedef struct LifecycleState {
    int load_count;
    int update_count;
    int draw_count;
    int unload_count;
    int exit_count;
    int saw_time;
    int lifecycle_stage;
    CNA_Handle borrowed_graphics_device;
    CNA_Bool supports_three_d;
    uint64_t renderer_name_bytes;
    CNA_Handle texture;
    CNA_Handle sprite_batch;
    uint32_t pressed_key_count;
    int readback_validated;
} LifecycleState;

typedef struct WrongThreadState {
    CNA_Handle game;
    CNA_Result result;
    CNA_Result keyboard_result;
} WrongThreadState;

static int set_title_on_wrong_thread(void* context)
{
    static const char title[] = "wrong thread";
    WrongThreadState* const state = (WrongThreadState*)context;
    state->result = cna_game_set_window_title(
        state->game,
        (CNA_StringView){title, sizeof(title) - 1U});
    CNA_KeyboardState keyboard_state = {
        sizeof(CNA_KeyboardState), UINT32_C(1), {0U, 0U, 0U, 0U}
    };
    state->keyboard_result = cna_keyboard_get_state(state->game, &keyboard_state);
    return 0;
}

static CNA_Result on_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    if (game_time != 0 || out_error == 0 || out_error->struct_size != sizeof(CNA_CallbackError)) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 0 && state->lifecycle_stage != 5) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    CNA_Handle same_graphics_device = CNA_INVALID_HANDLE;
    CNA_RendererInfo renderer_info = {
        sizeof(CNA_RendererInfo),
        UINT32_C(1),
        0U,
        0U,
        0U,
        0U
    };
    CNA_Bool supports_three_d = CNA_FALSE;
    uint64_t renderer_name_bytes = 0U;
    char renderer_name[32] = {0};
    if (cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        graphics_device == CNA_INVALID_HANDLE ||
        cna_game_get_graphics_device(game, &same_graphics_device) != CNA_RESULT_SUCCESS ||
        same_graphics_device != graphics_device ||
        cna_graphics_device_get_renderer_info(graphics_device, &renderer_info) !=
            CNA_RESULT_SUCCESS ||
        renderer_info.renderer_name_byte_length == 0U ||
        renderer_info.renderer_name_byte_length >= sizeof(renderer_name) ||
        renderer_info.renderer_type == CNA_GRAPHICS_RENDERER_UNKNOWN ||
        renderer_info.max_texture_dimension == 0U ||
        cna_graphics_device_get_renderer_name_size(graphics_device, &renderer_name_bytes) !=
            CNA_RESULT_SUCCESS ||
        renderer_name_bytes != renderer_info.renderer_name_byte_length ||
        cna_graphics_device_copy_renderer_name(
            graphics_device,
            renderer_name,
            renderer_name_bytes - 1U,
            &renderer_name_bytes) != CNA_RESULT_BUFFER_TOO_SMALL ||
        cna_graphics_device_copy_renderer_name(
            graphics_device,
            renderer_name,
            sizeof(renderer_name),
            &renderer_name_bytes) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_supports_capability(
            graphics_device,
            CNA_GRAPHICS_CAPABILITY_THREE_D,
            &supports_three_d) != CNA_RESULT_SUCCESS ||
        (((renderer_info.capability_flags & CNA_GRAPHICS_CAPABILITY_FLAG_THREE_D) != 0U) !=
         (supports_three_d == CNA_TRUE)) ||
        cna_graphics_device_supports_capability(
            graphics_device,
            UINT32_MAX,
            &supports_three_d) != CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->borrowed_graphics_device = graphics_device;
    state->supports_three_d = supports_three_d;
    state->renderer_name_bytes = renderer_name_bytes;

    CNA_Texture2DCreateInfo texture_create_info = {
        sizeof(CNA_Texture2DCreateInfo),
        UINT32_C(1),
        UINT32_C(2),
        UINT32_C(2),
        CNA_FALSE,
        {0U, 0U, 0U},
        CNA_SURFACE_FORMAT_BGR565
    };
    CNA_Handle unsupported_texture = CNA_INVALID_HANDLE;
    if (cna_texture2d_create(
            graphics_device,
            &texture_create_info,
            &unsupported_texture) != CNA_RESULT_NOT_SUPPORTED ||
        unsupported_texture != CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    texture_create_info.format = CNA_SURFACE_FORMAT_COLOR;
    CNA_Handle texture = CNA_INVALID_HANDLE;
    CNA_Texture2DInfo texture_info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    const CNA_Color pixels[4] = {
        {UINT8_C(255), UINT8_C(0), UINT8_C(0), UINT8_C(255)},
        {UINT8_C(0), UINT8_C(255), UINT8_C(0), UINT8_C(255)},
        {UINT8_C(0), UINT8_C(0), UINT8_C(255), UINT8_C(255)},
        {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(128)}
    };
    CNA_Color readback[4] = {
        {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
        {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
        {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
        {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)}
    };
    uint64_t required_pixels = 0U;
    if (cna_texture2d_create(graphics_device, &texture_create_info, &texture) !=
            CNA_RESULT_SUCCESS ||
        texture == CNA_INVALID_HANDLE ||
        cna_texture2d_get_info(texture, &texture_info) != CNA_RESULT_SUCCESS ||
        texture_info.width != 2U || texture_info.height != 2U ||
        texture_info.level_count != 1U || texture_info.format != CNA_SURFACE_FORMAT_COLOR ||
        cna_texture2d_set_data_rgba8(texture, pixels, 3U) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_texture2d_set_data_rgba8(texture, pixels, 4U) != CNA_RESULT_SUCCESS ||
        cna_texture2d_get_data_rgba8(texture, readback, 3U, &required_pixels) !=
            CNA_RESULT_BUFFER_TOO_SMALL ||
        required_pixels != 4U ||
        memcmp(readback, (CNA_Color[4]){
            {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
            {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
            {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
            {UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)}}, sizeof(readback)) != 0 ||
        cna_texture2d_get_data_rgba8(texture, readback, 4U, &required_pixels) !=
            CNA_RESULT_SUCCESS ||
        required_pixels != 4U || memcmp(readback, pixels, sizeof(pixels)) != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Handle sprite_batch = CNA_INVALID_HANDLE;
    if (cna_sprite_batch_create(graphics_device, &sprite_batch) != CNA_RESULT_SUCCESS ||
        sprite_batch == CNA_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    const CNA_SpriteSortMode sort_modes[] = {
        CNA_SPRITE_SORT_MODE_DEFERRED,
        CNA_SPRITE_SORT_MODE_IMMEDIATE,
        CNA_SPRITE_SORT_MODE_TEXTURE,
        CNA_SPRITE_SORT_MODE_FRONT_TO_BACK
    };
    CNA_SpriteBatchBeginInfo sort_probe = {
        sizeof(CNA_SpriteBatchBeginInfo), UINT32_C(1), UINT32_MAX, 0U
    };
    if (cna_sprite_batch_begin(sprite_batch, &sort_probe) != CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }
    for (size_t index = 0U; index < sizeof(sort_modes) / sizeof(sort_modes[0]); ++index) {
        sort_probe.sort_mode = sort_modes[index];
        if (cna_sprite_batch_begin(sprite_batch, &sort_probe) != CNA_RESULT_SUCCESS ||
            cna_sprite_batch_end(sprite_batch) != CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
    }
    CNA_Handle cancelled_sprite_batch = CNA_INVALID_HANDLE;
    const CNA_SpriteBatchBeginInfo cancelled_begin_info = {
        sizeof(CNA_SpriteBatchBeginInfo),
        UINT32_C(1),
        CNA_SPRITE_SORT_MODE_DEFERRED,
        0U
    };
    if (cna_sprite_batch_create(graphics_device, &cancelled_sprite_batch) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_begin(cancelled_sprite_batch, &cancelled_begin_info) !=
            CNA_RESULT_SUCCESS ||
        cna_sprite_batch_destroy(cancelled_sprite_batch) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_destroy(cancelled_sprite_batch) != CNA_RESULT_INVALID_HANDLE) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->texture = texture;
    state->sprite_batch = sprite_batch;
    ++state->lifecycle_stage;
    ++state->load_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_update(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)out_error;
    if (game_time == 0 || game_time->elapsed_game_time_ticks <= 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage == 1) {
        ++state->lifecycle_stage;
    } else if (state->lifecycle_stage != 2) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_KeyboardState keyboard_state = {
        sizeof(CNA_KeyboardState), UINT32_C(1), {0U, 0U, 0U, 0U}
    };
    uint32_t pressed_key_count = 0U;
    if (cna_keyboard_get_state(game, &keyboard_state) != CNA_RESULT_SUCCESS ||
        cna_keyboard_state_get_pressed_key_count(&keyboard_state, &pressed_key_count) !=
            CNA_RESULT_SUCCESS ||
        pressed_key_count > 256U) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->pressed_key_count = pressed_key_count;
    ++state->update_count;
    state->saw_time = 1;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_draw(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time == 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 2) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (cna_game_clear(game, (CNA_Color){UINT8_C(10), UINT8_C(20), UINT8_C(30), UINT8_C(255)}) !=
        CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_SpriteBatchBeginInfo begin_info = {
        sizeof(CNA_SpriteBatchBeginInfo),
        UINT32_C(1),
        CNA_SPRITE_SORT_MODE_BACK_TO_FRONT,
        0U
    };
    CNA_SpriteCommand commands[2] = {
        {
            sizeof(CNA_SpriteCommand),
            UINT32_C(1),
            state->texture,
            {0, 0, 2, 2},
            {0, 0, 2, 2},
            {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(255)},
            0.0F,
            {0.0F, 0.0F},
            CNA_SPRITE_EFFECT_NONE,
            0.25F
        },
        {
            sizeof(CNA_SpriteCommand),
            UINT32_C(1),
            state->texture,
            {2, 0, 2, 2},
            {0, 0, 2, 2},
            {UINT8_C(255), UINT8_C(255), UINT8_C(255), UINT8_C(128)},
            0.0F,
            {1.0F, 1.0F},
            CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY | CNA_SPRITE_EFFECT_FLIP_VERTICALLY,
            0.75F
        }
    };
    if (cna_sprite_batch_end(state->sprite_batch) != CNA_RESULT_INVALID_STATE ||
        cna_sprite_batch_begin(state->sprite_batch, &begin_info) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_begin(state->sprite_batch, &begin_info) != CNA_RESULT_INVALID_STATE ||
        cna_sprite_batch_submit_many(state->sprite_batch, 0, 0U) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    commands[0].effects = UINT32_C(4);
    if (cna_sprite_batch_submit_many(state->sprite_batch, commands, 2U) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return CNA_RESULT_INVALID_STATE;
    }
    commands[0].effects = CNA_SPRITE_EFFECT_NONE;
    if (cna_sprite_batch_submit_many(state->sprite_batch, commands, 2U) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }

    /* CBIND-044B: the canonical Draw family that places a sprite by position and scale rather than
       by destination rectangle. With a position, the origin is measured in source pixels and the
       scale applies after that offset, which a destination rectangle cannot express without
       repeating the canonical arithmetic -- so it is its own command and its own route. */
    {
        CNA_SpriteScaledCommand scaled[2];
        memset(scaled, 0, sizeof(scaled));
        scaled[0].struct_size = (uint32_t)sizeof(CNA_SpriteScaledCommand);
        scaled[0].struct_version = UINT32_C(1);
        scaled[0].texture = state->texture;
        scaled[0].position.x = 4.0f;
        scaled[0].position.y = 6.0f;
        /* Zero width and height is the empty optional: the whole texture. */
        scaled[0].color.r = UINT8_C(255);
        scaled[0].color.g = UINT8_C(255);
        scaled[0].color.b = UINT8_C(255);
        scaled[0].color.a = UINT8_C(255);
        scaled[0].scale.x = 3.0f;
        scaled[0].scale.y = 3.0f;
        scaled[1] = scaled[0];
        /* The second names a real source rectangle and a non-uniform scale, which is the other
           canonical overload. */
        scaled[1].position.x = 20.0f;
        scaled[1].source.width = 1;
        scaled[1].source.height = 1;
        scaled[1].scale.x = 2.0f;
        scaled[1].scale.y = 5.0f;
        scaled[1].rotation = 0.5f;
        scaled[1].origin.x = 0.5f;
        scaled[1].origin.y = 0.5f;
        scaled[1].effects = CNA_SPRITE_EFFECT_FLIP_HORIZONTALLY;

        if (cna_sprite_batch_submit_scaled_many(state->sprite_batch, 0, 0U) !=
                CNA_RESULT_SUCCESS ||
            cna_sprite_batch_submit_scaled_many(state->sprite_batch, scaled, 2U) !=
                CNA_RESULT_SUCCESS) {
            return CNA_RESULT_INVALID_STATE;
        }
        /* Every field is validated before anything is submitted: an undefined effect bit, a
           non-finite scale and a handle of the wrong family are all refused. */
        scaled[0].effects = UINT32_C(4);
        if (cna_sprite_batch_submit_scaled_many(state->sprite_batch, scaled, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return CNA_RESULT_INVALID_STATE;
        }
        scaled[0].effects = CNA_SPRITE_EFFECT_NONE;
        scaled[0].scale.x = 1.0f / 0.0f;
        if (cna_sprite_batch_submit_scaled_many(state->sprite_batch, scaled, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return CNA_RESULT_INVALID_STATE;
        }
        scaled[0].scale.x = 3.0f;
        scaled[0].struct_version = UINT32_C(2);
        if (cna_sprite_batch_submit_scaled_many(state->sprite_batch, scaled, 2U) !=
            CNA_RESULT_INVALID_ARGUMENT) {
            return CNA_RESULT_INVALID_STATE;
        }
        scaled[0].struct_version = UINT32_C(1);
        scaled[0].texture = state->sprite_batch;
        if (cna_sprite_batch_submit_scaled_many(state->sprite_batch, scaled, 2U) !=
            CNA_RESULT_INVALID_HANDLE) {
            return CNA_RESULT_INVALID_STATE;
        }
    }

    if (cna_texture2d_destroy(state->texture) != CNA_RESULT_INVALID_STATE ||
        cna_sprite_batch_end(state->sprite_batch) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_end(state->sprite_batch) != CNA_RESULT_INVALID_STATE) {
        return CNA_RESULT_INVALID_STATE;
    }

    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    CNA_RendererInfo renderer_info = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    CNA_BackBufferInfo backbuffer_info = {
        sizeof(CNA_BackBufferInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    uint64_t required_pixels = 0U;
    if (cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_renderer_info(graphics_device, &renderer_info) !=
            CNA_RESULT_SUCCESS ||
        cna_graphics_device_get_backbuffer_info(graphics_device, &backbuffer_info) !=
            CNA_RESULT_SUCCESS ||
        backbuffer_info.width < 8U || backbuffer_info.height < 8U ||
        backbuffer_info.format != CNA_SURFACE_FORMAT_COLOR || backbuffer_info.reserved != 0U ||
        cna_graphics_device_get_backbuffer_data_rgba8(
            graphics_device,
            0,
            0U,
            &required_pixels) != CNA_RESULT_BUFFER_TOO_SMALL ||
        required_pixels !=
            (uint64_t)backbuffer_info.width * (uint64_t)backbuffer_info.height) {
        return CNA_RESULT_INVALID_STATE;
    }
    CNA_Color* const backbuffer =
        (CNA_Color*)malloc((size_t)required_pixels * sizeof(CNA_Color));
    if (backbuffer == 0) {
        return CNA_RESULT_OUT_OF_MEMORY;
    }
    for (uint64_t index = 0U; index < required_pixels; ++index) {
        backbuffer[index] = (CNA_Color){UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)};
    }
    if (cna_graphics_device_get_backbuffer_data_rgba8(
            graphics_device,
            backbuffer,
            required_pixels - 1U,
            &required_pixels) != CNA_RESULT_BUFFER_TOO_SMALL ||
        memcmp(
            &backbuffer[0],
            &(CNA_Color){UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
            sizeof(CNA_Color)) != 0) {
        free(backbuffer);
        return CNA_RESULT_INVALID_STATE;
    }
    const CNA_Result readback_result = cna_graphics_device_get_backbuffer_data_rgba8(
        graphics_device,
        backbuffer,
        required_pixels,
        &required_pixels);
    /*
     * Backbuffer readback is a backend capability, never a renderer identity: a backend without it
     * must leave the destination untouched, and one with it must show the exact drawn texels.
     */
    int readback_ok = 0;
    if (readback_result == CNA_RESULT_NOT_SUPPORTED) {
        readback_ok = memcmp(
            &backbuffer[0],
            &(CNA_Color){UINT8_C(1), UINT8_C(2), UINT8_C(3), UINT8_C(4)},
            sizeof(CNA_Color)) == 0;
    } else if (readback_result == CNA_RESULT_SUCCESS) {
        const CNA_Color expected_red = {
            UINT8_C(255), UINT8_C(0), UINT8_C(0), UINT8_C(255)
        };
        const CNA_Color expected_green = {
            UINT8_C(0), UINT8_C(255), UINT8_C(0), UINT8_C(255)
        };
        const CNA_Color expected_blue = {
            UINT8_C(0), UINT8_C(0), UINT8_C(255), UINT8_C(255)
        };
        const CNA_Color expected_clear = {
            UINT8_C(10), UINT8_C(20), UINT8_C(30), UINT8_C(255)
        };
        readback_ok =
            memcmp(&backbuffer[0], &expected_red, sizeof(CNA_Color)) == 0 &&
            memcmp(&backbuffer[1], &expected_green, sizeof(CNA_Color)) == 0 &&
            memcmp(
                &backbuffer[backbuffer_info.width],
                &expected_blue,
                sizeof(CNA_Color)) == 0 &&
            memcmp(
                &backbuffer[(uint64_t)backbuffer_info.width * UINT64_C(4) + UINT64_C(4)],
                &expected_clear,
                sizeof(CNA_Color)) == 0;
    }
    free(backbuffer);
    if (!readback_ok) {
        return CNA_RESULT_INVALID_STATE;
    }
    state->readback_validated = 1;
    ++state->lifecycle_stage;
    ++state->draw_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_update_and_exit(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)out_error;
    if (game_time == 0 || state->lifecycle_stage != 6 ||
        cna_game_request_exit(game) != CNA_RESULT_SUCCESS) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->update_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_unload(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 4 && state->lifecycle_stage != 8) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->unload_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_exit(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    LifecycleState* const state = (LifecycleState*)context;
    (void)game;
    (void)out_error;
    if (game_time != 0) {
        return CNA_RESULT_INVALID_STATE;
    }
    if (state->lifecycle_stage != 3 && state->lifecycle_stage != 7) {
        return CNA_RESULT_INVALID_STATE;
    }
    ++state->lifecycle_stage;
    ++state->exit_count;
    return CNA_RESULT_SUCCESS;
}

static CNA_Result on_failing_load(
    CNA_Handle game,
    const CNA_GameTime* game_time,
    void* context,
    CNA_CallbackError* out_error)
{
    static const char message[] = "test callback failure";
    (void)game;
    (void)game_time;
    (void)context;
    out_error->message.data = message;
    out_error->message.byte_length = sizeof(message) - 1U;
    return CNA_RESULT_INVALID_STATE;
}

static CNA_GameCreateInfo make_create_info(
    const CNA_GameCallbacks* callbacks,
    const char* title,
    uint64_t title_bytes)
{
    CNA_GameCreateInfo create_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {title, title_bytes},
        callbacks
    };
    return create_info;
}

int main(void)
{
    LifecycleState state = {0};
    CNA_KeyboardState synthetic_keyboard = {
        sizeof(CNA_KeyboardState),
        UINT32_C(1),
        {
            UINT64_C(0),
            UINT64_C(1) << (CNA_KEY_A - UINT32_C(64)),
            UINT64_C(0),
            UINT64_C(1) << (CNA_KEY_OEM_CLEAR - UINT32_C(192))
        }
    };
    CNA_Bool key_state = CNA_FALSE;
    uint32_t synthetic_key_count = 0U;
    CNA_Key copied_keys[2] = {UINT32_MAX, UINT32_MAX};
    if (cna_keyboard_state_is_key_down(&synthetic_keyboard, CNA_KEY_A, &key_state) !=
            CNA_RESULT_SUCCESS ||
        key_state != CNA_TRUE ||
        cna_keyboard_state_is_key_up(&synthetic_keyboard, CNA_KEY_B, &key_state) !=
            CNA_RESULT_SUCCESS ||
        key_state != CNA_TRUE ||
        cna_keyboard_state_is_key_down(&synthetic_keyboard, UINT32_C(256), &key_state) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_keyboard_state_get_pressed_key_count(
            &synthetic_keyboard,
            &synthetic_key_count) != CNA_RESULT_SUCCESS ||
        synthetic_key_count != 2U ||
        cna_keyboard_state_copy_pressed_keys(
            &synthetic_keyboard,
            copied_keys,
            1U,
            &synthetic_key_count) != CNA_RESULT_BUFFER_TOO_SMALL ||
        synthetic_key_count != 2U || copied_keys[0] != UINT32_MAX ||
        cna_keyboard_state_copy_pressed_keys(
            &synthetic_keyboard,
            copied_keys,
            2U,
            &synthetic_key_count) != CNA_RESULT_SUCCESS ||
        copied_keys[0] != CNA_KEY_A || copied_keys[1] != CNA_KEY_OEM_CLEAR) {
        return 11;
    }
    CNA_GameCallbacks callbacks = {
        sizeof(CNA_GameCallbacks),
        UINT32_C(1),
        on_load,
        on_update,
        on_draw,
        on_unload,
        on_exit,
        &state
    };
    static const char initial_title[] = "C API lifecycle";
    CNA_GameCreateInfo create_info = make_create_info(
        &callbacks,
        initial_title,
        sizeof(initial_title) - 1U);
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS || game == CNA_INVALID_HANDLE) {
        return 1;
    }
    CNA_Handle graphics_device = CNA_INVALID_HANDLE;
    if (cna_game_get_graphics_device(game, &graphics_device) != CNA_RESULT_INVALID_STATE ||
        graphics_device != CNA_INVALID_HANDLE) {
        return 2;
    }
    WrongThreadState wrong_thread_state = {
        game, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS
    };
    thrd_t wrong_thread;
    int wrong_thread_return = 0;
    if (thrd_create(&wrong_thread, set_title_on_wrong_thread, &wrong_thread_state) != thrd_success ||
        thrd_join(wrong_thread, &wrong_thread_return) != thrd_success ||
        wrong_thread_return != 0 || wrong_thread_state.result != CNA_RESULT_THREAD ||
        wrong_thread_state.keyboard_result != CNA_RESULT_THREAD) {
        return 3;
    }
    if (cna_game_set_window_title(game, (CNA_StringView){"C API title", 11U}) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_SUCCESS ||
        state.load_count != 1 || state.update_count < 1 || state.draw_count != 1 ||
        state.saw_time != 1 || state.borrowed_graphics_device == CNA_INVALID_HANDLE ||
        state.renderer_name_bytes == 0U || state.texture == CNA_INVALID_HANDLE ||
        state.sprite_batch == CNA_INVALID_HANDLE || state.readback_validated != 1) {
        return 4;
    }
    CNA_RendererInfo stale_renderer_info = {
        sizeof(CNA_RendererInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    if (cna_graphics_device_get_renderer_info(
            state.borrowed_graphics_device,
            &stale_renderer_info) != CNA_RESULT_INVALID_HANDLE) {
        return 5;
    }
    CNA_Texture2DInfo live_texture_info = {
        sizeof(CNA_Texture2DInfo), UINT32_C(1), 0U, 0U, 0U, 0U
    };
    if (cna_texture2d_get_info(state.texture, &live_texture_info) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_sprite_batch_destroy(state.sprite_batch) != CNA_RESULT_SUCCESS ||
        cna_sprite_batch_destroy(state.sprite_batch) != CNA_RESULT_INVALID_HANDLE ||
        cna_sprite_batch_end(state.sprite_batch) != CNA_RESULT_INVALID_HANDLE ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_INVALID_HANDLE ||
        cna_texture2d_get_info(state.texture, &live_texture_info) != CNA_RESULT_INVALID_HANDLE) {
        return 6;
    }
    if (cna_game_request_exit(game) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS ||
        state.unload_count != 1 || state.exit_count != 1 ||
        state.lifecycle_stage != 5 ||
        cna_game_run_one_frame(game) != CNA_RESULT_INVALID_HANDLE) {
        return 7;
    }

    callbacks.update = on_update_and_exit;
    callbacks.draw = 0;
    callbacks.unload_content = on_unload;
    callbacks.exiting = on_exit;
    create_info = make_create_info(&callbacks, "", 0U);
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run(game) != CNA_RESULT_SUCCESS || state.texture == CNA_INVALID_HANDLE ||
        state.sprite_batch == CNA_INVALID_HANDLE ||
        cna_sprite_batch_destroy(state.sprite_batch) != CNA_RESULT_SUCCESS ||
        cna_texture2d_destroy(state.texture) != CNA_RESULT_SUCCESS ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS ||
        state.unload_count != 2 || state.exit_count != 2 || state.lifecycle_stage != 9) {
        return 8;
    }

    callbacks.load_content = on_failing_load;
    callbacks.update = 0;
    callbacks.draw = 0;
    callbacks.unload_content = 0;
    callbacks.exiting = 0;
    callbacks.context = 0;
    create_info = make_create_info(&callbacks, "", 0U);
    if (cna_game_create(&create_info, &game) != CNA_RESULT_SUCCESS ||
        cna_game_run_one_frame(game) != CNA_RESULT_CALLBACK) {
        return 9;
    }

    CNA_ErrorInfo error_info = {sizeof(CNA_ErrorInfo), UINT32_C(1), 0U, 0U, 0U};
    uint64_t message_bytes = 0U;
    char message[22] = {0};
    if (cna_error_get_last_info(&error_info) != CNA_RESULT_SUCCESS ||
        error_info.result != CNA_RESULT_CALLBACK ||
        error_info.category != CNA_ERROR_CATEGORY_CALLBACK ||
        error_info.message_byte_length != 21U ||
        cna_error_copy_last_message(message, sizeof(message) - 1U, &message_bytes) !=
            CNA_RESULT_SUCCESS ||
        message_bytes != 21U || memcmp(message, "test callback failure", 21U) != 0 ||
        cna_game_destroy(game) != CNA_RESULT_CALLBACK) {
        return 10;
    }

    return 0;
}
