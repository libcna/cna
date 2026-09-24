// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/GpuTimer.hpp"

#include <string>

#ifdef CNA_CNAEXT

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    GpuTimer::GpuTimer(GraphicsDevice& device) : device_(&device)
    {
        auto& renderer = device.GetRenderer();
        if (!renderer.SupportsGpuTimerEXT())
        {
            unsupportedReason_ =
                "the " + std::string(device.GetGraphicsRendererName()) + " renderer has no GPU timer query "
                "(the selected device/API exposes no implemented timestamp-query path)";
            return;
        }
        queries_[0] = renderer.CreateGpuTimerEXT();
        if (!queries_[0])
            unsupportedReason_ = "the " + std::string(device.GetGraphicsRendererName())
                               + " renderer reported a GPU timer and then did not create one";
    }

    GpuTimer::~GpuTimer() = default;

    bool GpuTimer::isSupported() const { return queries_[0] != nullptr; }

    const std::string& GpuTimer::getUnsupportedReason() const { return unsupportedReason_; }

    bool GpuTimer::isOpen() const { return open_; }

    void GpuTimer::begin()
    {
        if (!isSupported() || open_) return;
        // Every query still holds a range the GPU has not finished. Reopening one would discard
        // an answer that is on its way, which is how a CPU running ahead of the GPU used to lose
        // every answer; timing nothing this once keeps the ones in flight.
        if (pendingCount_ == kRangesInFlight) return;
        const std::size_t slot =
            pendingCount_ == 0 ? 0 : (oldest_ + pendingCount_) % kRangesInFlight;
        if (!queries_[slot])
        {
            queries_[slot] = device_->GetRenderer().CreateGpuTimerEXT();
            if (!queries_[slot]) return;
        }
        queries_[slot]->Begin();
        current_ = slot;
        open_ = true;
    }

    void GpuTimer::end()
    {
        if (!open_) return;
        queries_[current_]->End();
        open_ = false;
        if (pendingCount_ == 0) oldest_ = current_;
        ++pendingCount_;
    }

    bool GpuTimer::isResultAvailable() const
    {
        return pendingCount_ > 0 && queries_[oldest_]->IsResultAvailable();
    }

    bool GpuTimer::poll()
    {
        // Oldest first: a GPU finishes ranges in the order they were submitted, so the first one
        // still working is where collecting stops.
        bool collected = false;
        while (isResultAvailable())
        {
            // Nanoseconds to milliseconds. Done here rather than in the renderer because the
            // renderer's unit is what the API returns and this one is what a person reads.
            lastMilliseconds_ = static_cast<double>(queries_[oldest_]->ElapsedNanoseconds()) / 1.0e6;
            ++sampleCount_;
            oldest_ = (oldest_ + 1) % kRangesInFlight;
            --pendingCount_;
            collected = true;
        }
        return collected;
    }

    double GpuTimer::getLastMilliseconds() const { return lastMilliseconds_; }

    int GpuTimer::getSampleCount() const { return sampleCount_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
