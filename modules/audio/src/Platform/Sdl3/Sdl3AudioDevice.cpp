// SPDX-License-Identifier: MS-PL

#include "Platform/Sdl3/Sdl3AudioDevice.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Audio::Platform::Sdl3 {
namespace {

    [[nodiscard]] SDL_AudioFormat ToSdlFormat(const AudioSampleFormat format)
    {
        switch (format)
        {
            case AudioSampleFormat::Signed16:
                return SDL_AUDIO_S16;
            case AudioSampleFormat::Float32:
                return SDL_AUDIO_F32;
            case AudioSampleFormat::Unknown:
                break;
        }
        throw std::invalid_argument("unknown playback sample format");
    }

    [[nodiscard]] AudioSampleFormat FromSdlFormat(const SDL_AudioFormat format)
    {
        switch (format)
        {
            case SDL_AUDIO_S16:
                return AudioSampleFormat::Signed16;
            case SDL_AUDIO_F32:
                return AudioSampleFormat::Float32;
            default:
                return AudioSampleFormat::Unknown;
        }
    }

    [[nodiscard]] std::runtime_error SdlFailure(const char* operation)
    {
        return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
    }

} // namespace

    Sdl3AudioDevice::~Sdl3AudioDevice()
    {
        Close();
    }

    AudioFormat Sdl3AudioDevice::Open(
        const AudioFormat& requested,
        std::shared_ptr<IAudioBufferCallback> callback)
    {
        std::lock_guard lock(lifecycleMutex_);
        if (stream_)
        {
            throw std::logic_error("audio device is already open");
        }
        if (!IsValid(requested) || !callback
            || requested.sampleRate > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
        {
            throw std::invalid_argument("invalid audio open request");
        }

        if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            throw SdlFailure("SDL_InitSubSystem(SDL_INIT_AUDIO)");
        }

        SDL_AudioStream* openedStream = nullptr;
        try
        {
            SDL_AudioSpec requestedSpec{};
            requestedSpec.format = ToSdlFormat(requested.sampleFormat);
            requestedSpec.channels = static_cast<int>(requested.channels);
            requestedSpec.freq = static_cast<int>(requested.sampleRate);

            openedStream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                &requestedSpec,
                &Sdl3AudioDevice::FeedStream,
                this);
            if (!openedStream)
            {
                throw SdlFailure("SDL_OpenAudioDeviceStream");
            }

            SDL_AudioSpec applicationSpec{};
            if (!SDL_GetAudioStreamFormat(openedStream, &applicationSpec, nullptr))
            {
                throw SdlFailure("SDL_GetAudioStreamFormat");
            }

            if (applicationSpec.freq <= 0 || applicationSpec.channels <= 0
                || applicationSpec.channels > std::numeric_limits<std::uint8_t>::max())
            {
                throw std::runtime_error("SDL returned an invalid playback format");
            }

            AudioFormat negotiated{
                static_cast<std::uint32_t>(applicationSpec.freq),
                static_cast<std::uint8_t>(applicationSpec.channels),
                FromSdlFormat(applicationSpec.format)};
            if (!IsValid(negotiated))
            {
                throw std::runtime_error("SDL returned an unsupported playback sample format");
            }

            const std::size_t scratchBytes = ScratchFrameCount
                * negotiated.channels * BytesPerSample(negotiated.sampleFormat);
            std::vector<std::byte> scratch(scratchBytes);

            // SDL_OpenAudioDeviceStream returns paused. Publish every callback-visible field
            // before the stream can be resumed, preserving Open's no-callback guarantee.
            stream_ = openedStream;
            openedStream = nullptr;
            callback_ = std::move(callback);
            scratch_ = std::move(scratch);
            format_ = negotiated;
            running_ = false;
            audioSubsystemAcquired_ = true;
            callbackEnabled_.store(false, std::memory_order_release);
            streamError_.store(false, std::memory_order_release);
            return format_;
        }
        catch (...)
        {
            if (openedStream)
            {
                SDL_DestroyAudioStream(openedStream);
            }
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw;
        }
    }

    void Sdl3AudioDevice::Start()
    {
        std::lock_guard lock(lifecycleMutex_);
        if (!stream_)
        {
            throw std::logic_error("audio device is closed");
        }
        if (running_)
        {
            return;
        }

        streamError_.store(false, std::memory_order_release);
        callbackEnabled_.store(true, std::memory_order_release);
        if (!SDL_ResumeAudioStreamDevice(stream_))
        {
            callbackEnabled_.store(false, std::memory_order_release);
            throw SdlFailure("SDL_ResumeAudioStreamDevice");
        }
        running_ = true;
    }

    void Sdl3AudioDevice::StopLocked() noexcept
    {
        callbackEnabled_.store(false, std::memory_order_release);
        if (!stream_ || !running_)
        {
            running_ = false;
            return;
        }

        // Disable future CNA callbacks first, pause native demand, then take SDL's stream lock.
        // SDL holds this same recursive lock while FeedStream runs, so lock/unlock is the barrier
        // that makes IAudioDevice::Stop's postcondition concrete.
        if (!SDL_PauseAudioStreamDevice(stream_))
        {
            streamError_.store(true, std::memory_order_release);
        }
        if (SDL_LockAudioStream(stream_))
        {
            if (!SDL_UnlockAudioStream(stream_))
            {
                streamError_.store(true, std::memory_order_release);
            }
        }
        else
        {
            streamError_.store(true, std::memory_order_release);
        }
        running_ = false;
    }

    void Sdl3AudioDevice::Stop() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        StopLocked();
    }

    void Sdl3AudioDevice::Close() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        if (!stream_)
        {
            return;
        }

        StopLocked();
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        callback_.reset();
        scratch_.clear();
        format_ = {};
        if (audioSubsystemAcquired_)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audioSubsystemAcquired_ = false;
        }
    }

    bool Sdl3AudioDevice::IsOpen() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return stream_ != nullptr;
    }

    bool Sdl3AudioDevice::IsRunning() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return running_;
    }

    AudioFormat Sdl3AudioDevice::GetFormat() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return stream_ ? format_ : AudioFormat{};
    }

    bool Sdl3AudioDevice::HasStreamError() const noexcept
    {
        return streamError_.load(std::memory_order_acquire);
    }

    void SDLCALL Sdl3AudioDevice::FeedStream(void* userdata,
                                             SDL_AudioStream* stream,
                                             const int additionalAmount,
                                             const int totalAmount) noexcept
    {
        (void)totalAmount;
        static_cast<Sdl3AudioDevice*>(userdata)->FeedStream(stream, additionalAmount);
    }

    void Sdl3AudioDevice::FeedStream(SDL_AudioStream* stream,
                                    const int additionalAmount) noexcept
    {
        if (!callbackEnabled_.load(std::memory_order_acquire) || additionalAmount <= 0)
        {
            return;
        }

        const std::size_t sampleBytes = BytesPerSample(format_.sampleFormat);
        const std::size_t frameBytes = sampleBytes * format_.channels;
        if (sampleBytes == 0 || frameBytes == 0 || scratch_.empty()
            || additionalAmount % static_cast<int>(frameBytes) != 0)
        {
            streamError_.store(true, std::memory_order_release);
            return;
        }

        std::size_t remaining = static_cast<std::size_t>(additionalAmount);
        while (remaining > 0 && callbackEnabled_.load(std::memory_order_acquire))
        {
            const std::size_t chunkBytes = std::min(remaining, scratch_.size());
            callback_->FillBuffer(
                std::span<std::byte>(scratch_.data(), chunkBytes),
                chunkBytes / sampleBytes);
            if (!SDL_PutAudioStreamData(
                    stream, scratch_.data(), static_cast<int>(chunkBytes)))
            {
                streamError_.store(true, std::memory_order_release);
                return;
            }
            remaining -= chunkBytes;
        }
    }

} // namespace CNA::Audio::Platform::Sdl3
