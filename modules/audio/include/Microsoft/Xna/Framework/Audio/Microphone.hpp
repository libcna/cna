// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/MicrophoneState.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Audio::Platform
{
    class IAudioRecordingDevice;
}

namespace Microsoft::Xna::Framework::Audio
{
    /** @brief Represents a microphone capture device. */
    class Microphone : public System::Object
    {
    public:
        /** @brief Name of the microphone device. */
        const std::string Name;

        /** @brief Raised when enough captured data is available to be read. */
        System::EventHandler<System::EventArgs> BufferReady;

        /** @brief Destructor; closes this device's capture stream if it is still open. */
        ~Microphone() override;

        Microphone(const Microphone&) = delete;
        Microphone& operator=(const Microphone&) = delete;
        Microphone(Microphone&&) = delete;
        Microphone& operator=(Microphone&&) = delete;

        /**
         * @brief Gets the list of all available microphone devices.
         *
         * @return Reference to the vector of available microphones.
         */
        [[nodiscard]] static const std::vector<Microphone*>& getAllProperty();

        /**
         * @brief Gets the default microphone device, or nullptr when none is available.
         *
         * @return Pointer to the default Microphone, or nullptr.
         */
        [[nodiscard]] static Microphone* getDefaultProperty();

        /**
         * @brief Gets the duration threshold used for BufferReady notifications.
         *
         * @return Current buffer duration.
         */
        [[nodiscard]] System::TimeSpan getBufferDurationProperty() const;

        /**
         * @brief Sets the duration threshold used for BufferReady notifications.
         *
         * @param value New buffer duration.
         * @throws System::ArgumentOutOfRangeException if the millisecond component of
         *         @p value is below 100, above 1000, or is not a multiple of 10.
         */
        void setBufferDurationProperty(System::TimeSpan value);

        /**
         * @brief Gets whether this device is a headset microphone.
         *
         * @return true if this is a headset microphone; otherwise false.
         */
        [[nodiscard]] bool getIsHeadsetProperty() const;

        /**
         * @brief Gets the capture sample rate in Hz.
         *
         * @return Sample rate.
         */
        [[nodiscard]] SharpRuntime::intcs getSampleRateProperty() const;

        /**
         * @brief Gets the current capture state of this microphone.
         *
         * @return Current MicrophoneState.
         */
        [[nodiscard]] MicrophoneState getStateProperty() const;

        /**
         * @brief Reads captured audio bytes into the provided buffer.
         *
         * @param buffer Buffer to receive the captured data.
         * @return Number of bytes actually read.
         */
        SharpRuntime::intcs GetData(std::vector<SharpRuntime::bytecs>& buffer);

        /**
         * @brief Reads captured audio bytes into a range of the provided buffer.
         *
         * @param buffer Buffer to receive the captured data.
         * @param offset Byte offset within the buffer to start writing.
         * @param count  Maximum number of bytes to read.
         * @return Number of bytes actually read.
         * @throws System::ArgumentException if @p offset is negative or beyond @p buffer,
         *         or if @p count is not positive or @p offset + @p count exceeds @p buffer.
         */
        SharpRuntime::intcs GetData(std::vector<SharpRuntime::bytecs>& buffer, SharpRuntime::intcs offset,
                                    SharpRuntime::intcs count);

        /**
         * @brief Converts a byte count to its playback duration for this microphone's format.
         *
         * @param sizeInBytes Number of PCM data bytes.
         * @return Corresponding duration.
         */
        [[nodiscard]] System::TimeSpan GetSampleDuration(SharpRuntime::intcs sizeInBytes) const;

        /**
         * @brief Converts a duration to a byte count for this microphone's format.
         *
         * @param duration Desired duration.
         * @return Number of bytes required.
         */
        [[nodiscard]] SharpRuntime::intcs GetSampleSizeInBytes(System::TimeSpan duration) const;

        /** @brief Starts capturing audio samples from this device. */
        void Start();

        /** @brief Stops capturing audio samples from this device. */
        void Stop();

        /** @brief Calls CheckBuffer() on every known microphone (used by FrameworkDispatcher::Update). */
        CNAEXT static void CheckAllBuffers();

        GetTypeNameHPP()

    private:
        // MC-6: FNA has this as `internal void CheckBuffer()` -- it must not be a public C++ API
        // method (CLAUDE.md's Visibility Mapping; also T-1H's own accept criterion). The public,
        // sanctioned bridge for FrameworkDispatcher is the static CheckAllBuffers() above, which
        // already has private-member access to every instance's CheckBuffer() as a same-class
        // static method, so it doesn't need CheckBuffer() itself to be public.
        CNAEXT void CheckBuffer();

        // Production Microphone instances are constructed directly by getAllProperty() from
        // the selected recording provider; tests need a way to construct an isolated
        // instance directly, independent of whatever the current machine/driver enumerates.
        CNAEXT friend struct MicrophoneTestAccess;

        Microphone(std::uint64_t id, std::string name);

        System::TimeSpan bufferDuration_;
        std::uint64_t recordingDeviceId_;
        MicrophoneState state_;
        std::unique_ptr<CNA::Audio::Platform::IAudioRecordingDevice> captureDevice_;

        // FNA internals (Microphone.cs: micList, SAMPLERATE are both `internal`), not CNA additions.
        static std::vector<Microphone*>* micList;
        static constexpr SharpRuntime::intcs SAMPLERATE = 44100;
        SharpRuntime::intcs sampleRate_ = SAMPLERATE;

        static std::vector<std::unique_ptr<Microphone>> microphoneStorage_;

        [[nodiscard]] SharpRuntime::intcs GetQueuedBytes() const;
    };
}
