// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace CNA::Audio::Platform::Null {

    /** @brief Paced, silent playback device selected by `CNA_AUDIO_PLATFORM=NULL`. */
    class NullAudioDevice final : public IAudioDevice
    {
    public:
        NullAudioDevice() = default;
        ~NullAudioDevice() override;

        NullAudioDevice(const NullAudioDevice&) = delete;
        NullAudioDevice& operator=(const NullAudioDevice&) = delete;
        NullAudioDevice(NullAudioDevice&&) = delete;
        NullAudioDevice& operator=(NullAudioDevice&&) = delete;

        [[nodiscard]] AudioFormat Open(
            const AudioFormat& requested,
            std::shared_ptr<IAudioBufferCallback> callback) override;
        void Start() override;
        void Stop() noexcept override;
        void Close() noexcept override;

        [[nodiscard]] bool IsOpen() const noexcept override;
        [[nodiscard]] bool IsRunning() const noexcept override;
        [[nodiscard]] AudioFormat GetFormat() const noexcept override;

    private:
        static constexpr std::size_t BufferFrameCount = 512;

        void Run() noexcept;
        void StopLocked() noexcept;

        mutable std::mutex lifecycleMutex_;
        std::mutex waitMutex_;
        std::condition_variable waitCondition_;
        std::shared_ptr<IAudioBufferCallback> callback_;
        std::vector<std::byte> scratch_;
        AudioFormat format_{};
        std::thread worker_;
        std::atomic<bool> running_{false};
        bool open_ = false;
    };

} // namespace CNA::Audio::Platform::Null
