// SPDX-License-Identifier: MS-PL

#include "Platform/Sdl3/Sdl3AudioRecordingDevice.hpp"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>

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
        throw std::invalid_argument("unknown recording sample format");
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

    [[nodiscard]] SDL_AudioDeviceID ToNativeId(const AudioRecordingDeviceInfo& info)
    {
        if (info.id > std::numeric_limits<SDL_AudioDeviceID>::max())
        {
            throw std::invalid_argument("recording device id is not representable by SDL");
        }
        return static_cast<SDL_AudioDeviceID>(info.id);
    }

} // namespace

    Sdl3AudioRecordingDevice::Sdl3AudioRecordingDevice(AudioRecordingDeviceInfo info)
        : info_(std::move(info))
    {
    }

    Sdl3AudioRecordingDevice::~Sdl3AudioRecordingDevice()
    {
        Close();
    }

    const AudioRecordingDeviceInfo& Sdl3AudioRecordingDevice::GetInfo() const noexcept
    {
        return info_;
    }

    bool Sdl3AudioRecordingDevice::IsConnectedLocked() const noexcept
    {
        if (info_.id > std::numeric_limits<SDL_AudioDeviceID>::max()) return false;
        return SDL_GetAudioDeviceName(static_cast<SDL_AudioDeviceID>(info_.id)) != nullptr;
    }

    bool Sdl3AudioRecordingDevice::IsConnected() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return IsConnectedLocked();
    }

    AudioFormat Sdl3AudioRecordingDevice::Open(const AudioFormat& requested)
    {
        std::lock_guard lock(lifecycleMutex_);
        if (stream_)
        {
            throw std::logic_error("recording device is already open");
        }
        if (!IsValid(requested)
            || requested.sampleRate > static_cast<std::uint32_t>(
                std::numeric_limits<int>::max()))
        {
            throw std::invalid_argument("invalid recording open request");
        }
        if (!IsConnectedLocked())
        {
            throw std::runtime_error("recording device is disconnected");
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
                ToNativeId(info_), &requestedSpec, nullptr, nullptr);
            if (!openedStream)
            {
                throw SdlFailure("SDL_OpenAudioDeviceStream(recording)");
            }

            // For recording streams SDL binds the source side to hardware; the destination is
            // the application-facing format returned by Read().
            SDL_AudioSpec applicationSpec{};
            if (!SDL_GetAudioStreamFormat(openedStream, nullptr, &applicationSpec))
            {
                throw SdlFailure("SDL_GetAudioStreamFormat(recording)");
            }
            if (applicationSpec.freq <= 0 || applicationSpec.channels <= 0
                || applicationSpec.channels > std::numeric_limits<std::uint8_t>::max())
            {
                throw std::runtime_error("SDL returned an invalid recording format");
            }

            AudioFormat negotiated{
                static_cast<std::uint32_t>(applicationSpec.freq),
                static_cast<std::uint8_t>(applicationSpec.channels),
                FromSdlFormat(applicationSpec.format)};
            if (!IsValid(negotiated))
            {
                throw std::runtime_error("SDL returned an unsupported recording sample format");
            }
            if (!SDL_ResumeAudioStreamDevice(openedStream))
            {
                throw SdlFailure("SDL_ResumeAudioStreamDevice(recording)");
            }

            stream_ = openedStream;
            openedStream = nullptr;
            format_ = negotiated;
            audioSubsystemAcquired_ = true;
            return format_;
        }
        catch (...)
        {
            if (openedStream) SDL_DestroyAudioStream(openedStream);
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            throw;
        }
    }

    void Sdl3AudioRecordingDevice::Close() noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        if (stream_)
        {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
        format_ = {};
        if (audioSubsystemAcquired_)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            audioSubsystemAcquired_ = false;
        }
    }

    bool Sdl3AudioRecordingDevice::IsOpen() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return stream_ != nullptr;
    }

    AudioFormat Sdl3AudioRecordingDevice::GetFormat() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        return stream_ ? format_ : AudioFormat{};
    }

    AudioRecordingIoResult Sdl3AudioRecordingDevice::GetAvailableBytes() const noexcept
    {
        std::lock_guard lock(lifecycleMutex_);
        if (!IsConnectedLocked()) return {AudioRecordingIoStatus::DeviceLost, 0};
        if (!stream_) return {AudioRecordingIoStatus::Error, 0};

        const int available = SDL_GetAudioStreamAvailable(stream_);
        if (available > 0)
        {
            return {AudioRecordingIoStatus::Success, static_cast<std::size_t>(available)};
        }
        return available == 0
            ? AudioRecordingIoResult{AudioRecordingIoStatus::WouldBlock, 0}
            : AudioRecordingIoResult{AudioRecordingIoStatus::Error, 0};
    }

    AudioRecordingIoResult Sdl3AudioRecordingDevice::Read(
        const std::span<std::byte> destination) noexcept
    {
        if (destination.empty()) return {AudioRecordingIoStatus::WouldBlock, 0};

        std::lock_guard lock(lifecycleMutex_);
        if (!IsConnectedLocked()) return {AudioRecordingIoStatus::DeviceLost, 0};
        if (!stream_ || destination.size() > static_cast<std::size_t>(
                std::numeric_limits<int>::max()))
        {
            return {AudioRecordingIoStatus::Error, 0};
        }

        const int count = SDL_GetAudioStreamData(
            stream_, destination.data(), static_cast<int>(destination.size()));
        if (count > 0)
        {
            return {AudioRecordingIoStatus::Success, static_cast<std::size_t>(count)};
        }
        return count == 0
            ? AudioRecordingIoResult{AudioRecordingIoStatus::WouldBlock, 0}
            : AudioRecordingIoResult{AudioRecordingIoStatus::Error, 0};
    }

    Sdl3AudioRecordingDeviceProvider::Sdl3AudioRecordingDeviceProvider()
        : audioSubsystemAcquired_(SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
    }

    Sdl3AudioRecordingDeviceProvider::~Sdl3AudioRecordingDeviceProvider()
    {
        if (audioSubsystemAcquired_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    std::vector<AudioRecordingDeviceInfo>
    Sdl3AudioRecordingDeviceProvider::GetDevices() const
    {
        if (!audioSubsystemAcquired_) return {};

        int count = 0;
        SDL_AudioDeviceID* nativeDevices = SDL_GetAudioRecordingDevices(&count);
        std::vector<AudioRecordingDeviceInfo> result;
        if (nativeDevices && count > 0)
        {
            result.reserve(static_cast<std::size_t>(count) + 1);
            result.push_back({
                static_cast<AudioRecordingDeviceId>(SDL_AUDIO_DEVICE_DEFAULT_RECORDING),
                "Default Device", true});
            for (int i = 0; i < count; ++i)
            {
                const char* name = SDL_GetAudioDeviceName(nativeDevices[i]);
                result.push_back({
                    static_cast<AudioRecordingDeviceId>(nativeDevices[i]),
                    name ? name : "", false});
            }
            std::sort(result.begin() + 1, result.end(), [](const auto& left, const auto& right)
            {
                return left.id < right.id;
            });
        }
        SDL_free(nativeDevices);
        return result;
    }

    std::unique_ptr<IAudioRecordingDevice>
    Sdl3AudioRecordingDeviceProvider::CreateDevice(const AudioRecordingDeviceId id)
    {
        const auto devices = GetDevices();
        const auto found = std::find_if(devices.begin(), devices.end(), [id](const auto& info)
        {
            return info.id == id;
        });
        if (found == devices.end()) return nullptr;
        return std::make_unique<Sdl3AudioRecordingDevice>(*found);
    }

} // namespace CNA::Audio::Platform::Sdl3
