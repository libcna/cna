// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomState.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plans/plan_svg_dom.md design decision 5: points the draw path at a render target's own Canvas2D
// context, or back at the SVG backbuffer for id 0. While Module['cnaSvgDomBoundCtx'] is set,
// CNA_SvgDom_FlushSprites replays commands into it instead of writing SVG elements.
EM_JS(void, CNA_SvgDom_SetBoundTarget, (int id), {
    if (id === 0) { Module['cnaSvgDomBoundCtx'] = null; return; }
    const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][id];
    if (!entry) { console.error('[CNA] SVG_DOM: bind of unknown render target id', id); return; }
    Module['cnaSvgDomBoundCtx'] = entry.ctx;
});
#endif

namespace CNA::Internal::Renderers::SvgDom
{
    DomCompositeOp BlendStateToDomCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc)
    {
        // Raw Blend/BlendFunction ordinals: One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5;
        // Add=0 -- the same table IGraphicsRenderer::ApplyBlendState documents.
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
            "SVG_DOM renderer: only the 4 standard BlendState presets "
            "(Opaque/AlphaBlend/NonPremultiplied/Additive) are supported. Neither SVG nor CSS "
            "compositing expose a per-channel blend-factor/equation model, so an arbitrary custom "
            "BlendState cannot be expressed here.");
    }

    namespace
    {
        // Function-local statics, matching CNA::Internal::Renderers::HtmlDom's own shape: the
        // sprite-batch renderer needs this per draw and has no pointer back to the graphics
        // renderer that created it. Only one graphics renderer is ever live at a time.
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

        // Matches RasterizerState's own constructor default (scissorTestEnable_(false)).
        bool& CurrentScissorEnable()
        {
            static bool enabled = false;
            return enabled;
        }

        struct Rect4f { float x = 0, y = 0, w = 0, h = 0; };

        Rect4f& CurrentScissorRect()
        {
            static Rect4f rect;
            return rect;
        }

        // w/h <= 0 means "unset" -- no SetViewport call yet, matching the pre-viewport-clip default
        // of "the whole target, no clip" (SVGDOM-F).
        Rect4f& CurrentViewportRect()
        {
            static Rect4f rect;
            return rect;
        }
    }

    DomCompositeOp GetCurrentCompositeOpEXT() { return CurrentCompositeOp(); }

    void SetCurrentCompositeOpEXT(DomCompositeOp op) { CurrentCompositeOp() = op; }

    bool GetCurrentScissorEnableEXT() { return CurrentScissorEnable(); }

    void SetCurrentScissorEnableEXT(bool enabled) { CurrentScissorEnable() = enabled; }

    void SetCurrentScissorRectEXT(float x, float y, float w, float h)
    {
        CurrentScissorRect() = {x, y, w, h};
    }

    void GetCurrentScissorRectEXT(float& x, float& y, float& w, float& h)
    {
        const Rect4f& r = CurrentScissorRect();
        x = r.x; y = r.y; w = r.w; h = r.h;
    }

    void SetCurrentViewportRectEXT(float x, float y, float w, float h)
    {
        CurrentViewportRect() = {x, y, w, h};
    }

    void GetCurrentViewportRectEXT(float& x, float& y, float& w, float& h)
    {
        const Rect4f& r = CurrentViewportRect();
        x = r.x; y = r.y; w = r.w; h = r.h;
    }

    bool ComputeEffectiveClipRectEXT(float& outX, float& outY, float& outW, float& outH)
    {
        float vx = 0, vy = 0, vw = 0, vh = 0;
        GetCurrentViewportRectEXT(vx, vy, vw, vh);
        const bool hasViewport = vw > 0.0f && vh > 0.0f;

        const bool scissorOn = GetCurrentScissorEnableEXT();
        float sx = 0, sy = 0, sw = 0, sh = 0;
        if (scissorOn) GetCurrentScissorRectEXT(sx, sy, sw, sh);

        if (!hasViewport && !scissorOn) return false;

        constexpr float kInf = std::numeric_limits<float>::max();
        float x1 = hasViewport ? vx : -kInf;
        float y1 = hasViewport ? vy : -kInf;
        float x2 = hasViewport ? vx + vw : kInf;
        float y2 = hasViewport ? vy + vh : kInf;
        if (scissorOn)
        {
            x1 = std::max(x1, sx);
            y1 = std::max(y1, sy);
            x2 = std::min(x2, sx + sw);
            y2 = std::min(y2, sy + sh);
        }

        outX = x1;
        outY = y1;
        outW = std::max(0.0f, x2 - x1);
        outH = std::max(0.0f, y2 - y1);
        return true;
    }

    int GetBoundRenderTargetIdEXT() { return BoundRenderTargetId(); }

    void SetBoundRenderTargetIdEXT(int canvasId)
    {
        BoundRenderTargetId() = canvasId;
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_SetBoundTarget(canvasId);
#endif
    }

    int AllocateTextureIdEXT()
    {
        static int next = 1;
        return next++;
    }

    void ValidateAddressModesEXT(int addressU, int addressV, bool exceedsBounds,
                                 bool& tiled, bool& mirrored)
    {
        tiled = false;
        mirrored = false;
        if (addressU < 0 || addressU > 2 || addressV < 0 || addressV > 2)
            throw std::runtime_error(
                "SVG_DOM renderer: TextureAddressMode must be Wrap, Clamp, or Mirror.");
        if (!exceedsBounds) return;

        // Clamp=1. Overflow on a Clamp axis would need a per-draw edge-extended texture variant
        // (plans/plan_svg_dom.md SVGDOM-2, not yet implemented) -- a real, narrower boundary than
        // HtmlDom's own kMaxClampPadding-bounded padded-variant cache.
        if (addressU == 1 || addressV == 1)
            throw std::runtime_error(
                "SVG_DOM renderer: TextureAddressMode::Clamp with an out-of-bounds sourceRectangle "
                "is not yet implemented -- use the same Wrap or Mirror mode on both axes, or keep "
                "sourceRectangle within the texture.");

        if (addressU != addressV)
            throw std::runtime_error(
                "SVG_DOM renderer: mixed TextureAddressMode axes for an out-of-bounds "
                "sourceRectangle are not yet implemented (addressU=" + std::to_string(addressU) +
                ", addressV=" + std::to_string(addressV) + ") -- use the same Wrap or Mirror mode "
                "on both axes.");

        // addressU == addressV here, and both are 0 (Wrap) or 2 (Mirror) -- Clamp was rejected above.
        tiled = true;
        mirrored = (addressU == 2);
    }
}
