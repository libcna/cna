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
        /// CANVAS-42: maps the "expand"/magnification component of TextureFilter to
        /// ctx.imageSmoothingEnabled -- Canvas2D only has a binary smoothing toggle, same
        /// magnification-dominant reasoning SDL_RENDERER's Task 701 fix used for its own coarser
        /// single-SDL_ScaleMode primitive.
        void SetSamplerFilter(int textureFilter) override;
        /// CANVAS-43/44: stores the raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror) --
        /// Draw() only branches on these when a sourceRectangle actually exceeds the texture's own
        /// bounds (the only case Wrap/Mirror vs. Clamp can ever visibly differ).
        void SetSamplerAddressMode(int addressU, int addressV) override;

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
        bool smoothingEnabled_ = true;
        /// Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror); default Clamp matches XNA/FNA's
        /// own default SamplerState (LinearClamp).
        int addressU_ = 1;
        int addressV_ = 1;
    };
}
