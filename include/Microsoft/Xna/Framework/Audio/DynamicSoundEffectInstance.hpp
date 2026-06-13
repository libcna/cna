// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"

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
    /** @brief Sound effect instance whose audio buffers are submitted dynamically by user code. */
    class DynamicSoundEffectInstance final : public SoundEffectInstance
    {
    public:
        /** @brief Raised when the instance needs more submitted audio buffers. */
        System::EventHandler<System::EventArgs> BufferNeeded;

        /**
         * @brief Constructs a DynamicSoundEffectInstance with the specified format.
         *
         * @param sampleRate Audio sample rate in Hz.
         * @param channels   Channel layout (Mono or Stereo).
         */
        DynamicSoundEffectInstance(SharpRuntime::intcs sampleRate, AudioChannels channels);

        /** @brief Destroys the instance and releases the audio stream. */
        ~DynamicSoundEffectInstance() override;

        DynamicSoundEffectInstance(const DynamicSoundEffectInstance&) = delete;
        DynamicSoundEffectInstance& operator=(const DynamicSoundEffectInstance&) = delete;
        DynamicSoundEffectInstance(DynamicSoundEffectInstance&&) = delete;
        DynamicSoundEffectInstance& operator=(DynamicSoundEffectInstance&&) = delete;

        /**
         * @brief Gets the number of submitted buffers still pending hardware playback.
         *
         * @return Pending buffer count.
         */
        [[nodiscard]] SharpRuntime::intcs getPendingBufferCountProperty() const;

        /**
         * @brief Dynamic instances cannot be looped; always returns false.
         *
         * @return false.
         */
        [[nodiscard]] bool getIsLoopedProperty() const override;

        /**
         * @brief Attempting to set IsLooped on a dynamic instance has no effect.
         *
         * @param value Ignored.
         */
        void setIsLoopedProperty(bool value);

        /**
         * @brief Gets whether this instance has been disposed.
         *
         * @return true if disposed; otherwise false.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Converts a byte count to playback duration for this instance's format.
         *
         * @param sizeInBytes Number of PCM data bytes.
         * @return Corresponding playback duration.
         */
        [[nodiscard]] System::TimeSpan GetSampleDuration(SharpRuntime::intcs sizeInBytes) const;

        /**
         * @brief Converts a duration to a byte count for this instance's format.
         *
         * @param duration Desired playback duration.
         * @return Number of bytes required.
         */
        [[nodiscard]] SharpRuntime::intcs GetSampleSizeInBytes(System::TimeSpan duration) const;

        /** @brief Requests more buffers if needed, then starts or continues playback. */
        void Play() override;

        /** @brief Stops playback and clears all queued buffers. */
        void Stop() override;

        /**
         * @brief Submits a complete 16-bit PCM byte buffer for playback.
         *
         * @param buffer PCM audio data.
         */
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer);

        /**
         * @brief Submits a range from a 16-bit PCM byte buffer for playback.
         *
         * @param buffer PCM audio data.
         * @param offset Byte offset into the buffer.
         * @param count  Number of bytes to submit.
         */
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer,
                          SharpRuntime::intcs offset,
                          SharpRuntime::intcs count);

        /**
         * @brief Submits a complete float32 sample buffer for playback.
         *
         * @param buffer Float32 audio samples.
         */
        void SubmitFloatBufferEXT(const std::vector<float>& buffer);

        /**
         * @brief Submits a range from a float32 sample buffer for playback.
         *
         * @param buffer Float32 audio samples.
         * @param offset Sample offset into the buffer.
         * @param count  Number of samples to submit.
         */
        void SubmitFloatBufferEXT(const std::vector<float>& buffer,
                                  SharpRuntime::intcs offset,
                                  SharpRuntime::intcs count);

        /** @brief Submits any pending pre-play buffers to the hardware stream. */
        NOXNA void QueueInitialBuffers();

        /** @brief Clears all pending buffers without stopping playback. */
        NOXNA void ClearBuffers();

        /** @brief Pumps stream data and raises BufferNeeded when more data is required. */
        NOXNA void Update();

        /**
         * @brief Returns the current playback state based on the dynamic track.
         *
         * @return Current SoundState.
         */
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
