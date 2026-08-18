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

        if (passes_.empty())
        {
            copyPass_->apply(context);
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

            passes_[index]->apply(step);

            // The next pass reads what this one just wrote.
            step.source = step.destination;
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
