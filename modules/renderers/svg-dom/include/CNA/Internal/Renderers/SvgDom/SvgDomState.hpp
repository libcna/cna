// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Internal::Renderers::SvgDom
{
    /**
     * @brief How a draw's source pixels must be prepared, and how the result composites.
     *
     * plans/plan_svg_dom.md design decisions 3 and 4. Neither CSS Compositing (which `mix-blend-mode`
     * on an SVG element also participates in, since it applies to any element with a box, not only
     * HTML ones) nor SVG's own filter/compositing primitives expose a per-channel blend-factor
     * model, so the four standard XNA `BlendState` presets are reproduced by preparing the SOURCE
     * PIXELS instead of the compositing step -- the same strategy `CNA::Internal::Renderers::HtmlDom`
     * uses for its own CSS-composited `<div>` path, independently re-derived here for SVG's own
     * primitives (an `<image>`'s referenced bytes plus an `feColorMatrix` filter, not a
     * `background-image` variant):
     *
     * - `Opaque` (`srcBlend=One`, `dstBlend=Zero` for both colour and alpha) replaces the
     *   destination pixel with the source pixel exactly, alpha included -- a real Porter-Duff
     *   "copy". Neither CSS Compositing's `mix-blend-mode` list nor SVG's per-element stacking has
     *   an operator for that, so this renderer reproduces the same accepted, documented deviation
     *   HTML_DOM's own `<div>` path uses: the drawn element's own filter forces its OUTPUT alpha to
     *   a constant 1 (see `SvgDomTintFilterEXT`), which is the closest a single stacked, blended
     *   element can get to "ignore the destination" while still being composited by the browser.
     * - `AlphaBlend` (`srcBlend=One`) assumes source colour that is ALREADY premultiplied by its own
     *   alpha; ordinary SVG/CSS `source-over` compositing assumes straight alpha, so the sprite is
     *   drawn from a lazily generated, cached un-premultiplied copy of the texture (variant 1 below).
     * - `NonPremultiplied` (`srcBlend=SourceAlpha`) already matches what source-over assumes
     *   natively, so the texture's own uploaded bytes are used unmodified (variant 0).
     * - `Additive` needs no pixel preparation beyond the ordinary uploaded bytes, only
     *   `style="mix-blend-mode:plus-lighter"` on the sprite element.
     */
    enum class DomCompositeOp
    {
        /** @brief BlendState.Opaque -- straight source pixels, output alpha forced to 1 by filter. */
        Opaque = 0,
        /** @brief BlendState.NonPremultiplied -- source pixels as uploaded, normal compositing. */
        NonPremultiplied = 1,
        /** @brief BlendState.AlphaBlend -- un-premultiplied source pixels, normal compositing. */
        AlphaBlend = 2,
        /** @brief BlendState.Additive -- source pixels as uploaded, `mix-blend-mode: plus-lighter`. */
        Additive = 3
    };

    /**
     * @brief Which cached pixel variant of a texture a DomCompositeOp draws from.
     *
     * Unlike `HtmlDom::VariantModeFor`, `Opaque`'s alpha-forcing needs no separate pixel variant
     * here -- it is expressed with an `feColorMatrix` alpha row of `0 0 0 0 1` on top of variant 0,
     * applied at draw time (see `SvgDomTextureRenderer`). Only the un-premultiply transform is a
     * genuinely non-linear per-pixel operation `feColorMatrix` cannot express, so it is the only
     * one still baked into a cached, lazily generated texture-level variant.
     *
     * @param op The composite operation in effect for the draw.
     * @return 0 for the as-uploaded pixels, 1 for the un-premultiplied variant.
     */
    [[nodiscard]] constexpr int VariantModeFor(DomCompositeOp op)
    {
        return op == DomCompositeOp::AlphaBlend ? 1 : 0;
    }

    /**
     * @brief Maps raw XNA BlendState factor/function ordinals to a DomCompositeOp.
     *
     * Pure function -- no DOM access -- exercised directly by `CNA_SVG_DOM_HOST_TESTS`/
     * `CNA_RENDERER_SVG_DOM` GTest coverage without a browser. Ordinals match
     * `IGraphicsRenderer::ApplyBlendState`'s own documented convention (Blend: One=0, Zero=1,
     * SourceAlpha=4, InverseSourceAlpha=5; BlendFunction: Add=0) -- the same table
     * `HtmlDom::BlendStateToDomCompositeOp` and `CanvasRenderer::BlendStateToCompositeOp` read.
     *
     * @param colorSrcBlend  Raw Blend ordinal for the colour source factor.
     * @param alphaSrcBlend  Raw Blend ordinal for the alpha source factor.
     * @param colorDstBlend  Raw Blend ordinal for the colour destination factor.
     * @param alphaDstBlend  Raw Blend ordinal for the alpha destination factor.
     * @param colorBlendFunc Raw BlendFunction ordinal for the colour channels.
     * @param alphaBlendFunc Raw BlendFunction ordinal for the alpha channel.
     * @return The DomCompositeOp reproducing that BlendState.
     * @throws std::runtime_error if the combination is not one of the four standard presets.
     */
    [[nodiscard]] DomCompositeOp BlendStateToDomCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                                            int colorDstBlend, int alphaDstBlend,
                                                            int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief CNAEXT. Returns the composite operation the most recent ApplyBlendState selected.
     *
     * Shared between SvgDomRenderer (which sets it) and SvgDomSpriteBatchRenderer (which encodes
     * it into every draw command) -- plain C++ state rather than a JS-side flag, so both it and
     * every decision made from it stay unit-testable outside a browser.
     *
     * @return The active composite operation; DomCompositeOp::NonPremultiplied before any
     *         ApplyBlendState call, matching source-over's own native straight-alpha compositing.
     */
    [[nodiscard]] DomCompositeOp GetCurrentCompositeOpEXT();

    /** @brief CNAEXT. Records the composite operation subsequent draws must use. */
    void SetCurrentCompositeOpEXT(DomCompositeOp op);

    /** @brief CNAEXT. Returns whether the most recent ApplyRasterizerState enabled scissor testing. */
    [[nodiscard]] bool GetCurrentScissorEnableEXT();

    /** @brief CNAEXT. Records whether subsequent draws should honour the scissor rect. */
    void SetCurrentScissorEnableEXT(bool enabled);

    /** @brief CNAEXT. Records the scissor rectangle most recently set via SetScissorRect. */
    void SetCurrentScissorRectEXT(float x, float y, float w, float h);

    /** @brief CNAEXT. Returns the scissor rectangle most recently set via SetScissorRect. */
    void GetCurrentScissorRectEXT(float& x, float& y, float& w, float& h);

    /**
     * @brief CNAEXT. Records the active GraphicsDevice.Viewport rectangle (SVGDOM-F).
     *
     * plans/plan_svg_dom.md design decision 6: real XNA/FNA applies Viewport.X/Y strictly after
     * SpriteBatch's own Begin(transformMatrix), so (X,Y) is composed as the OUTERMOST translation on
     * top of each sprite's own placement (see SvgDomSpriteBatchRenderer::QueueDraw) -- the same
     * ordering HtmlDom's own per-batch viewport offset uses, independently re-derived here.
     * Width/Height are retained too (unlike the pre-SVGDOM-F renderer, which discarded them): per
     * this project's own cross-renderer SpriteBatch contract (spritebatch_custom_viewport_test.cpp),
     * a Viewport smaller than the active target must unconditionally CLIP rendering to its own
     * rectangle -- independent of RasterizerState.ScissorTestEnable -- while never rescaling sprite
     * coordinates. See ComputeEffectiveClipRectEXT.
     *
     * @param w,h Width/Height; a non-positive value means "unset" (full target, no viewport clip) --
     *            the state before any SetViewport call, matching real XNA's default full-target
     *            Viewport being a no-op for this renderer's own clip purposes.
     */
    void SetCurrentViewportRectEXT(float x, float y, float w, float h);

    /**
     * @brief CNAEXT. Returns the active viewport rectangle; w/h are <= 0 before any SetViewport
     * call (full target, no clip).
     */
    void GetCurrentViewportRectEXT(float& x, float& y, float& w, float& h);

    /**
     * @brief CNAEXT. Intersects the active Viewport rectangle (if set) with the scissor rectangle
     * (if RasterizerState.ScissorTestEnable is true) into the one effective clip rect a flush should
     * apply (SVGDOM-F). Pure function over the C++-side state above -- no DOM access.
     *
     * @param x,y,w,h Receive the effective clip rectangle, in absolute logical/target pixels, when
     *                this returns true. Meaningless when it returns false.
     * @return False when neither the viewport nor the scissor rect constrains anything (the caller
     *         should skip clip-path/clip-region work entirely -- the common, cheap case); true
     *         otherwise, including when the intersection is empty (w or h == 0), which the caller
     *         must still apply (it correctly clips everything away).
     */
    [[nodiscard]] bool ComputeEffectiveClipRectEXT(float& x, float& y, float& w, float& h);

    /**
     * @brief CNAEXT. Returns the JS-side canvas id of the currently bound render target.
     *
     * plans/plan_svg_dom.md design decision 5: while a render target is bound, draws cannot become real
     * `<svg>`/`<image>` elements (an SVG element cannot render into an off-screen surface a
     * `Texture2D::GetData` readback could later sample), so they route through the target's own
     * private Canvas2D context instead -- the same "render targets fall back to raster" boundary
     * `HtmlDom`'s own render-target path uses, independently justified here by the identical
     * constraint (no browser API rasterizes a live vector subtree back to pixels synchronously).
     *
     * @return The bound target's canvas id, or 0 when the SVG backbuffer is the active target.
     */
    [[nodiscard]] int GetBoundRenderTargetIdEXT();

    /** @brief CNAEXT. Records which render target is bound, or 0 for the SVG backbuffer. */
    void SetBoundRenderTargetIdEXT(int canvasId);

    /**
     * @brief CNAEXT. Allocates the next unique id for `Module['cnaSvgDomTextures']`.
     *
     * Shared by every texture (plain or render-target) so both kinds key into the same JS-side
     * registry without collisions -- a render target sampled later as an ordinary Draw() source
     * texture resolves through the identical id/lookup an uploaded texture uses.
     *
     * @return A process-wide monotonically increasing id, starting at 1 (0 means "no texture").
     */
    [[nodiscard]] int AllocateTextureIdEXT();

    /**
     * @brief Rejects an addressing-mode combination this renderer cannot reproduce for an
     * out-of-bounds source rectangle, and reports whether tiling/mirroring applies.
     *
     * plans/plan_svg_dom.md SVGDOM-1: `Wrap` (both axes) and symmetric `Mirror` (the same mode on both
     * axes -- `SamplerState` has no built-in Mirror preset, so a game always constructs a custom
     * one, and using the same mode on both axes is simply the natural way to do that) are
     * implemented via a real SVG `<pattern>` fill (backbuffer path) / `ctx.createPattern(...,
     * 'repeat')` (render-target Canvas2D path) -- see `SvgDomSpriteBatchRenderer::BuildDrawCommandEXT`.
     * `Clamp` overflow (edge-texel extension) and mixed per-axis addressing remain a narrower,
     * documented throw -- a real, smaller-scope boundary than `HtmlDom`'s own (which additionally
     * handles Clamp overflow and independent per-axis Wrap/Clamp combinations), not a silent
     * approximation. Pure function -- no DOM access.
     *
     * @param addressU      Raw TextureAddressMode ordinal for U (0=Wrap, 1=Clamp, 2=Mirror).
     * @param addressV      Raw TextureAddressMode ordinal for V.
     * @param exceedsBounds Whether the source rectangle leaves the texture's own bounds.
     * @param tiled         Receives true when the draw should tile via a pattern fill (Wrap or
     *                      symmetric Mirror, only meaningful when @p exceedsBounds is true).
     * @param mirrored      Receives true when the tiling is symmetric `Mirror` (as opposed to
     *                      `Wrap`), only meaningful when @p tiled is true.
     * @throws std::runtime_error for an out-of-range ordinal, or -- only when @p exceedsBounds is
     *         true -- for `Clamp` overflow or a mixed non-symmetric-Mirror axis combination.
     */
    void ValidateAddressModesEXT(int addressU, int addressV, bool exceedsBounds,
                                 bool& tiled, bool& mirrored);
}
