// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace CNA::Audio::Platform::Alsa {

    /**
     * @brief Playback through ALSA, selected by `CNA_AUDIO_PLATFORM=ALSA`.
     *
     * ALSA is the one Linux audio interface every system has: on a PipeWire desktop its `default`
     * device is PipeWire (pipewire-alsa), on a PulseAudio one it is PulseAudio (alsa-plugins),
     * and on a bare system it is the sound card through dmix. So one backend reaches all three.
     *
     * `libasound.so.2` is loaded at run time rather than linked, the way the X11 platform loads
     * GLX: a machine without it still starts the program, and the device then fails to open with
     * a message saying why -- which the XNA layer reports as NoAudioHardwareException.
     *
     * The device opened is `default`, or the ALSA PCM named by the `CNA_AUDIO_DEVICE` environment
     * variable -- `null` for a silent device, `file:FILE=out.raw,FORMAT=raw` to record exactly what
     * was played. The test suites set `null`, so no test ever makes a sound.
     */
    class AlsaAudioDevice final : public IAudioDevice
    {
    public:
        /** @brief A device for `default`, or `CNA_AUDIO_DEVICE` when it is set. */
        AlsaAudioDevice();

        /**
         * @brief A device for a named ALSA PCM.
         * @param deviceName The PCM, e.g. `default`, `null`, `hw:0,0`.
         */
        explicit AlsaAudioDevice(std::string deviceName);

        /** @brief Closes the device. */
        ~AlsaAudioDevice() override;

        AlsaAudioDevice(const AlsaAudioDevice&) = delete;
        AlsaAudioDevice& operator=(const AlsaAudioDevice&) = delete;
        AlsaAudioDevice(AlsaAudioDevice&&) = delete;
        AlsaAudioDevice& operator=(AlsaAudioDevice&&) = delete;

        /**
         * @brief Opens the PCM, paused.
         * @param requested The preferred format; the device may answer with another rate, channel
         * count or sample representation, and reports what it chose.
         * @param callback The buffer callback.
         * @return The negotiated format.
         * @throws std::runtime_error When libasound cannot be loaded or the PCM cannot be opened
         * or configured.
         */
        [[nodiscard]] AudioFormat Open(
            const AudioFormat& requested,
            std::shared_ptr<IAudioBufferCallback> callback) override;
        /** @brief Starts the playback thread. */
        void Start() override;
        /** @brief Stops the playback thread; a callback barrier. */
        void Stop() noexcept override;
        /** @brief Stops and closes the PCM. */
        void Close() noexcept override;

        /** @brief Gets whether the PCM is open. @return The answer. */
        [[nodiscard]] bool IsOpen() const noexcept override;
        /** @brief Gets whether the playback thread is running. @return The answer. */
        [[nodiscard]] bool IsRunning() const noexcept override;
        /** @brief Gets the negotiated format. @return The format, or an invalid one when closed. */
        [[nodiscard]] AudioFormat GetFormat() const noexcept override;

        /** @brief Gets the ALSA PCM name this device opens. @return The name. */
        [[nodiscard]] const std::string& GetDeviceName() const noexcept { return deviceName_; }

        /** @brief Gets the period the device negotiated, in frames. @return The period, or 0. */
        [[nodiscard]] std::size_t GetPeriodFrames() const noexcept { return periodFrames_; }

    private:
        void Run() noexcept;
        void StopLocked() noexcept;
        void CloseLocked() noexcept;

        std::string deviceName_;
        mutable std::mutex lifecycleMutex_;
        std::shared_ptr<IAudioBufferCallback> callback_;
        AudioFormat format_{};
        void* pcm_ = nullptr;
        std::size_t periodFrames_ = 0;
        // ALSA's null and file devices have no clock: they take any amount at once. The thread
        // paces itself for those, as the NULL platform's device does, so a sound lasts as long
        // as it should rather than finishing the instant it starts.
        bool unclocked_ = false;
        std::vector<std::byte> period_;
        std::thread worker_;
        std::atomic<bool> running_{false};
        std::atomic<bool> failed_{false};
    };

    /**
     * @brief Gets whether `libasound.so.2` can be loaded on this machine.
     * @return The answer; false also means every AlsaAudioDevice::Open() will throw.
     */
    [[nodiscard]] bool IsAlsaAvailable() noexcept;

} // namespace CNA::Audio::Platform::Alsa
