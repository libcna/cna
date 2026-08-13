#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::Canvas
{
    /// CANVAS-84: Canvas2D applies tint RGB to straight-alpha pixels and tint A separately via
    /// globalAlpha. AlphaBlend's XNA draw colour is premultiplied, so recover its straight RGB
    /// before entering that split path. Pure C++ so the conversion and zero-alpha canonicalization
    /// are covered by host tests without a browser CanvasRenderingContext2D.
    Color NormalizeCanvasSpriteTint(const Color& color, bool alphaBlend);

    /// plan_canvas.md CANVAS-44: throws std::runtime_error for the narrow, honestly-documented gaps
    /// around Wrap/Mirror addressing combined with an out-of-bounds sourceRectangle (mixed U/V
    /// modes; a tinted draw; an AlphaBlend draw needing per-pixel un-premultiply of the tiled
    /// pattern source) -- called from CanvasSpriteBatchRenderer's Draw() path before it ever reaches
    /// the JS draw function, so these are real C++ exceptions, not a silently-skipped draw. A no-op
    /// for Clamp or an in-bounds sourceRectangle (the common case, where Wrap/Mirror vs. Clamp can
    /// never visibly differ). Contains no EM_JS/JS calls -- exposed standalone so plan_canvas.md
    /// CANVAS-80's structural GTest coverage can unit test this validation directly.
    void ValidateAddressModeCombination(int addressU, int addressV, bool exceedsBounds,
                                        bool tinted, bool needsUnpremultiply);

    /**
     * @brief `SpriteBatch` renderer driven by `ctx.drawImage()` (Phase C4).
     *
     * `End()` needs no explicit flush -- each `Draw()` paints immediately, since Canvas2D has no
     * command-buffer batching concept to defer (plan_canvas.md CANVAS-30).
     */
    class CanvasSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        CanvasSpriteBatchRenderer() = default;

        void Begin() override;
        void End() override;
        /// CANVAS-36: `ctx.setTransform(a,b,c,d,e,f)` directly supports a full 2D affine matrix --
        /// called unconditionally per `Begin()` (Identity included), unlike SDL_RENDERER's own fix
        /// which needed a separate non-Identity-only code path.
        void SetTransformMatrix(const Matrix& m) override;
        /// CANVAS-38: throws for a non-null custom Effect (Design decision 10) -- no programmable
        /// shader stage exists on this renderer, same conclusion SDL_RENDERER reached (Task 676).
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

        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
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
