// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace CNA::Internal::Media
{
    /// Lock-free capture of the final mixed PCM stream, feeding
    /// Microsoft::Xna::Framework::Media::MediaPlayer::GetVisualizationData (plan_media.md
    /// MEDIA-186).
    ///
    /// SDL3_mixer delivers the post-mix buffer on the **audio thread**, so Push() must be
    /// real-time safe: no allocation, no locking, no exceptions. A single-producer/single-consumer
    /// ring buffer with one atomic write cursor satisfies that -- the audio thread only ever
    /// advances the cursor, and the game thread only ever reads behind it.
    ///
    /// Deliberately tolerant of a torn read: if the audio thread overwrites samples while Read()
    /// is copying them, the worst outcome is one visually imperfect frame of a visualizer, which
    /// is not worth a lock on the audio path.
    class VisualizationCapture
    {
    public:
        /// Ring capacity. Must exceed the 512 samples one FFT consumes so a reader can always take
        /// a full window from behind the write cursor.
        static constexpr std::size_t Capacity = 2048;

        /// Appends interleaved float PCM, downmixing to mono. Called from the audio thread.
        ///
        /// @param pcm      Interleaved float32 samples as delivered by SDL3_mixer.
        /// @param samples  Total float count in `pcm` (frames * channels).
        /// @param channels Channel count; values below 1 are treated as mono.
        void Push(const float* pcm, int samples, int channels) noexcept;

        /// Copies the most recently written `count` mono samples, oldest first.
        ///
        /// @param out   Destination buffer.
        /// @param count Number of samples to copy; must not exceed Capacity.
        void Read(float* out, std::size_t count) const noexcept;

        /// Zeroes the buffer and resets the cursor, so a newly enabled capture never reports
        /// samples left over from a previous playback session.
        void Reset() noexcept;

        /// True once any real audio has been captured since the last Reset().
        [[nodiscard]] bool HasData() const noexcept
        {
            return written_.load(std::memory_order_acquire) > 0;
        }

    private:
        std::array<float, Capacity> buffer_{};
        std::atomic<std::size_t> writeIndex_{0};
        std::atomic<std::size_t> written_{0};
    };
}
