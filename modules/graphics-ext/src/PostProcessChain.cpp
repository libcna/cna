// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/PostProcessChain.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

    PostProcessChain::PostProcessChain(GraphicsDevice& device)
        : device_(device), pool_(device), copyPass_(std::make_unique<BlitPass>(device))
    {
    }

    PostProcessChain::~PostProcessChain() = default;

    void PostProcessChain::addPass(PostProcessPass* pass)
    {
        if (pass != nullptr)
            passes_.push_back(pass);
    }

    void PostProcessChain::addOwnedPass(std::unique_ptr<PostProcessPass> pass)
    {
        if (!pass)
            return;
        passes_.push_back(pass.get());
        ownedPasses_.push_back(std::move(pass));
    }

    void PostProcessChain::clear()
    {
        passes_.clear();
        ownedPasses_.clear();
    }

    std::size_t PostProcessChain::getPassCount() const
    {
        return passes_.size();
    }

    void PostProcessChain::apply(const PostProcessContext& context)
    {
        if (context.source == nullptr)
            throw std::invalid_argument("CNA::Graphics::PostProcessChain::apply: source must not be null");
        if (context.width <= 0 || context.height <= 0)
            throw std::invalid_argument("CNA::Graphics::PostProcessChain::apply: size must be positive");

        // Collected **before** this frame's ranges open, and that ordering is the whole design.
        // A query object holds one result: reopening it discards whatever the last range put there,
        // so polling after the loop would read a range that was submitted moments ago and has not
        // finished -- which reports a number for the first pass, whose work the driver happens to
        // have retired, and nothing at all for every pass after it. Collecting first reads results
        // the previous frame's present or read-back has already flushed.
        updateTimings();

        if (passes_.empty())
        {
            copyPass_->apply(context);
            timings_.clear();
            return;
        }

        // Intermediates carry the source's own format, so an HDR chain stays HDR until the pass
        // that deliberately leaves it -- putting a Color intermediate between two float passes
        // would clamp the frame invisibly, which is the failure this whole layer exists to avoid.
        const auto intermediateFormat = context.source->getFormatProperty();

        PostProcessContext step = context;
        for (std::size_t index = 0; index < passes_.size(); ++index)
        {
            const bool isLast = index + 1 == passes_.size();

            // The last pass writes the caller's real destination; every earlier one writes an
            // intermediate, alternating between two so no pass reads what it is writing.
            step.destination = isLast
                ? context.destination
                : pool_.acquire(context.width, context.height, intermediateFormat,
                                DepthFormat::None, static_cast<int>(index % 2));

            const bool timed = gpuTimingRequested_ && index < timers_.size()
                            && timers_[index] && timers_[index]->isSupported();
            if (timed) timers_[index]->begin();
            passes_[index]->apply(step);
            if (timed) timers_[index]->end();

            // The next pass reads what this one just wrote.
            step.source = step.destination;
        }
    }

    bool PostProcessChain::isGpuTimingEnabled() const
    {
        return gpuTimingRequested_ && !timers_.empty() && timers_[0] && timers_[0]->isSupported();
    }

    void PostProcessChain::setGpuTimingEnabled(const bool value)
    {
        gpuTimingRequested_ = value;
        if (!value)
        {
            timers_.clear();
            timings_.clear();
        }
    }

    const std::vector<PostProcessChain::PassTiming>& PostProcessChain::getPassTimings() const
    {
        return timings_;
    }

    void PostProcessChain::updateTimings()
    {
        if (!gpuTimingRequested_)
        {
            timings_.clear();
            return;
        }

        // Timers are made lazily and one per slot, because the chain's contents change: a pipeline
        // rebuilds it whenever a pass is switched on. Growing here rather than in addPass keeps the
        // two lists in step without addPass having to know about timing at all.
        while (timers_.size() < passes_.size())
            timers_.push_back(std::make_unique<GpuTimer>(device_));

        if (!timers_.empty() && timers_[0] && !timers_[0]->isSupported())
        {
            // The renderer has no timer query. An empty list rather than a list of zeroes, so a
            // caller can tell "not measured here" from "took no time".
            timings_.clear();
            return;
        }

        timings_.resize(passes_.size());
        for (std::size_t index = 0; index < passes_.size(); ++index)
        {
            timings_[index].Name = passes_[index]->getName();
            // Never waits. A pass whose result has not landed keeps the last one it reported, which
            // is what makes this a measurement of the frame rather than a stall inside it.
            if (timers_[index]) timers_[index]->poll();
            if (timers_[index] && timers_[index]->getSampleCount() > 0)
            {
                timings_[index].Milliseconds = timers_[index]->getLastMilliseconds();
                timings_[index].SampleCount  = timers_[index]->getSampleCount();
            }
        }
    }

    void PostProcessChain::resetTargets()
    {
        pool_.reset();
    }

    const RenderTargetPool& PostProcessChain::getTargetPool() const
    {
        return pool_;
    }

    RenderTargetPool& PostProcessChain::getTargetPool() { return pool_; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
