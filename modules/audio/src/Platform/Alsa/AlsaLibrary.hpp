// SPDX-License-Identifier: MS-PL
#pragma once

#include <alsa/asoundlib.h>

#include <string>

namespace CNA::Audio::Platform::Alsa {

    /**
     * @brief The part of libasound CNA uses, resolved once from `libasound.so.2` loaded at run
     * time -- by playback (AlsaAudioDevice) and capture (AlsaAudioRecordingDevice) alike.
     *
     * Declared from ALSA's own headers, so a signature can never drift from the library. Immortal:
     * an audio thread may still be running while static destructors run.
     */
    struct AlsaApi
    {
        /** @brief Whether every PCM function was found. */
        bool loaded = false;
        /** @brief Whether the capture and enumeration functions were found as well. */
        bool captureLoaded = false;
        /** @brief Why loading failed, when it did. */
        std::string error;

        // --- PCM, both directions -------------------------------------------------------------
        decltype(&::snd_pcm_open) PcmOpen = nullptr;
        decltype(&::snd_pcm_close) PcmClose = nullptr;
        decltype(&::snd_pcm_type) PcmType = nullptr;
        decltype(&::snd_pcm_hw_params_malloc) HwParamsMalloc = nullptr;
        decltype(&::snd_pcm_hw_params_free) HwParamsFree = nullptr;
        decltype(&::snd_pcm_hw_params_any) HwParamsAny = nullptr;
        decltype(&::snd_pcm_hw_params_set_access) HwParamsSetAccess = nullptr;
        decltype(&::snd_pcm_hw_params_set_format) HwParamsSetFormat = nullptr;
        decltype(&::snd_pcm_hw_params_set_channels_near) HwParamsSetChannelsNear = nullptr;
        decltype(&::snd_pcm_hw_params_set_rate_resample) HwParamsSetRateResample = nullptr;
        decltype(&::snd_pcm_hw_params_set_rate_near) HwParamsSetRateNear = nullptr;
        decltype(&::snd_pcm_hw_params_set_period_size_near) HwParamsSetPeriodSizeNear = nullptr;
        decltype(&::snd_pcm_hw_params_set_buffer_size_near) HwParamsSetBufferSizeNear = nullptr;
        decltype(&::snd_pcm_hw_params) HwParams = nullptr;
        decltype(&::snd_pcm_hw_params_get_period_size) HwParamsGetPeriodSize = nullptr;
        decltype(&::snd_pcm_hw_params_get_buffer_size) HwParamsGetBufferSize = nullptr;
        decltype(&::snd_pcm_hw_params_get_channels) HwParamsGetChannels = nullptr;
        decltype(&::snd_pcm_hw_params_get_rate) HwParamsGetRate = nullptr;
        decltype(&::snd_pcm_sw_params_malloc) SwParamsMalloc = nullptr;
        decltype(&::snd_pcm_sw_params_free) SwParamsFree = nullptr;
        decltype(&::snd_pcm_sw_params_current) SwParamsCurrent = nullptr;
        decltype(&::snd_pcm_sw_params_set_start_threshold) SwParamsSetStartThreshold = nullptr;
        decltype(&::snd_pcm_sw_params_set_avail_min) SwParamsSetAvailMin = nullptr;
        decltype(&::snd_pcm_sw_params) SwParams = nullptr;
        decltype(&::snd_pcm_prepare) PcmPrepare = nullptr;
        decltype(&::snd_pcm_drop) PcmDrop = nullptr;
        decltype(&::snd_pcm_writei) PcmWritei = nullptr;
        decltype(&::snd_pcm_recover) PcmRecover = nullptr;
        decltype(&::snd_pcm_wait) PcmWait = nullptr;
        decltype(&::snd_pcm_avail_update) PcmAvailUpdate = nullptr;
        decltype(&::snd_strerror) StrError = nullptr;

        // --- capture (plans/plan_x11.md X11-0162) -----------------------------------------------
        decltype(&::snd_pcm_readi) PcmReadi = nullptr;
        decltype(&::snd_pcm_start) PcmStart = nullptr;

        // --- enumeration ----------------------------------------------------------------------
        decltype(&::snd_card_next) CardNext = nullptr;
        decltype(&::snd_ctl_open) CtlOpen = nullptr;
        decltype(&::snd_ctl_close) CtlClose = nullptr;
        decltype(&::snd_ctl_card_info_malloc) CtlCardInfoMalloc = nullptr;
        decltype(&::snd_ctl_card_info_free) CtlCardInfoFree = nullptr;
        decltype(&::snd_ctl_card_info) CtlCardInfo = nullptr;
        decltype(&::snd_ctl_card_info_get_name) CtlCardInfoGetName = nullptr;
        decltype(&::snd_ctl_pcm_next_device) CtlPcmNextDevice = nullptr;
        decltype(&::snd_ctl_pcm_info) CtlPcmInfo = nullptr;
        decltype(&::snd_pcm_info_malloc) PcmInfoMalloc = nullptr;
        decltype(&::snd_pcm_info_free) PcmInfoFree = nullptr;
        decltype(&::snd_pcm_info_set_device) PcmInfoSetDevice = nullptr;
        decltype(&::snd_pcm_info_set_subdevice) PcmInfoSetSubdevice = nullptr;
        decltype(&::snd_pcm_info_set_stream) PcmInfoSetStream = nullptr;
        decltype(&::snd_pcm_info_get_name) PcmInfoGetName = nullptr;
        decltype(&::snd_device_name_hint) DeviceNameHint = nullptr;
        decltype(&::snd_device_name_get_hint) DeviceNameGetHint = nullptr;
        decltype(&::snd_device_name_free_hint) DeviceNameFreeHint = nullptr;
    };

    /**
     * @brief Gets libasound, loading it the first time.
     *
     * @return The functions; `loaded` false, with `error` saying why, when the library or a PCM
     * function is missing.
     */
    [[nodiscard]] const AlsaApi& Alsa();

    /**
     * @brief Describes a failed ALSA call.
     *
     * @param what What was being done.
     * @param code The negative error code.
     * @return "what: ALSA's message".
     */
    [[nodiscard]] std::string DescribeAlsaError(const char* what, int code);

} // namespace CNA::Audio::Platform::Alsa
