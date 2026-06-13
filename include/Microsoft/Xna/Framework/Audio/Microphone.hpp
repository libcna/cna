// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"

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

        /** @brief Cached microphone list used by the framework dispatcher. */
        NOXNA static std::vector<Microphone*>* micList;

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

        /** @brief Checks whether enough data is queued and raises BufferReady when needed. */
        NOXNA void CheckBuffer();

        /** @brief Fixed capture sample rate used by XNA microphone capture. */
        NOXNA static constexpr SharpRuntime::intcs SAMPLERATE = 44100;

    private:
        friend class MicrophoneFactory;

        Microphone(SharpRuntime::uintcs id, std::string name);

        System::TimeSpan bufferDuration_;
        SharpRuntime::uintcs handle_;
        MicrophoneState state_;

        static std::vector<std::unique_ptr<Microphone>> microphoneStorage_;

        [[nodiscard]] SharpRuntime::intcs GetQueuedBytes() const;
    };
}
