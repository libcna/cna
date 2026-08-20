// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomTextureRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderTargetRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plans/plan_svg_dom.md design decision 6 (revised): pooled sprite elements, reused and dirty-diffed
// across frames rather than rebuilt every flush -- the same "pool cursor reset at frame start,
// reuse by index, hide the unused tail at Present()" shape HtmlDom's own DOM path uses,
// independently applied here to SVG's own primitives. Each pool slot is a wrapping <g> (carrying
// the sprite's own collapsed affine `transform="matrix(...)"`, image-rendering, mix-blend-mode and
// tint/filter/opacity -- all mode-independent) around TWO alternately shown children: a nested <svg>
// viewport (crop-only draws -- its own width/height/viewBox crop the source rectangle, an SVG
// nested <svg> clips its content to its own box by spec, so this needs no separate <clipPath>)
// containing one <image>, or a <rect> filled by a per-slot <pattern> (SVGDOM-1: Wrap/symmetric
// Mirror addressing on an out-of-bounds sourceRectangle -- see ValidateAddressModesEXT). Tint is an
// `feColorMatrix` filter, cached in <defs> and referenced by url(#id). SVGDOM-5 keeps alpha out of
// that filter and applies it as cheap element opacity; a white alpha-only fade therefore has no SVG
// filter at all. Every attribute/style write below is dirty-checked
// against the pool element's own last-applied state (`__cna`), so a steady frame (same sprite
// count, same per-sprite texture/geometry/tint/addressing) costs no DOM writes beyond the initial
// pool creation -- only `transform` typically changes frame to frame for a moving sprite.
EM_JS(void, CNA_SvgDom_FlushSpritesToSvg, (const void* cmds, int count, int stride,
                                           int clipActive, int cx, int cy, int cw, int ch), {
    if (typeof document === 'undefined') return;
    const root = Module['cnaSvgDomRoot'];
    const defs = Module['cnaSvgDomDefs'];
    const claimSlot = Module['cnaSvgDomClaimFlushSlot'];
    if (!root || !claimSlot) return;

    // SVGDOM-A/F: this flush is one whole SpriteBatch Begin/End batch, so the effective clip rect
    // (Viewport ∩ Scissor, computed once in C++ by ComputeEffectiveClipRectEXT) current RIGHT NOW
    // is the one that applies to every sprite in it -- matching real XNA/FNA semantics, where both
    // Viewport and ScissorRectangle are read once the batch's draw calls are actually issued.
    // cnaSvgDomClaimFlushSlot resolves this flush to its own document-ordered <g> slot (with its own
    // <clipPath> when the rect isn't the whole surface) so DOM order always equals actual flush
    // order -- a later batch's different clip state can never reach back and reclip, or repaint
    // under, sprites THIS batch already flushed.
    const slot = claimSlot(clipActive ? { x: cx, y: cy, w: cw, h: ch } : null);
    const container = slot.container;
    const pool = slot.pool;
    let used = slot.used;

    const svgNS = 'http://www.w3.org/2000/svg';
    const xlinkNS = 'http://www.w3.org/1999/xlink';
    // Proper (non-negative) modulo -- JS '%' keeps the dividend's sign, which a negative source
    // origin (a sourceRectangle scrolled past the left/top edge under Wrap/Mirror) would produce.
    const properMod = function (v, m) { return m > 0 ? ((v % m) + m) % m : 0; };

    const base = cmds >> 2;
    for (let i = 0; i < count; ++i) {
        const o = base + i * stride;
        const textureId = HEAP32[o + 0];
        const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][textureId];
        if (!entry) continue;
        const sx2 = HEAPF32[o + 1], sy2 = HEAPF32[o + 2], sw2 = HEAPF32[o + 3], sh2 = HEAPF32[o + 4];
        const m0 = HEAPF32[o + 5], m1 = HEAPF32[o + 6], m2 = HEAPF32[o + 7],
              m3 = HEAPF32[o + 8], m4 = HEAPF32[o + 9], m5 = HEAPF32[o + 10];
        const flags = HEAP32[o + 11];
        const variantMode = HEAP32[o + 12];
        const packedColor = HEAP32[o + 13] >>> 0;
        const tiled = (flags & 32) !== 0; // SvgFlagTiled

        const poolIndex = used++;
        let entryEl = pool[poolIndex];
        if (!entryEl) {
            const g = document.createElementNS(svgNS, 'g');
            const viewport = document.createElementNS(svgNS, 'svg');
            const image = document.createElementNS(svgNS, 'image');
            image.setAttribute('x', 0); image.setAttribute('y', 0);
            image.setAttribute('preserveAspectRatio', 'none');
            viewport.setAttribute('x', 0); viewport.setAttribute('y', 0);
            viewport.appendChild(image);
            const rect = document.createElementNS(svgNS, 'rect');
            rect.setAttribute('x', 0); rect.setAttribute('y', 0);
            rect.style.display = 'none';
            g.appendChild(viewport);
            g.appendChild(rect);
            container.appendChild(g);
            // A globally unique id, NOT poolIndex -- poolIndex is only unique WITHIN one region's
            // own pool array, and this id feeds the per-slot <pattern> id below, shared across every
            // region's slots in the same <defs>.
            const slotId = (Module['cnaSvgDomNextSlotId'] = (Module['cnaSvgDomNextSlotId'] || 0) + 1);
            entryEl = { g: g, viewport: viewport, image: image, rect: rect, slotId: slotId,
                       pattern: null, patternImage: null, cna: {} };
            pool[poolIndex] = entryEl;
        }
        const g = entryEl.g, viewport = entryEl.viewport, image = entryEl.image,
              rect = entryEl.rect, prev = entryEl.cna;
        if (prev.hidden) { g.style.removeProperty('display'); prev.hidden = false; }

        if (prev.tiled !== tiled) {
            if (tiled) {
                viewport.style.display = 'none';
                rect.style.removeProperty('display');
            } else {
                viewport.style.removeProperty('display');
                rect.style.display = 'none';
            }
            prev.tiled = tiled;
        }

        const transform = 'matrix(' + m0 + ',' + m1 + ',' + m2 + ',' + m3 + ',' + m4 + ',' + m5 + ')';
        if (prev.transform !== transform) { g.setAttribute('transform', transform); prev.transform = transform; }
        const smoothing = (flags & 4) ? 'auto' : 'pixelated'; // SvgFlagSmoothing = bit 2
        if (prev.smoothing !== smoothing) { g.style.imageRendering = smoothing; prev.smoothing = smoothing; }
        const additive = (flags & 8) !== 0; // SvgFlagAdditive
        if (prev.additive !== additive) {
            g.style.mixBlendMode = additive ? 'plus-lighter' : "";
            prev.additive = additive;
        }

        const href = entry.variants[variantMode] || String();

        if (!tiled) {
            const viewBox = sx2 + ' ' + sy2 + ' ' + sw2 + ' ' + sh2;
            if (prev.w !== sw2) { viewport.setAttribute('width', sw2); prev.w = sw2; }
            if (prev.h !== sh2) { viewport.setAttribute('height', sh2); prev.h = sh2; }
            if (prev.viewBox !== viewBox) { viewport.setAttribute('viewBox', viewBox); prev.viewBox = viewBox; }
            if (prev.href !== href) { image.setAttributeNS(xlinkNS, 'href', href); prev.href = href; }
            if (prev.texW !== entry.w) { image.setAttribute('width', entry.w); prev.texW = entry.w; }
            if (prev.texH !== entry.h) { image.setAttribute('height', entry.h); prev.texH = entry.h; }
        } else {
            if (prev.rw !== sw2) { rect.setAttribute('width', sw2); prev.rw = sw2; }
            if (prev.rh !== sh2) { rect.setAttribute('height', sh2); prev.rh = sh2; }

            if (!entryEl.pattern) {
                const pattern = document.createElementNS(svgNS, 'pattern');
                pattern.id = 'cna-svg-dom-pattern-' + entryEl.slotId;
                pattern.setAttribute('patternUnits', 'userSpaceOnUse');
                const patternImage = document.createElementNS(svgNS, 'image');
                patternImage.setAttribute('x', 0); patternImage.setAttribute('y', 0);
                patternImage.setAttribute('preserveAspectRatio', 'none');
                pattern.appendChild(patternImage);
                defs.appendChild(pattern);
                entryEl.pattern = pattern;
                entryEl.patternImage = patternImage;
                rect.setAttribute('fill', 'url(#' + pattern.id + ')');
            }
            const pattern = entryEl.pattern, patternImage = entryEl.patternImage;
            const dims = entry.variantDims && entry.variantDims[variantMode];
            const patW = dims ? dims.w : entry.w, patH = dims ? dims.h : entry.h;
            const patX = -properMod(sx2, patW), patY = -properMod(sy2, patH);
            if (prev.patW !== patW) { pattern.setAttribute('width', patW); patternImage.setAttribute('width', patW); prev.patW = patW; }
            if (prev.patH !== patH) { pattern.setAttribute('height', patH); patternImage.setAttribute('height', patH); prev.patH = patH; }
            if (prev.patX !== patX) { pattern.setAttribute('x', patX); prev.patX = patX; }
            if (prev.patY !== patY) { pattern.setAttribute('y', patY); prev.patY = patY; }
            if (prev.href !== href) { patternImage.setAttributeNS(xlinkNS, 'href', href); prev.href = href; }
        }

        const r = packedColor & 0xFF, g2 = (packedColor >> 8) & 0xFF,
              b = (packedColor >> 16) & 0xFF, a = (packedColor >> 24) & 0xFF;
        const isOpaqueOp = (flags & 16) !== 0; // SvgFlagOpaque
        // SVGDOM-5: alpha is a compositor property, not a colour-matrix concern. In particular,
        // Mobile Eggbert's rotating loading sprite varies alpha every frame; putting alpha into the
        // filter key created a new <filter>/<feColorMatrix> pair for every step and forced Firefox
        // down its expensive SVG-filter paint path. A single-child <g>'s opacity is equivalent to
        // multiplying the source alpha and stays on the cheap, bounded path.
        const opacity = isOpaqueOp ? 1 : (a / 255);
        if (prev.opacity !== opacity) {
            if (opacity === 1) g.style.removeProperty('opacity');
            else g.style.opacity = opacity;
            prev.opacity = opacity;
        }

        const needsTint = r !== 255 || g2 !== 255 || b !== 255 || isOpaqueOp;
        let filterId = "";
        if (needsTint) {
            const key = r + '_' + g2 + '_' + b + '_' + (isOpaqueOp ? 1 : 0);
            filterId = Module['cnaSvgDomFilterCache'] && Module['cnaSvgDomFilterCache'][key];
            if (!filterId) {
                if (!Module['cnaSvgDomFilterCache']) Module['cnaSvgDomFilterCache'] = {};
                filterId = 'cna-svg-dom-tint-' + key;
                const filter = document.createElementNS(svgNS, 'filter');
                filter.id = filterId;
                filter.setAttribute('color-interpolation-filters', 'sRGB');
                const cm = document.createElementNS(svgNS, 'feColorMatrix');
                cm.setAttribute('type', 'matrix');
                const rr = r / 255, gg = g2 / 255, bb = b / 255;
                const alphaRow = isOpaqueOp ? '0 0 0 0 1' : '0 0 0 1 0';
                cm.setAttribute('values',
                    rr + ' 0 0 0 0  0 ' + gg + ' 0 0 0  0 0 ' + bb + ' 0 0  ' + alphaRow);
                filter.appendChild(cm);
                defs.appendChild(filter);
                Module['cnaSvgDomFilterCache'][key] = filterId;
            }
        }
        const filterAttr = needsTint ? ('url(#' + filterId + ')') : "";
        if (prev.filter !== filterAttr) {
            if (needsTint) g.setAttribute('filter', filterAttr);
            else g.removeAttribute('filter');
            prev.filter = filterAttr;
        }
    }
    slot.used = used;
});

// Render-target-bound path: one call per sprite (simpler than the batched array the SVG path
// uses -- render targets are already the slower, rasterizing fallback path, matching HtmlDom's own
// documented "render targets fall back to Canvas2D rasterization" boundary). `pixels` is already
// blend-prepared and tinted in C++ (SvgDomTextureRenderer::PrepareSpritePixelsEXT), so this only
// needs to place it geometrically and pick the compositing operator.
EM_JS(void, CNA_SvgDom_DrawSpriteToTarget, (int targetId, const void* pixels, int sw, int sh,
                                            double m0, double m1, double m2, double m3, double m4, double m5,
                                            int additive, int opaque), {
    const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][targetId];
    if (!entry || !entry.ctx) return;
    const ctx = entry.ctx;
    const scratch = (typeof OffscreenCanvas !== 'undefined')
        ? new OffscreenCanvas(sw, sh)
        : Object.assign(document.createElement('canvas'), { width: sw, height: sh });
    const sctx = scratch.getContext('2d');
    const bytes = new Uint8ClampedArray(HEAPU8.buffer, pixels, sw * sh * 4);
    sctx.putImageData(new ImageData(new Uint8ClampedArray(bytes), sw, sh), 0, 0);

    ctx.save();
    ctx.setTransform(m0, m1, m2, m3, m4, m5);
    ctx.globalCompositeOperation = opaque ? 'copy' : (additive ? 'lighter' : 'source-over');
    ctx.drawImage(scratch, 0, 0);
    ctx.restore();
});

// SVGDOM-B/F: brackets one whole render-target-bound flush with the SAME effective clip rect
// (Viewport ∩ Scissor) the SVG backbuffer path applies via cnaSvgDomClaimFlushSlot -- previously
// this path consulted neither Viewport nor ScissorRectangle at all, so the same SpriteBatch drew
// differently depending only on whether its target was the SVG backbuffer or a RenderTarget2D.
// ctx.save() here nests around each sprite's own per-draw save()/restore() in
// CNA_SvgDom_DrawSpriteToTarget (a plain stack push), so the clip set here remains in effect for
// the whole flush until CNA_SvgDom_EndTargetScissor's own restore() -- matching the SVG path's
// batch-level (not per-sprite) clip granularity exactly. Captured ONCE per Flush() call, so a
// scissor/viewport change made AFTER a batch's End() already ran never retroactively reclips it.
EM_JS(void, CNA_SvgDom_BeginTargetScissor, (int targetId, int clipActive,
                                            int cx, int cy, int cw, int ch), {
    const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][targetId];
    if (!entry || !entry.ctx) return;
    const ctx = entry.ctx;
    ctx.save();
    if (clipActive) {
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        ctx.beginPath();
        ctx.rect(cx, cy, Math.max(0, cw), Math.max(0, ch));
        ctx.clip();
    }
});

EM_JS(void, CNA_SvgDom_EndTargetScissor, (int targetId), {
    const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][targetId];
    if (!entry || !entry.ctx) return;
    entry.ctx.restore();
});
#endif

namespace CNA::Internal::Renderers::SvgDom
{
    namespace
    {
        std::uint8_t StraightTintChannel(std::uint8_t premultiplied, std::uint8_t alpha)
        {
            // SVGDOM-5: XNA supplies a premultiplied draw colour to AlphaBlend. SVG/Canvas2D use
            // straight-alpha source pixels, with RGB tint and element/source alpha applied
            // independently, so recover the straight RGB or a fade gets darkened twice. At zero
            // alpha RGB is invisible and unknowable; canonical white avoids pointless filters.
            if (alpha == 0) return 255;
            const int straight = (static_cast<int>(premultiplied) * 255 + alpha / 2) / alpha;
            return static_cast<std::uint8_t>(std::min(255, straight));
        }

        struct Mat2x3
        {
            float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
        };

        // Composes m1 . m2 -- m2 is applied to the point FIRST, then m1 (standard 2x3 affine
        // matrix product, treating (a,b,c,d,e,f) as [[a,c,e],[b,d,f],[0,0,1]]).
        Mat2x3 Compose(const Mat2x3& m1, const Mat2x3& m2)
        {
            Mat2x3 r;
            r.a = m1.a * m2.a + m1.c * m2.b;
            r.b = m1.b * m2.a + m1.d * m2.b;
            r.c = m1.a * m2.c + m1.c * m2.d;
            r.d = m1.b * m2.c + m1.d * m2.d;
            r.e = m1.a * m2.e + m1.c * m2.f + m1.e;
            r.f = m1.b * m2.e + m1.d * m2.f + m1.f;
            return r;
        }

        Mat2x3 MatTranslate(float tx, float ty) { return {1, 0, 0, 1, tx, ty}; }
        Mat2x3 MatRotate(float rad) { const float c = std::cos(rad), s = std::sin(rad); return {c, s, -s, c, 0, 0}; }
        Mat2x3 MatScale(float sx, float sy) { return {sx, 0, 0, sy, 0, 0}; }
    }

    SvgDomDrawCommand BuildDrawCommandEXT(int textureId,
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
        bool tiled = false, mirrored = false;
        ValidateAddressModesEXT(addressU, addressV, exceedsBounds, tiled, mirrored);

        const float scaleX = sourceW != 0.0f ? static_cast<float>(dest.Width) / sourceW : 0.0f;
        const float scaleY = sourceH != 0.0f ? static_cast<float>(dest.Height) / sourceH : 0.0f;
        const float lx = -origin.X;
        const float ly = -origin.Y;
        // Flip mirrors about the sprite's OWN centre, leaving the destination footprint unchanged,
        // matching real XNA/FNA SpriteEffects semantics -- the unclamped source size defines it.
        const float flipCenterX = -origin.X + sourceW * 0.5f;
        const float flipCenterY = -origin.Y + sourceH * 0.5f;
        const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
        const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;

        Mat2x3 m = MatTranslate(static_cast<float>(dest.X), static_cast<float>(dest.Y));
        if (rotation != 0.0f) m = Compose(m, MatRotate(rotation));
        if (scaleX != 1.0f || scaleY != 1.0f) m = Compose(m, MatScale(scaleX, scaleY));
        if (flipH) m = Compose(Compose(Compose(m, MatTranslate(flipCenterX, 0)), MatScale(-1, 1)), MatTranslate(-flipCenterX, 0));
        if (flipV) m = Compose(Compose(Compose(m, MatTranslate(0, flipCenterY)), MatScale(1, -1)), MatTranslate(0, -flipCenterY));
        m = Compose(m, MatTranslate(lx, ly));

        SvgDomDrawCommand cmd{};
        cmd.textureId = textureId;
        cmd.sx = sourceX;
        cmd.sy = sourceY;
        cmd.sw = sourceW;
        cmd.sh = sourceH;
        cmd.m0 = m.a; cmd.m1 = m.b; cmd.m2 = m.c; cmd.m3 = m.d; cmd.m4 = m.e; cmd.m5 = m.f;

        int flags = 0;
        if (flipH) flags |= SvgFlagFlipHorizontally;
        if (flipV) flags |= SvgFlagFlipVertically;
        if (smoothing) flags |= SvgFlagSmoothing;
        if (op == DomCompositeOp::Additive) flags |= SvgFlagAdditive;
        if (op == DomCompositeOp::Opaque) flags |= SvgFlagOpaque;
        if (tiled) flags |= SvgFlagTiled;
        if (mirrored) flags |= SvgFlagMirrorTiled;
        cmd.flags = flags;

        // Mirror-tiled variants live at slots 2/3 (see SvgDomTextureRenderer::GetDataUriEXT); a
        // tiled-but-not-mirrored (Wrap) draw reuses the plain straight/unpremultiplied slots 0/1
        // since a plain 'repeat' tiling of the untouched texture already reproduces Wrap addressing.
        cmd.variantMode = VariantModeFor(op) + (mirrored ? 2 : 0);
        const std::uint8_t alpha = color.getAProperty();
        const std::uint8_t red = op == DomCompositeOp::AlphaBlend
            ? StraightTintChannel(color.getRProperty(), alpha) : color.getRProperty();
        const std::uint8_t green = op == DomCompositeOp::AlphaBlend
            ? StraightTintChannel(color.getGProperty(), alpha) : color.getGProperty();
        const std::uint8_t blue = op == DomCompositeOp::AlphaBlend
            ? StraightTintChannel(color.getBProperty(), alpha) : color.getBProperty();
        cmd.packedColor = static_cast<std::uint32_t>(red)
                        | (static_cast<std::uint32_t>(green) << 8)
                        | (static_cast<std::uint32_t>(blue) << 16)
                        | (static_cast<std::uint32_t>(alpha) << 24);
        return cmd;
    }

    void SvgDomSpriteBatchRenderer::Begin()
    {
        begun_ = true;
        commands_.clear();
        commandTextures_.clear();
    }

    void SvgDomSpriteBatchRenderer::End()
    {
        begun_ = false;
        Flush(commands_.data(), commandTextures_.data(), static_cast<int>(commands_.size()));
        commands_.clear();
        commandTextures_.clear();
        hasMatrix_ = false;
    }

    namespace
    {
        // Reconstructs the DomCompositeOp a command's own flags/variantMode encode -- see
        // BuildDrawCommandEXT's own encoding. Avoids carrying a redundant op field in the command.
        DomCompositeOp ReconstructOpEXT(const SvgDomDrawCommand& c)
        {
            if (c.flags & SvgFlagOpaque) return DomCompositeOp::Opaque;
            if (c.variantMode == 1) return DomCompositeOp::AlphaBlend;
            if (c.flags & SvgFlagAdditive) return DomCompositeOp::Additive;
            return DomCompositeOp::NonPremultiplied;
        }

        // SVGDOM-E: IRenderTargetRenderer and SvgDomTextureRenderer both descend from
        // ITextureRenderer along two SEPARATE branches (SvgDomRenderTargetRenderer *contains* a
        // SvgDomTextureRenderer, it does not inherit from one) -- so a source texture handed to
        // Draw() may be either a plain SvgDomTextureRenderer OR a SvgDomRenderTargetRenderer (a
        // RenderTarget2D sampled back as a texture). The previous code's
        // `static_cast<const SvgDomTextureRenderer&>(texture)` silently reinterpreted a
        // SvgDomRenderTargetRenderer's memory as a same-offset SvgDomTextureRenderer whenever that
        // happened -- undefined behaviour in every build, not only Emscripten. A contained
        // dynamic_cast against the two known concrete types is the safe equivalent, the same
        // conclusion HtmlDom's own TextureIdOf reached for the identical sibling-class shape.
        const SvgDomRenderTargetRenderer* AsRenderTargetEXT(const ITextureRenderer& texture)
        {
            return dynamic_cast<const SvgDomRenderTargetRenderer*>(&texture);
        }

        int CanvasIdOfEXT(const ITextureRenderer& texture)
        {
            if (const auto* rt = AsRenderTargetEXT(texture)) return rt->GetCanvasIdEXT();
            if (const auto* t = dynamic_cast<const SvgDomTextureRenderer*>(&texture)) return t->GetCanvasIdEXT();
            return 0;
        }

        // Refreshes-if-stale (SVGDOM-D/E) then returns the CPU-side pixel buffer to sample: a plain
        // texture's own buffer, or -- for a render target -- the buffer EnsureFreshEXT() has just
        // reconciled with its canvas, so a render target drawn into and immediately sampled (without
        // an explicit GetData call in between) never serves stale pixels.
        const std::vector<std::uint8_t>& PixelsOfEXT(const ITextureRenderer& texture)
        {
            if (const auto* rt = AsRenderTargetEXT(texture)) return rt->GetPixelsEXT();
            if (const auto* t = dynamic_cast<const SvgDomTextureRenderer*>(&texture)) return t->GetPixelsEXT();
            static const std::vector<std::uint8_t> empty;
            return empty;
        }

        // Eagerly registers (idempotent, cached) the JS-side data URI for the requested variant --
        // needed before a command referencing this texture/render-target can be flushed to the SVG
        // backbuffer path. Refreshes a render target from its canvas first (SVGDOM-D/E) so sampling
        // it as a Draw() source always reflects what was most recently rendered into it.
        const std::string& DataUriOfEXT(const ITextureRenderer& texture, int variantMode)
        {
            if (const auto* rt = AsRenderTargetEXT(texture)) return rt->GetDataUriEXT(variantMode);
            if (const auto* t = dynamic_cast<const SvgDomTextureRenderer*>(&texture)) return t->GetDataUriEXT(variantMode);
            static const std::string empty;
            return empty;
        }
    }

    void SvgDomSpriteBatchRenderer::Flush(const SvgDomDrawCommand* cmds,
                                          const ITextureRenderer* const* textures, int count)
    {
        if (count <= 0) return;
#if defined(__EMSCRIPTEN__)
        // SVGDOM-B/F: captured ONCE per flush (batch-level timing, matching real XNA/FNA -- both
        // Viewport and ScissorRectangle are read when the batch's draws are actually issued, not
        // retroactively when a LATER batch changes them), and applied identically on both the SVG
        // backbuffer path and the render-target-bound Canvas2D path below -- previously only the
        // SVG path consulted either at all.
        float cx = 0, cy = 0, cw = 0, ch = 0;
        const bool clipActive = ComputeEffectiveClipRectEXT(cx, cy, cw, ch);

        const int boundTarget = GetBoundRenderTargetIdEXT();
        if (boundTarget != 0)
        {
            CNA_SvgDom_BeginTargetScissor(boundTarget, clipActive ? 1 : 0,
                                          static_cast<int>(cx), static_cast<int>(cy),
                                          static_cast<int>(cw), static_cast<int>(ch));
            for (int i = 0; i < count; ++i)
            {
                const SvgDomDrawCommand& c = cmds[i];
                const ITextureRenderer* tex = textures[i];
                if (!tex) continue;
                const Color tint(static_cast<std::uint8_t>(c.packedColor & 0xFF),
                                 static_cast<std::uint8_t>((c.packedColor >> 8) & 0xFF),
                                 static_cast<std::uint8_t>((c.packedColor >> 16) & 0xFF),
                                 static_cast<std::uint8_t>((c.packedColor >> 24) & 0xFF));
                const DomCompositeOp op = ReconstructOpEXT(c);
                const bool tiled = (c.flags & SvgFlagTiled) != 0;
                const std::vector<std::uint8_t>& fullPixels = PixelsOfEXT(*tex);
                const std::vector<std::uint8_t> pixels = tiled
                    ? PrepareTiledSpritePixelsEXT(
                          fullPixels, tex->GetWidth(), tex->GetHeight(),
                          static_cast<int>(c.sx), static_cast<int>(c.sy),
                          static_cast<int>(c.sw), static_cast<int>(c.sh),
                          (c.flags & SvgFlagMirrorTiled) != 0, tint, op)
                    : PrepareSpritePixelsEXT(
                          fullPixels, tex->GetWidth(),
                          static_cast<int>(c.sx), static_cast<int>(c.sy),
                          static_cast<int>(c.sw), static_cast<int>(c.sh), tint, op);
                CNA_SvgDom_DrawSpriteToTarget(
                    boundTarget, pixels.data(), static_cast<int>(c.sw), static_cast<int>(c.sh),
                    c.m0, c.m1, c.m2, c.m3, c.m4, c.m5,
                    (op == DomCompositeOp::Additive) ? 1 : 0, (op == DomCompositeOp::Opaque) ? 1 : 0);
            }
            CNA_SvgDom_EndTargetScissor(boundTarget);
            return;
        }

        // Note: the sprite group itself is cleared once per FRAME (SvgDomRenderer::Clear), not
        // here -- a frame commonly contains several SpriteBatch Begin/End blocks (e.g. one per
        // layer), and clearing on every flush would wipe out sprites an earlier batch in the SAME
        // frame already flushed.
        CNA_SvgDom_FlushSpritesToSvg(cmds, count, SvgDomDrawCommandFields, clipActive ? 1 : 0,
                                     static_cast<int>(cx), static_cast<int>(cy),
                                     static_cast<int>(cw), static_cast<int>(ch));
#else
        (void)cmds; (void)textures;
#endif
    }

    void SvgDomSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        // XNA/FNA Matrix is row-major (row-vector convention: v' = v * M); matrix(a,b,c,d,e,f)
        // defines x' = a*x + c*y + e, y' = b*x + d*y + f. Matching terms gives a=M11, b=M12,
        // c=M21, d=M22, e=M41, f=M42.
        matrix_[0] = m.M11; matrix_[1] = m.M12;
        matrix_[2] = m.M21; matrix_[3] = m.M22;
        matrix_[4] = m.M41; matrix_[5] = m.M42;
        hasMatrix_ = !(m.M11 == 1.0f && m.M12 == 0.0f && m.M21 == 0.0f &&
                       m.M22 == 1.0f && m.M41 == 0.0f && m.M42 == 0.0f);
    }

    void SvgDomSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "SVG_DOM renderer: custom SpriteBatch Effects are not yet implemented. Neither SVG "
                "nor CSS compositing has a programmable shader stage for one to run in.");
    }

    void SvgDomSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        switch (textureFilter)
        {
            case 0: smoothingEnabled_ = true; break;  // Linear
            case 1: smoothingEnabled_ = false; break; // Point
            default:
                throw std::runtime_error(
                    "SVG_DOM renderer: only TextureFilter::Linear and TextureFilter::Point are "
                    "supported. Anisotropic and mip/min-mag-split filters cannot be represented by "
                    "one image-rendering toggle.");
        }
    }

    void SvgDomSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU < 0 || addressU > 2 || addressV < 0 || addressV > 2)
            throw std::runtime_error(
                "SVG_DOM renderer: TextureAddressMode must be Wrap, Clamp, or Mirror.");
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void SvgDomSpriteBatchRenderer::QueueDraw(const ITextureRenderer& texture,
                                              const Rectangle& destinationRectangle,
                                              const Rectangle& sourceRectangle,
                                              const Color& color,
                                              float rotation,
                                              const Vector2& origin,
                                              SpriteEffects effects)
    {
        if (!begun_)
            throw std::runtime_error("SvgDomSpriteBatchRenderer::Draw called before Begin().");
        const int canvasId = CanvasIdOfEXT(texture);
        if (canvasId == 0) return;
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;

        SvgDomDrawCommand cmd = BuildDrawCommandEXT(
            canvasId, texture.GetWidth(), texture.GetHeight(), destinationRectangle,
            sourceRectangle, color, rotation, origin, effects, smoothingEnabled_,
            addressU_, addressV_, GetCurrentCompositeOpEXT());

        // Compose (in order) the batch's own SetTransformMatrix and the active Viewport's (X,Y)
        // offset on top of the intrinsic per-sprite placement BuildDrawCommandEXT already produced.
        // Real XNA/FNA applies both AFTER the sprite's own placement, and the viewport offset
        // strictly after the batch transform (rasterizer stage, past the projection matrix) -- see
        // SetCurrentViewportRectEXT's own doc. Width/Height (SVGDOM-F) do not affect this per-sprite
        // translation at all -- they only ever bound the CLIP rect applied at flush time (Flush()) --
        // so only X/Y are read here, matching this project's own cross-renderer SpriteBatch contract
        // (spritebatch_custom_viewport_test.cpp Check A/C: Viewport is a translation + a hard clip,
        // never an extra coordinate rescale).
        Mat2x3 composed{cmd.m0, cmd.m1, cmd.m2, cmd.m3, cmd.m4, cmd.m5};
        if (hasMatrix_)
        {
            const Mat2x3 batch{matrix_[0], matrix_[1], matrix_[2], matrix_[3], matrix_[4], matrix_[5]};
            composed = Compose(batch, composed);
        }
        float vpX = 0, vpY = 0, vpW = 0, vpH = 0;
        GetCurrentViewportRectEXT(vpX, vpY, vpW, vpH);
        if (vpX != 0.0f || vpY != 0.0f)
            composed = Compose(MatTranslate(vpX, vpY), composed);
        cmd.m0 = composed.a; cmd.m1 = composed.b; cmd.m2 = composed.c;
        cmd.m3 = composed.d; cmd.m4 = composed.e; cmd.m5 = composed.f;

        // Ensures the texture's needed pixel variant has a JS-side data URI registered before this
        // command can be flushed to the SVG path (idempotent, cached; refreshed from the canvas
        // first when the source is a currently/recently-bound render target -- SVGDOM-D/E).
        (void)DataUriOfEXT(texture, cmd.variantMode);

        if (immediateMode_)
        {
            const ITextureRenderer* texPtr = &texture;
            Flush(&cmd, &texPtr, 1);
            return;
        }
        commands_.push_back(cmd);
        commandTextures_.push_back(&texture);
    }

    void SvgDomSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y),
                                 texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        QueueDraw(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                  SpriteEffects::None);
    }

    void SvgDomSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        QueueDraw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0),
                  SpriteEffects::None);
    }

    void SvgDomSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        (void)layerDepth;
        QueueDraw(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects);
    }
}
