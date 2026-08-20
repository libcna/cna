#pragma once

#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::PixiJs
{
    /**
     * @brief `SpriteBatch` renderer that drives a pooled `PIXI.Sprite` set and commits at every
     * submission point (Design decisions 5/7, PIXIJS-87).
     *
     * `Draw()` appends a fixed-stride POD command to a C++ `std::vector`; `End()` hands the whole
     * array to one `EM_JS` call that configures a pooled array of `PIXI.Sprite` objects via their
     * own native `position`/`anchor`/`rotation`/`scale`/`tint`/`alpha` properties -- no manual
     * transform math, unlike `CanvasSpriteBatchRenderer`'s `ctx.translate`/`rotate`/`scale`
     * composition -- and then renders them into the active target immediately.
     *
     * Rendering at the submission point rather than leaving pooled sprites parented in the retained
     * scene graph is what makes this renderer obey XNA's `SpriteBatch` contract: batches accumulate
     * in submission order, `SpriteSortMode::Immediate` really is immediate, a render-target switch
     * cannot move another target's content, per-batch blend/sampler state cannot be rewritten after
     * the fact, and a `Texture2D` may be destroyed as soon as `End()` returns. See
     * `PixiJsRenderer.cpp`'s own submission-model comment for the full account.
     */
    class PixiJsSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /** @brief Creates a standalone batch renderer with its own default state. */
        PixiJsSpriteBatchRenderer();

        /**
         * @brief Creates a batch renderer sharing state with its owning PixiJS renderer.
         *
         * @param state Renderer state used to capture the active blend mode at Begin().
         */
        explicit PixiJsSpriteBatchRenderer(std::shared_ptr<PixiJsRendererState> state);

        void Begin() override;
        void End() override;
        /** @brief Selects immediate per-draw replay or one deferred bulk replay from End(). */
        void SetImmediateMode(bool immediate) override;
        /// plans/plan_pixijs.md PIXIJS-45: applied by composing the batch's 2D affine transform with each
        /// sprite's own local placement matrix at flush time (`sprite.transform.setFromMatrix`),
        /// matching FNA's own "transformMatrix applied after per-sprite local placement" contract.
        void SetTransformMatrix(const Matrix& m) override;
        /// plans/plan_pixijs.md PIXIJS-47/Design decision 10: throws for a non-null custom Effect in
        /// this v1 scope.
        void SetCustomEffect(Effect* effect) override;
        /**
         * @brief Selects the sampler's magnification filter (PIXIJS-53).
         *
         * @param textureFilter Raw XNA TextureFilter int; maps to PIXI.SCALE_MODES on the sampled
         *        base texture at flush time.
         * @throws std::runtime_error For a value outside the TextureFilter enumeration.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Selects the sampler's texture addressing mode (PIXIJS-46/PIXIJS-90).
         *
         * Maps to native `PIXI.WRAP_MODES` (`gl.REPEAT`/`gl.CLAMP_TO_EDGE`/`gl.MIRRORED_REPEAT`),
         * unlike CANVAS-44's pattern-source emulation. Known boundary (see
         * PixiJsSpriteBatchRenderer.cpp): PixiJS rejects any per-draw texture frame larger than the
         * base texture, so wrapping affects linear-filter edge bleed rather than the classic XNA
         * "oversized source rect tiles under Wrap" trick.
         *
         * @param addressU Raw XNA TextureAddressMode int for the U axis.
         * @param addressV Raw XNA TextureAddressMode int for the V axis.
         * @throws std::runtime_error For a value outside the TextureAddressMode enumeration.
         * @throws System::NotSupportedException If the two axes differ -- a PIXI.BaseTexture has a
         *         single `wrapMode` covering both, so independent per-axis addressing is rejected
         *         rather than silently approximated by whichever axis happened to win.
         */
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
        };

        void QueueOrDraw(const ITextureRenderer& texture,
                         const Rectangle& destinationRectangle,
                         const Rectangle& sourceRectangle,
                         const Color& color,
                         float rotation,
                         const Vector2& origin,
                         SpriteEffects effects);

        /// PIXIJS-87: hands `count` packed commands to PixiJS and rasterizes them into the active
        /// target before returning. Called once per `End()` in a deferred batch and once per
        /// `Draw()` in `SpriteSortMode::Immediate`.
        void Flush(const DrawCommand* commands, int count);

        std::shared_ptr<PixiJsRendererState> state_;
        PixiJsBlendMode activeBlendMode_ = PixiJsBlendMode::AlphaBlend;
        std::vector<DrawCommand> commands_;
        bool begun_ = false;
        bool immediateMode_ = false;
        /// Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror); default Clamp matches XNA/FNA's
        /// own default SamplerState (LinearClamp). The two are always equal -- a mixed pair is
        /// rejected by SetSamplerAddressMode rather than half-applied (PIXIJS-90).
        int addressU_ = 1;
        int addressV_ = 1;
        /// True for a "linear" (smoothed) sampler, false for "point" (nearest) -- same
        /// magnification-dominant TextureFilter grouping CANVAS-42 already established.
        bool linearFilter_ = true;
        /// plans/plan_pixijs.md PIXIJS-45: the batch's 2D affine transform (`Begin(transformMatrix)`),
        /// stored as its own upper-left 2x2 + translation components (XNA's `Matrix.M11/M12/M21/
        /// M22/M41/M42`) rather than the full 4x4 `Matrix` -- SpriteBatch's transform is always a 2D
        /// affine map in this v1 scope, matching FNA's own `SpriteEffect` vertex shader usage.
        /// Defaults to identity.
        float transformA_ = 1.0f;
        float transformB_ = 0.0f;
        float transformC_ = 0.0f;
        float transformD_ = 1.0f;
        float transformTx_ = 0.0f;
        float transformTy_ = 0.0f;
        /// PIXIJS-87/88/89: the batch's own graphics state, captured from PixiJsRendererState at
        /// Begin() and used by every flush this batch performs. Captured rather than read at flush
        /// time so a later batch's state change cannot reach an already-begun one -- the retained
        /// equivalent of what a real GPU renderer gets for free by recording state into the command
        /// stream.
        int blendSrcRGB_ = 1;
        int blendDstRGB_ = 0;
        int blendSrcAlpha_ = 1;
        int blendDstAlpha_ = 0;
        int blendEquationRGB_ = 32774;
        int blendEquationAlpha_ = 32774;
        float blendFactorR_ = 0.0f;
        float blendFactorG_ = 0.0f;
        float blendFactorB_ = 0.0f;
        float blendFactorA_ = 0.0f;
        int colorWriteChannels_ = 15;
    };
}
