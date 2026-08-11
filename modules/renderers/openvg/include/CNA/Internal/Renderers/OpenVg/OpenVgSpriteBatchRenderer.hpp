// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::OpenVg
{
    class OpenVgRenderer;

    /**
     * @brief `SpriteBatch` renderer driven by real `vgDrawImage` calls against a `VGImage` per
     * texture (`OpenVgTextureRenderer`).
     *
     * Geometry follows the same translate/rotate/scale/flip composition CanvasSpriteBatchRenderer
     * uses (CanvasRenderer.cpp's own `CNA_Canvas2D_DrawSprite` -- verified there against FNA's
     * `GenerateVertexInfo` placement formula), expressed through `vgTranslate`/`vgRotate`/
     * `vgScale`/`vgMultMatrix` against `VG_MATRIX_IMAGE_USER_TO_SURFACE` instead of a Canvas2D
     * `CanvasRenderingContext2D`. One extra outer transform (`vgTranslate(0,H); vgScale(1,-1)`,
     * applied once per draw before the per-sprite stack, where H is the CURRENT LOGICAL canvas
     * height -- `OpenVgRenderer::GetLogicalHeightEXT()`, not the physical framebuffer height)
     * compensates for OpenVG's Y-up image space against XNA's Y-down screen space -- see
     * OpenVgSpriteBatchRenderer.cpp's own comment. The logical->physical presentation mapping
     * (Letterbox/Overscan/Stretch/FixedHeightDynamicWidth) is carried entirely by
     * OpenVgRenderer's own GL_PROJECTION ortho and glViewport, so this per-draw transform never
     * needs to know about the physical window at all.
     *
     * `End()` needs no explicit flush: each `Draw()` issues its `vgDrawImage` immediately, matching
     * every other CNA SpriteBatch renderer's "flush model" query (`SetImmediateMode` is a no-op
     * here for the same reason it is on Canvas -- already synchronous per draw).
     */
    class OpenVgSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit OpenVgSpriteBatchRenderer(OpenVgRenderer& owner);
        ~OpenVgSpriteBatchRenderer() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
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
        OpenVgRenderer& owner_;
        bool begun_ = false;
        bool linearFilter_ = true;
        /// Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror); default Clamp matches XNA/FNA's
        /// own default SamplerState (LinearClamp).
        int addressU_ = 1;
        int addressV_ = 1;
        /// Row-major XNA Matrix decomposed to a 2D affine (a,b,c,d,e,f), Canvas'
        /// setTransform-style convention: x'=a*x+c*y+e, y'=b*x+d*y+f. Identity by default.
        float transform_[6] = {1, 0, 0, 1, 0, 0};
        void* tintPaint_ = nullptr; // VGPaint, created lazily on first tinted draw
    };
}
