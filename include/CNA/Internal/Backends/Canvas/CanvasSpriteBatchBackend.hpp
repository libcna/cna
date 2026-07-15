#pragma once

#include "../Common/IGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Canvas
{
    /**
     * @brief `SpriteBatch` backend driven by `ctx.drawImage()` (Phase C4).
     *
     * `End()` needs no explicit flush -- each `Draw()` paints immediately, since Canvas2D has no
     * command-buffer batching concept to defer (plan_canvas.md CANVAS-30).
     */
    class CanvasSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        CanvasSpriteBatchBackend() = default;

        void Begin() override;
        void End() override;
        /// CANVAS-36: `ctx.setTransform(a,b,c,d,e,f)` directly supports a full 2D affine matrix --
        /// called unconditionally per `Begin()` (Identity included), unlike SDL_RENDERER's own fix
        /// which needed a separate non-Identity-only code path.
        void SetTransformMatrix(const Matrix& m) override;
        /// CANVAS-38: throws for a non-null custom Effect (Design decision 10) -- no programmable
        /// shader stage exists on this backend, same conclusion SDL_RENDERER reached (Task 676).
        void SetCustomEffect(Effect* effect) override;

        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        [[nodiscard]] bool IsBegun() const { return begun_; }

    private:
        bool begun_ = false;
    };
}
