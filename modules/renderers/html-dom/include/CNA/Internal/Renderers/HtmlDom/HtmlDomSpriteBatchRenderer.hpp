// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "HtmlDomState.hpp"

#include <vector>

namespace CNA::Internal::Renderers::HtmlDom
{
    /**
     * @brief Resolves one XNA sprite draw into the transform-ready command both draw paths replay.
     *
     * plan_html_dom.md HTMLDOM-33/34/35. Pure function -- no DOM access -- so the geometry that
     * matters most (pivot placement invariant under rotation, flip mirroring about the sprite's own
     * centre, source-rectangle clamping) is unit-testable without a browser.
     *
     * The resulting transform is
     * `translate(destX,destY) rotate(rotation) scale(scaleX,scaleY) [flip] translate(localX,localY)`,
     * which places @p origin -- expressed in source-pixel space -- exactly at the destination
     * rectangle's position for any rotation, matching FNA's own `GenerateVertexInfo` placement.
     *
     * @param textureId     Source texture's JS-side canvas id.
     * @param textureWidth  Source texture width, for source-rectangle clamping.
     * @param textureHeight Source texture height.
     * @param dest          Destination rectangle, in logical game pixels.
     * @param source        Source rectangle, in texture pixels; may leave the texture.
     * @param color         Tint; RGB selects the cached variant, A becomes the sprite's opacity.
     * @param rotation      Clockwise rotation about @p origin, in radians.
     * @param origin        Rotation/scale pivot, in source-pixel space.
     * @param effects       Horizontal/vertical mirroring.
     * @param smoothing     Whether magnification should be smooth rather than nearest-neighbour.
     * @param addressU      Raw TextureAddressMode ordinal for U.
     * @param addressV      Raw TextureAddressMode ordinal for V.
     * @param op            The composite operation in effect, selecting the pixel variant.
     * @return The encoded command.
     * @throws std::runtime_error for an addressing mode this renderer cannot reproduce; see
     *         ValidateAddressModes.
     */
    [[nodiscard]] HtmlDomDrawCommand BuildDrawCommandEXT(int textureId,
                                                         int textureWidth, int textureHeight,
                                                         const Rectangle& dest,
                                                         const Rectangle& source,
                                                         const Color& color,
                                                         float rotation,
                                                         const Vector2& origin,
                                                         SpriteEffects effects,
                                                         bool smoothing,
                                                         int addressU, int addressV,
                                                         DomCompositeOp op);

    /**
     * @brief `SpriteBatch` renderer that emits pooled, CSS-transformed `<div>` elements.
     *
     * plan_html_dom.md design decision 5: `Draw()` does no DOM work at all. It encodes one
     * HtmlDomDrawCommand per sprite into a reusable `std::vector`, and `End()` hands the whole
     * array to JS in a single `EM_JS` call. The JS side then walks the array, assigning sprite *n*
     * of the frame to pool element *n* and writing only the CSS properties whose values actually
     * changed since the previous frame -- in the steady state that is `transform` and `opacity`
     * alone, both compositor-only properties that trigger neither layout nor repaint.
     *
     * While a render target is bound the same command array is replayed into that target's Canvas2D
     * context instead (design decision 10); the batching and the geometry are identical either way.
     */
    class HtmlDomSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /** @brief Creates an empty batch; no DOM element is allocated until the first flush. */
        HtmlDomSpriteBatchRenderer() = default;

        /** @brief Starts a batch, discarding any commands left over from a previous one. */
        void Begin() override;

        /** @brief Ends the batch, flushing every queued sprite to the DOM in one call. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of every sprite's own placement.
         *
         * Composed as a leading CSS `matrix(...)` in each sprite's `transform` list rather than
         * applied to a wrapper element, so two batches with different transforms can coexist in one
         * frame. Omitted entirely for Identity, keeping the common case's style string short.
         *
         * @param m Row-major XNA matrix; only the 2D affine components are used.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Rejects custom sprite effects.
         *
         * @param effect The effect requested; null restores the built-in path.
         * @throws std::runtime_error for any non-null effect -- there is no programmable shader
         *         stage in CSS compositing, the same conclusion the native renderer and `CANVAS` reached.
         */
        void SetCustomEffect(Effect* effect) override;

        /**
         * @brief Selects nearest or smooth magnification for subsequent draws.
         *
         * Maps the magnification ("expand") component of `TextureFilter` to CSS `image-rendering`,
         * which is the only sampling control CSS backgrounds expose -- the same
         * magnification-dominant grouping the native renderer (Task 701) and `CANVAS` (CANVAS-42) use
         * against their own coarser primitives.
         *
         * @param textureFilter Raw TextureFilter ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Records the texture addressing modes subsequent draws use.
         *
         * @param addressU Raw TextureAddressMode ordinal for U (0=Wrap, 1=Clamp, 2=Mirror).
         * @param addressV Raw TextureAddressMode ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief CNAEXT. Records whether the current batch is SpriteSortMode::Immediate.
         *
         * plan_html_dom.md HTMLDOM-118: called by SpriteBatch::Begin(), before Begin() itself, with
         * the sort mode actually requested for this batch. When true, each subsequent Draw() flushes
         * itself to the DOM/Canvas2D immediately (see QueueDraw()'s own comment) instead of being
         * queued for a single end-of-batch flush -- matching real XNA/FNA Immediate semantics, where
         * a device state change between two Draw() calls is honestly reflected per sprite.
         *
         * @param immediate True when SpriteSortMode::Immediate is active for this batch.
         */
        void SetImmediateMode(bool immediate) override { immediateMode_ = immediate; }

        /**
         * @brief Draws a whole texture, unrotated and untinted, at the given position.
         *
         * @param texture Source texture.
         * @param x       Destination left edge, in logical game pixels.
         * @param y       Destination top edge, in logical game pixels.
         * @throws std::runtime_error if called before Begin().
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source rectangle into a destination rectangle with a tint.
         *
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in logical game pixels.
         * @param sourceRectangle      Source rectangle, in texture pixels.
         * @param color                Tint; RGB multiplies the texels, A scales their opacity.
         * @throws std::runtime_error if called before Begin().
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Draws a source rectangle with a tint, rotation, pivot and flip.
         *
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in logical game pixels.
         * @param sourceRectangle      Source rectangle, in texture pixels.
         * @param color                Tint; RGB multiplies the texels, A scales their opacity.
         * @param rotation             Clockwise rotation about @p origin, in radians.
         * @param origin               Rotation/scale pivot, in source-pixel space.
         * @param effects              Horizontal/vertical mirroring.
         * @param layerDepth           Ignored: sorting happens in the shared SpriteBatch layer, and
         *                             DOM document order already realizes the resulting paint order.
         * @throws std::runtime_error if called before Begin(), or for an unsupported addressing
         *         mode combination (see ValidateAddressModes).
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        /** @brief CNAEXT. True between Begin() and End(). */
        [[nodiscard]] bool IsBegun() const { return begun_; }

        /**
         * @brief CNAEXT. Returns the commands queued so far, for tests and diagnostics.
         *
         * @return The batch's command array; empty outside a Begin()/End() block that drew.
         */
        [[nodiscard]] const std::vector<HtmlDomDrawCommand>& GetCommandsEXT() const { return commands_; }

    private:
        void QueueDraw(const ITextureRenderer& texture,
                       const Rectangle& destinationRectangle,
                       const Rectangle& sourceRectangle,
                       const Color& color,
                       float rotation,
                       const Vector2& origin,
                       SpriteEffects effects);

        bool begun_ = false;
        bool immediateMode_ = false;
        bool smoothingEnabled_ = true;
        /// Raw TextureAddressMode ordinals; Clamp matches XNA/FNA's default LinearClamp sampler.
        int addressU_ = 1;
        int addressV_ = 1;
        /// Retained across batches so the vector's capacity is reused instead of reallocated.
        std::vector<HtmlDomDrawCommand> commands_;
        /// Batch transform, flattened to the six CSS `matrix()` components.
        float matrix_[6] = {1, 0, 0, 1, 0, 0};
        bool hasMatrix_ = false;
    };
}
