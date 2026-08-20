// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

namespace CNA::Internal::Renderers::HtmlDom
{
    /**
     * @brief How a draw's source pixels must be prepared, and how the result composites.
     *
     * plans/plan_html_dom.md design decisions 7 and 8. CSS compositing offers exactly one blend operator
     * this renderer needs (`plus-lighter`) and no blend-factor model at all, so three of the four
     * standard XNA `BlendState` presets are reproduced by preparing the SOURCE PIXELS instead of
     * the compositing step:
     *
     * - `Opaque` (`srcBlend=One`, `dstBlend=Zero` for BOTH colour and alpha -- confirmed via this
     *   project's own `BlendState.cpp`) replaces the destination pixel with the source pixel
     *   exactly, alpha included; it does NOT force the result to alpha=255. HTMLDOM-100: the two
     *   draw paths reproduce this differently. The bound-render-target path has a real Canvas2D
     *   context and uses Porter-Duff `'copy'` on the texture's STRAIGHT (as-uploaded) pixels,
     *   clipped to exactly the sprite's own footprint first (`'copy'` is evaluated over the WHOLE
     *   compositing area, not just the drawn shape -- the same pitfall plans/plan_canvas.md CANVAS-44
     *   found and fixed with an identical clip). The DOM `<div>` backbuffer path has no such
     *   operator -- CSS cannot make one stacked, blended element erase whatever is beneath another
     *   while still showing its own partial alpha through -- so it falls back to an alpha-stripped
     *   copy of the texture instead: an accepted, deliberate deviation for that path only, not a
     *   bug, since full opacity is the closest a `<div>` can get to "ignore the destination".
     * - `AlphaBlend` (`srcBlend=One`) assumes source colour that is ALREADY premultiplied by its
     *   own alpha. CSS composites straight alpha and premultiplies internally, so genuinely
     *   premultiplied data would be multiplied by its alpha a second time; the sprite is therefore
     *   drawn from an un-premultiplied copy.
     * - `NonPremultiplied` (`srcBlend=SourceAlpha`) already matches what CSS assumes natively, so
     *   its pixels are used exactly as uploaded.
     * - `Additive` needs no pixel preparation, only `mix-blend-mode: plus-lighter`.
     *
     * plans/plan_html_dom.md HTMLDOM-105: the colour-channel equivalences above are exact and pixel-
     * verified (HTMLDOM-84/85/86/87), but re-deriving each preset's RAW factor-based RGBA equation
     * (both colour AND alpha channels, independently of what any browser actually does) found a
     * real, architectural ALPHA-channel gap for two presets specifically -- confirmed via a headless-
     * Chromium probe, not assumed:
     *
     * - `AlphaBlend` (`alphaSrcBlend=One, alphaDstBlend=InverseSourceAlpha`) has NO gap: its alpha
     *   equation, `result_a = src_a + dst_a*(1-src_a)`, is exactly the CSS/Canvas2D compositing
     *   spec's own Porter-Duff "over" alpha formula. `source-over` reproduces it exactly for any
     *   combination of source/destination alpha, not just opaque ones.
     * - `NonPremultiplied` and `Additive` both set `alphaSrcBlend=SourceAlpha` -- i.e. the ALPHA
     *   channel's own blend factor is `src_a` itself, so their real per-factor alpha equations are
     *   `result_a = src_a*src_a + dst_a*(1-src_a)` (NonPremultiplied) and
     *   `result_a = src_a*src_a + dst_a` (Additive, clamped to 1) -- src_a is literally SQUARED.
     *   Neither `source-over` (Porter-Duff "over": `src_a + dst_a*(1-src_a)`, no squaring) nor
     *   `lighter`/`plus-lighter` (Porter-Duff "plus": `min(1, src_a+dst_a)`, also no squaring)
     *   reproduces that squared term -- CSS's compositing model has no per-channel-independent
     *   blend-factor customization to express it with, and implementing it exactly would mean
     *   per-pixel JS blending on every draw instead of native browser compositing, defeating this
     *   renderer's entire "zero cost when nothing changes, GPU-composited otherwise" performance
     *   premise (design decisions, above) for a channel that only diverges when the SOURCE itself is
     *   translucent (src_a=255 makes src_a*src_a==src_a, so the two formulas coincide exactly for
     *   every opaque-source draw -- confirmed by hand and why HTMLDOM-87's own opaque-only Additive
     *   test could never have caught this). **Accepted, documented architectural limitation**: the
     *   ALPHA channel of a `NonPremultiplied`/`Additive` result with a translucent source will not
     *   exactly match real XNA/D3D hardware blending; the COLOUR channels always will. Verified with
     *   real translucent-source-and-destination raw RGBA readback (not just colour), confirming this
     *   renderer's own actual (CSS-native, non-squared) result -- see
     *   `htmldom_pixel_verification_test.cpp`'s HTMLDOM-105 checks.
     */
    enum class DomCompositeOp
    {
        /**
         * @brief BlendState.Opaque -- DOM path: alpha-stripped source pixels, normal compositing
         * (accepted deviation, see the class doc). Canvas2D render-target path: straight source
         * pixels with `'copy'` compositing, which preserves the real XNA alpha-included-replace
         * semantics exactly (HTMLDOM-100).
         */
        Opaque = 0,
        /** @brief BlendState.NonPremultiplied -- source pixels as uploaded, normal compositing. */
        NonPremultiplied = 1,
        /** @brief BlendState.AlphaBlend -- un-premultiplied source pixels, normal compositing. */
        AlphaBlend = 2,
        /** @brief BlendState.Additive -- source pixels as uploaded, `mix-blend-mode: plus-lighter`. */
        Additive = 3
    };

    /**
     * @brief Which cached variant of a texture's pixels a DomCompositeOp draws from.
     *
     * The JS-side variant cache is keyed on this plus the tint colour (plans/plan_html_dom.md
     * HTMLDOM-23), so `Additive` and `NonPremultiplied` deliberately share variant 0 rather than
     * generating two identical copies of the same pixels.
     *
     * HTMLDOM-100: mode 2 (alpha-stripped) is fetched verbatim by the DOM `<div>` backbuffer path,
     * but the Canvas2D render-target path treats mode 2 as a request for the STRAIGHT (mode 0)
     * pixels composited with `'copy'` instead -- see CNA_HtmlDom_FlushSprites.
     *
     * @param op The composite operation in effect for the draw.
     * @return 0 for straight (as-uploaded) pixels, 1 for un-premultiplied, 2 for alpha-stripped.
     */
    [[nodiscard]] constexpr int VariantModeFor(DomCompositeOp op)
    {
        switch (op)
        {
            case DomCompositeOp::AlphaBlend: return 1;
            case DomCompositeOp::Opaque:     return 2;
            default:                         return 0;
        }
    }

    /**
     * @brief Maps raw XNA BlendState factor/function ordinals to a DomCompositeOp.
     *
     * Pure function -- contains no `EM_JS`/DOM access at all, so plans/plan_html_dom.md HTMLDOM-70's
     * GTest coverage can exercise it directly without a browser. The ordinals are the same ones
     * `IGraphicsRenderer::ApplyBlendState` documents (Blend: One=0, Zero=1, SourceAlpha=4,
     * InverseSourceAlpha=5; BlendFunction: Add=0).
     *
     * @param colorSrcBlend  Raw Blend ordinal for the colour source factor.
     * @param alphaSrcBlend  Raw Blend ordinal for the alpha source factor.
     * @param colorDstBlend  Raw Blend ordinal for the colour destination factor.
     * @param alphaDstBlend  Raw Blend ordinal for the alpha destination factor.
     * @param colorBlendFunc Raw BlendFunction ordinal for the colour channels.
     * @param alphaBlendFunc Raw BlendFunction ordinal for the alpha channel.
     * @return The DomCompositeOp reproducing that BlendState.
     * @throws std::runtime_error if the combination is not one of the four standard presets --
     *         CSS compositing has no generic blend-factor/equation model to approximate it with.
     */
    [[nodiscard]] DomCompositeOp BlendStateToDomCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                                            int colorDstBlend, int alphaDstBlend,
                                                            int colorBlendFunc, int alphaBlendFunc);

    /**
     * @brief CNAEXT. Returns the composite operation the most recent ApplyBlendState selected.
     *
     * Shared between HtmlDomRenderer (which sets it) and HtmlDomSpriteBatchRenderer (which
     * encodes it into every draw command). Kept as plain C++ state rather than a JS-side flag so
     * both the state itself and every decision made from it stay unit-testable outside a browser.
     *
     * @return The active composite operation; DomCompositeOp::NonPremultiplied before any
     *         ApplyBlendState call, matching CSS's own native straight-alpha compositing.
     */
    [[nodiscard]] DomCompositeOp GetCurrentCompositeOpEXT();

    /**
     * @brief CNAEXT. Records the composite operation subsequent draws must use.
     *
     * @param op The composite operation selected by ApplyBlendState.
     */
    void SetCurrentCompositeOpEXT(DomCompositeOp op);

    /**
     * @brief CNAEXT. Returns whether the most recent ApplyRasterizerState enabled scissor testing.
     *
     * plans/plan_html_dom.md HTMLDOM-102: RasterizerState.ScissorTestEnable was previously never read by
     * this renderer at all -- SetScissorRect's clip-path/Canvas2D clip applied unconditionally,
     * matching the native 2D renderer's omission but not real XNA fidelity (that renderer never
     * overrides ApplyRasterizerState either, so it has no enable bit to read in the first place;
     * HTML_DOM now does). Kept as plain C++ state -- the same shape GetCurrentCompositeOpEXT/
     * SetCurrentCompositeOpEXT already use for BlendState -- so it stays unit-testable outside a
     * browser and is captured at the same per-batch granularity the scissor rect itself uses.
     *
     * @return true if scissor testing is currently enabled; false before any ApplyRasterizerState
     *         call, matching RasterizerState's own constructor default.
     */
    [[nodiscard]] bool GetCurrentScissorEnableEXT();

    /**
     * @brief CNAEXT. Records whether subsequent draws should honour the scissor rect.
     *
     * @param enabled The scissor-test-enable bit selected by ApplyRasterizerState.
     */
    void SetCurrentScissorEnableEXT(bool enabled);

    /**
     * @brief CNAEXT. Returns the JS-side canvas id of the currently bound render target.
     *
     * plans/plan_html_dom.md design decision 10: while a render target is bound, draws cannot go to the
     * DOM (a `<div>` cannot render into an off-screen surface) and route through the target's own
     * Canvas2D context instead. The sprite-batch renderer reads this to pick its path.
     *
     * @return The bound target's canvas id, or 0 when the DOM backbuffer is the active target.
     */
    [[nodiscard]] int GetBoundRenderTargetIdEXT();

    /**
     * @brief CNAEXT. Records which render target is bound, or 0 for the DOM backbuffer.
     *
     * @param canvasId The target's JS-side canvas id, or 0 to restore the DOM backbuffer.
     */
    void SetBoundRenderTargetIdEXT(int canvasId);

    /**
     * @brief One sprite in a batch, encoded for a single wasm→JS handoff.
     *
     * plans/plan_html_dom.md design decision 5: `Draw()` appends one of these per sprite and `End()`
     * hands the whole array to JS in ONE `EM_JS` call, so a 2000-sprite frame crosses the wasm/JS
     * boundary once instead of 2000 times. The layout is deliberately 20 four-byte fields so JS can
     * walk it with plain `HEAP32`/`HEAPF32`/`HEAPU32` index arithmetic and no per-field offset
     * table.
     *
     * All geometry is resolved on the C++ side into a form both draw paths apply identically:
     * `translate(destX,destY) rotate(rotation) scale(scaleX,scaleY) [flip] translate(localX,localY)`,
     * with everything after the scale expressed in source-pixel units. Resolving it here rather
     * than in JS keeps source-rectangle clamping, pivot placement and flip mirroring unit-testable
     * without a browser, and leaves the JS side doing nothing but writing style values.
     */
    struct HtmlDomDrawCommand
    {
        /** @brief Source texture's JS-side canvas id (`Module['cnaDomTextures']`). */
        std::int32_t textureId;
        /** @brief Clamped source rectangle left edge, in texture pixels. */
        float sx;
        /** @brief Clamped source rectangle top edge, in texture pixels. */
        float sy;
        /** @brief Clamped source rectangle width, in texture pixels. */
        float sw;
        /** @brief Clamped source rectangle height, in texture pixels. */
        float sh;
        /** @brief Destination position X, in logical game pixels: where `origin` lands. */
        float destX;
        /** @brief Destination position Y, in logical game pixels: where `origin` lands. */
        float destY;
        /** @brief Horizontal scale, destination width over unclamped source width. */
        float scaleX;
        /** @brief Vertical scale, destination height over unclamped source height. */
        float scaleY;
        /** @brief Clockwise rotation about the pivot, in radians. */
        float rotation;
        /** @brief Final local translation X, in source-pixel units: pivot offset plus clamp offset. */
        float localX;
        /** @brief Final local translation Y, in source-pixel units. */
        float localY;
        /** @brief Horizontal mirror axis, in source-pixel local space (the sprite's own centre). */
        float flipCenterX;
        /** @brief Vertical mirror axis, in source-pixel local space. */
        float flipCenterY;
        /** @brief Bit flags: see FlagFlipHorizontally and friends. */
        std::int32_t flags;
        /** @brief Cached-variant selector; see VariantModeFor(). */
        std::int32_t variantMode;
        /** @brief Tint colour packed as R | G<<8 | B<<16 | A<<24. */
        std::uint32_t packedColor;
        /** @brief Reserved; always 0, keeps the stride at exactly 80 bytes. */
        std::int32_t reserved0;
        /** @brief Reserved; always 0. */
        std::int32_t reserved1;
        /** @brief Reserved; always 0. */
        std::int32_t reserved2;
    };

    static_assert(sizeof(HtmlDomDrawCommand) == 80,
                  "HtmlDomDrawCommand must stay 20 four-byte fields -- the JS flush walks it with "
                  "fixed HEAP32/HEAPF32 index arithmetic.");

    /** @brief Number of four-byte fields in one HtmlDomDrawCommand; the JS flush's stride. */
    inline constexpr int HtmlDomDrawCommandFields = 20;

    /** @brief HtmlDomDrawCommand::flags bit: mirror the sprite horizontally about its own centre. */
    inline constexpr std::int32_t FlagFlipHorizontally = 1 << 0;
    /** @brief HtmlDomDrawCommand::flags bit: mirror the sprite vertically about its own centre. */
    inline constexpr std::int32_t FlagFlipVertically = 1 << 1;
    /** @brief HtmlDomDrawCommand::flags bit: smooth (linear) magnification rather than nearest. */
    inline constexpr std::int32_t FlagSmoothing = 1 << 2;
    /** @brief HtmlDomDrawCommand::flags bit: tile the U axis (TextureAddressMode Wrap or Mirror). */
    inline constexpr std::int32_t FlagWrap = 1 << 3;
    /** @brief HtmlDomDrawCommand::flags bit: composite additively (`mix-blend-mode: plus-lighter`). */
    inline constexpr std::int32_t FlagAdditive = 1 << 4;
    /**
     * @brief HtmlDomDrawCommand::flags bit: tile the V axis (TextureAddressMode Wrap or Mirror).
     *
     * Independent of FlagWrap (the U axis) so a mixed-axis draw (e.g. U=Wrap, V=Clamp) can tile one
     * axis and not the other -- CSS `background-repeat`/`CanvasPattern` repetition both natively
     * accept independent per-axis values (HTMLDOM-97).
     */
    inline constexpr std::int32_t FlagWrapV = 1 << 5;
    /**
     * @brief HtmlDomDrawCommand::flags bit: tile from the pre-tiled MIRRORED variant, not the plain
     * texture (TextureAddressMode::Mirror, symmetric on both axes -- HTMLDOM-97).
     *
     * Only ever set together with both FlagWrap and FlagWrapV: ValidateAddressModes only allows
     * Mirror through when it is the SAME mode on both axes.
     */
    inline constexpr std::int32_t FlagMirror = 1 << 6;

    /**
     * @brief Rejects the one TextureAddressMode combination this renderer still cannot reproduce.
     *
     * plans/plan_html_dom.md HTMLDOM-45/HTMLDOM-97. Only relevant when the requested source rectangle
     * actually leaves the texture's own bounds -- the single case in which Wrap/Mirror can ever
     * differ visibly from Clamp. `Wrap` maps to CSS background tiling; symmetric `Mirror` (the same
     * mode on both axes -- `SamplerState` has no built-in Mirror preset at all, so a game always
     * constructs a custom one, and using the same mode on both axes is simply the natural way to do
     * that) maps to a cached, pre-tiled-and-mirrored texture variant tiled the same way; mixed non-`Mirror` per-axis
     * modes (e.g. U=Wrap, V=Clamp) map to independent per-axis `background-repeat`/`CanvasPattern`
     * repetition values. The one combination genuinely unsupported is `Mirror` paired with a
     * DIFFERENT mode on the other axis: that would need a per-axis-mirrored tile image (composing a
     * pre-tiled-mirror source with independent per-axis repetition), real extra complexity for a
     * combination no built-in `SamplerState` preset can even produce. Pure function, no DOM access
     * -- directly unit-testable.
     *
     * @param addressU       Raw TextureAddressMode ordinal for U (0=Wrap, 1=Clamp, 2=Mirror).
     * @param addressV       Raw TextureAddressMode ordinal for V.
     * @param exceedsBounds  Whether the source rectangle leaves the texture's own bounds.
     * @throws std::runtime_error when @p addressU != @p addressV and either is Mirror, while
     *         @p exceedsBounds.
     */
    void ValidateAddressModes(int addressU, int addressV, bool exceedsBounds);
}
