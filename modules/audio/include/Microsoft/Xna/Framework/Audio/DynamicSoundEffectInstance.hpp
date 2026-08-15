// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
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
         * @param looped Ignored.
         */
        void setIsLoopedProperty(const bool& looped) override;

        /** @brief Attempting to set IsLooped on a dynamic instance has no effect (move overload). */
        CNAEXT void setIsLoopedProperty(bool&& looped) override;

        /** @brief Stops playback, releases the dynamic audio stream, and disposes the instance. */
        void Dispose() override;

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
         * @brief Stops playback and clears all queued buffers.
         *
         * @param immediate Must be true; dynamic instances have no authored loop to release
         *        into, so a non-immediate stop is not a valid operation.
         * @throws System::InvalidOperationException if @p immediate is false.
         */
        void Stop(bool immediate) override;

        // Pause()/Resume() are NOT overridden here (P13-DYNAMIC-001): now that this class shares
        // the inherited `track_` with SoundEffectInstance instead of its own separate
        // `dynamicTrack_`, the base class's own virtual Pause()/Resume() already operate on the
        // right field and need no dynamic-specific override -- Resume()'s own `Play()` call
        // dispatches virtually to this class's override regardless of which class's Resume() body
        // runs it. Previously overridden here solely because of the old field split (CP-15); that
        // reason no longer applies.

        /**
         * @brief Submits a complete 16-bit PCM byte buffer for playback.
         *
         * @param buffer PCM audio data.
         */
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer);

        /**
         * @brief Submits a range from a 16-bit PCM byte buffer for playback.
         *
         * If SubmitFloatBufferEXT() was previously used on this instance while it is not
         * currently Stopped, throws System::InvalidOperationException instead of feeding int16
         * bytes into a live float-format stream (a CNAEXT safety guard; real XNA has no float
         * submission path at all).
         *
         * @param buffer PCM audio data.
         * @param offset Byte offset into the buffer.
         * @param count  Number of bytes to submit.
         * @throws System::InvalidOperationException if this instance is currently playing/paused
         *         in float mode (see SubmitFloatBufferEXT()).
         */
        void SubmitBuffer(const std::vector<SharpRuntime::bytecs>& buffer,
                          SharpRuntime::intcs offset,
                          SharpRuntime::intcs count);

        /**
         * @brief Submits a complete float32 sample buffer for playback.
         *
         * @param buffer Float32 audio samples.
         */
        CNAEXT void SubmitFloatBufferEXT(const std::vector<float>& buffer);

        /**
         * @brief Submits a range from a float32 sample buffer for playback.
         *
         * Switches this instance to float mode; throws System::InvalidOperationException if
         * called while playing/paused in int16 mode. See SubmitBuffer()'s symmetric guard for
         * switching back.
         *
         * @param buffer Float32 audio samples.
         * @param offset Sample offset into the buffer.
         * @param count  Number of samples to submit.
         * @throws System::InvalidOperationException if this instance is currently playing/paused
         *         in int16 mode.
         */
        CNAEXT void SubmitFloatBufferEXT(const std::vector<float>& buffer,
                                         SharpRuntime::intcs offset,
                                         SharpRuntime::intcs count);

        /** @brief Submits any pending pre-play buffers to the hardware stream. */
        CNAEXT void QueueInitialBuffers();

        /** @brief Clears all pending buffers without stopping playback. */
        CNAEXT void ClearBuffers();

        /** @brief Pumps stream data and raises BufferNeeded when more data is required. */
        CNAEXT void Update();

        /**
         * @brief Returns the current playback state based on the dynamic track.
         *
         * @return Current SoundState.
         */
        [[nodiscard]] SoundState getStateProperty() const override;

        GetTypeNameHPP()

    private:
        SharpRuntime::intcs sampleRate_;
        AudioChannels       channels_;

        // AUD-15-006: written by SubmitBuffer()/SubmitFloatBufferEXT() (this class's own
        // documented producer-thread-callable entry points) and read by EnsureStream() (called
        // from Play(), the game thread) -- a real TSAN-confirmed data race as a plain bool, since
        // neither side takes queueMutex_ for it (EnsureStream() runs before a track/stream
        // exists to synchronize around, and this flag must be visible before that point).
        std::atomic<bool>   isFloat_ = false;

        bool                streamIsFloat_ = false; // format of the live queued source; game-thread-only

        void* audioStream_    = nullptr; // opaque internal mixer stream

        mutable std::mutex queueMutex_;
        std::vector<std::vector<SharpRuntime::bytecs>> queuedBuffers_;

        // Byte size of each chunk handed to the mixer stream, oldest first. A chunk is only
        // popped once Update() observes that the stream no longer holds that many bytes -- once it has
        // actually been consumed by playback, not merely submitted. Counted alongside
        // queuedBuffers_ by getPendingBufferCountProperty() (matches FNA's PendingBufferCount,
        // which only shrinks once the native voice reports a buffer as consumed).
        std::deque<std::size_t> submittedChunkSizes_;

        static constexpr SharpRuntime::intcs MINIMUM_BUFFER_CHECK = 3;

        void EnsureStream();
        void DestroyStream();
        void SubmitQueuedToStream();

        // AUD-15-006: the actual submit logic, extracted so SubmitBuffer()/SubmitFloatBufferEXT()
        // can drive it from within their own already-held queueMutex_ lock (matching FNA's real
        // SubmitBuffer, which checks State and submits to the native voice atomically under its
        // own queuedBuffers lock) without double-locking the non-recursive queueMutex_. Caller
        // must already hold queueMutex_.
        void SubmitQueuedToStreamLocked();

        void StopInternal();
    };
}
