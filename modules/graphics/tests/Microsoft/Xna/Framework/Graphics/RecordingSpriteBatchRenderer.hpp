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

#include <cmath>
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
        /// Casting a non-finite float to int is undefined behaviour, and since CABI-38 sprite
        /// destinations can be non-finite: they are XNA-valid and travel into the vertex path.
        /// UBSan reported four such casts here, but only once the build enabled
        /// `float-cast-overflow`, which plain `-fsanitize=undefined` leaves off on this GCC.
        ///
        /// The float fields above are the record that matters and keep the value exactly; this is
        /// the quantised convenience copy, so a non-finite component becomes 0 rather than
        /// whatever the cast happened to produce.
        [[nodiscard]] static int Quantise(float value) noexcept
        {
            return std::isfinite(value) ? static_cast<int>(value) : 0;
        }

        struct DrawCall
        {
            const ITextureRenderer* texture = nullptr;
            // The destination as SpriteBatch actually delivered it -- unrounded, matching
            // XNA/FNA. destinationRectangle is the same value quantised, so the assertions
            // written before sub-pixel destinations existed keep their meaning.
            float destinationX = 0.0f;
            float destinationY = 0.0f;
            float destinationWidth = 0.0f;
            float destinationHeight = 0.0f;
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
            call.destinationRectangle = Rectangle(Quantise(x), Quantise(y), 0, 0);
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
            Draw(texture,
                 static_cast<float>(destinationRectangle.X),
                 static_cast<float>(destinationRectangle.Y),
                 static_cast<float>(destinationRectangle.Width),
                 static_cast<float>(destinationRectangle.Height),
                 sourceRectangle, color, rotation, origin, effects, layerDepth);
        }

        void Draw(const ITextureRenderer& texture,
                  float destinationX,
                  float destinationY,
                  float destinationWidth,
                  float destinationHeight,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  Microsoft::Xna::Framework::Graphics::SpriteEffects effects,
                  float layerDepth) override
        {
            DrawCall call;
            call.texture = &texture;
            call.destinationX = destinationX;
            call.destinationY = destinationY;
            call.destinationWidth = destinationWidth;
            call.destinationHeight = destinationHeight;
            call.destinationRectangle = Rectangle(
                Quantise(destinationX), Quantise(destinationY),
                Quantise(destinationWidth), Quantise(destinationHeight));
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
