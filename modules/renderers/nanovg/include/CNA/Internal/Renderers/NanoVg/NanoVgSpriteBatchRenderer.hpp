// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgGlLoader.hpp"

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
     * established -- see NanoVgSpriteBatchRenderer.cpp's own comment. An out-of-bounds
     * `sourceRectangle` resolves through real GPU sampling under whichever
     * `TextureAddressMode` the batch was begun with, all three of which map onto a genuine GL wrap
     * enum (`GL_CLAMP_TO_EDGE`, `GL_REPEAT`, `GL_MIRRORED_REPEAT`) -- no CPU-side edge padding and
     * no rejection.
     *
     * `SamplerState` is honoured per batch, not per texture. NanoVG's own image flags
     * (`NVG_IMAGE_NEAREST`, `NVG_IMAGE_REPEATX`/`Y`) are creation-time properties of an image and
     * cannot express a state XNA chooses at `SpriteBatch.Begin()` independently of which texture
     * is drawn, so each `Draw()` writes the batch's filter/address pair onto the image's own GL
     * texture object instead (`ApplyNanoVgImageSamplerState`). `TextureFilter.Anisotropic` is
     * rejected; every other `TextureFilter` maps exactly onto a `GL_TEXTURE_MIN_FILTER`/
     * `GL_TEXTURE_MAG_FILTER` pair.
     *
     * Sprite quads are filled with shape antialiasing disabled (`nvgShapeAntiAlias(ctx, 0)`,
     * scoped to the draw). NanoVG's `NVG_ANTIALIAS` fill insets a path by half a pixel and
     * feathers the missing half; XNA's SpriteBatch has no coverage antialiasing at all, and this
     * renderer never creates a multisample-capable context, so the feathering would be a
     * systematically different rasterization model rather than a refinement.
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
        /// Records the batch's `TextureFilter`, rejecting `Anisotropic` and any ordinal outside the
        /// enum. Every accepted ordinal resolves to an exact GL minification/magnification pair,
        /// written onto each drawn texture by `Draw()`.
        void SetSamplerFilter(int textureFilter) override;
        /// Records the batch's `TextureAddressMode` pair, rejecting any ordinal outside the enum.
        /// `Wrap`, `Clamp` and `Mirror` all map onto a real GL wrap enum.
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
        /// The batch's own sampler state. Defaults to linear filtering and clamped addressing,
        /// matching XNA/FNA's own default `SamplerState.LinearClamp`.
        NanoVgImageSamplerState sampler_{};
        /// The image whose GL texture object already carries `sampler_`, so a batch drawing one
        /// texture repeatedly writes its parameters once. Reset whenever the sampler changes and
        /// at every Begin()/End() boundary, because the next batch may want a different one.
        int lastSamplerImage_ = 0;
        /// Row-major XNA Matrix decomposed to a 2D affine (a,b,c,d,e,f), same Canvas/OpenVG
        /// convention: x'=a*x+c*y+e, y'=b*x+d*y+f. Identity by default.
        float transform_[6] = {1, 0, 0, 1, 0, 0};
    };
}
