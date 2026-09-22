// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Internal/Graphics/EngineLayerFloatFiltering.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    FullscreenPass::FullscreenPass(GraphicsDevice& device)
        : device_(device), spriteBatch_(std::make_unique<SpriteBatch>(device))
    {
    }

    FullscreenPass::~FullscreenPass() = default;

    void FullscreenPass::draw(Texture2D* source, RenderTarget2D* destination, Effect* effect,
                              const int width, const int height, SamplerState* sampler)
    {
        if (source == nullptr)
            throw std::invalid_argument("CNA::Graphics::FullscreenPass::draw: source must not be null");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("CNA::Graphics::FullscreenPass::draw: destination size must be positive");

        // plans/plan_modern.md MOD-203: bound for this scope only. If the draw throws -- a shader that
        // will not link, a SpriteBatch already inside a Begin -- the destination does not stay
        // bound, so the next thing to render does not silently draw into a pass's intermediate.
        ScopedRenderTarget bound(device_, destination);
        drawOverCurrentTarget(source, effect, width, height, sampler);
    }

    void FullscreenPass::drawOverCurrentTarget(Texture2D* source, Effect* effect, const int width,
                                               const int height, SamplerState* sampler)
    {
        if (source == nullptr)
            throw std::invalid_argument(
                "CNA::Graphics::FullscreenPass::drawOverCurrentTarget: source must not be null");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument(
                "CNA::Graphics::FullscreenPass::drawOverCurrentTarget: the size must be positive");

        // Opaque, not AlphaBlend: a post-process pass replaces the destination rather than
        // compositing onto it, and blending a pass's own output against whatever the target held
        // is a source of results that look almost right.
        // A null sampler means the device default, which is what every pass wanted before
        // MOD-220 and still wants unless it says otherwise.
        //
        // plans/plan_vulkan_modern_graphics.md VMG-0006: that default -- and a pass's explicit
        // linear request -- filters, and XNA refuses a filtered read of a float or half-float
        // source (SOFTWARE-217). Inside the engine-layer scope below such a read is permitted where
        // the renderer really filters the format; where it does not, the pass reads the source
        // with point sampling, which is exact for the same-size passes and the documented
        // degradation for resampling ones, instead of throwing.
        SamplerState* effective = sampler;
        const auto format = source->getFormatProperty();
        const bool filters = (effective == nullptr) ||
            effective->getFilterProperty() != Microsoft::Xna::Framework::Graphics::TextureFilter::Point;
        if (filters && CNA::Internal::EngineLayerFloatFilteringScope::IsPointFilterOnlyFormat(format) &&
            !CNA::Internal::EngineLayerFloatFilteringScope::RendererFiltersFormat(device_, format))
        {
            // The const_cast is forced by the XNA-shaped API (see BloomPass): SamplerState's stock
            // objects are static const and SpriteBatch::Begin takes a non-const pointer.
            effective = const_cast<SamplerState*>(&SamplerState::PointClamp);
        }

        CNA::Internal::EngineLayerFloatFilteringScope engineDraw(device_);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            effective, nullptr, nullptr, effect);
        spriteBatch_->Draw(*source, Rectangle(0, 0, width, height), Color::White);
        spriteBatch_->End();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
