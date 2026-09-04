// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgGlLoader.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"

namespace CNA::Internal::Renderers::NanoVg
{

    /**
     * @brief `SpriteBatch` renderer driven by `nvgImagePattern` + a filled rectangle path, wrapped
     * in an `NVGcontext` frame opened at `Begin()` and submitted at `End()`.
     *
     * Normally that is exactly one `nvgBeginFrame`/`nvgEndFrame` pair per `Begin()`/`End()`. One
     * case re-opens the frame mid-batch: an Immediate batch whose sprite coordinate space changes
     * between two draws (a `GraphicsDevice.Viewport` resized under it). That is safe only because
     * an Immediate draw has already flushed by then -- `glnvg__renderFlush` zeroes the recorded
     * vertex/path/call/uniform counts -- so `nvgBeginFrame`'s own reset discards nothing. It is a
     * documented dependency on the pinned NanoVG revision's behaviour rather than on anything its
     * public API promises, and must be re-verified if that pin moves.
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
     * `nvgEndFrame()` is where NanoVG normally submits its accumulated GL draw calls, so this
     * renderer defers its GPU work across `Draw()` calls -- unlike `OpenVgSpriteBatchRenderer`'s
     * per-`Draw()`-immediate `vgDrawImage`. That makes `SetImmediateMode` load-bearing rather than
     * decorative: under `SpriteSortMode::Immediate` each `Draw()` submits its own work before
     * returning (`nvgInternalParams(ctx)->renderFlush`, which flushes the recorded call list
     * WITHOUT ending the frame, so the batch's scissor/transform/blend state survives), exactly as
     * `ISpriteBatchRenderer::SetImmediateMode`'s own contract requires. Without it, a
     * `GraphicsDevice` operation issued between two Immediate `Draw()` calls -- a `Clear()`, most
     * visibly -- would land BEFORE sprites the caller has already drawn, which is the wrong order.
     * Deferred batches are unaffected and still flush once, at `End()`.
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
        /// Records whether this batch is `SpriteSortMode::Immediate`. Immediate batches submit each
        /// `Draw()`'s work before returning instead of accumulating it until `End()`.
        void SetImmediateMode(bool immediate) override;

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
        /// Opens a NanoVG frame for @p projection and re-establishes the batch state that
        /// `nvgBeginFrame`'s own `nvgReset()` clears (blend factors, transform).
        void BeginFrameForProjection(const NanoVgRenderer::SpriteProjection& projection);
        /// Pushes the owner's current blend factors into the open frame.
        void ApplyCurrentBlendFunc();

        NanoVgRenderer& owner_;
        bool begun_ = false;
        /// The batch's own sampler state. Defaults to linear filtering and clamped addressing,
        /// matching XNA/FNA's own default `SamplerState.LinearClamp`.
        NanoVgImageSamplerState sampler_{};
        /// The image whose GL texture object already carries `sampler_`, so a batch drawing one
        /// texture repeatedly writes its parameters once. Reset whenever the sampler changes and
        /// at every Begin()/End() boundary, because the next batch may want a different one.
        int lastSamplerImage_ = 0;
        /// True for a `SpriteSortMode::Immediate` batch, set by `SpriteBatch::Begin()` before
        /// `Begin()` itself. Drives the per-draw flush; see this class's own doc comment.
        bool immediate_ = false;
        /// The sprite coordinate space this batch's frame was opened for, and the blend factors
        /// currently pushed into it. Compared per Draw() in Immediate mode so a device state change
        /// between two draws is picked up without re-issuing work that has not changed.
        NanoVgRenderer::SpriteProjection projection_{};
        NanoVgBlendFunc appliedBlend_{};
        /// Row-major XNA Matrix decomposed to a 2D affine (a,b,c,d,e,f), same Canvas/OpenVG
        /// convention: x'=a*x+c*y+e, y'=b*x+d*y+f. Identity by default.
        float transform_[6] = {1, 0, 0, 1, 0, 0};
    };
}
