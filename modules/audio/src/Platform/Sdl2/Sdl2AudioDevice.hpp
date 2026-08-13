// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <SDL2/SDL_audio.h>

#include <atomic>
#include <memory>
#include <mutex>

namespace CNA::Audio::Platform::Sdl2 {

    /**
     * @brief SDL2 callback-device implementation selected by `CNA_AUDIO_PLATFORM=SDL2`.
     *
     * SDL2 has no SDL3-style audio stream object.  Its callback is nevertheless an exact fit for
     * IAudioDevice: SDL owns the fixed whole buffer and CNA fills it in-place.  Stop takes SDL's
     * device lock after pausing, giving IAudioDevice's callback-barrier guarantee.
     */
    class Sdl2AudioDevice final : public IAudioDevice
    {
    public:
        Sdl2AudioDevice() = default;
        ~Sdl2AudioDevice() override;

        Sdl2AudioDevice(const Sdl2AudioDevice&) = delete;
        Sdl2AudioDevice& operator=(const Sdl2AudioDevice&) = delete;

        [[nodiscard]] AudioFormat Open(
            const AudioFormat& requested,
            std::shared_ptr<IAudioBufferCallback> callback) override;
        void Start() override;
        void Stop() noexcept override;
        void Close() noexcept override;

        [[nodiscard]] bool IsOpen() const noexcept override;
        [[nodiscard]] bool IsRunning() const noexcept override;
        [[nodiscard]] AudioFormat GetFormat() const noexcept override;

        [[nodiscard]] bool HasDeviceError() const noexcept;

    private:
        static void SDLCALL FeedDevice(void* userdata, Uint8* output, int byteCount) noexcept;
        void StopLocked() noexcept;

        mutable std::mutex lifecycleMutex_;
        SDL_AudioDeviceID device_ = 0;
        std::shared_ptr<IAudioBufferCallback> callback_;
        AudioFormat format_{};
        bool running_ = false;
        bool audioSubsystemAcquired_ = false;
        std::atomic<bool> callbackEnabled_{false};
        std::atomic<bool> deviceError_{false};
    };

} // namespace CNA::Audio::Platform::Sdl2
