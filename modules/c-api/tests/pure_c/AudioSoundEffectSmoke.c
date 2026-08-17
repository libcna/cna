// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

#include <string.h>

static CNA_StringView view(const char* const text)
{
    CNA_StringView result;
    result.data = text;
    result.byte_length = (uint64_t)strlen(text);
    return result;
}

/* Decoding an encoded file needs a decoder this build may not have, so both answers are correct. */
static int decoded_or_unsupported(const CNA_Result result)
{
    return result == CNA_RESULT_SUCCESS || result == CNA_RESULT_NOT_SUPPORTED;
}

static int validate_identities(void)
{
    return CNA_AUDIO_STOP_OPTIONS_AS_AUTHORED == UINT32_C(0) &&
        CNA_AUDIO_STOP_OPTIONS_IMMEDIATE == UINT32_C(1) &&
        CNA_AUDIO_STOP_OPTIONS_MAXIMUM == CNA_AUDIO_STOP_OPTIONS_IMMEDIATE;
}

/* The four 3D-audio settings belong to the process, not to a sound effect: the game handle is taken
   for thread affinity only. */
static int validate_static_settings(const CNA_Handle game)
{
    float value = -1.0F;

    if (cna_sound_effect_get_master_volume(game, &value) != CNA_RESULT_SUCCESS || value < 0.0F ||
        cna_sound_effect_get_master_volume(game, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_set_master_volume(game, 0.25F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_get_master_volume(game, &value) != CNA_RESULT_SUCCESS ||
        value != 0.25F || cna_sound_effect_set_master_volume(game, 1.0F) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_sound_effect_get_distance_scale(game, &value) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_set_distance_scale(game, 2.0F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_get_distance_scale(game, &value) != CNA_RESULT_SUCCESS ||
        value != 2.0F || cna_sound_effect_set_distance_scale(game, 1.0F) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    if (cna_sound_effect_get_doppler_scale(game, &value) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_set_doppler_scale(game, 0.5F) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_get_doppler_scale(game, &value) != CNA_RESULT_SUCCESS ||
        value != 0.5F || cna_sound_effect_set_doppler_scale(game, 1.0F) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    return cna_sound_effect_get_speed_of_sound(game, &value) == CNA_RESULT_SUCCESS &&
        cna_sound_effect_set_speed_of_sound(game, 300.0F) == CNA_RESULT_SUCCESS &&
        cna_sound_effect_get_speed_of_sound(game, &value) == CNA_RESULT_SUCCESS &&
        value == 300.0F &&
        cna_sound_effect_set_speed_of_sound(game, 343.5F) == CNA_RESULT_SUCCESS &&
        cna_sound_effect_get_speed_of_sound(game, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

/* Both sample computations are canonical statics, so they take no game handle and no sound. */
static int validate_sample_math(void)
{
    int64_t ticks = INT64_C(-1);
    int32_t bytes = -1;

    /* 1600 bytes of 8 kHz mono PCM16 is 800 frames, a tenth of a second, a million ticks. */
    if (cna_sound_effect_get_sample_duration_ticks(1600, 8000, CNA_AUDIO_CHANNELS_MONO, &ticks) !=
            CNA_RESULT_SUCCESS ||
        ticks < INT64_C(900000) || ticks > INT64_C(1100000) ||
        cna_sound_effect_get_sample_duration_ticks(1600, 8000, UINT32_C(99), &ticks) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_get_sample_duration_ticks(1600, 8000, CNA_AUDIO_CHANNELS_MONO, 0) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_sound_effect_get_sample_size_in_bytes(
               INT64_C(1000000), 8000, CNA_AUDIO_CHANNELS_MONO, &bytes) == CNA_RESULT_SUCCESS &&
        bytes >= 1500 && bytes <= 1700 &&
        cna_sound_effect_get_sample_size_in_bytes(
            INT64_C(1000000), 8000, UINT32_C(99), &bytes) == CNA_RESULT_INVALID_ARGUMENT &&
        cna_sound_effect_get_sample_size_in_bytes(
            INT64_C(1000000), 8000, CNA_AUDIO_CHANNELS_MONO, 0) == CNA_RESULT_INVALID_ARGUMENT;
}

static void write_wav_header(uint8_t* const header, const uint32_t data_bytes)
{
    const uint32_t sample_rate = 8000U;
    const uint32_t byte_rate = sample_rate * 2U;
    memcpy(header, "RIFF", 4U);
    header[4] = (uint8_t)((36U + data_bytes) & 0xFFU);
    header[5] = (uint8_t)(((36U + data_bytes) >> 8U) & 0xFFU);
    header[6] = (uint8_t)(((36U + data_bytes) >> 16U) & 0xFFU);
    header[7] = (uint8_t)(((36U + data_bytes) >> 24U) & 0xFFU);
    memcpy(header + 8, "WAVEfmt ", 8U);
    header[16] = 16U; header[17] = 0U; header[18] = 0U; header[19] = 0U;
    header[20] = 1U; header[21] = 0U;
    header[22] = 1U; header[23] = 0U;
    header[24] = (uint8_t)(sample_rate & 0xFFU);
    header[25] = (uint8_t)((sample_rate >> 8U) & 0xFFU);
    header[26] = 0U; header[27] = 0U;
    header[28] = (uint8_t)(byte_rate & 0xFFU);
    header[29] = (uint8_t)((byte_rate >> 8U) & 0xFFU);
    header[30] = 0U; header[31] = 0U;
    header[32] = 2U; header[33] = 0U;
    header[34] = 16U; header[35] = 0U;
    memcpy(header + 36, "data", 4U);
    header[40] = (uint8_t)(data_bytes & 0xFFU);
    header[41] = (uint8_t)((data_bytes >> 8U) & 0xFFU);
    header[42] = (uint8_t)((data_bytes >> 16U) & 0xFFU);
    header[43] = (uint8_t)((data_bytes >> 24U) & 0xFFU);
}

static int validate_creation(const CNA_Handle game, CNA_Handle* const out_sound_effect)
{
    static uint8_t silence[1600];
    static uint8_t wav[44 + 1600];
    CNA_SoundEffectCreateInfo create_info = {
        sizeof(CNA_SoundEffectCreateInfo),
        UINT32_C(1),
        UINT32_C(8000),
        CNA_AUDIO_CHANNELS_MONO,
        UINT64_C(0)
    };
    CNA_Handle sound_effect = CNA_INVALID_HANDLE;
    CNA_Handle ranged = CNA_INVALID_HANDLE;
    CNA_Handle decoded = CNA_INVALID_HANDLE;
    CNA_Handle silent = CNA_INVALID_HANDLE;

    memset(silence, 0, sizeof(silence));
    memset(wav, 0, sizeof(wav));
    write_wav_header(wav, (uint32_t)sizeof(silence));

    /* The canonical seven-argument constructor takes an explicit range and loop region. */
    if (cna_sound_effect_create_pcm16_range_ext(
            game, &create_info, silence, (uint64_t)sizeof(silence), 0, (int32_t)sizeof(silence),
            0, 0, &sound_effect) != CNA_RESULT_SUCCESS ||
        sound_effect == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_sound_effect_create_pcm16_range_ext(
            game, &create_info, silence, (uint64_t)sizeof(silence), 800, 400, 0, 200,
            &ranged) != CNA_RESULT_SUCCESS ||
        ranged == CNA_INVALID_HANDLE) {
        return 0;
    }
    /* A range that leaves the buffer, a negative offset and an empty count are all refused before
       the decoder ever sees a length nobody checked. */
    {
        CNA_Handle rejected = CNA_INVALID_HANDLE;
        if (cna_sound_effect_create_pcm16_range_ext(
                game, &create_info, silence, (uint64_t)sizeof(silence), 800,
                (int32_t)sizeof(silence), 0, 0, &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            rejected != CNA_INVALID_HANDLE ||
            cna_sound_effect_create_pcm16_range_ext(
                game, &create_info, silence, (uint64_t)sizeof(silence), -1, 400, 0, 0,
                &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_sound_effect_create_pcm16_range_ext(
                game, &create_info, silence, (uint64_t)sizeof(silence), 0, 0, 0, 0,
                &rejected) != CNA_RESULT_INVALID_ARGUMENT ||
            cna_sound_effect_create_pcm16_range_ext(
                game, &create_info, silence, (uint64_t)sizeof(silence), 0, 400, 0, 0, 0) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
    }
    /* Encoded audio is whatever the backend can decode, so a real WAV either loads or reports that
       this build cannot decode it. An empty buffer is refused either way. */
    {
        /* A refused creation clears its output first, so the refusal checks take a handle of their
           own rather than destroying the one just created. */
        CNA_Handle rejected = CNA_INVALID_HANDLE;
        const CNA_Result created = cna_sound_effect_create_from_encoded_ext(
            game, wav, (uint64_t)sizeof(wav), &decoded);
        if (!decoded_or_unsupported(created) ||
            (created == CNA_RESULT_SUCCESS && decoded == CNA_INVALID_HANDLE) ||
            cna_sound_effect_create_from_encoded_ext(game, wav, UINT64_C(0), &rejected) !=
                CNA_RESULT_INVALID_ARGUMENT ||
            rejected != CNA_INVALID_HANDLE ||
            cna_sound_effect_create_from_encoded_ext(game, wav, (uint64_t)sizeof(wav), 0) !=
                CNA_RESULT_INVALID_ARGUMENT) {
            return 0;
        }
        if (created == CNA_RESULT_SUCCESS &&
            cna_sound_effect_destroy(decoded) != CNA_RESULT_SUCCESS) {
            return 0;
        }
    }
    /* An empty asset path is not an error: the canonical constructor answers an effect with no
       audio rather than throwing, and this route reports that as it is. */
    if (cna_sound_effect_create_from_asset_ext(game, view(""), 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_create_from_asset_ext(game, view(""), &silent) != CNA_RESULT_SUCCESS ||
        silent == CNA_INVALID_HANDLE) {
        return 0;
    }
    if (cna_sound_effect_destroy(silent) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_destroy(ranged) != CNA_RESULT_SUCCESS) {
        return 0;
    }
    *out_sound_effect = sound_effect;
    return 1;
}

static int validate_effect(const CNA_Handle sound_effect)
{
    CNA_Handle instance = CNA_INVALID_HANDLE;
    CNA_Bool flag = UINT8_C(9);
    uint64_t bytes = UINT64_C(9);
    char text[128];

    if (cna_sound_effect_get_is_disposed(sound_effect, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_sound_effect_get_is_disposed(sound_effect, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* The name round-trips; the canonical class has two setters that store the same name, so C has
       one route. */
    memset(text, 0, sizeof(text));
    if (cna_sound_effect_get_name_size(sound_effect, &bytes) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_set_name(sound_effect, view("footstep")) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_get_name_size(sound_effect, &bytes) != CNA_RESULT_SUCCESS ||
        bytes != UINT64_C(8) ||
        cna_sound_effect_copy_name(sound_effect, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "footstep") != 0 ||
        cna_sound_effect_copy_name(sound_effect, text, UINT64_C(2), &bytes) !=
            CNA_RESULT_BUFFER_TOO_SMALL) {
        return 0;
    }
    memset(text, 0, sizeof(text));
    if (cna_sound_effect_get_type_name_size(sound_effect, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_sound_effect_copy_type_name(sound_effect, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Audio.SoundEffect") != 0) {
        return 0;
    }
    /* Fire-and-forget playback. The canonical asymmetry is preserved: pan is range-checked and
       pitch is clamped, so an extreme pitch plays and an out-of-range pan is refused. */
    if (cna_sound_effect_play(sound_effect, &flag) != CNA_RESULT_SUCCESS ||
        (flag != CNA_FALSE && flag != CNA_TRUE) ||
        cna_sound_effect_play(sound_effect, 0) != CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_play_with_settings(sound_effect, 0.5F, 99.0F, 0.0F, &flag) !=
            CNA_RESULT_SUCCESS ||
        cna_sound_effect_play_with_settings(sound_effect, 0.5F, 0.0F, 2.0F, &flag) !=
            CNA_RESULT_INVALID_ARGUMENT ||
        cna_sound_effect_play_with_settings(sound_effect, 0.5F, 0.0F, -2.0F, &flag) !=
            CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    /* An instance answers its own disposal state and type name. */
    memset(text, 0, sizeof(text));
    if (cna_sound_effect_create_instance(sound_effect, &instance) != CNA_RESULT_SUCCESS ||
        cna_sound_effect_instance_get_is_disposed(instance, &flag) != CNA_RESULT_SUCCESS ||
        flag != CNA_FALSE ||
        cna_sound_effect_instance_get_type_name_size(instance, &bytes) != CNA_RESULT_SUCCESS ||
        bytes >= (uint64_t)sizeof(text) ||
        cna_sound_effect_instance_copy_type_name(instance, text, (uint64_t)sizeof(text), &bytes) !=
            CNA_RESULT_SUCCESS ||
        strcmp(text, "Microsoft.Xna.Framework.Audio.SoundEffectInstance") != 0 ||
        cna_sound_effect_instance_get_is_disposed(instance, 0) != CNA_RESULT_INVALID_ARGUMENT) {
        return 0;
    }
    return cna_sound_effect_instance_destroy(instance) == CNA_RESULT_SUCCESS &&
        cna_sound_effect_instance_get_is_disposed(instance, &flag) == CNA_RESULT_INVALID_HANDLE;
}

int main(void)
{
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API sound effect smoke", UINT64_C(24)},
        0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    CNA_Handle sound_effect = CNA_INVALID_HANDLE;

    if (!validate_identities() || !validate_sample_math()) {
        return 1;
    }
    if (cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        return 2;
    }
    if (!validate_static_settings(game)) {
        return 3;
    }
    if (!validate_creation(game, &sound_effect)) {
        return 4;
    }
    if (!validate_effect(sound_effect)) {
        return 5;
    }
    {
        /* Argument validation runs before handle validation everywhere in this ABI, so a stale
           handle is checked with a real output rather than a null one. */
        CNA_Bool disposed = UINT8_C(9);
        if (cna_sound_effect_destroy(sound_effect) != CNA_RESULT_SUCCESS ||
            cna_sound_effect_get_is_disposed(sound_effect, &disposed) !=
                CNA_RESULT_INVALID_HANDLE) {
            return 6;
        }
    }
    return cna_game_destroy(game) == CNA_RESULT_SUCCESS ? 0 : 7;
}
