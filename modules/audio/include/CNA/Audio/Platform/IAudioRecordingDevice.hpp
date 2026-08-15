// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Audio/Platform/IAudioDevice.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace CNA::Audio::Platform {

    /** @brief Identifies a recording device while it remains connected. */
    using AudioRecordingDeviceId = std::uint64_t;

    /** @brief Stable identity and user-visible metadata for one recording device. */
    struct AudioRecordingDeviceInfo
    {
        /** @brief Backend-independent identity, stable until the device disconnects. */
        AudioRecordingDeviceId id = 0;
        /** @brief Human-readable device name, or empty when the platform provides none. */
        std::string name;
        /** @brief Whether this entry follows the host's current default input route. */
        bool isDefault = false;

        /** @brief Compares identity and all advertised metadata. */
        [[nodiscard]] bool operator==(const AudioRecordingDeviceInfo&) const noexcept = default;
    };

    /** @brief Outcome of a non-blocking capture availability/read operation. */
    enum class AudioRecordingIoStatus
    {
        /** @brief Bytes were available or transferred; `byteCount` is meaningful. */
        Success,
        /** @brief No bytes are currently available; this is ordinary non-blocking flow. */
        WouldBlock,
        /** @brief The opened device disconnected and this session can no longer capture. */
        DeviceLost,
        /** @brief The backend reported another capture error. */
        Error
    };

    /** @brief Result of an availability query or bounded capture read. */
    struct AudioRecordingIoResult
    {
        /** @brief Classification of the operation's outcome. */
        AudioRecordingIoStatus status = AudioRecordingIoStatus::WouldBlock;
        /** @brief Available or transferred byte count; zero unless `status` is `Success`. */
        std::size_t byteCount = 0;

        /** @brief Compares status and count. */
        [[nodiscard]] constexpr bool operator==(const AudioRecordingIoResult&) const noexcept = default;
    };

    /**
     * @brief Pull-based capture session for one recording device.
     *
     * Recording deliberately does not share playback's real-time callback: XNA's `Microphone`
     * API polls queued bytes and pulls into caller-owned buffers. `Open` starts capture and
     * `Close` stops it; an implementation may use a native callback internally, but no native
     * handle or callback convention crosses this boundary.
     *
     * Availability and reads are non-blocking. They distinguish an ordinary empty queue from a
     * lost device and a backend error, so higher layers may preserve their own public failure
     * shape without the platform contract destroying diagnostic information first.
     */
    class IAudioRecordingDevice
    {
    public:
        /** @brief Destroys the recording session after its implementation has been closed. */
        virtual ~IAudioRecordingDevice() = default;

        /** @brief Returns the identity this session was created for. */
        [[nodiscard]] virtual const AudioRecordingDeviceInfo& GetInfo() const noexcept = 0;

        /** @brief Returns whether the physical/default-route entry is currently connected. */
        [[nodiscard]] virtual bool IsConnected() const noexcept = 0;

        /**
         * @brief Opens the device, starts capture, and reports the application-facing PCM format.
         * @param requested Preferred capture format; must satisfy `IsValid`.
         * @return The actual format returned by `Read`.
         *
         * Invalid formats, an already-open session, a disconnected device, and native open/start
         * failures are exceptional. A failed call leaves the session closed.
         */
        [[nodiscard]] virtual AudioFormat Open(const AudioFormat& requested) = 0;

        /** @brief Stops capture and closes the session. Safe to call repeatedly. */
        virtual void Close() noexcept = 0;

        /** @brief Returns whether capture is currently open and started. */
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;

        /** @brief Returns the negotiated read format, or an invalid default while closed. */
        [[nodiscard]] virtual AudioFormat GetFormat() const noexcept = 0;

        /**
         * @brief Gets the number of bytes that a following read can obtain without blocking.
         * @return `Success` with a positive count, `WouldBlock` with zero, or a failure status.
         */
        [[nodiscard]] virtual AudioRecordingIoResult GetAvailableBytes() const noexcept = 0;

        /**
         * @brief Reads at most the caller-provided byte span without blocking.
         * @param destination Buffer receiving interleaved PCM in `GetFormat()`.
         * @return `Success` with a positive count no larger than `destination.size()`,
         *         `WouldBlock` with zero, or a failure status with zero.
         *
         * Bytes after the returned count are never modified. An empty destination returns
         * `WouldBlock`; it is not a backend error.
         */
        [[nodiscard]] virtual AudioRecordingIoResult Read(
            std::span<std::byte> destination) noexcept = 0;
    };

    /**
     * @brief Optional recording capability of the selected audio-platform implementation.
     *
     * The selected audio platform exposes a null provider when recording is unsupported. A
     * non-null provider may still return an empty list when the capability exists but no input
     * hardware is connected. This distinction prevents a NULL/playback-only backend from
     * pretending to support capture with silent no-ops.
     */
    class IAudioRecordingDeviceProvider
    {
    public:
        /** @brief Destroys the provider after all devices created from it. */
        virtual ~IAudioRecordingDeviceProvider() = default;

        /**
         * @brief Enumerates currently connected devices in deterministic order.
         *
         * At most one entry has `isDefault=true`; when present it is first. Remaining entries are
         * ordered by ascending stable id. A default-route entry may coexist with the physical
         * device it currently selects because they have different reconnection semantics.
         */
        [[nodiscard]] virtual std::vector<AudioRecordingDeviceInfo> GetDevices() const = 0;

        /**
         * @brief Creates a closed session for a currently enumerated id.
         * @param id Device identity returned by `GetDevices`.
         * @return An independent session, or null if the device disappeared after enumeration.
         */
        [[nodiscard]] virtual std::unique_ptr<IAudioRecordingDevice> CreateDevice(
            AudioRecordingDeviceId id) = 0;
    };

} // namespace CNA::Audio::Platform
