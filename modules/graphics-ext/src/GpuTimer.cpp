// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/GpuTimer.hpp"

#include <string>

#ifdef CNA_CNAEXT

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    GpuTimer::GpuTimer(GraphicsDevice& device)
    {
        auto& renderer = device.GetRenderer();
        if (!renderer.SupportsGpuTimerEXT())
        {
            unsupportedReason_ =
                "the " + std::string(device.GetGraphicsRendererName()) + " renderer has no GPU timer query "
                "(GL ES needs GL_EXT_disjoint_timer_query, desktop GL needs 3.3 or ARB_timer_query)";
            return;
        }
        renderer_ = renderer.CreateGpuTimerEXT();
        if (!renderer_)
            unsupportedReason_ = "the " + std::string(device.GetGraphicsRendererName())
                               + " renderer reported a GPU timer and then did not create one";
    }

    GpuTimer::~GpuTimer() = default;

    bool GpuTimer::isSupported() const { return renderer_ != nullptr; }

    const std::string& GpuTimer::getUnsupportedReason() const { return unsupportedReason_; }

    bool GpuTimer::isOpen() const { return open_; }

    void GpuTimer::begin()
    {
        if (!renderer_ || open_) return;
        renderer_->Begin();
        open_ = true;
    }

    void GpuTimer::end()
    {
        if (!renderer_ || !open_) return;
        renderer_->End();
        open_ = false;
        pending_ = true;
    }

    bool GpuTimer::isResultAvailable() const
    {
        return renderer_ != nullptr && pending_ && renderer_->IsResultAvailable();
    }

    bool GpuTimer::poll()
    {
        if (!isResultAvailable()) return false;
        // Nanoseconds to milliseconds. Done here rather than in the renderer because the renderer's
        // unit is what the API returns and this one is what a person reads.
        lastMilliseconds_ = static_cast<double>(renderer_->ElapsedNanoseconds()) / 1.0e6;
        pending_ = false;
        ++sampleCount_;
        return true;
    }

    double GpuTimer::getLastMilliseconds() const { return lastMilliseconds_; }

    int GpuTimer::getSampleCount() const { return sampleCount_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
