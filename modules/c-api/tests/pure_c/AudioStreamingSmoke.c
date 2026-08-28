// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include "CnaTestReport.h"

#include <string.h>

typedef struct EventState {
    int calls;
} EventState;

static void on_audio_event(void* const context)
{
    ++((EventState*)context)->calls;
}

static int validate_identities(void)
{
    return CNA_MICROPHONE_STATE_STARTED == UINT32_C(0) &&
        CNA_MICROPHONE_STATE_STOPPED == UINT32_C(1) &&
        CNA_MICROPHONE_STATE_MAXIMUM == CNA_MICROPHONE_STATE_STOPPED;
}

/* A streaming instance is a sound-effect instance: it lives under the same handle kind, so every
   instance route accepts it, and what this kind adds is the buffer queue. */
static int validate_streaming(const CNA_Handle game)
{
    static uint8_t pcm[1600];
    static float floats[400];
    CNA_Handle instance = CNA_INVALID_HANDLE;
    CNA_Handle floating = CNA_INVALID_HANDLE;
    CNA_Handle rejected = CNA_INVALID_HANDLE;
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_SoundEffectInstanceInfo info;
    EventState needed = {0};
    int32_t value = -1;
    int64_t ticks = INT64_C(-1);
    uint64_t bytes = UINT64_C(9);
    char text[128];

    memset(pcm, 0, sizeof(pcm));
    memset(floats, 0, sizeof(floats));
    memset(&info, 0, sizeof(info));

    if (cna_dynamic_sound_effect_instance_create(game, 8000, CNA_AUDIO_CHANNELS_MONO, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_dynamic_sound_effect_instance_create(game, 8000, UINT32_C(99), &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        rejected != CNA_INVALID_HANDLE ||
        cna_dynamic_sound_effect_instance_create(game, 0, CNA_AUDIO_CHANNELS_MONO, &rejected) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_dynamic_sound_effect_instance_create(
            game, 8000, CNA_AUDIO_CHANNELS_MONO, &instance) != CNA_RESULT_SUCCESS ||
        instance == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The ordinary instance routes accept it unchanged, which is the point of sharing the kind. */
    info.struct_size = (uint32_t)sizeof(info);
    info.struct_version = UINT32_C(1);
    if (cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_volume(instance, 0.5F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_is_looped(instance, CNA_FALSE) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_dynamic_sound_effect_instance_get_type_name_size(instance, &bytes) !=
            CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_dynamic_sound_effect_instance_copy_type_name(
            instance, text, (uint64_t)sizeof(text), &bytes) != CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Audio.DynamicSoundEffectInstance") != 0) {
        return 0;
    }
    /* These two computations are instance methods: they use the rate and channel count the instance
       was created with rather than taking them as arguments. */
    if (cna_dynamic_sound_effect_instance_get_sample_duration_ticks(instance, 1600, &ticks) !=
            CNA_RESULT_SUCCESS ||
        ticks < INT64_C(900000) || ticks > INT64_C(1100000) ||
        cna_dynamic_sound_effect_instance_get_sample_size_in_bytes(
            instance, INT64_C(1000000), &value) != CNA_RESULT_SUCCESS ||
        value < 1500 || value > 1700 ||
        cna_dynamic_sound_effect_instance_get_sample_duration_ticks(instance, 1600, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Submitting copies the bytes, so this buffer may be reused the moment the call returns. The
       pending count only shrinks once playback has consumed a buffer, not when it is handed over. */
    if (cna_dynamic_sound_effect_instance_get_pending_buffer_count(instance, &value) !=
            CNA_RESULT_SUCCESS ||
        value != 0 ||
        cna_dynamic_sound_effect_instance_submit_buffer(
            instance, pcm, (uint64_t)sizeof(pcm), 0, (int32_t)sizeof(pcm)) != CNA_RESULT_SUCCESS ||
        cna_dynamic_sound_effect_instance_get_pending_buffer_count(instance, &value) !=
            CNA_RESULT_SUCCESS ||
        value < 1) {
        return 0;
    }
    memset(pcm, 0x7F, sizeof(pcm));
    if (cna_dynamic_sound_effect_instance_get_pending_buffer_count(instance, &value) !=
            CNA_RESULT_SUCCESS ||
        value < 1) {
        return 0;
    }
    /* A range that leaves the buffer, a negative offset and an empty count are refused. */
    if (cna_dynamic_sound_effect_instance_submit_buffer(
            instance, pcm, (uint64_t)sizeof(pcm), 800, (int32_t)sizeof(pcm)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_dynamic_sound_effect_instance_submit_buffer(
            instance, pcm, (uint64_t)sizeof(pcm), -1, 400) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_dynamic_sound_effect_instance_submit_buffer(
            instance, pcm, (uint64_t)sizeof(pcm), 0, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (cna_dynamic_sound_effect_instance_subscribe_buffer_needed(
            instance, 0, &needed, &registration) != CNA_RESULT_INVALID_ARGUMENT ||
        registration != CNA_INVALID_HANDLE ||
        cna_dynamic_sound_effect_instance_subscribe_buffer_needed(
            instance, on_audio_event, &needed, &registration) != CNA_RESULT_SUCCESS ||
        registration == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* Advancing the queue is what raises the event; a caller running the game loop never needs it,
       because the framework pump does it. */
    if (cna_dynamic_sound_effect_instance_update_ext(instance) != CNA_RESULT_SUCCESS ||
        cna_dynamic_sound_effect_instance_queue_initial_buffers_ext(instance) !=
            CNA_RESULT_SUCCESS ||
        cna_dynamic_sound_effect_instance_clear_buffers_ext(instance) != CNA_RESULT_SUCCESS ||
        cna_dynamic_sound_effect_instance_get_pending_buffer_count(instance, &value) !=
            CNA_RESULT_SUCCESS ||
        value != 0) {
        return 0;
    }
    if (cna_audio_unsubscribe_ext(registration) != CNA_RESULT_SUCCESS ||
        cna_audio_unsubscribe_ext(registration) != CNA_RESULT_INVALID_HANDLE) {
        return 0;
    }
    /* A float-fed instance is the same kind with a different sample format. */
    if (cna_dynamic_sound_effect_instance_create(
            game, 8000, CNA_AUDIO_CHANNELS_MONO, &floating) != CNA_RESULT_SUCCESS ||
        cna_dynamic_sound_effect_instance_submit_float_buffer_ext(
            floating, floats, (uint64_t)(sizeof(floats) / sizeof(floats[0])), 0,
            (int32_t)(sizeof(floats) / sizeof(floats[0]))) != CNA_RESULT_SUCCESS ||
        cna_dynamic_sound_effect_instance_submit_float_buffer_ext(
            floating, floats, (uint64_t)(sizeof(floats) / sizeof(floats[0])), 0, 0) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_instance_destroy(floating) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    /* An instance that does not stream refuses every streaming route, which is how a caller learns
       the two kinds apart. */
    {
        static const uint8_t silence[1600] = {0U};
        CNA_SoundEffectCreateInfo create_info = {
            sizeof(CNA_SoundEffectCreateInfo), UINT32_C(1), UINT32_C(8000),
            CNA_AUDIO_CHANNELS_MONO, UINT64_C(0)
        };
        CNA_Handle effect = CNA_INVALID_HANDLE;
        CNA_Handle plain = CNA_INVALID_HANDLE;
        if (cna_sound_effect_create_pcm16(
                game, &create_info, silence, (uint64_t)sizeof(silence), &effect) !=
                CNA_RESULT_SUCCESS ||
            cna_sound_effect_create_instance(effect, &plain) != CNA_RESULT_SUCCESS ||
            cna_dynamic_sound_effect_instance_get_pending_buffer_count(plain, &value) !=
                CNA_RESULT_INVALID_STATE ||
            cna_dynamic_sound_effect_instance_update_ext(plain) != CNA_RESULT_INVALID_STATE ||
            cna_sound_effect_instance_destroy(plain) != CNA_RESULT_SUCCESS ||
            cna_sound_effect_destroy(effect) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    return cna_sound_effect_instance_destroy(instance) == CNA_RESULT_SUCCESS &&
        cna_dynamic_sound_effect_instance_get_pending_buffer_count(instance, &value) ==
            CNA_RESULT_INVALID_HANDLE;
}

/* No verification machine has a capture device, so the count is zero and every index route refuses.
   That is the microphone's real availability rather than a gap in this ABI. */
static int validate_capture(const CNA_Handle game)
{
    CNA_Handle registration = CNA_INVALID_HANDLE;
    CNA_MicrophoneState state = UINT32_C(99);
    CNA_Bool available = UINT8_C(9);
    EventState ready = {0};
    uint64_t count = UINT64_C(99);
    uint64_t index = UINT64_C(1234);
    uint64_t bytes = UINT64_C(9);
    int32_t value = -1;
    int64_t ticks = INT64_C(-1);
    uint8_t captured[64];
    char text[128];

    if (cna_microphone_get_count(game, &count) != CNA_RESULT_SUCCESS ||
        cna_microphone_get_count(game, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Availability is separate from the answer: with no default microphone the flag is clear and
       the index is left exactly as the caller set it. */
    if (cna_microphone_get_default_index_ext(game, &index, &available) != CNA_RESULT_SUCCESS ||
        (available == CNA_FALSE && index != UINT64_C(1234)) ||
        cna_microphone_get_default_index_ext(game, 0, &available) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* Every route below the count answers; the first index past it is refused. */
    {
        uint64_t at = UINT64_C(0);
        for (at = UINT64_C(0); at < count; ++at) {
            memset(text, 0, sizeof(text));
            if (cna_microphone_get_name_size_at(game, at, &bytes) != CNA_RESULT_SUCCESS ||
                bytes >= (uint64_t)sizeof(text) ||
                cna_microphone_copy_name_at(game, at, text, (uint64_t)sizeof(text), &bytes) !=
                    CNA_RESULT_SUCCESS ||
                cna_microphone_get_state_at(game, at, &state) != CNA_RESULT_SUCCESS ||
                state > CNA_MICROPHONE_STATE_MAXIMUM ||
                cna_microphone_get_sample_rate_at(game, at, &value) != CNA_RESULT_SUCCESS ||
                cna_microphone_get_buffer_duration_ticks_at(game, at, &ticks) !=
                    CNA_RESULT_SUCCESS ||
                cna_microphone_get_is_headset_at(game, at, &available) != CNA_RESULT_SUCCESS ||
                cna_microphone_get_data_at(
                    game, at, captured, (uint64_t)sizeof(captured), &bytes) !=
                    CNA_RESULT_SUCCESS ||
                bytes > (uint64_t)sizeof(captured)) {
                return 0;
            }
        }
    }
    if (cna_microphone_get_name_size_at(game, count, &bytes) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_get_state_at(game, count, &state) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_start_at(game, count) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_stop_at(game, count) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_get_data_at(game, count, captured, (uint64_t)sizeof(captured), &bytes) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_get_sample_duration_ticks_at(game, count, 1600, &ticks) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_get_sample_size_in_bytes_at(game, count, INT64_C(1000000), &value) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_set_buffer_duration_ticks_at(game, count, INT64_C(1000000)) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_microphone_subscribe_buffer_ready_at(
            game, count, on_audio_event, &ready, &registration) != CNA_RESULT_INVALID_ARGUMENT ||
        registration != CNA_INVALID_HANDLE) {
        return 0;
    }
    /* The type name belongs to the type, so it answers on a machine with no microphone at all. */
    memset(text, 0, sizeof(text));
    return cna_microphone_get_type_name_size(game, &bytes) == CNA_RESULT_SUCCESS &&
        bytes < (uint64_t)sizeof(text) &&
        cna_microphone_copy_type_name(game, text, (uint64_t)sizeof(text), &bytes) ==
            CNA_RESULT_SUCCESS &&
        strcmp(text, "Microsoft.Xna.Framework.Audio.Microphone") == 0 &&
        cna_microphone_copy_type_name(game, text, UINT64_C(2), &bytes) ==
            CNA_RESULT_BUFFER_TOO_SMALL &&
        cna_microphone_check_all_buffers_ext(game) == CNA_RESULT_SUCCESS;
}

int main(void)
{
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API streaming audio smoke", UINT64_C(27)},
        0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;

    if (!validate_identities()) {
        return CNA_TEST_FAIL(1);
    }
    if (cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        return CNA_TEST_FAIL(2);
    }
    if (!validate_streaming(game)) {
        return CNA_TEST_FAIL(3);
    }
    if (!validate_capture(game)) {
        return CNA_TEST_FAIL(4);
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 5;
}
