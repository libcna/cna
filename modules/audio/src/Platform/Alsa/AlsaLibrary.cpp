// SPDX-License-Identifier: MS-PL

#include "Platform/Alsa/AlsaLibrary.hpp"

#include <dlfcn.h>

namespace CNA::Audio::Platform::Alsa {

    namespace {

        template <typename Function>
        bool Resolve(void* library, const char* name, Function& function)
        {
            function = reinterpret_cast<Function>(dlsym(library, name));
            return function != nullptr;
        }

    } // namespace

    const AlsaApi& Alsa()
    {
        // Immortal: an audio thread may still be running while static destructors run.
        static const AlsaApi* api = [] {
            auto* result = new AlsaApi();
            void* library = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
            if (library == nullptr)
            {
                const char* reason = dlerror();
                result->error = std::string("libasound.so.2 could not be loaded: ") +
                                (reason != nullptr ? reason : "unknown error");
                return result;
            }
            const bool complete =
                Resolve(library, "snd_pcm_open", result->PcmOpen) &&
                Resolve(library, "snd_pcm_close", result->PcmClose) &&
                Resolve(library, "snd_pcm_type", result->PcmType) &&
                Resolve(library, "snd_pcm_hw_params_malloc", result->HwParamsMalloc) &&
                Resolve(library, "snd_pcm_hw_params_free", result->HwParamsFree) &&
                Resolve(library, "snd_pcm_hw_params_any", result->HwParamsAny) &&
                Resolve(library, "snd_pcm_hw_params_set_access", result->HwParamsSetAccess) &&
                Resolve(library, "snd_pcm_hw_params_set_format", result->HwParamsSetFormat) &&
                Resolve(library, "snd_pcm_hw_params_set_channels_near",
                        result->HwParamsSetChannelsNear) &&
                Resolve(library, "snd_pcm_hw_params_set_rate_resample",
                        result->HwParamsSetRateResample) &&
                Resolve(library, "snd_pcm_hw_params_set_rate_near", result->HwParamsSetRateNear) &&
                Resolve(library, "snd_pcm_hw_params_set_period_size_near",
                        result->HwParamsSetPeriodSizeNear) &&
                Resolve(library, "snd_pcm_hw_params_set_buffer_size_near",
                        result->HwParamsSetBufferSizeNear) &&
                Resolve(library, "snd_pcm_hw_params", result->HwParams) &&
                Resolve(library, "snd_pcm_hw_params_get_period_size",
                        result->HwParamsGetPeriodSize) &&
                Resolve(library, "snd_pcm_hw_params_get_buffer_size",
                        result->HwParamsGetBufferSize) &&
                Resolve(library, "snd_pcm_hw_params_get_channels", result->HwParamsGetChannels) &&
                Resolve(library, "snd_pcm_hw_params_get_rate", result->HwParamsGetRate) &&
                Resolve(library, "snd_pcm_sw_params_malloc", result->SwParamsMalloc) &&
                Resolve(library, "snd_pcm_sw_params_free", result->SwParamsFree) &&
                Resolve(library, "snd_pcm_sw_params_current", result->SwParamsCurrent) &&
                Resolve(library, "snd_pcm_sw_params_set_start_threshold",
                        result->SwParamsSetStartThreshold) &&
                Resolve(library, "snd_pcm_sw_params_set_avail_min", result->SwParamsSetAvailMin) &&
                Resolve(library, "snd_pcm_sw_params", result->SwParams) &&
                Resolve(library, "snd_pcm_prepare", result->PcmPrepare) &&
                Resolve(library, "snd_pcm_drop", result->PcmDrop) &&
                Resolve(library, "snd_pcm_writei", result->PcmWritei) &&
                Resolve(library, "snd_pcm_recover", result->PcmRecover) &&
                Resolve(library, "snd_pcm_wait", result->PcmWait) &&
                Resolve(library, "snd_pcm_avail_update", result->PcmAvailUpdate) &&
                Resolve(library, "snd_strerror", result->StrError);
            if (!complete)
            {
                result->error = "libasound.so.2 lacks a PCM function this device needs";
                return result;
            }
            result->loaded = true;
            result->captureLoaded =
                Resolve(library, "snd_pcm_readi", result->PcmReadi) &&
                Resolve(library, "snd_pcm_start", result->PcmStart) &&
                Resolve(library, "snd_card_next", result->CardNext) &&
                Resolve(library, "snd_ctl_open", result->CtlOpen) &&
                Resolve(library, "snd_ctl_close", result->CtlClose) &&
                Resolve(library, "snd_ctl_card_info_malloc", result->CtlCardInfoMalloc) &&
                Resolve(library, "snd_ctl_card_info_free", result->CtlCardInfoFree) &&
                Resolve(library, "snd_ctl_card_info", result->CtlCardInfo) &&
                Resolve(library, "snd_ctl_card_info_get_name", result->CtlCardInfoGetName) &&
                Resolve(library, "snd_ctl_pcm_next_device", result->CtlPcmNextDevice) &&
                Resolve(library, "snd_ctl_pcm_info", result->CtlPcmInfo) &&
                Resolve(library, "snd_pcm_info_malloc", result->PcmInfoMalloc) &&
                Resolve(library, "snd_pcm_info_free", result->PcmInfoFree) &&
                Resolve(library, "snd_pcm_info_set_device", result->PcmInfoSetDevice) &&
                Resolve(library, "snd_pcm_info_set_subdevice", result->PcmInfoSetSubdevice) &&
                Resolve(library, "snd_pcm_info_set_stream", result->PcmInfoSetStream) &&
                Resolve(library, "snd_pcm_info_get_name", result->PcmInfoGetName) &&
                Resolve(library, "snd_device_name_hint", result->DeviceNameHint) &&
                Resolve(library, "snd_device_name_get_hint", result->DeviceNameGetHint) &&
                Resolve(library, "snd_device_name_free_hint", result->DeviceNameFreeHint);
            return result;
        }();
        return *api;
    }

    std::string DescribeAlsaError(const char* what, const int code)
    {
        return std::string(what) + ": " + Alsa().StrError(code);
    }

} // namespace CNA::Audio::Platform::Alsa
