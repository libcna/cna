// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
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

        DynamicSoundEffectInstance(SharpRuntime::intcs sampleRate, AudioChannels channels);
        ~DynamicSoundEffectInstance() override;

        DynamicSoundEffectInstance(const DynamicSoundEffectInstance&) = delete;
        DynamicSoundEffectInstance& operator=(const DynamicSoundEffectInstance&) = delete;
        DynamicSoundEffectInstance(DynamicSoundEffectInstance&&) = delete;
        DynamicSoundEffectInstance& operator=(DynamicSoundEffectInstance&&) = delete;

        /// Gets the number of submitted buffers still pending hardware playback.
        [[nodiscard]] SharpRuntime::intcs getPendingBufferCountProperty() const;

        /// Dynamic instances cannot be looped; always returns false.
        [[nodiscard]] bool getIsLoopedProperty() const override;
        void setIsLoopedProperty(bool value);

        /// Gets whether this instance has been disposed.
        [[nodiscard]] bool getIsDisposedProperty() const;

        /// Converts a byte count to playback duration for this instance's format.
        [[nodiscard]] System::TimeSpan GetSampleDuration(SharpRuntime::intcs sizeInBytes) const;

        /// Converts a duration to a byte count for this instance's format.
        [[nodiscard]] SharpRuntime::intcs GetSampleSizeInBytes(System::TimeSpan duration) const;

        /// Requests more buffers if needed, then starts or continues playback.
        void Play() override;

        /// Stops playback and clears all queued buffers.
        void Stop() override;

        /// Submits a complete 16-bit PCM byte buffer.
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer);

        /// Submits a range from a 16-bit PCM byte buffer.
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer,
                          SharpRuntime::intcs offset,
                          SharpRuntime::intcs count);

        /// Submits a complete float32 sample buffer.
        void SubmitFloatBufferEXT(const std::vector<float>& buffer);

        /// Submits a range from a float32 sample buffer.
        void SubmitFloatBufferEXT(const std::vector<float>& buffer,
                                  SharpRuntime::intcs offset,
                                  SharpRuntime::intcs count);

        /// Submits any pending pre-play buffers to the hardware stream.
        void QueueInitialBuffers();

        /// Clears all pending buffers without stopping playback.
        void ClearBuffers();

        /// Pumps stream data and raises BufferNeeded when more data is required.
        void Update();

        /// Returns the current playback state based on the dynamic track.
        [[nodiscard]] SoundState getStateProperty() const override;

    private:
        SharpRuntime::intcs sampleRate_;
        AudioChannels       channels_;
        bool                isFloat_ = false;
        bool                disposed_ = false;

        void* dynamicTrack_   = nullptr;
        void* audioStream_    = nullptr; // SDL_AudioStream*

        mutable std::mutex queueMutex_;
        std::vector<std::vector<SharpRuntime::bytecs>> queuedBuffers_;

        static constexpr SharpRuntime::intcs MINIMUM_BUFFER_CHECK = 3;

        [[nodiscard]] SharpRuntime::intcs getBytesPerSampleFrame() const;
        void EnsureStream();
        void DestroyStream();
        void SubmitQueuedToStream();

        GetTypeNameHPP()
    };
}
