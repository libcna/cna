// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"

#include <SDL3/SDL_audio.h>

#include <mutex>

namespace CNA::Audio::Platform::Sdl3 {

    /** @brief SDL3 pull-capture session for one enumerated recording route. */
    class Sdl3AudioRecordingDevice final : public IAudioRecordingDevice
    {
    public:
        explicit Sdl3AudioRecordingDevice(AudioRecordingDeviceInfo info);
        ~Sdl3AudioRecordingDevice() override;

        Sdl3AudioRecordingDevice(const Sdl3AudioRecordingDevice&) = delete;
        Sdl3AudioRecordingDevice& operator=(const Sdl3AudioRecordingDevice&) = delete;
        Sdl3AudioRecordingDevice(Sdl3AudioRecordingDevice&&) = delete;
        Sdl3AudioRecordingDevice& operator=(Sdl3AudioRecordingDevice&&) = delete;

        [[nodiscard]] const AudioRecordingDeviceInfo& GetInfo() const noexcept override;
        [[nodiscard]] bool IsConnected() const noexcept override;
        [[nodiscard]] AudioFormat Open(const AudioFormat& requested) override;
        void Close() noexcept override;
        [[nodiscard]] bool IsOpen() const noexcept override;
        [[nodiscard]] AudioFormat GetFormat() const noexcept override;
        [[nodiscard]] AudioRecordingIoResult GetAvailableBytes() const noexcept override;
        [[nodiscard]] AudioRecordingIoResult Read(
            std::span<std::byte> destination) noexcept override;

    private:
        [[nodiscard]] bool IsConnectedLocked() const noexcept;

        AudioRecordingDeviceInfo info_;
        mutable std::mutex lifecycleMutex_;
        SDL_AudioStream* stream_ = nullptr;
        AudioFormat format_{};
        bool audioSubsystemAcquired_ = false;
    };

    /** @brief SDL3 recording capability provider selected by `CNA_AUDIO_PLATFORM=SDL3`. */
    class Sdl3AudioRecordingDeviceProvider final : public IAudioRecordingDeviceProvider
    {
    public:
        Sdl3AudioRecordingDeviceProvider();
        ~Sdl3AudioRecordingDeviceProvider() override;

        Sdl3AudioRecordingDeviceProvider(const Sdl3AudioRecordingDeviceProvider&) = delete;
        Sdl3AudioRecordingDeviceProvider& operator=(
            const Sdl3AudioRecordingDeviceProvider&) = delete;

        [[nodiscard]] std::vector<AudioRecordingDeviceInfo> GetDevices() const override;
        [[nodiscard]] std::unique_ptr<IAudioRecordingDevice> CreateDevice(
            AudioRecordingDeviceId id) override;

    private:
        bool audioSubsystemAcquired_ = false;
    };

} // namespace CNA::Audio::Platform::Sdl3
