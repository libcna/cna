// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <string.h>
#include <threads.h>

typedef struct WrongThreadState {
    CNA_Handle instance;
    CNA_Result info_result;
    CNA_Result destroy_result;
} WrongThreadState;

static int inspect_on_wrong_thread(void* const context)
{
    WrongThreadState* const state = (WrongThreadState*)context;
    CNA_SoundEffectInstanceInfo info = {
        sizeof(CNA_SoundEffectInstanceInfo), UINT32_C(1), 0U, CNA_FALSE,
        {0U, 0U, 0U}, 0.0F, 0.0F, 0.0F, 0U
    };
    state->info_result = cna_sound_effect_instance_get_info(state->instance, &info);
    state->destroy_result = cna_sound_effect_instance_destroy(state->instance);
    return 0;
}

int main(void)
{
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API audio smoke", UINT64_C(17)},
        0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        return 1;
    }

    static const uint8_t silence[1600] = {0U};
    CNA_SoundEffectCreateInfo create_info = {
        sizeof(CNA_SoundEffectCreateInfo),
        UINT32_C(1),
        UINT32_C(8000),
        CNA_AUDIO_CHANNELS_MONO,
        UINT64_C(0)
    };
    CNA_Handle sound_effect = UINT64_C(77);
    if (cna_sound_effect_create_pcm16(
            game,
            &create_info,
            silence,
            sizeof(silence) - 1U,
            &sound_effect) != CNA_RESULT_INVALID_ARGUMENT ||
        sound_effect != CNA_INVALID_HANDLE) {
        return 2;
    }
    create_info.reserved = UINT64_C(1);
    if (cna_sound_effect_create_pcm16(
            game,
            &create_info,
            silence,
            sizeof(silence),
            &sound_effect) != CNA_RESULT_INVALID_ARGUMENT ||
        sound_effect != CNA_INVALID_HANDLE) {
        return 3;
    }
    create_info.reserved = UINT64_C(0);
    if (cna_sound_effect_create_pcm16(
            game,
            &create_info,
            silence,
            sizeof(silence),
            &sound_effect) != CNA_RESULT_SUCCESS ||
        sound_effect == CNA_INVALID_HANDLE) {
        return 4;
    }

    int64_t duration_ticks = 0;
    if (cna_sound_effect_get_duration_ticks(sound_effect, &duration_ticks) != CNA_RESULT_SUCCESS ||
        duration_ticks < INT64_C(900000) || duration_ticks > INT64_C(1100000)) {
        return 5;
    }

    CNA_Handle instance = CNA_INVALID_HANDLE;
    if (cna_sound_effect_create_instance(sound_effect, &instance) != CNA_RESULT_SUCCESS ||
        instance == CNA_INVALID_HANDLE) {
        return 6;
    }
    CNA_SoundEffectInstanceInfo info = {
        sizeof(CNA_SoundEffectInstanceInfo), UINT32_C(1), 0U, CNA_FALSE,
        {0U, 0U, 0U}, 0.0F, 0.0F, 0.0F, 0U
    };
    if (cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        info.state != CNA_SOUND_STATE_STOPPED || info.is_looped != CNA_FALSE ||
        info.volume != 1.0F || info.pitch != 0.0F || info.pan != 0.0F ||
        info.reserved1 != 0U) {
        return 7;
    }

    if (cna_sound_effect_instance_set_volume(instance, 0.25F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_pitch(instance, 2.0F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_pan(instance, -0.5F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_is_looped(instance, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_pan(instance, 1.5F) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_instance_set_volume(instance, NAN) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_instance_stop(instance, UINT8_C(2)) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        info.volume != 0.25F || info.pitch != 1.0F || info.pan != -0.5F ||
        info.is_looped != CNA_TRUE) {
        return 8;
    }

    if (cna_game_destroy(game) != CNA_RESULT_INVALID_STATE ||
        cna_sound_effect_destroy(sound_effect) != CNA_RESULT_INVALID_STATE ||
        cna_sound_effect_instance_play(instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        info.state != CNA_SOUND_STATE_PLAYING ||
        cna_sound_effect_instance_set_is_looped(instance, CNA_FALSE) !=
            CNA_RESULT_INVALID_STATE ||
        cna_sound_effect_instance_pause(instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        info.state != CNA_SOUND_STATE_PAUSED ||
        cna_sound_effect_instance_resume(instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        info.state != CNA_SOUND_STATE_PLAYING ||
        cna_sound_effect_instance_stop(instance, CNA_FALSE) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_stop(instance, CNA_TRUE) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS ||
        info.state != CNA_SOUND_STATE_STOPPED) {
        return 9;
    }

    WrongThreadState wrong_thread = {instance, CNA_RESULT_SUCCESS, CNA_RESULT_SUCCESS};
    thrd_t thread;
    if (thrd_create(&thread, inspect_on_wrong_thread, &wrong_thread) != thrd_success ||
        thrd_join(thread, 0) != thrd_success ||
        wrong_thread.info_result != CNA_RESULT_THREAD ||
        wrong_thread.destroy_result != CNA_RESULT_THREAD) {
        return 10;
    }

    if (cna_sound_effect_instance_destroy(instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_INVALID_HANDLE ||
        cna_sound_effect_destroy(sound_effect) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_destroy(sound_effect) != CNA_RESULT_INVALID_HANDLE ||
        cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 11;
    }
    return 0;
}
