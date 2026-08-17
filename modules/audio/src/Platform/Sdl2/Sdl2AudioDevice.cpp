// SPDX-License-Identifier: MS-PL

#include "Platform/Sdl2/Sdl2AudioDevice.hpp"

#include <SDL2/SDL.h>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Audio::Platform::Sdl2 {
namespace {

    [[nodiscard]] SDL_AudioFormat ToSdlFormat(const AudioSampleFormat format)
    {
        switch (format)
        {
            case AudioSampleFormat::Signed16: return AUDIO_S16SYS;
            case AudioSampleFormat::Float32: return AUDIO_F32SYS;
            case AudioSampleFormat::Unknown: break;
        }
        throw std::invalid_argument("unknown playback sample format");
    }

    [[nodiscard]] AudioSampleFormat FromSdlFormat(const SDL_AudioFormat format)
    {
        switch (format)
        {
            case AUDIO_S16SYS: return AudioSampleFormat::Signed16;
            case AUDIO_F32SYS: return AudioSampleFormat::Float32;
            default: return AudioSampleFormat::Unknown;
        }
    }

    [[nodiscard]] std::runtime_error SdlFailure(const char* operation)
    {
        return std::runtime_error(std::string(operation) + " failed: " + SDL_GetError());
    }

} // namespace

Sdl2AudioDevice::~Sdl2AudioDevice()
{
    Close();
}

AudioFormat Sdl2AudioDevice::Open(const AudioFormat& requested,
                                  std::shared_ptr<IAudioBufferCallback> callback)
{
    std::lock_guard lock(lifecycleMutex_);
    if (device_ != 0)
    {
        throw std::logic_error("audio device is already open");
    }
    if (!IsValid(requested) || !callback
        || requested.sampleRate > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
    {
        throw std::invalid_argument("invalid audio open request");
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
    {
        throw SdlFailure("SDL_InitSubSystem(SDL_INIT_AUDIO)");
    }

    try
    {
        SDL_AudioSpec desired{};
        desired.freq = static_cast<int>(requested.sampleRate);
        desired.format = ToSdlFormat(requested.sampleFormat);
        desired.channels = requested.channels;
        desired.samples = 1024;
        desired.callback = &Sdl2AudioDevice::FeedDevice;
        desired.userdata = this;

        SDL_AudioSpec obtained{};
        const SDL_AudioDeviceID opened = SDL_OpenAudioDevice(
            nullptr, 0, &desired, &obtained,
            SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_CHANNELS_CHANGE
                | SDL_AUDIO_ALLOW_FORMAT_CHANGE);
        if (opened == 0)
        {
            throw SdlFailure("SDL_OpenAudioDevice");
        }
        const AudioFormat negotiated{
            static_cast<std::uint32_t>(obtained.freq), obtained.channels,
            FromSdlFormat(obtained.format)};
        if (!IsValid(negotiated))
        {
            SDL_CloseAudioDevice(opened);
            throw std::runtime_error("SDL2 returned an unsupported playback format");
        }

        // SDL_OpenAudioDevice returns paused.  Publish all callback-visible state before Start.
        device_ = opened;
        callback_ = std::move(callback);
        format_ = negotiated;
        running_ = false;
        audioSubsystemAcquired_ = true;
        callbackEnabled_.store(false, std::memory_order_release);
        deviceError_.store(false, std::memory_order_release);
        return format_;
    }
    catch (...)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        throw;
    }
}

void Sdl2AudioDevice::Start()
{
    std::lock_guard lock(lifecycleMutex_);
    if (device_ == 0)
    {
        throw std::logic_error("audio device is closed");
    }
    if (running_)
    {
        return;
    }
    deviceError_.store(false, std::memory_order_release);
    callbackEnabled_.store(true, std::memory_order_release);
    SDL_PauseAudioDevice(device_, 0);
    running_ = true;
}

void Sdl2AudioDevice::StopLocked() noexcept
{
    callbackEnabled_.store(false, std::memory_order_release);
    if (device_ == 0 || !running_)
    {
        running_ = false;
        return;
    }

    SDL_PauseAudioDevice(device_, 1);
    SDL_LockAudioDevice(device_);
    SDL_UnlockAudioDevice(device_);
    running_ = false;
}

void Sdl2AudioDevice::Stop() noexcept
{
    std::lock_guard lock(lifecycleMutex_);
    StopLocked();
}

void Sdl2AudioDevice::Close() noexcept
{
    std::lock_guard lock(lifecycleMutex_);
    StopLocked();
    if (device_ != 0)
    {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    callback_.reset();
    format_ = {};
    if (audioSubsystemAcquired_)
    {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        audioSubsystemAcquired_ = false;
    }
}

bool Sdl2AudioDevice::IsOpen() const noexcept
{
    std::lock_guard lock(lifecycleMutex_);
    return device_ != 0;
}

bool Sdl2AudioDevice::IsRunning() const noexcept
{
    std::lock_guard lock(lifecycleMutex_);
    return running_;
}

AudioFormat Sdl2AudioDevice::GetFormat() const noexcept
{
    std::lock_guard lock(lifecycleMutex_);
    return device_ != 0 ? format_ : AudioFormat{};
}

bool Sdl2AudioDevice::HasDeviceError() const noexcept
{
    return deviceError_.load(std::memory_order_acquire);
}

void SDLCALL Sdl2AudioDevice::FeedDevice(void* userdata, Uint8* output,
                                         const int byteCount) noexcept
{
    auto* self = static_cast<Sdl2AudioDevice*>(userdata);
    if (!self || !output || byteCount < 0 || !self->callbackEnabled_.load(std::memory_order_acquire))
    {
        if (output && byteCount > 0) std::memset(output, 0, static_cast<std::size_t>(byteCount));
        return;
    }

    const std::size_t sampleBytes = BytesPerSample(self->format_.sampleFormat);
    if (!self->callback_ || sampleBytes == 0
        || static_cast<std::size_t>(byteCount) % sampleBytes != 0)
    {
        std::memset(output, 0, static_cast<std::size_t>(byteCount));
        self->deviceError_.store(true, std::memory_order_release);
        return;
    }
    self->callback_->FillBuffer(
        std::span<std::byte>(reinterpret_cast<std::byte*>(output), static_cast<std::size_t>(byteCount)),
        static_cast<std::size_t>(byteCount) / sampleBytes);
}

} // namespace CNA::Audio::Platform::Sdl2
