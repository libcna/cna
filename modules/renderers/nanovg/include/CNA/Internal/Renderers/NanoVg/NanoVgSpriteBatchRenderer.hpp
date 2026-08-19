// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::NanoVg
{
    class NanoVgRenderer;

    /**
     * @brief `SpriteBatch` renderer driven by `nvgImagePattern` + a filled rectangle path, one
     * `NVGcontext` frame (`nvgBeginFrame`/`nvgEndFrame`) per `Begin()`/`End()` pair.
     *
     * NanoVG has no "draw image" primitive of its own -- every textured draw is a filled path
     * whose paint samples an image (`nvgImagePattern(ox,oy,ex,ey,angle,image,alpha)` describes
     * where the FULL image would map if drawn as a box; the fill path decides which part of that
     * box is actually visible). Geometry follows the exact same translate -> rotate -> scale ->
     * flip composition `CanvasSpriteBatchRenderer`'s own `CNA_Canvas2D_DrawSprite` uses (verified
     * there against FNA's real `GenerateVertexInfo` placement formula) -- unlike
     * `OpenVgSpriteBatchRenderer`, NO outer device-flip transform is needed, because NanoVG's
     * coordinate system already matches XNA's (top-left origin, Y-down), the same reason Canvas
     * needs none either.
     *
     * Source-rectangle cropping needs no CPU-side image copy (unlike OpenVG's `vgCopyImage`
     * workaround for ShivaVG's broken `vgChildImage`): the pattern box is simply positioned so the
     * image's own natural extent maps correctly relative to the filled sub-rectangle, all still
     * inside the SOURCE-pixel-scaled coordinate system `nvgScale(destW/sw, destH/sh)` already
     * established -- see NanoVgSpriteBatchRenderer.cpp's own comment. Out-of-bounds
     * `sourceRectangle` + the default `TextureAddressMode.Clamp` is real, automatic GPU
     * `GL_CLAMP_TO_EDGE` sampling (every `NanoVgTextureRenderer` is created with no
     * `NVG_IMAGE_REPEATX`/`NVG_IMAGE_REPEATY` flag) -- no CPU-side edge-padding needed either.
     * `Wrap`/`Mirror` combined with an out-of-bounds `sourceRectangle` is rejected: NanoVG bakes
     * repeat/clamp into the IMAGE at creation time, not per draw, so a genuinely different
     * per-draw wrap mode cannot be honored without recreating the texture.
     *
     * `End()` calls `nvgEndFrame()`, which is where NanoVG actually submits its accumulated GL
     * draw calls -- so, unlike `OpenVgSpriteBatchRenderer`'s per-`Draw()`-immediate `vgDrawImage`,
     * this renderer's draws are recorded during `Begin()..End()` and flushed once at `End()`.
     * `SetImmediateMode` is still a no-op: `SpriteSortMode::Immediate` already calls `Draw()`
     * once per sprite with no batching above this layer, and NanoVG's own internal call list still
     * flushes correctly regardless of how many `Draw()`s occur between `Begin()`/`End()`.
     */
    class NanoVgSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit NanoVgSpriteBatchRenderer(NanoVgRenderer& owner);
        ~NanoVgSpriteBatchRenderer() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
        /// No-op: NanoVG's sampling filter (`NVG_IMAGE_NEAREST`) is a per-IMAGE creation flag, not
        /// a per-draw sampler state -- see NanoVgTextureRenderer's own comment.
        void SetSamplerFilter(int textureFilter) override;
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
        NanoVgRenderer& owner_;
        bool begun_ = false;
        /// Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror); default Clamp matches XNA/FNA's
        /// own default SamplerState (LinearClamp).
        int addressU_ = 1;
        int addressV_ = 1;
        /// Row-major XNA Matrix decomposed to a 2D affine (a,b,c,d,e,f), same Canvas/OpenVG
        /// convention: x'=a*x+c*y+e, y'=b*x+d*y+f. Identity by default.
        float transform_[6] = {1, 0, 0, 1, 0, 0};
    };
}
