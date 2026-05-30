#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Audio
{
    /// Sound effect instance whose audio buffers are submitted dynamically by user code.
    class DynamicSoundEffectInstance final : public SoundEffectInstance
    {
    public:
        /// Raised when the instance needs more submitted audio buffers.
        System::EventHandler<System::EventArgs> BufferNeeded;

        /// Creates a dynamic sound effect instance for the specified sample rate and channel count.
        DynamicSoundEffectInstance(SharpRuntime::intcs sampleRate, AudioChannels channels);

        /// Releases queued dynamic buffers.
        ~DynamicSoundEffectInstance();

        DynamicSoundEffectInstance(const DynamicSoundEffectInstance&) = delete;
        DynamicSoundEffectInstance& operator=(const DynamicSoundEffectInstance&) = delete;
        DynamicSoundEffectInstance(DynamicSoundEffectInstance&&) = delete;
        DynamicSoundEffectInstance& operator=(DynamicSoundEffectInstance&&) = delete;

        /// Gets the number of submitted buffers still pending playback/processing.
        [[nodiscard]] SharpRuntime::intcs getPendingBufferCountProperty() const;

        /// Dynamic sound effect instances cannot be looped.
        [[nodiscard]] bool getIsLoopedProperty() const;

        /// Setting this property is ignored because dynamic instances cannot loop.
        void setIsLoopedProperty(bool value);

        /// Gets whether this instance has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Converts a byte count to its playback duration for this instance format.
        [[nodiscard]] System::TimeSpan GetSampleDuration(SharpRuntime::intcs sizeInBytes) const;

        /// Converts a duration to a byte count for this instance format.
        [[nodiscard]] SharpRuntime::intcs GetSampleSizeInBytes(System::TimeSpan duration) const;

        /// Requests more buffers if needed, starts playback and registers this stream for dispatcher updates.
        void Play();

        /// Submits a full 16-bit PCM byte buffer.
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer);

        /// Submits a range from a 16-bit PCM byte buffer.
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer, SharpRuntime::intcs offset,
                          SharpRuntime::intcs count);

        /// Submits a full floating-point sample buffer.
        void SubmitFloatBufferEXT(const std::vector<float>& buffer);

        /// Submits a range from a floating-point sample buffer.
        void SubmitFloatBufferEXT(const std::vector<float>& buffer, SharpRuntime::intcs offset,
                                  SharpRuntime::intcs count);

        /// Queues buffers submitted while stopped.
        void QueueInitialBuffers();

        /// Clears all queued buffers.
        void ClearBuffers();

        /// Performs stream maintenance and raises BufferNeeded when more data is needed.
        void Update();

    private:
        struct WaveFormat
        {
            std::uint16_t wFormatTag;
            std::uint16_t nChannels;
            std::uint32_t nSamplesPerSec;
            std::uint32_t nAvgBytesPerSec;
            std::uint16_t nBlockAlign;
            std::uint16_t wBitsPerSample;
            std::uint16_t cbSize;
        };

        SharpRuntime::intcs sampleRate_;
        AudioChannels channels_;
        WaveFormat format_;
        bool disposed_;

        mutable std::mutex queuedBuffersMutex_;
        std::vector<std::vector<SharpRuntime::bytecs>> queuedBuffers_;
        std::vector<std::uint32_t> queuedSizes_;

        static constexpr SharpRuntime::intcs MINIMUM_BUFFER_CHECK = 3;

        [[nodiscard]] SharpRuntime::intcs getBytesPerSampleFrame() const;

        GetTypeNameHPP()
    };
}
