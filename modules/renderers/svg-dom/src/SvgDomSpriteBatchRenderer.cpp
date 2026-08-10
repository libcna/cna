// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomTextureRenderer.hpp"

#include <cmath>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_svg_dom.md design decision 6 (revised): pooled sprite elements, reused and dirty-diffed
// across frames rather than rebuilt every flush -- the same "pool cursor reset at frame start,
// reuse by index, hide the unused tail at Present()" shape HtmlDom's own DOM path uses,
// independently applied here to SVG's own primitives. Each sprite is a pooled nested <svg>
// viewport (its own width/height/viewBox crop the source rectangle -- an SVG nested <svg> clips
// its content to its own box by spec, so this needs no separate <clipPath>) containing one pooled
// <image>, with the sprite's own collapsed affine `transform="matrix(...)"` placing it. Tint is an
// `feColorMatrix` filter, cached in <defs> and referenced by url(#id) -- omitted entirely for an
// opaque-alpha white tint with no Opaque-alpha-forcing, the overwhelmingly common case, so most
// sprites carry no filter attribute at all. Every attribute/style write below is dirty-checked
// against the pool element's own last-applied state (`__cna`), so a steady frame (same sprite
// count, same per-sprite texture/geometry/tint) costs no DOM writes beyond the initial pool
// creation -- only `transform` typically changes frame to frame for a moving sprite.
EM_JS(void, CNA_SvgDom_FlushSpritesToSvg, (const void* cmds, int count, int stride,
                                           int scissorEnabled, int sx, int sy, int sw, int sh), {
    if (typeof document === 'undefined') return;
    const root = Module['cnaSvgDomRoot'];
    const defs = Module['cnaSvgDomDefs'];
    const group = Module['cnaSvgDomSpriteGroup'];
    if (!root || !group) return;

    // One clip-path shared by the whole frame's sprite group (V1: a single global scissor region,
    // not HtmlDom's own per-batch-isolated regions -- see docs/svg-dom-renderer.md).
    if (scissorEnabled) {
        let clipRect = Module['cnaSvgDomClipRect'];
        if (!clipRect) {
            const clipPath = document.createElementNS('http://www.w3.org/2000/svg', 'clipPath');
            clipPath.id = 'cna-svg-dom-scissor';
            clipRect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
            clipPath.appendChild(clipRect);
            defs.appendChild(clipPath);
            Module['cnaSvgDomClipRect'] = clipRect;
            group.setAttribute('clip-path', 'url(#cna-svg-dom-scissor)');
        }
        clipRect.setAttribute('x', sx); clipRect.setAttribute('y', sy);
        clipRect.setAttribute('width', Math.max(0, sw)); clipRect.setAttribute('height', Math.max(0, sh));
        group.setAttribute('clip-path', 'url(#cna-svg-dom-scissor)');
    } else {
        group.removeAttribute('clip-path');
    }

    if (!Module['cnaSvgDomSpritePool']) Module['cnaSvgDomSpritePool'] = [];
    if (Module['cnaSvgDomSpriteUsed'] === undefined) Module['cnaSvgDomSpriteUsed'] = 0;
    const pool = Module['cnaSvgDomSpritePool'];
    const svgNS = 'http://www.w3.org/2000/svg';
    const xlinkNS = 'http://www.w3.org/1999/xlink';

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

        const poolIndex = Module['cnaSvgDomSpriteUsed']++;
        let entryEl = pool[poolIndex];
        if (!entryEl) {
            const viewport = document.createElementNS(svgNS, 'svg');
            const image = document.createElementNS(svgNS, 'image');
            image.setAttribute('x', 0); image.setAttribute('y', 0);
            image.setAttribute('preserveAspectRatio', 'none');
            viewport.setAttribute('x', 0); viewport.setAttribute('y', 0);
            viewport.appendChild(image);
            group.appendChild(viewport);
            entryEl = { viewport: viewport, image: image, cna: {} };
            pool[poolIndex] = entryEl;
        }
        const viewport = entryEl.viewport, image = entryEl.image, prev = entryEl.cna;
        if (prev.hidden) { viewport.style.display = ''; prev.hidden = false; }

        const viewBox = sx2 + ' ' + sy2 + ' ' + sw2 + ' ' + sh2;
        if (prev.w !== sw2) { viewport.setAttribute('width', sw2); prev.w = sw2; }
        if (prev.h !== sh2) { viewport.setAttribute('height', sh2); prev.h = sh2; }
        if (prev.viewBox !== viewBox) { viewport.setAttribute('viewBox', viewBox); prev.viewBox = viewBox; }
        const transform = 'matrix(' + m0 + ',' + m1 + ',' + m2 + ',' + m3 + ',' + m4 + ',' + m5 + ')';
        if (prev.transform !== transform) { viewport.setAttribute('transform', transform); prev.transform = transform; }
        const smoothing = (flags & 4) ? 'auto' : 'pixelated'; // FlagSmoothing = bit 2
        if (prev.smoothing !== smoothing) { viewport.style.imageRendering = smoothing; prev.smoothing = smoothing; }
        const additive = (flags & 8) !== 0; // SvgFlagAdditive
        if (prev.additive !== additive) {
            viewport.style.mixBlendMode = additive ? 'plus-lighter' : "";
            prev.additive = additive;
        }

        const href = entry.variants[variantMode] || '';
        if (prev.href !== href) { image.setAttributeNS(xlinkNS, 'href', href); prev.href = href; }
        if (prev.texW !== entry.w) { image.setAttribute('width', entry.w); prev.texW = entry.w; }
        if (prev.texH !== entry.h) { image.setAttribute('height', entry.h); prev.texH = entry.h; }

        const r = packedColor & 0xFF, g = (packedColor >> 8) & 0xFF,
              b = (packedColor >> 16) & 0xFF, a = (packedColor >> 24) & 0xFF;
        const isOpaqueOp = (flags & 16) !== 0; // SvgFlagOpaque
        const needsTint = r !== 255 || g !== 255 || b !== 255 || a !== 255 || isOpaqueOp;
        let filterId = "";
        if (needsTint) {
            const key = r + '_' + g + '_' + b + '_' + a + '_' + (isOpaqueOp ? 1 : 0);
            filterId = Module['cnaSvgDomFilterCache'] && Module['cnaSvgDomFilterCache'][key];
            if (!filterId) {
                if (!Module['cnaSvgDomFilterCache']) Module['cnaSvgDomFilterCache'] = {};
                filterId = 'cna-svg-dom-tint-' + key;
                const filter = document.createElementNS(svgNS, 'filter');
                filter.id = filterId;
                filter.setAttribute('color-interpolation-filters', 'sRGB');
                const cm = document.createElementNS(svgNS, 'feColorMatrix');
                cm.setAttribute('type', 'matrix');
                const rr = r / 255, gg = g / 255, bb = b / 255;
                const alphaRow = isOpaqueOp ? '0 0 0 0 1' : ('0 0 0 ' + (a / 255) + ' 0');
                cm.setAttribute('values',
                    rr + ' 0 0 0 0  0 ' + gg + ' 0 0 0  0 0 ' + bb + ' 0 0  ' + alphaRow);
                filter.appendChild(cm);
                defs.appendChild(filter);
                Module['cnaSvgDomFilterCache'][key] = filterId;
            }
        }
        const filterAttr = needsTint ? ('url(#' + filterId + ')') : "";
        if (prev.filter !== filterAttr) {
            if (needsTint) image.setAttribute('filter', filterAttr);
            else image.removeAttribute('filter');
            prev.filter = filterAttr;
        }
    }
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
#endif

namespace CNA::Internal::Renderers::SvgDom
{
    namespace
    {
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
                                          DomCompositeOp op)
    {
        const float sourceX = static_cast<float>(source.X);
        const float sourceY = static_cast<float>(source.Y);
        const float sourceW = static_cast<float>(source.Width);
        const float sourceH = static_cast<float>(source.Height);

        const bool exceedsBounds = source.X < 0 || source.Y < 0 ||
                                   source.X + source.Width > textureWidth ||
                                   source.Y + source.Height > textureHeight;
        ValidateSourceRectangleEXT(exceedsBounds);

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
        cmd.flags = flags;

        cmd.variantMode = VariantModeFor(op);
        cmd.packedColor = static_cast<std::uint32_t>(color.getRProperty())
                        | (static_cast<std::uint32_t>(color.getGProperty()) << 8)
                        | (static_cast<std::uint32_t>(color.getBProperty()) << 16)
                        | (static_cast<std::uint32_t>(color.getAProperty()) << 24);
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
    }

    void SvgDomSpriteBatchRenderer::Flush(const SvgDomDrawCommand* cmds,
                                          const SvgDomTextureRenderer* const* textures, int count)
    {
        if (count <= 0) return;
#if defined(__EMSCRIPTEN__)
        const int boundTarget = GetBoundRenderTargetIdEXT();
        if (boundTarget != 0)
        {
            for (int i = 0; i < count; ++i)
            {
                const SvgDomDrawCommand& c = cmds[i];
                const SvgDomTextureRenderer* tex = textures[i];
                if (!tex) continue;
                const Color tint(static_cast<std::uint8_t>(c.packedColor & 0xFF),
                                 static_cast<std::uint8_t>((c.packedColor >> 8) & 0xFF),
                                 static_cast<std::uint8_t>((c.packedColor >> 16) & 0xFF),
                                 static_cast<std::uint8_t>((c.packedColor >> 24) & 0xFF));
                const DomCompositeOp op = ReconstructOpEXT(c);
                const std::vector<std::uint8_t> pixels = PrepareSpritePixelsEXT(
                    tex->GetPixelsEXT(), tex->GetWidth(),
                    static_cast<int>(c.sx), static_cast<int>(c.sy),
                    static_cast<int>(c.sw), static_cast<int>(c.sh), tint, op);
                CNA_SvgDom_DrawSpriteToTarget(
                    boundTarget, pixels.data(), static_cast<int>(c.sw), static_cast<int>(c.sh),
                    c.m0, c.m1, c.m2, c.m3, c.m4, c.m5,
                    (op == DomCompositeOp::Additive) ? 1 : 0, (op == DomCompositeOp::Opaque) ? 1 : 0);
            }
            return;
        }

        // Note: the sprite group itself is cleared once per FRAME (SvgDomRenderer::Clear), not
        // here -- a frame commonly contains several SpriteBatch Begin/End blocks (e.g. one per
        // layer), and clearing on every flush would wipe out sprites an earlier batch in the SAME
        // frame already flushed.
        float sx = 0, sy = 0, sw = 0, sh = 0;
        const bool scissorEnabled = GetCurrentScissorEnableEXT();
        GetCurrentScissorRectEXT(sx, sy, sw, sh);
        CNA_SvgDom_FlushSpritesToSvg(cmds, count, SvgDomDrawCommandFields, scissorEnabled ? 1 : 0,
                                     static_cast<int>(sx), static_cast<int>(sy),
                                     static_cast<int>(sw), static_cast<int>(sh));
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
        const auto& tex = static_cast<const SvgDomTextureRenderer&>(texture);
        if (tex.GetCanvasIdEXT() == 0) return;
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;

        SvgDomDrawCommand cmd = BuildDrawCommandEXT(
            tex.GetCanvasIdEXT(), texture.GetWidth(), texture.GetHeight(), destinationRectangle,
            sourceRectangle, color, rotation, origin, effects, smoothingEnabled_,
            GetCurrentCompositeOpEXT());

        // Compose (in order) the batch's own SetTransformMatrix and the active Viewport's (X,Y)
        // offset on top of the intrinsic per-sprite placement BuildDrawCommandEXT already produced.
        // Real XNA/FNA applies both AFTER the sprite's own placement, and the viewport offset
        // strictly after the batch transform (rasterizer stage, past the projection matrix) -- see
        // SetCurrentViewportOffsetEXT's own doc.
        Mat2x3 composed{cmd.m0, cmd.m1, cmd.m2, cmd.m3, cmd.m4, cmd.m5};
        if (hasMatrix_)
        {
            const Mat2x3 batch{matrix_[0], matrix_[1], matrix_[2], matrix_[3], matrix_[4], matrix_[5]};
            composed = Compose(batch, composed);
        }
        float vpX = 0, vpY = 0;
        GetCurrentViewportOffsetEXT(vpX, vpY);
        if (vpX != 0.0f || vpY != 0.0f)
            composed = Compose(MatTranslate(vpX, vpY), composed);
        cmd.m0 = composed.a; cmd.m1 = composed.b; cmd.m2 = composed.c;
        cmd.m3 = composed.d; cmd.m4 = composed.e; cmd.m5 = composed.f;

        // Ensures the texture's needed pixel variant has a JS-side data URI registered before this
        // command can be flushed to the SVG path (idempotent, cached by SvgDomTextureRenderer).
        (void)tex.GetDataUriEXT(cmd.variantMode);

        if (immediateMode_)
        {
            const SvgDomTextureRenderer* texPtr = &tex;
            Flush(&cmd, &texPtr, 1);
            return;
        }
        commands_.push_back(cmd);
        commandTextures_.push_back(&tex);
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
