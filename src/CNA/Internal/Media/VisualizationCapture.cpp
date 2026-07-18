// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Media/VisualizationCapture.hpp"

#include <algorithm>

namespace CNA::Internal::Media
{
    void VisualizationCapture::Push(const float* pcm, int samples, int channels) noexcept
    {
        if (pcm == nullptr || samples <= 0)
        {
            return;
        }
        const std::size_t ch = channels > 1 ? static_cast<std::size_t>(channels) : 1u;
        const std::size_t total = static_cast<std::size_t>(samples);
        const std::size_t frames = total / ch;
        if (frames == 0)
        {
            return;
        }

        std::size_t w = writeIndex_.load(std::memory_order_relaxed);
        for (std::size_t f = 0; f < frames; ++f)
        {
            // Downmix to mono: XNA's visualization Samples array is a single channel, and an
            // average is the cheapest representative signal (a sum would clip on loud stereo).
            float sum = 0.0f;
            for (std::size_t c = 0; c < ch; ++c)
            {
                sum += pcm[f * ch + c];
            }
            buffer_[w] = sum / static_cast<float>(ch);
            w = (w + 1) % Capacity;
        }

        // Release so a consumer that acquires these counters also sees the sample writes above.
        writeIndex_.store(w, std::memory_order_release);
        written_.fetch_add(frames, std::memory_order_release);
    }

    void VisualizationCapture::Read(float* out, std::size_t count) const noexcept
    {
        if (out == nullptr || count == 0)
        {
            return;
        }
        count = std::min(count, Capacity);

        const std::size_t w = writeIndex_.load(std::memory_order_acquire);
        const std::size_t available = written_.load(std::memory_order_acquire);

        // Not enough real audio captured yet -- zero-fill rather than reporting stale/garbage
        // samples, matching VisualizationData's own zero-initialized state.
        if (available < count)
        {
            std::fill_n(out, count, 0.0f);
            return;
        }

        // Copy the `count` samples immediately behind the write cursor, oldest first.
        const std::size_t start = (w + Capacity - count) % Capacity;
        for (std::size_t i = 0; i < count; ++i)
        {
            out[i] = buffer_[(start + i) % Capacity];
        }
    }

    void VisualizationCapture::Reset() noexcept
    {
        buffer_.fill(0.0f);
        writeIndex_.store(0, std::memory_order_release);
        written_.store(0, std::memory_order_release);
    }
}
