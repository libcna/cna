// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace CNA::Audio::Platform {

    /** @brief Scalar sample representation exchanged with an audio device. */
    enum class AudioSampleFormat
    {
        /** @brief No usable sample representation. Never valid for an open device. */
        Unknown,
        /** @brief Native-endian signed 16-bit PCM. */
        Signed16,
        /** @brief Native-endian 32-bit IEEE 754 floating-point PCM. */
        Float32
    };

    /** @brief PCM layout requested from, or negotiated by, an audio device. */
    struct AudioFormat
    {
        /** @brief Scalar samples per second in each channel. Zero is invalid. */
        std::uint32_t sampleRate = 0;
        /** @brief Number of interleaved channels. Zero is invalid. */
        std::uint8_t channels = 0;
        /** @brief Representation of each scalar sample. `Unknown` is invalid. */
        AudioSampleFormat sampleFormat = AudioSampleFormat::Unknown;

        /** @brief Compares every negotiated-format component. */
        [[nodiscard]] constexpr bool operator==(const AudioFormat&) const noexcept = default;
    };

    /** @brief Returns the byte width of one scalar sample, or zero for `Unknown`. */
    [[nodiscard]] constexpr std::size_t BytesPerSample(const AudioSampleFormat format) noexcept
    {
        switch (format)
        {
            case AudioSampleFormat::Signed16:
                return sizeof(std::int16_t);
            case AudioSampleFormat::Float32:
                return sizeof(float);
            case AudioSampleFormat::Unknown:
                return 0;
        }
        return 0;
    }

    /** @brief Returns whether a format can be requested or reported as negotiated. */
    [[nodiscard]] constexpr bool IsValid(const AudioFormat& format) noexcept
    {
        return format.sampleRate > 0 && format.channels > 0
            && BytesPerSample(format.sampleFormat) > 0;
    }

    /**
     * @brief Supplies one complete interleaved PCM buffer to a playback device.
     *
     * The callback is deliberately buffer-shaped: one virtual dispatch fills one complete
     * backend-owned buffer, never one dispatch per sample. A backend may split a larger native
     * request into bounded preallocated buffers. `sampleCount` counts scalar interleaved samples,
     * not frames; it is therefore a multiple of the negotiated channel count and `output.size()`
     * is exactly `sampleCount * BytesPerSample(negotiatedFormat.sampleFormat)`.
     *
     * Implementations may invoke this method on a real-time/native audio thread. It must not
     * block, allocate, or throw. It must initialize the complete output span; silence is all-zero
     * bits in both currently supported representations. Device lifecycle methods must not be
     * called from this callback; `Stop` and `Close` may wait for an in-flight callback to finish.
     */
    class IAudioBufferCallback
    {
    public:
        /** @brief Destroys the callback after no device retains it. */
        virtual ~IAudioBufferCallback() = default;

        /**
         * @brief Fills one complete output buffer.
         * @param output Writable bytes for the whole buffer.
         * @param sampleCount Number of scalar interleaved samples represented by `output`.
         */
        virtual void FillBuffer(std::span<std::byte> output,
                                std::size_t sampleCount) noexcept = 0;
    };

    /**
     * @brief Owns one platform playback device independently of the window/input platform.
     *
     * `Open` retains shared ownership of the callback until `Close` returns. On success it returns
     * the actual PCM format the callback must produce; a device may negotiate a different rate,
     * channel count, or representation from the request, but it must never silently report the
     * request when a different format is delivered. A newly opened device is paused: the callback
     * is not invoked until `Start`. `Stop` is a callback barrier, and after it returns no callback
     * is active or begins until the next `Start`. After `Close` returns the callback will never be
     * invoked again.
     *
     * Invalid requests, a null callback, opening an already-open device, and device-open failures
     * are exceptional. A failed `Open` leaves the device closed. `Close` is idempotent and may be
     * called during cleanup after partial initialization.
     */
    class IAudioDevice
    {
    public:
        /** @brief Destroys the playback device after its implementation has been closed. */
        virtual ~IAudioDevice() = default;

        /**
         * @brief Opens playback paused and installs its whole-buffer callback.
         * @param requested Preferred application-side PCM format; must satisfy `IsValid`.
         * @param callback Callback retained through the matching `Close`; must not be null.
         * @return The actual format subsequently passed to the callback.
         */
        [[nodiscard]] virtual AudioFormat Open(
            const AudioFormat& requested,
            std::shared_ptr<IAudioBufferCallback> callback) = 0;

        /** @brief Starts or resumes callbacks. Idempotent while already running. */
        virtual void Start() = 0;

        /** @brief Pauses playback and waits for any in-flight callback. Safe to call repeatedly. */
        virtual void Stop() noexcept = 0;

        /** @brief Stops callbacks and closes the device. Safe to call repeatedly. */
        virtual void Close() noexcept = 0;

        /** @brief Returns whether the device is currently open. */
        [[nodiscard]] virtual bool IsOpen() const noexcept = 0;

        /** @brief Returns whether an open device currently permits callbacks. */
        [[nodiscard]] virtual bool IsRunning() const noexcept = 0;

        /**
         * @brief Returns the negotiated format, or an invalid default format while closed.
         */
        [[nodiscard]] virtual AudioFormat GetFormat() const noexcept = 0;
    };

} // namespace CNA::Audio::Platform
