// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "SvgDomState.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::SvgDom
{
    class SvgDomTextureRenderer;

    /** @brief SvgDomDrawCommand::flags bit: mirror the sprite horizontally about its own centre. */
    inline constexpr std::int32_t SvgFlagFlipHorizontally = 1 << 0;
    /** @brief SvgDomDrawCommand::flags bit: mirror the sprite vertically about its own centre. */
    inline constexpr std::int32_t SvgFlagFlipVertically = 1 << 1;
    /** @brief SvgDomDrawCommand::flags bit: smooth (linear) magnification rather than nearest. */
    inline constexpr std::int32_t SvgFlagSmoothing = 1 << 2;
    /** @brief SvgDomDrawCommand::flags bit: composite additively (`mix-blend-mode: plus-lighter`). */
    inline constexpr std::int32_t SvgFlagAdditive = 1 << 3;
    /** @brief SvgDomDrawCommand::flags bit: force output alpha to 1 (BlendState.Opaque). */
    inline constexpr std::int32_t SvgFlagOpaque = 1 << 4;
    /**
     * @brief SvgDomDrawCommand::flags bit: fill with a repeating pattern instead of a single crop
     * (`TextureAddressMode::Wrap`/symmetric `Mirror` with an out-of-bounds sourceRectangle --
     * SVGDOM-1). See `ValidateAddressModesEXT`.
     */
    inline constexpr std::int32_t SvgFlagTiled = 1 << 5;
    /**
     * @brief SvgDomDrawCommand::flags bit: the tiled pattern's source is the mirror-doubled variant
     * (symmetric `TextureAddressMode::Mirror`) rather than the plain texture (`Wrap`). Only ever set
     * together with @ref SvgFlagTiled.
     */
    inline constexpr std::int32_t SvgFlagMirrorTiled = 1 << 6;

    /**
     * @brief One sprite in a batch, encoded for a single wasm->JS handoff.
     *
     * plans/plan_svg_dom.md design decision 6, matching `HtmlDom::HtmlDomDrawCommand`'s own "one crossing
     * per batch" strategy: `Draw()` appends one of these per sprite and `End()` hands the whole
     * array to JS in one call. All geometry is resolved on the C++ side (see BuildDrawCommandEXT)
     * into the same `translate(destX,destY) rotate(rotation) scale(scaleX,scaleY) [flip]
     * translate(localX,localY)` chain HtmlDom uses -- collapsed here into one 2x3 affine matrix
     * (m0..m5, the SVG/CSS `matrix(a,b,c,d,e,f)` convention) rather than a transform-list string, so
     * it is both compact for the crossing and directly unit-testable numerically.
     */
    struct SvgDomDrawCommand
    {
        /** @brief Source texture's JS-side id (`Module['cnaSvgDomTextures']`). */
        std::int32_t textureId;
        /** @brief Source rectangle left edge, in texture pixels (validated in-bounds). */
        float sx;
        /** @brief Source rectangle top edge, in texture pixels. */
        float sy;
        /** @brief Source rectangle width, in texture pixels. */
        float sw;
        /** @brief Source rectangle height, in texture pixels. */
        float sh;
        /** @brief Collapsed affine transform, SVG/CSS `matrix(a,b,c,d,e,f)` order: a. */
        float m0;
        /** @brief Collapsed affine transform: b. */
        float m1;
        /** @brief Collapsed affine transform: c. */
        float m2;
        /** @brief Collapsed affine transform: d. */
        float m3;
        /** @brief Collapsed affine transform: e (includes destX and the batch/viewport offset). */
        float m4;
        /** @brief Collapsed affine transform: f (includes destY and the batch/viewport offset). */
        float m5;
        /** @brief Bit flags: see SvgFlagFlipHorizontally and friends. */
        std::int32_t flags;
        /** @brief Cached-variant selector; see VariantModeFor(). */
        std::int32_t variantMode;
        /** @brief Tint colour packed as R | G<<8 | B<<16 | A<<24. */
        std::uint32_t packedColor;
    };

    static_assert(sizeof(SvgDomDrawCommand) == 56,
                  "SvgDomDrawCommand must stay 14 four-byte fields -- the JS flush walks it with "
                  "fixed HEAP32/HEAPF32 index arithmetic.");

    /** @brief Number of four-byte fields in one SvgDomDrawCommand; the JS flush's stride. */
    inline constexpr int SvgDomDrawCommandFields = 14;

    /**
     * @brief Resolves one XNA sprite draw into the transform-ready command both draw paths replay.
     *
     * Pure function -- no DOM access -- so the geometry that matters most (pivot placement invariant
     * under rotation, flip mirroring about the sprite's own centre, the collapsed affine matrix) is
     * unit-testable without a browser. Places @p origin (source-pixel space) exactly at @p dest's
     * position for any rotation, matching FNA's own `GenerateVertexInfo` placement -- the same
     * XNA `SpriteBatch` geometry contract every 2D-only CNA renderer implements.
     *
     * @param textureId     Source texture's JS-side id.
     * @param textureWidth  Source texture width, for source-rectangle bounds validation.
     * @param textureHeight Source texture height.
     * @param dest          Destination rectangle, in logical game pixels.
     * @param source        Source rectangle, in texture pixels; may leave the texture only for
     *                      Wrap/symmetric-Mirror addressing (see ValidateAddressModesEXT).
     * @param color         Tint; RGBA all multiply the texels (see DomCompositeOp's own doc).
     * @param rotation      Clockwise rotation about @p origin, in radians.
     * @param origin        Rotation/scale pivot, in source-pixel space.
     * @param effects       Horizontal/vertical mirroring.
     * @param smoothing     Whether magnification should be smooth rather than nearest-neighbour.
     * @param addressU      Raw TextureAddressMode ordinal for U (0=Wrap, 1=Clamp, 2=Mirror).
     * @param addressV      Raw TextureAddressMode ordinal for V.
     * @param op            The composite operation in effect, selecting the pixel variant/flags.
     * @return The encoded command.
     * @throws std::runtime_error for an addressing-mode combination this renderer cannot reproduce
     *         for an out-of-bounds @p source; see ValidateAddressModesEXT.
     */
    [[nodiscard]] SvgDomDrawCommand BuildDrawCommandEXT(int textureId,
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
     * @brief `SpriteBatch` renderer that emits real `<svg>`/`<image>` sprite elements.
     *
     * plans/plan_svg_dom.md design decision 6: `Draw()` does no DOM work at all -- it encodes one
     * SvgDomDrawCommand per sprite into a reusable `std::vector`, and `End()` hands the whole array
     * to JS in a single call. The JS side pools/reuses sprite elements across frames and
     * dirty-diffs every attribute/style write against each element's own last-applied state (the
     * same "pool cursor reset at Clear(), reuse by index, hide the unused tail at Present()" shape
     * `HtmlDom`'s own DOM path uses) -- a steady frame (same sprite count, same per-sprite
     * texture/geometry/tint) costs no DOM writes beyond the pool's initial creation. V1's own
     * smaller-scope-than-`HtmlDom` boundary is the single flat pool/global scissor clip rather than
     * `HtmlDom`'s per-scissor-rect-isolated region pools (see docs/svg-dom-renderer.md).
     *
     * While a render target is bound the same command array is replayed into that target's Canvas2D
     * context instead (SvgDomState::GetBoundRenderTargetIdEXT) -- unpooled, since render targets
     * are already the slower rasterizing fallback path.
     */
    class SvgDomSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        /** @brief Creates an empty batch; no DOM element is allocated until the first flush. */
        SvgDomSpriteBatchRenderer() = default;

        /** @brief Starts a batch, discarding any commands left over from a previous one. */
        void Begin() override;

        /** @brief Ends the batch, flushing every queued sprite in one call. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of every sprite's own placement.
         *
         * Composed into each sprite's own collapsed matrix rather than applied to a wrapper element,
         * so two batches with different transforms can coexist in one frame.
         *
         * @param m Row-major XNA matrix; only the 2D affine components are used.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Rejects custom sprite effects.
         *
         * @throws std::runtime_error for any non-null effect -- there is no programmable shader
         *         stage in SVG/CSS compositing.
         */
        void SetCustomEffect(Effect* effect) override;

        /** @brief Maps the magnification component of TextureFilter to SVG `image-rendering`. */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Records the texture addressing modes subsequent draws use.
         *
         * Only consulted once a source rectangle actually leaves the texture: `Wrap` and symmetric
         * `Mirror` (the same mode on both axes) tile via a pattern fill; `Clamp` overflow and mixed
         * per-axis addressing remain a narrower, documented throw (see ValidateAddressModesEXT).
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /** @brief CNAEXT. Records whether the current batch is SpriteSortMode::Immediate. */
        void SetImmediateMode(bool immediate) override { immediateMode_ = immediate; }

        /**
         * @brief Draws a whole texture, unrotated and untinted, at the given position.
         *
         * @throws std::runtime_error if called before Begin().
         */
        void Draw(const ITextureRenderer& texture, float x, float y) override;

        /**
         * @brief Draws a source rectangle into a destination rectangle with a tint.
         *
         * @throws std::runtime_error if called before Begin().
         */
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Draws a source rectangle with a tint, rotation, pivot and flip.
         *
         * @param layerDepth Ignored: sorting happens in the shared SpriteBatch layer, and document
         *                   order already realizes the resulting paint order.
         * @throws std::runtime_error if called before Begin(), or for an addressing-mode
         *         combination this renderer cannot reproduce when sourceRectangle leaves the
         *         texture's bounds (see ValidateAddressModesEXT).
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

        /** @brief CNAEXT. Returns the commands queued so far, for tests and diagnostics. */
        [[nodiscard]] const std::vector<SvgDomDrawCommand>& GetCommandsEXT() const { return commands_; }

    private:
        void QueueDraw(const ITextureRenderer& texture,
                       const Rectangle& destinationRectangle,
                       const Rectangle& sourceRectangle,
                       const Color& color,
                       float rotation,
                       const Vector2& origin,
                       SpriteEffects effects);

        /// Flushes @p count commands (and their index-aligned source textures, needed only by the
        /// render-target-bound Canvas2D path to reach each texture's own CPU pixel buffer). Textures
        /// are stored as the generic ITextureRenderer* (SVGDOM-E): the source may be an ordinary
        /// SvgDomTextureRenderer OR a SvgDomRenderTargetRenderer (a RenderTarget2D sampled as a
        /// texture) -- sibling concrete classes, not a subclass relationship, resolved via the
        /// dynamic_cast helpers in the .cpp rather than an unsafe static_cast.
        void Flush(const SvgDomDrawCommand* cmds, const ITextureRenderer* const* textures,
                  int count);

        bool begun_ = false;
        bool immediateMode_ = false;
        bool smoothingEnabled_ = true;
        /// Raw TextureAddressMode ordinals; Clamp matches XNA/FNA's default LinearClamp sampler.
        int addressU_ = 1;
        int addressV_ = 1;
        std::vector<SvgDomDrawCommand> commands_;
        /// Index-aligned with commands_; only consulted by the render-target draw path.
        std::vector<const ITextureRenderer*> commandTextures_;
        /// Batch transform, flattened to the six matrix() components.
        float matrix_[6] = {1, 0, 0, 1, 0, 0};
        bool hasMatrix_ = false;
    };
}
