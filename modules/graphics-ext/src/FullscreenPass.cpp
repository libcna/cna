// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::Effect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    FullscreenPass::FullscreenPass(GraphicsDevice& device)
        : device_(device), spriteBatch_(std::make_unique<SpriteBatch>(device))
    {
    }

    FullscreenPass::~FullscreenPass() = default;

    void FullscreenPass::draw(Texture2D* source, RenderTarget2D* destination, Effect* effect,
                              const int width, const int height)
    {
        if (source == nullptr)
            throw std::invalid_argument("CNA::Graphics::FullscreenPass::draw: source must not be null");
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("CNA::Graphics::FullscreenPass::draw: destination size must be positive");

        // plan_modern.md MOD-203: bound for this scope only. If the draw throws -- a shader that
        // will not link, a SpriteBatch already inside a Begin -- the destination does not stay
        // bound, so the next thing to render does not silently draw into a pass's intermediate.
        ScopedRenderTarget bound(device_, destination);
        drawOverCurrentTarget(source, effect, width, height);
    }

    void FullscreenPass::drawOverCurrentTarget(Texture2D* source, Effect* effect, const int width,
                                               const int height)
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
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            nullptr, nullptr, nullptr, effect);
        spriteBatch_->Draw(*source, Rectangle(0, 0, width, height), Color::White);
        spriteBatch_->End();
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
