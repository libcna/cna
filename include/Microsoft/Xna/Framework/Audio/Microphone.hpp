// SPDX-License-Identifier: MS-PL
#pragma once

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
    /// Represents a microphone capture device.
    class Microphone : public System::Object
    {
    public:
        /// Name of the microphone device.
        const std::string Name;

        /// Raised when enough captured data is available to be read.
        System::EventHandler<System::EventArgs> BufferReady;

        /// Gets all available microphones.
        [[nodiscard]] static const std::vector<Microphone*>& getAllProperty();

        /// Gets the default microphone, or nullptr when no microphones are available.
        [[nodiscard]] static Microphone* getDefaultProperty();

        /// Cached microphone list used by the framework dispatcher.
        static std::vector<Microphone*>* micList;

        /// Gets the duration threshold used for BufferReady notifications.
        [[nodiscard]] System::TimeSpan getBufferDurationProperty() const;

        /// Sets the duration threshold used for BufferReady notifications.
        void setBufferDurationProperty(System::TimeSpan value);

        /// Gets whether this device is a headset microphone.
        [[nodiscard]] bool getIsHeadsetProperty() const;

        /// Gets the capture sample rate.
        [[nodiscard]] SharpRuntime::intcs getSampleRateProperty() const;

        /// Gets the current microphone state.
        [[nodiscard]] MicrophoneState getStateProperty() const;

        /// Reads captured sample bytes into a buffer.
        SharpRuntime::intcs GetData(std::vector<SharpRuntime::bytecs>& buffer);

        /// Reads captured sample bytes into a range of a buffer.
        SharpRuntime::intcs GetData(std::vector<SharpRuntime::bytecs>& buffer, SharpRuntime::intcs offset,
                                    SharpRuntime::intcs count);

        /// Converts a byte count to its duration for this microphone format.
        [[nodiscard]] System::TimeSpan GetSampleDuration(SharpRuntime::intcs sizeInBytes) const;

        /// Converts a duration to a byte count for this microphone format.
        [[nodiscard]] SharpRuntime::intcs GetSampleSizeInBytes(System::TimeSpan duration) const;

        /// Starts capturing samples.
        void Start();

        /// Stops capturing samples.
        void Stop();

        /// Checks whether enough data is queued and raises BufferReady when needed.
        void CheckBuffer();

        static constexpr SharpRuntime::intcs SAMPLERATE = 44100;

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
