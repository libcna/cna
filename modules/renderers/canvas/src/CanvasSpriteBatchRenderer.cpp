#include "CNA/Internal/Renderers/Canvas/CanvasSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasTextureRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasRenderTargetRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-31/32/33/34: single shared drawImage-based draw path covering every
// SpriteBatch::Draw() overload (the simpler ones just pass identity rotation/origin/flip and
// Color.White -- CANVAS-35/37's own conclusion is that no separate per-overload renderer code is
// needed). Geometry: translate to (destX,destY), rotate, scale by (destW/sw, destH/sh), then draw
// the source rect at local offset (-originX,-originY) -- this places `origin` (in source-pixel
// space) exactly at (destX,destY) invariant under rotation, matching FNA's real GenerateVertexInfo
// formula (SDL_RENDERER's Task 671 fix derives/verifies the identical placement, just against a
// different native rotation API). Flip mirrors the drawn content about the sprite's OWN local
// center (not the pivot, and not the whole coordinate system) so the destination quad's screen
// footprint is unaffected by flipping -- matching real XNA/FNA semantics where SpriteEffects only
// changes which source texture corner maps to which (unchanged) destination corner.
//
// Opaque clip: ctx.clip() is set to the EXACT rect this draw actually paints (in both branches
// below) before globalCompositeOperation is applied. This matters specifically for
// BlendState.Opaque's 'copy' operator: Porter-Duff 'copy' is Co=Cs/alphao=alphas evaluated over the
// WHOLE compositing area, not just the drawn shape's own footprint -- an earlier version of this
// code set 'copy' without a clip, which cleared the ENTIRE canvas to transparent outside the
// sprite's own quad (confirmed via the CSS Compositing spec's per-pixel formula, not assumed). The
// clip is a no-op for 'source-over'/'lighter' (they never touch pixels outside the drawn shape
// anyway), so it's applied unconditionally rather than conditionally on the composite-op string.
//
// Tint (CANVAS-32) and premultiply-alpha handling (CANVAS-41): both are implemented as a single
// per-pixel getImageData/putImageData pass, below, rather than composite-mode tricks -- an earlier
// multiply+destination-in version of the tint pass was mathematically wrong
// (verified algebraically against the CSS Compositing spec's blend-mode formula: it produced
// Rt*(1-As*(1-Rs)) instead of the desired Rt*Rs, a real A^2-style darkening error at semi-transparent,
// non-white edge pixels). The per-pixel pass is exact: RGB channels are transformed directly,
// alpha is copied through completely untouched. Skipped entirely when neither tint nor
// un-premultiply is needed (the common case), so most draws pay zero extra-canvas cost.
//
// CANVAS-42: smoothingEnabled maps the "expand"/magnification component of TextureFilter to
// ctx.imageSmoothingEnabled (Canvas2D's only smoothing control).
//
// CANVAS-43/44: addressU/addressV are the raw TextureAddressMode ints (0=Wrap, 1=Clamp, 2=Mirror).
// Clamp is implemented for real by explicitly clamping the source rect into the texture's actual
// bounds -- not by relying on drawImage's own out-of-bounds behavior, which this dev loop cannot
// verify in a real browser (Design decision 9). Wrap/Mirror only change behavior when the requested
// sourceRectangle exceeds the texture's own bounds (the only case they can ever visibly differ from
// Clamp) -- ctx.createPattern(...,'repeat') for Wrap; for Mirror, a lazily-built 2x2 pre-tiled
// mirrored canvas (createPattern has no native mirror-repeat mode) is used as the pattern source
// instead, per plan_canvas.md CANVAS-44's own suggested "pre-composited 2x-tile" approach. Both
// patterns are filled via ctx.fillRect() under the SAME rotate/scale/flip transform stack as the
// plain drawImage path, so rotation/flip apply for free. Mixed per-axis modes, a tinted draw, and an
// AlphaBlend draw needing un-premultiply are all validated and thrown for in C++ (DrawSprite, below)
// BEFORE this function is ever called when combined with an out-of-bounds Wrap/Mirror
// sourceRectangle -- narrow, honestly-documented gaps (plan_canvas.md CANVAS-44's notes), not
// silently-wrong output -- so this function can assume neither case reaches the pattern-fill branch.
EM_JS(void, CNA_Canvas2D_DrawSprite, (
    int id, int sx, int sy, int sw, int sh,
    double destX, double destY, double destW, double destH,
    double rotation, double originX, double originY,
    int flipH, int flipV,
    int r, int g, int b, int a,
    int smoothingEnabled, int addressU, int addressV
), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (!entry) { console.error('[CNA] Canvas2D: Draw on unknown texture id', id); return; }
    CNA_Canvas2D_EnsureMainContext();
    const ctx = Module['cnaCurrentCtx'];
    if (!ctx || sw <= 0 || sh <= 0 || destW === 0 || destH === 0) return;

    const texW = entry.canvas.width, texH = entry.canvas.height;
    const exceedsBounds = sx < 0 || sy < 0 || sx + sw > texW || sy + sh > texH;
    const tinted = (r !== 255 || g !== 255 || b !== 255);
    const needsUnpremultiply = !!Module['cnaNeedsUnpremultiply'];

    ctx.save();
    ctx.imageSmoothingEnabled = !!smoothingEnabled;
    ctx.globalCompositeOperation = Module['cnaCompositeOp'] || 'source-over';
    ctx.globalAlpha = a / 255;
    ctx.translate(destX, destY);
    ctx.rotate(rotation);
    ctx.scale(destW / sw, destH / sh);
    if (flipH) {
        const cx = -originX + sw / 2;
        ctx.translate(cx, 0); ctx.scale(-1, 1); ctx.translate(-cx, 0);
    }
    if (flipV) {
        const cy = -originY + sh / 2;
        ctx.translate(0, cy); ctx.scale(1, -1); ctx.translate(0, -cy);
    }

    if (exceedsBounds && (addressU !== 1 || addressV !== 1)) {
        // C++'s DrawSprite already validated addressU===addressV and !tinted and
        // !needsUnpremultiply before ever calling here -- see this function's own header comment.
        let tileSource = entry.canvas;
        if (addressU === 2) { // Mirror
            if (!Module['cnaMirrorTiles']) Module['cnaMirrorTiles'] = {};
            let tile = Module['cnaMirrorTiles'][id];
            if (!tile) {
                tile = (typeof OffscreenCanvas !== 'undefined')
                    ? new OffscreenCanvas(texW * 2, texH * 2) : document.createElement('canvas');
                tile.width = texW * 2; tile.height = texH * 2;
                const tctx = tile.getContext('2d');
                tctx.drawImage(entry.canvas, 0, 0);
                tctx.save(); tctx.translate(texW * 2, 0); tctx.scale(-1, 1); tctx.drawImage(entry.canvas, 0, 0); tctx.restore();
                tctx.save(); tctx.translate(0, texH * 2); tctx.scale(1, -1); tctx.drawImage(entry.canvas, 0, 0); tctx.restore();
                tctx.save(); tctx.translate(texW * 2, texH * 2); tctx.scale(-1, -1); tctx.drawImage(entry.canvas, 0, 0); tctx.restore();
                Module['cnaMirrorTiles'][id] = tile;
            }
            tileSource = tile;
        }
        const pattern = ctx.createPattern(tileSource, 'repeat');
        if (pattern.setTransform) {
            const m = new DOMMatrix();
            m.e = -originX - sx; m.f = -originY - sy;
            pattern.setTransform(m);
        }
        ctx.beginPath();
        ctx.rect(-originX, -originY, sw, sh);
        ctx.clip();
        ctx.fillStyle = pattern;
        ctx.fillRect(-originX, -originY, sw, sh);
        ctx.restore();
        return;
    }

    // Clamp path (also the common in-bounds path, regardless of address mode): clamp the source
    // rect into the texture's real bounds explicitly, then place/scale the clamped sub-rect at the
    // equivalent offset within the destination -- a no-op when already in-bounds.
    const csx = Math.max(0, Math.min(sx, texW));
    const csy = Math.max(0, Math.min(sy, texH));
    const csw = Math.max(0, Math.min(sx + sw, texW) - csx);
    const csh = Math.max(0, Math.min(sy + sh, texH) - csy);
    if (csw <= 0 || csh <= 0) { ctx.restore(); return; }

    let source = entry.canvas;
    let drawSx = csx, drawSy = csy;
    if (tinted || needsUnpremultiply) {
        // Exact per-pixel processing: divide RGB by alpha first (un-premultiply, only for
        // AlphaBlend), then multiply RGB by the tint (only if tinted) -- alpha is read and written
        // back completely untouched in every case.
        const imgData = entry.ctx.getImageData(csx, csy, csw, csh);
        const data = imgData.data;
        for (let i = 0; i < data.length; i += 4) {
            let rr = data[i], gg = data[i + 1], bb = data[i + 2];
            const aa = data[i + 3];
            if (needsUnpremultiply && aa > 0) {
                const inv = 255 / aa;
                rr = Math.min(255, rr * inv);
                gg = Math.min(255, gg * inv);
                bb = Math.min(255, bb * inv);
            }
            if (tinted) {
                rr = (rr * r) / 255;
                gg = (gg * g) / 255;
                bb = (bb * b) / 255;
            }
            data[i] = rr; data[i + 1] = gg; data[i + 2] = bb;
        }
        if (!Module['cnaScratchCanvas']) {
            Module['cnaScratchCanvas'] = (typeof OffscreenCanvas !== 'undefined')
                ? new OffscreenCanvas(1, 1) : document.createElement('canvas');
        }
        const scratch = Module['cnaScratchCanvas'];
        scratch.width = csw; scratch.height = csh;
        scratch.getContext('2d').putImageData(imgData, 0, 0);
        source = scratch;
        drawSx = 0; drawSy = 0;
    }

    const offX = (csx - sx) * (destW / sw), offY = (csy - sy) * (destH / sh);
    const outW = csw * (destW / sw), outH = csh * (destH / sh);
    ctx.beginPath();
    ctx.rect(-originX + offX, -originY + offY, outW, outH);
    ctx.clip();
    ctx.drawImage(source, drawSx, drawSy, csw, csh, -originX + offX, -originY + offY, outW, outH);
    ctx.restore();
});

// C++-callable query for the current BlendState's un-premultiply requirement (set by
// CanvasRenderer::ApplyBlendState -> CNA_Canvas2D_SetCompositeOp) -- used by DrawSprite
// (below) to validate the AlphaBlend + out-of-bounds-Wrap/Mirror combination before ever reaching
// the JS draw path, consistent with how the tint/mixed-address-mode checks are validated in C++.
EM_JS(int, CNA_Canvas2D_GetNeedsUnpremultiply, (), {
    return Module['cnaNeedsUnpremultiply'] ? 1 : 0;
});

// plan_canvas.md CANVAS-36: sets the base transform every subsequent CNA_Canvas2D_DrawSprite call
// composes on top of (each Draw's own save()/restore() snapshots and restores this baseline) --
// see this file's own header comment on SetTransformMatrix for the row-major-Matrix-to-setTransform
// field mapping.
EM_JS(void, CNA_Canvas2D_SetTransform, (double a, double b, double c, double d, double e, double f), {
    CNA_Canvas2D_EnsureMainContext();
    const ctx = Module['cnaCurrentCtx'];
    if (!ctx) return;
    ctx.setTransform(a, b, c, d, e, f);
});
#endif

namespace CNA::Internal::Renderers::Canvas
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        // Sibling concrete classes (not a subclass relationship -- IRenderTargetRenderer :
        // ITextureRenderer and CanvasTextureRenderer : ITextureRenderer are two separate branches),
        // same underlying problem SDL_RENDERER's Task 705 comment documents for its own two sibling
        // texture-renderer classes. SDL_Renderer now unifies them through an SDL-only sibling
        // interface; Canvas2D's "native handle" is a JS-side integer id, so a contained
        // dynamic_cast against the two known concrete Canvas types is the safe equivalent here.
        int CanvasIdOf(const ITextureRenderer& texture)
        {
            if (const auto* t = dynamic_cast<const CanvasTextureRenderer*>(&texture)) return t->GetCanvasId();
            if (const auto* rt = dynamic_cast<const CanvasRenderTargetRenderer*>(&texture)) return rt->GetCanvasId();
            return 0;
        }
    }

    void ValidateAddressModeCombination(int addressU, int addressV, bool exceedsBounds,
                                        bool tinted, bool needsUnpremultiply)
    {
        if (!exceedsBounds || (addressU == 1 && addressV == 1)) return; // Clamp, or in-bounds: always fine.
        if (addressU != addressV)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D): mixed U/V TextureAddressMode with an out-of-bounds "
                "sourceRectangle is not supported (addressU=" + std::to_string(addressU) +
                ", addressV=" + std::to_string(addressV) + ")");
        if (tinted)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D): a tinted draw combined with a Wrap/Mirror out-of-bounds "
                "sourceRectangle is not supported");
        if (needsUnpremultiply)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D): an AlphaBlend draw combined with a Wrap/Mirror "
                "out-of-bounds sourceRectangle is not supported (needs per-pixel un-premultiply of "
                "a tiled pattern source, not yet implemented)");
    }

    namespace
    {
        void DrawSprite(const ITextureRenderer& texture,
                        const Rectangle& destinationRectangle,
                        const Rectangle& sourceRectangle,
                        const Color& color,
                        float rotation,
                        const Vector2& origin,
                        SpriteEffects effects,
                        bool smoothingEnabled,
                        int addressU,
                        int addressV)
        {
            const int id = CanvasIdOf(texture);
            if (id == 0) return;
#if defined(__EMSCRIPTEN__)
            const bool exceedsBounds = sourceRectangle.X < 0 || sourceRectangle.Y < 0 ||
                sourceRectangle.X + sourceRectangle.Width > texture.GetWidth() ||
                sourceRectangle.Y + sourceRectangle.Height > texture.GetHeight();
            const bool tinted = color.getRProperty() != 255 || color.getGProperty() != 255 ||
                                color.getBProperty() != 255;
            const bool needsUnpremultiply = CNA_Canvas2D_GetNeedsUnpremultiply() != 0;
            ValidateAddressModeCombination(addressU, addressV, exceedsBounds, tinted, needsUnpremultiply);

            const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
            const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
            CNA_Canvas2D_DrawSprite(
                id, sourceRectangle.X, sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height,
                destinationRectangle.X, destinationRectangle.Y,
                destinationRectangle.Width, destinationRectangle.Height,
                static_cast<double>(rotation), static_cast<double>(origin.X), static_cast<double>(origin.Y),
                flipH ? 1 : 0, flipV ? 1 : 0,
                color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty(),
                smoothingEnabled ? 1 : 0, addressU, addressV);
#else
            (void)destinationRectangle; (void)sourceRectangle; (void)color; (void)rotation; (void)origin; (void)effects;
            (void)smoothingEnabled; (void)addressU; (void)addressV;
#endif
        }
    }

    void CanvasSpriteBatchRenderer::Begin()
    {
        begun_ = true;
    }

    void CanvasSpriteBatchRenderer::End()
    {
        begun_ = false;
        // Restore the identity transform so a subsequent Clear() (whose fillRect/clearRect assume
        // an untransformed context) isn't corrupted by a lingering SetTransformMatrix() from this
        // batch -- Clear() also defensively resets its own transform (belt and suspenders).
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_SetTransform(1, 0, 0, 1, 0, 0);
#endif
    }

    void CanvasSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
#if defined(__EMSCRIPTEN__)
        // XNA/FNA Matrix is row-major (row-vector convention: v' = v * M); Canvas2D's
        // setTransform(a,b,c,d,e,f) defines x'=a*x+c*y+e, y'=b*x+d*y+f. Matching terms:
        // a=M11, b=M12, c=M21, d=M22, e=M41, f=M42.
        CNA_Canvas2D_SetTransform(m.M11, m.M12, m.M21, m.M22, m.M41, m.M42);
#else
        (void)m;
#endif
    }

    void CanvasSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D) does not support custom SpriteBatch Effects: "
                "no programmable shader stage exists on this renderer.");
    }

    void CanvasSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        // Same magnification-dominant reasoning as SdlSpriteBatchRenderer::SetSamplerFilter (Task
        // 701): SpriteBatch draws are near-universally magnification-dominant, so the "expand"
        // component of TextureFilter is what visibly matters against Canvas2D's single binary
        // smoothing toggle. Linear=0, Anisotropic=2, LinearMipPoint=3, MinPointMagLinearMipLinear=7,
        // MinPointMagLinearMipPoint=8 all have mag=Linear -> smoothing on; everything else (Point
        // and the MagPoint variants) -> smoothing off.
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

    void CanvasSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void CanvasSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        if (!begun_) throw std::runtime_error("CanvasSpriteBatchRenderer::Draw called before Begin().");
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        DrawSprite(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None,
                  smoothingEnabled_, addressU_, addressV_);
    }

    void CanvasSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        if (!begun_) throw std::runtime_error("CanvasSpriteBatchRenderer::Draw called before Begin().");
        DrawSprite(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None,
                  smoothingEnabled_, addressU_, addressV_);
    }

    void CanvasSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float layerDepth)
    {
        (void)layerDepth;
        if (!begun_) throw std::runtime_error("CanvasSpriteBatchRenderer::Draw called before Begin().");
        DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
                  smoothingEnabled_, addressU_, addressV_);
    }
}
