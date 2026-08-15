// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace CNA::Audio::Platform::Sdl3 {

    /** @brief SDL3 playback implementation selected by `CNA_AUDIO_PLATFORM=SDL3`. */
    class Sdl3AudioDevice final : public IAudioDevice
    {
    public:
        Sdl3AudioDevice() = default;
        ~Sdl3AudioDevice() override;

        Sdl3AudioDevice(const Sdl3AudioDevice&) = delete;
        Sdl3AudioDevice& operator=(const Sdl3AudioDevice&) = delete;
        Sdl3AudioDevice(Sdl3AudioDevice&&) = delete;
        Sdl3AudioDevice& operator=(Sdl3AudioDevice&&) = delete;

        [[nodiscard]] AudioFormat Open(
            const AudioFormat& requested,
            std::shared_ptr<IAudioBufferCallback> callback) override;
        void Start() override;
        void Stop() noexcept override;
        void Close() noexcept override;

        [[nodiscard]] bool IsOpen() const noexcept override;
        [[nodiscard]] bool IsRunning() const noexcept override;
        [[nodiscard]] AudioFormat GetFormat() const noexcept override;

        /** @brief Reports a native queue/barrier failure observed since the last `Open`. */
        [[nodiscard]] bool HasStreamError() const noexcept;

    private:
        static constexpr std::size_t ScratchFrameCount = 4096;

        static void SDLCALL FeedStream(void* userdata,
                                      SDL_AudioStream* stream,
                                      int additionalAmount,
                                      int totalAmount) noexcept;
        void FeedStream(SDL_AudioStream* stream, int additionalAmount) noexcept;
        void StopLocked() noexcept;

        mutable std::mutex lifecycleMutex_;
        SDL_AudioStream* stream_ = nullptr;
        std::shared_ptr<IAudioBufferCallback> callback_;
        std::vector<std::byte> scratch_;
        AudioFormat format_{};
        bool running_ = false;
        bool audioSubsystemAcquired_ = false;
        std::atomic<bool> callbackEnabled_{false};
        std::atomic<bool> streamError_{false};
    };

} // namespace CNA::Audio::Platform::Sdl3
