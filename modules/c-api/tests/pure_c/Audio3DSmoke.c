// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <math.h>
#include <string.h>

/* An emitter and a listener are values, so the defaults are checked without any runtime at all. */
static int validate_defaults(void)
{
    CNA_AudioEmitter emitter;
    CNA_AudioListener listener;

    memset(&emitter, 0xEE, sizeof(emitter));
    memset(&listener, 0xEE, sizeof(listener));

    if (cna_audio_emitter_init(&emitter) != CNA_RESULT_SUCCESS ||
        cna_audio_emitter_init(0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_audio_listener_init(&listener) != CNA_RESULT_SUCCESS ||
        cna_audio_listener_init(0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    if (emitter.struct_size != (uint32_t)sizeof(CNA_AudioEmitter) ||
        emitter.struct_version != UINT32_C(1) ||
        listener.struct_size != (uint32_t)sizeof(CNA_AudioListener) ||
        listener.struct_version != UINT32_C(1)) {
        return 0;
    }
    /* Facing forward is -Z with +Y up, exactly as the canonical constants define it. */
    if (emitter.forward.z != -1.0F || emitter.up.y != 1.0F || listener.forward.z != -1.0F ||
        listener.up.y != 1.0F) {
        return 0;
    }
    return emitter.doppler_scale == 1.0F && emitter.position.x == 0.0F &&
        emitter.position.y == 0.0F && emitter.position.z == 0.0F && emitter.velocity.x == 0.0F &&
        emitter.velocity.y == 0.0F && emitter.velocity.z == 0.0F && listener.position.x == 0.0F &&
        listener.position.y == 0.0F && listener.position.z == 0.0F && listener.velocity.x == 0.0F &&
        listener.velocity.y == 0.0F && listener.velocity.z == 0.0F;
}

static int validate_structure_refusals(const CNA_Handle instance)
{
    CNA_AudioEmitter emitter;
    CNA_AudioListener listener;
    CNA_AudioEmitter broken_emitter;
    CNA_AudioListener broken_listener;
    const float infinity = (float)INFINITY;

    if (cna_audio_emitter_init(&emitter) != CNA_RESULT_SUCCESS ||
        cna_audio_listener_init(&listener) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_sound_effect_instance_apply_3d(instance, 0, &emitter) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_instance_apply_3d(instance, &listener, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    broken_listener = listener;
    broken_listener.struct_version = UINT32_C(9999);
    if (cna_sound_effect_instance_apply_3d(instance, &broken_listener, &emitter) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    broken_listener = listener;
    broken_listener.struct_size = UINT32_C(4);
    if (cna_sound_effect_instance_apply_3d(instance, &broken_listener, &emitter) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    broken_listener = listener;
    broken_listener.position.y = infinity;
    if (cna_sound_effect_instance_apply_3d(instance, &broken_listener, &emitter) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }

    broken_emitter = emitter;
    broken_emitter.struct_version = UINT32_C(9999);
    if (cna_sound_effect_instance_apply_3d(instance, &listener, &broken_emitter) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    broken_emitter = emitter;
    broken_emitter.doppler_scale = infinity;
    if (cna_sound_effect_instance_apply_3d(instance, &listener, &broken_emitter) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    broken_emitter = emitter;
    broken_emitter.velocity.z = infinity;
    return cna_sound_effect_instance_apply_3d(instance, &listener, &broken_emitter) ==
        CNA_RESULT_INVALID_ARGUMENT;
}

/* The process-wide 3D settings are what Apply3D reads, so they are moved before positioning. */
static int validate_positioning(const CNA_Handle game, const CNA_Handle instance)
{
    CNA_AudioEmitter emitter;
    CNA_AudioListener listener;
    CNA_SoundEffectInstanceInfo info = {sizeof(CNA_SoundEffectInstanceInfo), UINT32_C(1),
                                        UINT32_C(0), CNA_FALSE, {0U, 0U, 0U},
                                        0.0F, 0.0F, 0.0F, 0U};

    if (cna_audio_emitter_init(&emitter) != CNA_RESULT_SUCCESS ||
        cna_audio_listener_init(&listener) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_sound_effect_set_distance_scale(game, 4.0F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_set_doppler_scale(game, 2.0F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_set_speed_of_sound(game, 343.5F) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Right of the listener, well beyond the distance scale, and closing fast. */
    emitter.position.x = 40.0F;
    emitter.velocity.x = -60.0F;
    listener.forward.z = -1.0F;
    listener.up.y = 1.0F;
    if (cna_sound_effect_instance_play(instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_apply_3d(instance, &listener, &emitter) != CNA_RESULT_SUCCESS) {
        return 0;
    }

    /* Positioning latches spatial gain, pan and pitch inside the instance; the instance's own
       properties keep reading back whatever the caller last set them to. */
    if (cna_sound_effect_instance_set_volume(instance, 0.75F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_pitch(instance, 0.25F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_set_pan(instance, -1.0F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_info(instance, &info) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (info.volume != 0.75F || info.pitch != 0.25F || info.pan != -1.0F) {
        return 0;
    }

    /* Positioning again from the other side is accepted just as the first call was. */
    emitter.position.x = -40.0F;
    emitter.velocity.x = 60.0F;
    if (cna_sound_effect_instance_apply_3d(instance, &listener, &emitter) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_sound_effect_set_distance_scale(game, 1.0F) == CNA_RESULT_SUCCESS &&
        cna_sound_effect_set_doppler_scale(game, 1.0F) == CNA_RESULT_SUCCESS &&
        cna_sound_effect_instance_stop(instance, CNA_TRUE) == CNA_RESULT_SUCCESS;
}

/* This runtime supports exactly one listener; every other count is refused, not approximated. */
static int validate_multi_listener(const CNA_Handle instance)
{
    CNA_AudioEmitter emitter;
    CNA_AudioListener listeners[2];

    if (cna_audio_emitter_init(&emitter) != CNA_RESULT_SUCCESS ||
        cna_audio_listener_init(&listeners[0]) != CNA_RESULT_SUCCESS ||
        cna_audio_listener_init(&listeners[1]) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    listeners[1].position.x = 10.0F;
    emitter.position.z = 5.0F;

    if (cna_sound_effect_instance_apply_3d_multi_ext(instance, listeners, UINT64_C(1), &emitter) !=
        CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_sound_effect_instance_apply_3d_multi_ext(instance, listeners, UINT64_C(2), &emitter) !=
        CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    /* An empty array is a count the canonical overload refuses, not a null array. */
    if (cna_sound_effect_instance_apply_3d_multi_ext(instance, listeners, UINT64_C(0), &emitter) !=
        CNA_RESULT_NOT_SUPPORTED) {
        return 0;
    }
    if (cna_sound_effect_instance_apply_3d_multi_ext(instance, 0, UINT64_C(1), &emitter) !=
        CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_sound_effect_instance_apply_3d_multi_ext(instance, listeners, UINT64_C(1), 0) ==
        CNA_RESULT_INVALID_ARGUMENT;
}

int main(void)
{
    static uint8_t silence[1600];
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API 3D audio smoke", UINT64_C(20)},
        0
    };
    CNA_SoundEffectCreateInfo create_info = {
        sizeof(CNA_SoundEffectCreateInfo),
        UINT32_C(1),
        UINT32_C(8000),
        CNA_AUDIO_CHANNELS_MONO,
        UINT64_C(0)
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Handle sound_effect = CNA_INVALID_HANDLE;
    CNA_Handle instance = CNA_INVALID_HANDLE;
    CNA_AudioEmitter emitter;
    CNA_AudioListener listener;

    memset(silence, 0, sizeof(silence));

    if (!validate_defaults()) {
        return 1;
    }
    if (cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        return 2;
    }
    if (cna_sound_effect_create_pcm16_range_ext(
            game, &create_info, silence, (uint64_t)sizeof(silence), 0, (int32_t)sizeof(silence),
            0, 0, &sound_effect) != CNA_RESULT_SUCCESS) {
        return 3;
    }
    if (cna_sound_effect_create_instance(sound_effect, &instance) != CNA_RESULT_SUCCESS ||
        instance == CNA_INVALID_HANDLE) {
        return 4;
    }
    if (!validate_structure_refusals(instance)) {
        return 5;
    }
    if (!validate_positioning(game, instance)) {
        return 6;
    }
    if (!validate_multi_listener(instance)) {
        return 7;
    }
    if (cna_audio_emitter_init(&emitter) != CNA_RESULT_SUCCESS ||
        cna_audio_listener_init(&listener) != CNA_RESULT_SUCCESS) {
        return 8;
    }
    /* A released instance is gone; positioning it is a handle failure, not a disposal report. */
    if (cna_sound_effect_instance_destroy(instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_apply_3d(instance, &listener, &emitter) !=
            CNA_RESULT_INVALID_HANDLE ||
        cna_sound_effect_instance_apply_3d_multi_ext(instance, &listener, UINT64_C(1), &emitter) !=
            CNA_RESULT_INVALID_HANDLE) {
        return 9;
    }
    if (cna_sound_effect_destroy(sound_effect) != CNA_RESULT_SUCCESS) {
        return 10;
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 11;
}
