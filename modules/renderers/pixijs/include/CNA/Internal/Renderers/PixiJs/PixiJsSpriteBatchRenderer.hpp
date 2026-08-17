#pragma once

#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace CNA::Internal::Renderers::PixiJs
{
    /**
     * @brief `SpriteBatch` renderer driven by a pooled, retained `PIXI.Sprite` scene graph
     * (Design decisions 5/7).
     *
     * `Draw()` appends a fixed-stride POD command to a C++ `std::vector`; `End()` hands the whole
     * array to one `EM_JS` call that updates a pooled array of `PIXI.Sprite` objects via their own
     * native `position`/`anchor`/`rotation`/`scale`/`tint`/`alpha` properties -- no manual transform
     * math, unlike `CanvasSpriteBatchRenderer`'s `ctx.translate`/`rotate`/`scale` composition.
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
        /// plan_pixijs.md PIXIJS-45: applied by composing the batch's 2D affine transform with each
        /// sprite's own local placement matrix at flush time (`sprite.transform.setFromMatrix`),
        /// matching FNA's own "transformMatrix applied after per-sprite local placement" contract.
        void SetTransformMatrix(const Matrix& m) override;
        /// plan_pixijs.md PIXIJS-47/Design decision 10: throws for a non-null custom Effect in
        /// this v1 scope.
        void SetCustomEffect(Effect* effect) override;
        /// plan_pixijs.md PIXIJS-53: maps to PIXI.SCALE_MODES on the sampled base texture.
        void SetSamplerFilter(int textureFilter) override;
        /// plan_pixijs.md PIXIJS-46: maps to PIXI.WRAP_MODES on the sampled base texture (native
        /// gl.REPEAT/gl.MIRRORED_REPEAT, unlike CANVAS-44's pattern-source emulation). Known
        /// boundary (see PixiJsSpriteBatchRenderer.cpp): PixiJS rejects any per-draw texture frame
        /// larger than the base texture, so this only affects linear-filter edge bleed, not the
        /// classic XNA "oversized source rect tiles under Wrap" trick.
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

        std::shared_ptr<PixiJsRendererState> state_;
        PixiJsBlendMode activeBlendMode_ = PixiJsBlendMode::AlphaBlend;
        std::vector<DrawCommand> commands_;
        bool begun_ = false;
        bool immediateMode_ = false;
        /// Raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror); default Clamp matches XNA/FNA's
        /// own default SamplerState (LinearClamp). PixiJS's BaseTexture exposes one wrapMode for
        /// both axes -- addressU is the representative value applied at flush time (PIXIJS-46's
        /// own documented boundary: mixed per-axis U/V modes are not expressible this way).
        int addressU_ = 1;
        int addressV_ = 1;
        /// True for a "linear" (smoothed) sampler, false for "point" (nearest) -- same
        /// magnification-dominant TextureFilter grouping CANVAS-42 already established.
        bool linearFilter_ = true;
        /// plan_pixijs.md PIXIJS-45: the batch's 2D affine transform (`Begin(transformMatrix)`),
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
        /// plan_pixijs.md PIXIJS-52: real WebGL blend-factor/equation GL enum values, captured from
        /// PixiJsRendererState at Begin() (mirroring how activeBlendMode_ itself is captured),
        /// meaningful only when activeBlendMode_ == PixiJsBlendMode::Custom.
        int customBlendSrcRGB_ = 1;
        int customBlendDstRGB_ = 0;
        int customBlendSrcAlpha_ = 1;
        int customBlendDstAlpha_ = 0;
        int customBlendEquationRGB_ = 32774;
        int customBlendEquationAlpha_ = 32774;
    };
}
