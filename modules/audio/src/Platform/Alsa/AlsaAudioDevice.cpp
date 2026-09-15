// SPDX-License-Identifier: MS-PL

#include "Platform/Alsa/AlsaAudioDevice.hpp"

#include <alsa/asoundlib.h>
#include <dlfcn.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace CNA::Audio::Platform::Alsa {

    namespace {

        /// The part of libasound this device uses, resolved once from the library loaded at run
        /// time. Declared from ALSA's own headers, so a signature can never drift from the library.
        struct AlsaApi
        {
            bool loaded = false;
            std::string error;
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
        };

        template <typename Function>
        bool Resolve(void* library, const char* name, Function& function)
        {
            function = reinterpret_cast<Function>(dlsym(library, name));
            return function != nullptr;
        }

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
                return result;
            }();
            return *api;
        }

        snd_pcm_t* Pcm(void* handle)
        {
            return static_cast<snd_pcm_t*>(handle);
        }

        std::string Describe(const char* what, const int code)
        {
            return std::string(what) + ": " + Alsa().StrError(code);
        }

        /// The device period: about ten milliseconds, so one period of latency is small, with
        /// four of them buffered to ride out a scheduling hiccup.
        constexpr snd_pcm_uframes_t kPeriodMilliseconds = 10;
        constexpr snd_pcm_uframes_t kPeriodsBuffered = 4;

        std::string DefaultDeviceName()
        {
            const char* configured = std::getenv("CNA_AUDIO_DEVICE");
            return configured != nullptr && configured[0] != '\0' ? std::string(configured)
                                                                   : std::string("default");
        }

    } // namespace

    bool IsAlsaAvailable() noexcept
    {
        try
        {
            return Alsa().loaded;
        }
        catch (...)
        {
            return false;
        }
    }

    AlsaAudioDevice::AlsaAudioDevice() : deviceName_(DefaultDeviceName()) {}

    AlsaAudioDevice::AlsaAudioDevice(std::string deviceName) : deviceName_(std::move(deviceName)) {}

    AlsaAudioDevice::~AlsaAudioDevice()
    {
        Close();
    }

    AudioFormat AlsaAudioDevice::Open(const AudioFormat& requested,
                                      std::shared_ptr<IAudioBufferCallback> callback)
    {
        std::lock_guard lock(lifecycleMutex_);
        if (pcm_ != nullptr)
        {
            throw std::logic_error("ALSA audio device is already open");
        }
        if (!IsValid(requested) || !callback)
        {
            throw std::invalid_argument("invalid ALSA audio open request");
        }
        const AlsaApi& alsa = Alsa();
        if (!alsa.loaded)
        {
            throw std::runtime_error(alsa.error);
        }

        snd_pcm_t* pcm = nullptr;
        int code = alsa.PcmOpen(&pcm, deviceName_.c_str(), SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
        if (code < 0)
        {
            throw std::runtime_error(
                Describe(("ALSA device '" + deviceName_ + "' could not be opened").c_str(), code));
        }

        AudioFormat negotiated{};
        snd_pcm_uframes_t period = 0;
        try
        {
            snd_pcm_hw_params_t* hardware = nullptr;
            if ((code = alsa.HwParamsMalloc(&hardware)) < 0)
            {
                throw std::runtime_error(Describe("snd_pcm_hw_params_malloc", code));
            }
            const std::unique_ptr<snd_pcm_hw_params_t, decltype(alsa.HwParamsFree)> hardwareOwner(
                hardware, alsa.HwParamsFree);
            if ((code = alsa.HwParamsAny(pcm, hardware)) < 0 ||
                (code = alsa.HwParamsSetAccess(pcm, hardware, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
            {
                throw std::runtime_error(Describe("interleaved access", code));
            }

            // The requested representation first, then the other one: `default` and every plug
            // device take both, a bare hw device may take only one.
            const bool wantsFloat = requested.sampleFormat == AudioSampleFormat::Float32;
            const snd_pcm_format_t first = wantsFloat ? SND_PCM_FORMAT_FLOAT : SND_PCM_FORMAT_S16;
            const snd_pcm_format_t second = wantsFloat ? SND_PCM_FORMAT_S16 : SND_PCM_FORMAT_FLOAT;
            snd_pcm_format_t chosen = first;
            if (alsa.HwParamsSetFormat(pcm, hardware, first) < 0)
            {
                chosen = second;
                if ((code = alsa.HwParamsSetFormat(pcm, hardware, second)) < 0)
                {
                    throw std::runtime_error(Describe("16-bit or float samples", code));
                }
            }

            unsigned int channels = requested.channels;
            if ((code = alsa.HwParamsSetChannelsNear(pcm, hardware, &channels)) < 0)
            {
                throw std::runtime_error(Describe("channel count", code));
            }
            (void) alsa.HwParamsSetRateResample(pcm, hardware, 1);
            unsigned int rate = requested.sampleRate;
            int direction = 0;
            if ((code = alsa.HwParamsSetRateNear(pcm, hardware, &rate, &direction)) < 0)
            {
                throw std::runtime_error(Describe("sample rate", code));
            }
            period = std::max<snd_pcm_uframes_t>(64, rate * kPeriodMilliseconds / 1000);
            direction = 0;
            (void) alsa.HwParamsSetPeriodSizeNear(pcm, hardware, &period, &direction);
            snd_pcm_uframes_t buffer = period * kPeriodsBuffered;
            (void) alsa.HwParamsSetBufferSizeNear(pcm, hardware, &buffer);
            if ((code = alsa.HwParams(pcm, hardware)) < 0)
            {
                throw std::runtime_error(Describe("hardware parameters", code));
            }
            direction = 0;
            (void) alsa.HwParamsGetPeriodSize(hardware, &period, &direction);
            (void) alsa.HwParamsGetBufferSize(hardware, &buffer);
            (void) alsa.HwParamsGetChannels(hardware, &channels);
            (void) alsa.HwParamsGetRate(hardware, &rate, &direction);
            if (period == 0 || channels == 0 || channels > 255 || rate == 0)
            {
                throw std::runtime_error("ALSA negotiated an unusable format");
            }

            snd_pcm_sw_params_t* software = nullptr;
            if ((code = alsa.SwParamsMalloc(&software)) < 0)
            {
                throw std::runtime_error(Describe("snd_pcm_sw_params_malloc", code));
            }
            const std::unique_ptr<snd_pcm_sw_params_t, decltype(alsa.SwParamsFree)> softwareOwner(
                software, alsa.SwParamsFree);
            // Start once two periods are queued, and wake when one can be written.
            if ((code = alsa.SwParamsCurrent(pcm, software)) < 0 ||
                (code = alsa.SwParamsSetStartThreshold(pcm, software,
                                                       std::min(buffer, period * 2))) < 0 ||
                (code = alsa.SwParamsSetAvailMin(pcm, software, period)) < 0 ||
                (code = alsa.SwParams(pcm, software)) < 0)
            {
                throw std::runtime_error(Describe("software parameters", code));
            }

            negotiated.sampleRate = rate;
            negotiated.channels = static_cast<std::uint8_t>(channels);
            negotiated.sampleFormat = chosen == SND_PCM_FORMAT_FLOAT ? AudioSampleFormat::Float32
                                                                     : AudioSampleFormat::Signed16;
        }
        catch (...)
        {
            alsa.PcmClose(pcm);
            throw;
        }

        const snd_pcm_type_t type = alsa.PcmType(pcm);
        unclocked_ = type == SND_PCM_TYPE_NULL || type == SND_PCM_TYPE_FILE;
        pcm_ = pcm;
        periodFrames_ = period;
        period_.assign(period * negotiated.channels * BytesPerSample(negotiated.sampleFormat),
                       std::byte{});
        callback_ = std::move(callback);
        format_ = negotiated;
        failed_.store(false, std::memory_order_release);
        return format_;
    }

    void AlsaAudioDevice::Start()
    {
        std::lock_guard lock(lifecycleMutex_);
        if (pcm_ == nullptr)
        {
            throw std::logic_error("ALSA audio device is closed");
        }
        if (running_.load(std::memory_order_acquire))
        {
            return;
        }
        const int code = Alsa().PcmPrepare(Pcm(pcm_));
        if (code < 0)
        {
            throw std::runtime_error(Describe("snd_pcm_prepare", code));
        }
        running_.store(true, std::memory_order_release);
        try
        {
            worker_ = std::thread(&AlsaAudioDevice::Run, this);
        }
        catch (...)
        {
            running_.store(false, std::memory_order_release);
            throw;
        }
    }

    void AlsaAudioDevice::StopLocked() noexcept
    {
        running_.store(false, std::memory_order_release);
        if (worker_.joinable())
        {
            worker_.join();
        }
        if (pcm_ != nullptr)
        {
            // Discards what is still buffered: a stopped device goes quiet now, not a buffer later.
            (void) Alsa().PcmDrop(Pcm(pcm_));
        }
    }

    void AlsaAudioDevice::Stop() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        StopLocked();
    }

    void AlsaAudioDevice::CloseLocked() noexcept
    {
        StopLocked();
        if (pcm_ != nullptr)
        {
            (void) Alsa().PcmClose(Pcm(pcm_));
            pcm_ = nullptr;
        }
        callback_.reset();
        period_.clear();
        periodFrames_ = 0;
        format_ = {};
    }

    void AlsaAudioDevice::Close() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        CloseLocked();
    }

    bool AlsaAudioDevice::IsOpen() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return pcm_ != nullptr;
    }

    bool AlsaAudioDevice::IsRunning() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return pcm_ != nullptr && running_.load(std::memory_order_acquire) &&
               !failed_.load(std::memory_order_acquire);
    }

    AudioFormat AlsaAudioDevice::GetFormat() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return pcm_ != nullptr ? format_ : AudioFormat{};
    }

    void AlsaAudioDevice::Run() noexcept
    {
        const AlsaApi& alsa = Alsa();
        snd_pcm_t* pcm = Pcm(pcm_);
        const std::size_t frameBytes = format_.channels * BytesPerSample(format_.sampleFormat);
        const std::size_t sampleCount = periodFrames_ * format_.channels;
        const auto periodDuration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(static_cast<double>(periodFrames_) / format_.sampleRate));
        auto deadline = std::chrono::steady_clock::now();

        // Frames of the current period not yet accepted by ALSA; a new period is generated only
        // once the last one is written in full.
        std::size_t pending = 0;
        while (running_.load(std::memory_order_acquire))
        {
            if (pending == 0)
            {
                callback_->FillBuffer(period_, sampleCount);
                pending = periodFrames_;
            }

            const std::byte* data = period_.data() + (periodFrames_ - pending) * frameBytes;
            const snd_pcm_sframes_t written =
                alsa.PcmWritei(pcm, data, static_cast<snd_pcm_uframes_t>(pending));
            if (written >= 0)
            {
                pending -= static_cast<std::size_t>(written);
                if (pending == 0 && unclocked_)
                {
                    // No clock to wait on: keep real time ourselves.
                    deadline += periodDuration;
                    std::this_thread::sleep_until(deadline);
                }
                continue;
            }
            if (written == -EAGAIN)
            {
                // The buffer is full. Wait for room, but never so long that Stop() waits on us.
                (void) alsa.PcmWait(pcm, 100);
                continue;
            }
            // An underrun (EPIPE), a suspend (ESTRPIPE) or an interrupted call: the library knows
            // how to come back from each. Anything else means the device is gone.
            if (alsa.PcmRecover(pcm, static_cast<int>(written), 1) < 0)
            {
                failed_.store(true, std::memory_order_release);
                return;
            }
        }
    }

} // namespace CNA::Audio::Platform::Alsa
