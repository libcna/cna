#include "CNA/Internal/Backends/Canvas/CanvasSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Canvas/CanvasTextureBackend.hpp"
#include "CNA/Internal/Backends/Canvas/CanvasRenderTargetBackend.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-31/32/33/34: single shared drawImage-based draw path covering every
// SpriteBatch::Draw() overload (the simpler ones just pass identity rotation/origin/flip and
// Color.White -- CANVAS-35/37's own conclusion is that no separate per-overload backend code is
// needed). Geometry: translate to (destX,destY), rotate, scale by (destW/sw, destH/sh), then draw
// the source rect at local offset (-originX,-originY) -- this places `origin` (in source-pixel
// space) exactly at (destX,destY) invariant under rotation, matching FNA's real GenerateVertexInfo
// formula (SDL_RENDERER's Task 671 fix derives/verifies the identical placement, just against a
// different native rotation API). Flip mirrors the drawn content about the sprite's OWN local
// center (not the pivot, and not the whole coordinate system) so the destination quad's screen
// footprint is unaffected by flipping -- matching real XNA/FNA semantics where SpriteEffects only
// changes which source texture corner maps to which (unchanged) destination corner.
//
// Tint (CANVAS-32): color.A is always applied via ctx.globalAlpha (exact, zero extra cost). A
// non-white RGB tint needs the scratch-canvas multiply trick Design decision/CANVAS-32 anticipated
// (Canvas2D has no native per-draw RGB-multiply primitive) -- skipped entirely for the overwhelmingly
// common Color.White case, so most draws pay zero extra-canvas cost.
EM_JS(void, CNA_Canvas2D_DrawSprite, (
    int id, int sx, int sy, int sw, int sh,
    double destX, double destY, double destW, double destH,
    double rotation, double originX, double originY,
    int flipH, int flipV,
    int r, int g, int b, int a
), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (!entry) { console.error('[CNA] Canvas2D: Draw on unknown texture id', id); return; }
    CNA_Canvas2D_EnsureMainContext();
    const ctx = Module['cnaCurrentCtx'];
    if (!ctx || sw <= 0 || sh <= 0 || destW === 0 || destH === 0) return;

    let source = entry.canvas;
    let drawSx = sx, drawSy = sy;
    if (r !== 255 || g !== 255 || b !== 255) {
        if (!Module['cnaScratchCanvas']) {
            Module['cnaScratchCanvas'] = (typeof OffscreenCanvas !== 'undefined')
                ? new OffscreenCanvas(1, 1) : document.createElement('canvas');
        }
        const scratch = Module['cnaScratchCanvas'];
        scratch.width = sw; scratch.height = sh;
        const sctx = scratch.getContext('2d');
        sctx.setTransform(1, 0, 0, 1, 0, 0);
        sctx.globalAlpha = 1;
        sctx.globalCompositeOperation = 'source-over';
        sctx.clearRect(0, 0, sw, sh);
        sctx.drawImage(entry.canvas, sx, sy, sw, sh, 0, 0, sw, sh);
        sctx.globalCompositeOperation = 'multiply';
        sctx.fillStyle = 'rgb(' + r + ',' + g + ',' + b + ')';
        sctx.fillRect(0, 0, sw, sh);
        sctx.globalCompositeOperation = 'destination-in';
        sctx.drawImage(entry.canvas, sx, sy, sw, sh, 0, 0, sw, sh);
        sctx.globalCompositeOperation = 'source-over';
        source = scratch;
        drawSx = 0; drawSy = 0;
    }

    const scaleX = destW / sw, scaleY = destH / sh;
    ctx.save();
    ctx.globalAlpha = a / 255;
    ctx.translate(destX, destY);
    ctx.rotate(rotation);
    ctx.scale(scaleX, scaleY);
    if (flipH) {
        const cx = -originX + sw / 2;
        ctx.translate(cx, 0); ctx.scale(-1, 1); ctx.translate(-cx, 0);
    }
    if (flipV) {
        const cy = -originY + sh / 2;
        ctx.translate(0, cy); ctx.scale(1, -1); ctx.translate(0, -cy);
    }
    ctx.drawImage(source, drawSx, drawSy, sw, sh, -originX, -originY, sw, sh);
    ctx.restore();
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

namespace CNA::Internal::Backends::Canvas
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        // Sibling concrete classes (not a subclass relationship -- IRenderTargetBackend :
        // ITextureBackend and CanvasTextureBackend : ITextureBackend are two separate branches),
        // same underlying problem SDL_RENDERER's Task 705 comment documents for its own two sibling
        // texture-backend classes. SDL_Renderer unifies via a shared virtual already on
        // ITextureBackend (GetNativeTexture()); Canvas2D's "native handle" is a JS-side integer id
        // with no such shared accessor, so a contained dynamic_cast against the two known concrete
        // Canvas types is the safe equivalent here.
        int CanvasIdOf(const ITextureBackend& texture)
        {
            if (const auto* t = dynamic_cast<const CanvasTextureBackend*>(&texture)) return t->GetCanvasId();
            if (const auto* rt = dynamic_cast<const CanvasRenderTargetBackend*>(&texture)) return rt->GetCanvasId();
            return 0;
        }

        void DrawSprite(const ITextureBackend& texture,
                        const Rectangle& destinationRectangle,
                        const Rectangle& sourceRectangle,
                        const Color& color,
                        float rotation,
                        const Vector2& origin,
                        SpriteEffects effects)
        {
            const int id = CanvasIdOf(texture);
            if (id == 0) return;
#if defined(__EMSCRIPTEN__)
            const bool flipH = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0;
            const bool flipV = (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0;
            CNA_Canvas2D_DrawSprite(
                id, sourceRectangle.X, sourceRectangle.Y, sourceRectangle.Width, sourceRectangle.Height,
                destinationRectangle.X, destinationRectangle.Y,
                destinationRectangle.Width, destinationRectangle.Height,
                static_cast<double>(rotation), static_cast<double>(origin.X), static_cast<double>(origin.Y),
                flipH ? 1 : 0, flipV ? 1 : 0,
                color.getRProperty(), color.getGProperty(), color.getBProperty(), color.getAProperty());
#else
            (void)destinationRectangle; (void)sourceRectangle; (void)color; (void)rotation; (void)origin; (void)effects;
#endif
        }
    }

    void CanvasSpriteBatchBackend::Begin()
    {
        begun_ = true;
    }

    void CanvasSpriteBatchBackend::End()
    {
        begun_ = false;
        // Restore the identity transform so a subsequent Clear() (whose fillRect/clearRect assume
        // an untransformed context) isn't corrupted by a lingering SetTransformMatrix() from this
        // batch -- Clear() also defensively resets its own transform (belt and suspenders).
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_SetTransform(1, 0, 0, 1, 0, 0);
#endif
    }

    void CanvasSpriteBatchBackend::SetTransformMatrix(const Matrix& m)
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

    void CanvasSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D) does not support custom SpriteBatch Effects: "
                "no programmable shader stage exists on this backend.");
    }

    void CanvasSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        if (!begun_) throw std::runtime_error("CanvasSpriteBatchBackend::Draw called before Begin().");
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        DrawSprite(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0), SpriteEffects::None);
    }

    void CanvasSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        if (!begun_) throw std::runtime_error("CanvasSpriteBatchBackend::Draw called before Begin().");
        DrawSprite(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None);
    }

    void CanvasSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float layerDepth)
    {
        (void)layerDepth;
        if (!begun_) throw std::runtime_error("CanvasSpriteBatchBackend::Draw called before Begin().");
        DrawSprite(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects);
    }
}
