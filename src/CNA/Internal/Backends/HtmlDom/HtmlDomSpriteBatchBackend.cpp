// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/HtmlDom/HtmlDomSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomTextureBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomRenderTargetBackend.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_html_dom.md design decision 5 (HTMLDOM-30/31/32): the whole batch crosses the wasm/JS
// boundary exactly once. Everything geometric has already been resolved on the C++ side
// (BuildDrawCommandEXT), so this walks a flat array of 20-field commands and does nothing but
// select each sprite's pooled element and write the CSS properties whose values actually changed.
//
// Why the diffing matters: `transform` and `opacity` are compositor-only properties. A frame that
// merely moves sprites writes those two and nothing else, so the browser neither lays out nor
// repaints -- it re-composites existing layers. Writing every property unconditionally, or
// rebuilding the elements, would give up exactly that. Nothing here ever READS layout (no
// getBoundingClientRect, no offsetWidth), so no forced synchronous reflow is possible either.
//
// Sprite n of a frame always lands on pool element n, and elements are appended to the container in
// pool order, so document order equals draw order -- painter's-algorithm ordering with no z-index
// bookkeeping at all.
//
// With a render target bound there is no DOM to write to (a <div> cannot render into an off-screen
// surface), so the very same command array is replayed into that target's Canvas2D context instead
// (design decision 10). The transform stack below is the exact CSS transform list, in the same
// order, so both paths place a sprite identically.
EM_JS(void, CNA_HtmlDom_FlushSprites, (const void* cmds, int count, int stride,
                                       double m0, double m1, double m2, double m3, double m4, double m5,
                                       int hasMatrix), {
    if (count <= 0) return;
    const base = cmds >> 2;
    const targetCtx = Module['cnaDomBoundCtx'];

    if (targetCtx) {
        for (let i = 0; i < count; ++i) {
            const o = base + i * stride;
            const variant = Module['cnaDomGetVariant'](
                HEAP32[o], HEAP32[o + 15],
                HEAPU32[o + 16] & 255, (HEAPU32[o + 16] >>> 8) & 255, (HEAPU32[o + 16] >>> 16) & 255);
            if (!variant) continue;
            const sx = HEAPF32[o + 1], sy = HEAPF32[o + 2], sw = HEAPF32[o + 3], sh = HEAPF32[o + 4];
            const flags = HEAP32[o + 14];
            targetCtx.save();
            targetCtx.setTransform(1, 0, 0, 1, 0, 0);
            if (hasMatrix) targetCtx.transform(m0, m1, m2, m3, m4, m5);
            targetCtx.imageSmoothingEnabled = (flags & 4) !== 0;
            targetCtx.globalCompositeOperation = (flags & 16) !== 0 ? 'lighter' : 'source-over';
            targetCtx.globalAlpha = ((HEAPU32[o + 16] >>> 24) & 255) / 255;
            targetCtx.translate(HEAPF32[o + 5], HEAPF32[o + 6]);
            const rot = HEAPF32[o + 9];
            if (rot !== 0) targetCtx.rotate(rot);
            targetCtx.scale(HEAPF32[o + 7], HEAPF32[o + 8]);
            if (flags & 1) {
                const cx = HEAPF32[o + 12];
                targetCtx.translate(cx, 0); targetCtx.scale(-1, 1); targetCtx.translate(-cx, 0);
            }
            if (flags & 2) {
                const cy = HEAPF32[o + 13];
                targetCtx.translate(0, cy); targetCtx.scale(1, -1); targetCtx.translate(0, -cy);
            }
            const lx = HEAPF32[o + 10], ly = HEAPF32[o + 11];
            if ((flags & 8) !== 0) {
                // Wrap: CSS background tiling has no Canvas2D equivalent other than a repeating
                // pattern, offset so the tile phase matches the requested source rectangle.
                const pattern = targetCtx.createPattern(variant.canvas, 'repeat');
                if (pattern && pattern.setTransform) {
                    const dm = new DOMMatrix();
                    dm.e = lx - sx; dm.f = ly - sy;
                    pattern.setTransform(dm);
                }
                targetCtx.fillStyle = pattern;
                targetCtx.fillRect(lx, ly, sw, sh);
            } else {
                targetCtx.drawImage(variant.canvas, sx, sy, sw, sh, lx, ly, sw, sh);
            }
            targetCtx.restore();
        }
        return;
    }

    const root = Module['cnaDomRoot'];
    if (!root) { console.error('[CNA] HTML_DOM: flush before the DOM surface existed'); return; }
    const pool = Module['cnaDomPool'];
    let used = Module['cnaDomUsed'];

    for (let i = 0; i < count; ++i) {
        const o = base + i * stride;
        const packed = HEAPU32[o + 16];
        const variant = Module['cnaDomGetVariant'](
            HEAP32[o], HEAP32[o + 15], packed & 255, (packed >>> 8) & 255, (packed >>> 16) & 255);
        if (!variant) continue;
        Module['cnaDomEnsureUrl'](variant);

        let el = pool[used];
        if (el === undefined) {
            el = document.createElement('div');
            el.className = 'cna-sprite';
            // will-change keeps each sprite on its own compositor layer, so moving it never
            // repaints. pointer-events:none keeps the sprite layer out of hit testing entirely --
            // input belongs to SDL's canvas, which is still the element receiving events.
            el.style.cssText = 'position:absolute;left:0;top:0;transform-origin:0 0;' +
                               'background-repeat:no-repeat;will-change:transform;pointer-events:none;';
            el.__cnaState = { w: -1, h: -1, url: null, bp: null, rep: null, ir: null,
                              mb: null, tf: null, op: -1, hidden: false };
            root.appendChild(el);
            pool[used] = el;
        }
        const st = el.__cnaState;
        const style = el.style;

        const sw = HEAPF32[o + 3], sh = HEAPF32[o + 4];
        if (st.w !== sw) { style.width = sw + 'px'; st.w = sw; }
        if (st.h !== sh) { style.height = sh + 'px'; st.h = sh; }
        if (st.url !== variant.url) { style.backgroundImage = 'url(' + variant.url + ')'; st.url = variant.url; }

        const bp = (-HEAPF32[o + 1]) + 'px ' + (-HEAPF32[o + 2]) + 'px';
        if (st.bp !== bp) { style.backgroundPosition = bp; st.bp = bp; }

        const flags = HEAP32[o + 14];
        const rep = (flags & 8) !== 0 ? 'repeat' : 'no-repeat';
        if (st.rep !== rep) { style.backgroundRepeat = rep; st.rep = rep; }
        const ir = (flags & 4) !== 0 ? 'auto' : 'pixelated';
        if (st.ir !== ir) { style.imageRendering = ir; st.ir = ir; }
        const mb = (flags & 16) !== 0 ? 'plus-lighter' : 'normal';
        if (st.mb !== mb) { style.mixBlendMode = mb; st.mb = mb; }

        let tf = hasMatrix
            ? 'matrix(' + m0 + ',' + m1 + ',' + m2 + ',' + m3 + ',' + m4 + ',' + m5 + ') '
            : "";
        tf += 'translate(' + HEAPF32[o + 5] + 'px,' + HEAPF32[o + 6] + 'px)';
        const rot = HEAPF32[o + 9];
        if (rot !== 0) tf += ' rotate(' + rot + 'rad)';
        const scaleX = HEAPF32[o + 7], scaleY = HEAPF32[o + 8];
        if (scaleX !== 1 || scaleY !== 1) tf += ' scale(' + scaleX + ',' + scaleY + ')';
        if (flags & 1) {
            const cx = HEAPF32[o + 12];
            tf += ' translate(' + cx + 'px,0) scale(-1,1) translate(' + (-cx) + 'px,0)';
        }
        if (flags & 2) {
            const cy = HEAPF32[o + 13];
            tf += ' translate(0,' + cy + 'px) scale(1,-1) translate(0,' + (-cy) + 'px)';
        }
        const lx = HEAPF32[o + 10], ly = HEAPF32[o + 11];
        if (lx !== 0 || ly !== 0) tf += ' translate(' + lx + 'px,' + ly + 'px)';
        if (st.tf !== tf) { style.transform = tf; st.tf = tf; }

        const opacity = ((packed >>> 24) & 255) / 255;
        if (st.op !== opacity) { style.opacity = opacity; st.op = opacity; }
        if (st.hidden) { style.display = ""; st.hidden = false; }
        ++used;
    }
    Module['cnaDomUsed'] = used;
});
#endif

namespace CNA::Internal::Backends::HtmlDom
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        // Sibling concrete classes rather than a subclass relationship (IRenderTargetBackend and
        // HtmlDomTextureBackend descend from ITextureBackend along two separate branches), and this
        // backend's "native handle" is a JS-side integer with no shared accessor on ITextureBackend
        // to expose it -- so a contained dynamic_cast against the two known concrete types is the
        // safe equivalent here, the same conclusion CANVAS and SDL_RENDERER reached.
        int TextureIdOf(const ITextureBackend& texture)
        {
            if (const auto* t = dynamic_cast<const HtmlDomTextureBackend*>(&texture)) return t->GetCanvasId();
            if (const auto* rt = dynamic_cast<const HtmlDomRenderTargetBackend*>(&texture)) return rt->GetCanvasId();
            return 0;
        }
    }

    HtmlDomDrawCommand BuildDrawCommandEXT(int textureId,
                                           int textureWidth, int textureHeight,
                                           const Rectangle& dest,
                                           const Rectangle& source,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           bool smoothing,
                                           int addressU, int addressV,
                                           DomCompositeOp op)
    {
        const float sourceX = static_cast<float>(source.X);
        const float sourceY = static_cast<float>(source.Y);
        const float sourceW = static_cast<float>(source.Width);
        const float sourceH = static_cast<float>(source.Height);

        const bool exceedsBounds = source.X < 0 || source.Y < 0 ||
                                   source.X + source.Width > textureWidth ||
                                   source.Y + source.Height > textureHeight;
        ValidateAddressModes(addressU, addressV, exceedsBounds);
        const bool wrap = exceedsBounds && addressU == 0;

        // Clamp is implemented for real, by narrowing the source rectangle into the texture and
        // shifting the sprite's local box by the same amount -- not by relying on any implicit
        // out-of-bounds behaviour of CSS background painting or drawImage. Wrap deliberately keeps
        // the full rectangle: tiling is what makes it differ from Clamp in the first place.
        float clampedX = sourceX, clampedY = sourceY, clampedW = sourceW, clampedH = sourceH;
        if (exceedsBounds && !wrap)
        {
            const float texW = static_cast<float>(textureWidth);
            const float texH = static_cast<float>(textureHeight);
            clampedX = std::clamp(sourceX, 0.0f, texW);
            clampedY = std::clamp(sourceY, 0.0f, texH);
            clampedW = std::max(0.0f, std::min(sourceX + sourceW, texW) - clampedX);
            clampedH = std::max(0.0f, std::min(sourceY + sourceH, texH) - clampedY);
        }

        HtmlDomDrawCommand cmd{};
        cmd.textureId = textureId;
        cmd.sx = clampedX;
        cmd.sy = clampedY;
        cmd.sw = clampedW;
        cmd.sh = clampedH;
        cmd.destX = static_cast<float>(dest.X);
        cmd.destY = static_cast<float>(dest.Y);
        cmd.scaleX = sourceW != 0.0f ? static_cast<float>(dest.Width) / sourceW : 0.0f;
        cmd.scaleY = sourceH != 0.0f ? static_cast<float>(dest.Height) / sourceH : 0.0f;
        cmd.rotation = rotation;
        // Everything below the scale is in source-pixel units: the pivot moves the sprite so that
        // `origin` sits at (destX,destY), and the clamp offset shifts the narrowed box back to
        // where the un-narrowed one would have put those same texels.
        cmd.localX = -origin.X + (clampedX - sourceX);
        cmd.localY = -origin.Y + (clampedY - sourceY);
        // Flip mirrors about the sprite's OWN centre -- not the pivot, and not the coordinate
        // system -- so the destination footprint is unchanged by flipping, matching real XNA/FNA
        // semantics where SpriteEffects only changes which source corner maps to which (unchanged)
        // destination corner. The unclamped source size defines that centre.
        cmd.flipCenterX = -origin.X + sourceW * 0.5f;
        cmd.flipCenterY = -origin.Y + sourceH * 0.5f;

        int flags = 0;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
            flags |= FlagFlipHorizontally;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
            flags |= FlagFlipVertically;
        if (smoothing) flags |= FlagSmoothing;
        if (wrap) flags |= FlagWrap;
        if (op == DomCompositeOp::Additive) flags |= FlagAdditive;
        cmd.flags = flags;

        cmd.variantMode = VariantModeFor(op);
        cmd.packedColor = static_cast<std::uint32_t>(color.getRProperty())
                        | (static_cast<std::uint32_t>(color.getGProperty()) << 8)
                        | (static_cast<std::uint32_t>(color.getBProperty()) << 16)
                        | (static_cast<std::uint32_t>(color.getAProperty()) << 24);
        return cmd;
    }

    void HtmlDomSpriteBatchBackend::Begin()
    {
        begun_ = true;
        commands_.clear();
    }

    void HtmlDomSpriteBatchBackend::End()
    {
        begun_ = false;
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_FlushSprites(commands_.data(), static_cast<int>(commands_.size()),
                                 HtmlDomDrawCommandFields,
                                 matrix_[0], matrix_[1], matrix_[2], matrix_[3], matrix_[4], matrix_[5],
                                 hasMatrix_ ? 1 : 0);
#endif
        commands_.clear();
        hasMatrix_ = false;
    }

    void HtmlDomSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
    {
        // XNA/FNA Matrix is row-major (row-vector convention: v' = v * M); CSS matrix(a,b,c,d,e,f)
        // defines x' = a*x + c*y + e, y' = b*x + d*y + f. Matching terms gives a=M11, b=M12, c=M21,
        // d=M22, e=M41, f=M42 -- the same mapping Canvas2D's setTransform needs.
        matrix_[0] = m.M11; matrix_[1] = m.M12;
        matrix_[2] = m.M21; matrix_[3] = m.M22;
        matrix_[4] = m.M41; matrix_[5] = m.M42;
        hasMatrix_ = !(m.M11 == 1.0f && m.M12 == 0.0f && m.M21 == 0.0f &&
                       m.M22 == 1.0f && m.M41 == 0.0f && m.M42 == 0.0f);
    }

    void HtmlDomSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "HTML_DOM backend: custom SpriteBatch Effects are not yet implemented. CSS "
                "compositing has no programmable shader stage for one to run in.");
    }

    void HtmlDomSpriteBatchBackend::SetSamplerFilter(int textureFilter)
    {
        // Magnification-dominant grouping, the same one SDL_RENDERER (Task 701) and CANVAS
        // (CANVAS-42) apply to their own single smoothing toggle: SpriteBatch draws are
        // near-universally magnifying, so the "expand" component of TextureFilter is what visibly
        // matters. Linear=0, Anisotropic=2, LinearMipPoint=3, MinPointMagLinearMipLinear=7 and
        // MinPointMagLinearMipPoint=8 all magnify linearly; everything else magnifies by point.
        switch (textureFilter)
        {
            case 0: case 2: case 3: case 7: case 8:
                smoothingEnabled_ = true;
                break;
            default:
                smoothingEnabled_ = false;
                break;
        }
    }

    void HtmlDomSpriteBatchBackend::SetSamplerAddressMode(int addressU, int addressV)
    {
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void HtmlDomSpriteBatchBackend::QueueDraw(const ITextureBackend& texture,
                                              const Rectangle& destinationRectangle,
                                              const Rectangle& sourceRectangle,
                                              const Color& color,
                                              float rotation,
                                              const Vector2& origin,
                                              SpriteEffects effects)
    {
        if (!begun_)
            throw std::runtime_error("HtmlDomSpriteBatchBackend::Draw called before Begin().");
        const int id = TextureIdOf(texture);
        if (id == 0) return;
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;

        commands_.push_back(BuildDrawCommandEXT(
            id, texture.GetWidth(), texture.GetHeight(), destinationRectangle, sourceRectangle,
            color, rotation, origin, effects, smoothingEnabled_, addressU_, addressV_,
            GetCurrentCompositeOpEXT()));
    }

    void HtmlDomSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y),
                                 texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        QueueDraw(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                  SpriteEffects::None);
    }

    void HtmlDomSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        QueueDraw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0),
                  SpriteEffects::None);
    }

    void HtmlDomSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        // layerDepth is deliberately unused: SpriteSortMode sorting happens entirely in the shared
        // SpriteBatch layer, and this backend realizes the resulting order as DOM document order.
        (void)layerDepth;
        QueueDraw(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects);
    }
}
