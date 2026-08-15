#pragma once

#include "CNA/Internal/Renderers/Canvas/CanvasRenderer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <vector>

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
     * Deferred batches cross the wasm/JavaScript boundary once from `End()` and replay all
     * `ctx.drawImage()` calls there. Immediate batches continue to paint from each `Draw()`.
     */
    class CanvasSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /** @brief Creates a standalone batch renderer with its own default state. */
        CanvasSpriteBatchRenderer();

        /**
         * @brief Creates a batch renderer sharing state with its owning Canvas renderer.
         *
         * @param state Renderer state used to capture the active blend mode at Begin().
         */
        explicit CanvasSpriteBatchRenderer(std::shared_ptr<CanvasRendererState> state);

        void Begin() override;
        void End() override;
        /**
         * @brief Selects immediate per-draw replay or one deferred bulk replay from End().
         *
         * @param immediate True to replay each draw immediately; false to flush once from End().
         */
        void SetImmediateMode(bool immediate) override;
        /// CANVAS-36: `ctx.setTransform(a,b,c,d,e,f)` directly supports a full 2D affine matrix --
        /// called unconditionally per `Begin()` (Identity included), unlike the native renderer's own fix
        /// which needed a separate non-Identity-only code path.
        void SetTransformMatrix(const Matrix& m) override;
        /// CANVAS-38: throws for a non-null custom Effect (Design decision 10) -- no programmable
        /// shader stage exists on this renderer, same conclusion the native renderer reached (Task 676).
        void SetCustomEffect(Effect* effect) override;
        /// CANVAS-42: maps the "expand"/magnification component of TextureFilter to
        /// ctx.imageSmoothingEnabled -- Canvas2D only has a binary smoothing toggle, same
        /// magnification-dominant reasoning the native renderer's Task 701 fix used for its own
        /// coarser single-filter primitive.
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
        struct DrawCommand
        {
            std::int32_t textureId;
            std::int32_t sourceX;
            std::int32_t sourceY;
            std::int32_t sourceWidth;
            std::int32_t sourceHeight;
            float destinationX;
            float destinationY;
            float destinationWidth;
            float destinationHeight;
            float rotation;
            float originX;
            float originY;
            std::int32_t flags;
            std::uint32_t packedColor;
            std::int32_t addressU;
            std::int32_t addressV;
        };

        void QueueOrDraw(const ITextureRenderer& texture,
                         const Rectangle& destinationRectangle,
                         const Rectangle& sourceRectangle,
                         const Color& color,
                         float rotation,
                         const Vector2& origin,
                         SpriteEffects effects);

        std::shared_ptr<CanvasRendererState> state_;
        CanvasCompositeOp activeCompositeOp_ = CanvasCompositeOp::AlphaBlendSourceOver;
        Matrix transform_ = Matrix::getIdentityProperty();
        std::vector<DrawCommand> commands_;
        bool begun_ = false;
        bool immediateMode_ = false;
        bool smoothingEnabled_ = true;
        /// Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror); default Clamp matches XNA/FNA's
        /// own default SamplerState (LinearClamp).
        int addressU_ = 1;
        int addressV_ = 1;
    };
}
