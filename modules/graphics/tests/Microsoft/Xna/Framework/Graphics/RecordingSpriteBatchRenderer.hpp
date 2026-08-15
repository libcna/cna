// SPDX-License-Identifier: MS-PL
#pragma once

// Test-only mock/recording ISpriteBatchRenderer + a minimal ITextureRenderer double, shared by
// multiple SpriteBatch test files (Task 411 and its dependents, Tasks 412-416) so SpriteBatch's
// Begin/Draw/End batching and sort-mode logic can be exercised deterministically without a real
// graphics context. Mirrors the project's existing *TestAccess.hpp shared-test-header convention
// (see tests/Microsoft/Xna/Framework/Audio/CueTestAccess.hpp).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"

#include <vector>

namespace CNA::Internal::Renderers
{
    // Minimal ITextureRenderer double: identifies a texture without owning any GPU resource.
    // SpriteBatch::flushSingle() dereferences Texture2D::GetRenderer(), so every Texture2D
    // handed to a SpriteBatch under test needs a non-null renderer of some kind.
    class DummyTextureRenderer : public ITextureRenderer
    {
    public:
        explicit DummyTextureRenderer(int width = 1, int height = 1)
            : width_(width), height_(height)
        {
        }

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }

    private:
        int width_;
        int height_;
    };

    // Records every ISpriteBatchRenderer call, in order, so tests can assert both the content
    // and the delivery order of Begin/Draw/End dispatch without a real graphics context.
    class RecordingSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        struct DrawCall
        {
            const ITextureRenderer* texture = nullptr;
            Rectangle destinationRectangle;
            Rectangle sourceRectangle;
            Color color = Color(255, 255, 255, 255);
            float rotation = 0.0f;
            Vector2 origin;
            Microsoft::Xna::Framework::Graphics::SpriteEffects effects =
                Microsoft::Xna::Framework::Graphics::SpriteEffects::None;
            float layerDepth = 0.0f;
        };

        int beginCount = 0;
        int endCount = 0;
        std::vector<DrawCall> drawCalls;

        void Begin() override { ++beginCount; }
        void End() override { ++endCount; }

        void Draw(const ITextureRenderer& texture, float x, float y) override
        {
            DrawCall call;
            call.texture = &texture;
            call.destinationRectangle = Rectangle(static_cast<int>(x), static_cast<int>(y), 0, 0);
            drawCalls.push_back(call);
        }

        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override
        {
            DrawCall call;
            call.texture = &texture;
            call.destinationRectangle = destinationRectangle;
            call.sourceRectangle = sourceRectangle;
            call.color = color;
            drawCalls.push_back(call);
        }

        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  Microsoft::Xna::Framework::Graphics::SpriteEffects effects,
                  float layerDepth) override
        {
            DrawCall call;
            call.texture = &texture;
            call.destinationRectangle = destinationRectangle;
            call.sourceRectangle = sourceRectangle;
            call.color = color;
            call.rotation = rotation;
            call.origin = origin;
            call.effects = effects;
            call.layerDepth = layerDepth;
            drawCalls.push_back(call);
        }
    };
}
