// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioRecordingDevice.hpp"

#include <atomic>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace CNA::Audio::Platform::Alsa {

    /**
     * @brief Capture through ALSA, for `CNA_AUDIO_PLATFORM=ALSA` (plans/plan_x11.md X11-0162).
     *
     * A thread reads the PCM as it fills and queues what it read, so a game that polls once a frame
     * -- XNA's `Microphone` does -- loses nothing to the device's own buffer running over between
     * polls. The queue holds at most eight seconds; past that the oldest audio is dropped, as a
     * game that never reads wants the latest rather than an unbounded backlog.
     *
     * ALSA's `null` and `file` devices have no clock and deliver any amount at once, so for those
     * the thread keeps real time itself, as playback does: a second of capture takes a second.
     */
    class AlsaAudioRecordingDevice final : public IAudioRecordingDevice
    {
    public:
        /**
         * @brief A session for one ALSA PCM.
         * @param info What the provider enumerated it as.
         * @param pcmName The PCM to open, e.g. `default`, `plughw:1,0`, `null`.
         */
        AlsaAudioRecordingDevice(AudioRecordingDeviceInfo info, std::string pcmName);

        /** @brief Stops capture and closes the PCM. */
        ~AlsaAudioRecordingDevice() override;

        AlsaAudioRecordingDevice(const AlsaAudioRecordingDevice&) = delete;
        AlsaAudioRecordingDevice& operator=(const AlsaAudioRecordingDevice&) = delete;

        /** @brief Gets the identity. @return The enumerated entry. */
        [[nodiscard]] const AudioRecordingDeviceInfo& GetInfo() const noexcept override { return info_; }
        /** @brief Gets whether the device is still there. @return False once a read found it gone. */
        [[nodiscard]] bool IsConnected() const noexcept override;
        /**
         * @brief Opens the PCM and starts capture.
         * @param requested The preferred format; the device may answer with another rate, channel
         * count or sample representation, and reports what it chose.
         * @return The negotiated format.
         * @throws std::invalid_argument For an invalid format.
         * @throws std::logic_error When already open.
         * @throws std::runtime_error When libasound is missing, or the PCM cannot be opened,
         * configured or started.
         */
        [[nodiscard]] AudioFormat Open(const AudioFormat& requested) override;
        /** @brief Stops capture, discards what was queued and closes the PCM. */
        void Close() noexcept override;
        /** @brief Gets whether capture is running. @return The answer. */
        [[nodiscard]] bool IsOpen() const noexcept override;
        /** @brief Gets the negotiated format. @return The format, or an invalid one when closed. */
        [[nodiscard]] AudioFormat GetFormat() const noexcept override;
        /**
         * @brief Gets the bytes a read can take now.
         * @return Success with the queued count, WouldBlock when none, DeviceLost once the device
         * is gone and its queue empty, Error after another failure.
         */
        [[nodiscard]] AudioRecordingIoResult GetAvailableBytes() const noexcept override;
        /**
         * @brief Takes queued bytes, whole frames only.
         * @param destination Receives interleaved PCM in GetFormat().
         * @return As GetAvailableBytes(), with the count taken.
         */
        [[nodiscard]] AudioRecordingIoResult Read(std::span<std::byte> destination) noexcept override;

        /** @brief Gets the ALSA PCM this session opens. @return The name. */
        [[nodiscard]] const std::string& GetPcmName() const noexcept { return pcmName_; }

    private:
        void Run() noexcept;
        void CloseLocked() noexcept;

        AudioRecordingDeviceInfo info_;
        std::string pcmName_;
        mutable std::mutex lifecycleMutex_;
        void* pcm_ = nullptr;
        AudioFormat format_{};
        std::size_t frameBytes_ = 0;
        std::size_t periodFrames_ = 0;
        bool unclocked_ = false;
        std::thread worker_;
        std::atomic<bool> running_{false};

        // What the thread captured and a reader has not taken; guarded by queueMutex_.
        mutable std::mutex queueMutex_;
        std::deque<std::byte> queue_;
        std::size_t queueLimit_ = 0;
        bool lost_ = false;
        bool failed_ = false;
    };

    /**
     * @brief The ALSA recording devices of this machine.
     *
     * With `CNA_AUDIO_RECORDING_DEVICE` set, exactly that PCM, as the default -- how a user points
     * capture at a device, and how the test suites point it at `null` so that no test ever records
     * a room. Otherwise: ALSA's `default` PCM first, marked default, when the host's configuration
     * lists one that captures; then each sound card's capture devices, opened through `plughw` so
     * the requested format is converted, named after the card and the device.
     */
    class AlsaAudioRecordingDeviceProvider final : public IAudioRecordingDeviceProvider
    {
    public:
        /**
         * @brief Enumerates the devices.
         * @return Default first, then ascending ids; empty when libasound is missing.
         */
        [[nodiscard]] std::vector<AudioRecordingDeviceInfo> GetDevices() const override;
        /**
         * @brief Creates a closed session.
         * @param id An id GetDevices() returned.
         * @return The session, or null for an id no longer enumerated.
         */
        [[nodiscard]] std::unique_ptr<IAudioRecordingDevice> CreateDevice(
            AudioRecordingDeviceId id) override;

    private:
        struct Entry
        {
            AudioRecordingDeviceInfo info;
            std::string pcmName;
        };

        [[nodiscard]] std::vector<Entry> Enumerate() const;
    };

} // namespace CNA::Audio::Platform::Alsa
