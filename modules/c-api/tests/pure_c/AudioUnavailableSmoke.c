// SPDX-License-Identifier: MS-PL

#include <CNA/C/cna.h>

int main(void)
{
    CNA_GameCreateInfo game_info = {
        sizeof(CNA_GameCreateInfo),
        UINT32_C(1),
        CNA_TRUE,
        {0U, 0U, 0U, 0U, 0U, 0U, 0U},
        INT64_C(166667),
        {"C API unavailable audio smoke", UINT64_C(29)},
        0
    };
    CNA_Handle game = CNA_INVALID_HANDLE;
    if (cna_game_create(&game_info, &game) != CNA_RESULT_SUCCESS) {
        return 1;
    }

    CNA_AudioCapabilities capabilities = {
        sizeof(CNA_AudioCapabilities), UINT32_C(1), CNA_TRUE,
        {UINT8_C(1), UINT8_C(2), UINT8_C(3)}, UINT32_C(4)
    };
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (cna_audio_get_capabilities(game, &capabilities) != CNA_RESULT_SUCCESS ||
            capabilities.is_playback_available != CNA_FALSE ||
            capabilities.reserved0[0] != 0U || capabilities.reserved0[1] != 0U ||
            capabilities.reserved0[2] != 0U || capabilities.reserved1 != 0U) {
            return 2;
        }
    }

    static const uint8_t silence[16] = {0U};
    const CNA_SoundEffectCreateInfo create_info = {
        sizeof(CNA_SoundEffectCreateInfo),
        UINT32_C(1),
        UINT32_C(8000),
        CNA_AUDIO_CHANNELS_MONO,
        UINT64_C(0)
    };
    for (int attempt = 0; attempt < 2; ++attempt) {
        CNA_Handle sound_effect = UINT64_C(77);
        if (cna_sound_effect_create_pcm16(
                game,
                &create_info,
                silence,
                sizeof(silence),
                &sound_effect) != CNA_RESULT_NOT_SUPPORTED ||
            sound_effect != CNA_INVALID_HANDLE) {
            return 3;
        }
        CNA_ErrorInfo error = {
            sizeof(CNA_ErrorInfo), UINT32_C(1), CNA_RESULT_SUCCESS, CNA_ERROR_CATEGORY_NONE, 0U
        };
        if (cna_error_get_last_info(&error) != CNA_RESULT_SUCCESS ||
            error.result != CNA_RESULT_NOT_SUPPORTED ||
            error.category != CNA_ERROR_CATEGORY_NOT_SUPPORTED ||
            error.message_byte_length == 0U) {
            return 4;
        }
    }

    if (cna_game_destroy(game) != CNA_RESULT_SUCCESS) {
        return 5;
    }
    return 0;
}
