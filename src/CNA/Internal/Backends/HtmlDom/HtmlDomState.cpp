// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/HtmlDom/HtmlDomState.hpp"

#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_html_dom.md HTMLDOM-50/51/52: points the draw path at a render target's own Canvas2D
// context, or back at the DOM backbuffer for id 0. While Module['cnaDomBoundCtx'] is set,
// CNA_HtmlDom_FlushSprites replays commands into it instead of writing DOM elements, and Clear
// targets it too.
//
// Binding also drops that target's cached variant URLs: a render target's pixels change through
// draws made while it is bound, which never pass through UpdatePixels(). Without this, sampling the
// target as a texture afterwards would keep showing whatever it held before the binding.
// Conservative but correct -- every bind is treated as "this target's content may be about to
// change", even when the caller only reads it back.
//
// This lives here, behind SetBoundRenderTargetIdEXT, rather than in either backend that needs it:
// both the graphics backend (SetRenderTarget2D) and the render target itself (Bind/Unbind) switch
// the binding, and an EM_JS function cannot be called from a translation unit other than the one
// that defines it -- the import attribute belongs to the definition.
EM_JS(void, CNA_HtmlDom_SetBoundTarget, (int id), {
    if (id === 0) { Module['cnaDomBoundCtx'] = null; return; }
    const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
    if (!entry) { console.error('[CNA] HTML_DOM: bind of unknown render target id', id); return; }
    Module['cnaDomBoundCtx'] = entry.ctx;
    entry.variants = {};
});
#endif

namespace CNA::Internal::Backends::HtmlDom
{
    DomCompositeOp BlendStateToDomCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc)
    {
        // Raw Blend/BlendFunction ordinals, the same table SdlGraphicsBackend::ToSdlBlendFactor and
        // CanvasGraphicsBackend::BlendStateToCompositeOp read: One=0, Zero=1, SourceAlpha=4,
        // InverseSourceAlpha=5; Add=0.
        const bool isAdd = colorBlendFunc == 0 && alphaBlendFunc == 0;
        const bool symmetric = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend;

        if (isAdd && symmetric && colorSrcBlend == 0 && colorDstBlend == 1)
            return DomCompositeOp::Opaque;
        if (isAdd && symmetric && colorSrcBlend == 0 && colorDstBlend == 5)
            return DomCompositeOp::AlphaBlend;
        if (isAdd && symmetric && colorSrcBlend == 4 && colorDstBlend == 5)
            return DomCompositeOp::NonPremultiplied;
        if (isAdd && symmetric && colorSrcBlend == 4 && colorDstBlend == 0)
            return DomCompositeOp::Additive;

        throw std::runtime_error(
            "HTML_DOM backend: only the 4 standard BlendState presets "
            "(Opaque/AlphaBlend/NonPremultiplied/Additive) are supported. CSS compositing exposes "
            "one blend operator and no blend-factor or blend-equation model, so an arbitrary custom "
            "BlendState cannot be expressed here.");
    }

    namespace
    {
        // Function-local statics rather than members of HtmlDomGraphicsBackend: the sprite-batch
        // backend needs both values per draw and has no pointer back to the graphics backend that
        // created it (ISpriteBatchBackend deliberately carries no such link on any backend). Only
        // one graphics backend is ever live at a time, so process-wide state is exact here, and
        // keeping it in plain C++ leaves every decision made from it testable outside a browser.
        DomCompositeOp& CurrentCompositeOp()
        {
            static DomCompositeOp op = DomCompositeOp::NonPremultiplied;
            return op;
        }

        int& BoundRenderTargetId()
        {
            static int id = 0;
            return id;
        }
    }

    DomCompositeOp GetCurrentCompositeOpEXT() { return CurrentCompositeOp(); }

    void SetCurrentCompositeOpEXT(DomCompositeOp op) { CurrentCompositeOp() = op; }

    int GetBoundRenderTargetIdEXT() { return BoundRenderTargetId(); }

    void SetBoundRenderTargetIdEXT(int canvasId)
    {
        BoundRenderTargetId() = canvasId;
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_SetBoundTarget(canvasId);
#endif
    }

    void ValidateAddressModes(int addressU, int addressV, bool exceedsBounds)
    {
        // Clamp is exact (the source rect is clamped into the texture before it ever reaches CSS),
        // and an in-bounds source rect makes every mode indistinguishable, so neither case can be
        // wrong regardless of what was requested.
        if (!exceedsBounds || (addressU == 1 && addressV == 1)) return;

        // HTMLDOM-97: Wrap (per axis, independently) and symmetric Mirror (same mode both axes) are
        // both supported now -- see this function's own header doc for how. The one combination
        // that remains genuinely unsupported is Mirror mixed with a DIFFERENT mode on the other
        // axis: a per-axis-mirrored tile image is real extra complexity for a combination no
        // built-in SamplerState preset can even produce.
        if (addressU != addressV && (addressU == 2 || addressV == 2))
            throw std::runtime_error(
                "HTML_DOM backend: TextureAddressMode::Mirror combined with a DIFFERENT mode on the "
                "other axis, with an out-of-bounds sourceRectangle, is not yet implemented "
                "(addressU=" + std::to_string(addressU) + ", addressV=" + std::to_string(addressV) +
                "). Use the same mode on both axes, or Clamp/Wrap for the non-Mirror axis.");
    }
}
