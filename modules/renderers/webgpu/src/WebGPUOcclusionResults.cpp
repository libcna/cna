// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/WebGPU/WebGPUOcclusionResults.hpp"

namespace CNA::Internal::Renderers::WebGPU
{
    void WebGPUOcclusionResults::Begin()
    {
        *this = WebGPUOcclusionResults{};
    }

    void WebGPUOcclusionResults::End() { ended_ = true; }
    void WebGPUOcclusionResults::QueueDraw() { ++queuedDraws_; }
    void WebGPUOcclusionResults::RecordBatch() { ++pendingReadbacks_; }
    void WebGPUOcclusionResults::RecordDraw()
    {
        --queuedDraws_;
    }

    void WebGPUOcclusionResults::Resolve(std::uint64_t samples)
    {
        // XNA Refresh GraphicsDevice::SetRenderTargets IL_0000-0557 and Present
        // IL_0013-00ce leave the query interval intact across native pass boundaries.
        count_.Add(samples);
        anySamples_ = anySamples_ || samples != 0;
        --pendingReadbacks_;
    }

    bool WebGPUOcclusionResults::IsComplete() const
    {
        return ended_ && queuedDraws_ == 0 && pendingReadbacks_ == 0;
    }

    bool WebGPUOcclusionResults::Ended() const { return ended_; }
    int WebGPUOcclusionResults::PixelCount(bool precise) const
    {
        return precise ? count_.Value() : int(anySamples_);
    }
}
