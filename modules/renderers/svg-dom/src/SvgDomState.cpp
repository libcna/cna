// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomState.hpp"

#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_svg_dom.md design decision 5: points the draw path at a render target's own Canvas2D
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

        struct Offset2f { float x = 0, y = 0; };

        Offset2f& CurrentViewportOffset()
        {
            static Offset2f offset;
            return offset;
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

    void SetCurrentViewportOffsetEXT(float x, float y) { CurrentViewportOffset() = {x, y}; }

    void GetCurrentViewportOffsetEXT(float& x, float& y)
    {
        const Offset2f& o = CurrentViewportOffset();
        x = o.x; y = o.y;
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

    void ValidateSourceRectangleEXT(bool exceedsBounds)
    {
        if (exceedsBounds)
            throw std::runtime_error(
                "SVG_DOM renderer: sourceRectangle exceeds the texture's own bounds. "
                "TextureAddressMode Wrap/Mirror/edge-extended Clamp tiling is not yet implemented "
                "(plan_svg_dom.md SVGDOM-1) -- keep sourceRectangle within the texture.");
    }
}
