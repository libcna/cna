#include "CNA/Internal/Renderers/PixiJs/PixiJsTextureRenderer.hpp"

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_pixijs.md PIXIJS-30/Design decision 8: synchronous, buffer-backed PIXI.Texture upload --
// PIXI.BufferResource wraps a plain typed array directly as the GPU texture source, avoiding the
// async Image/ImageBitmap decode path PixiJS's own Texture.from() otherwise uses. Registered by
// integer id in Module['cnaPixiTextures'] (mirrors CanvasTextureRenderer's own
// Module['cnaTextures'] registry) -- PixiJsSpriteBatchRenderer sources pooled sprite.texture
// assignments from this registry by id.
EM_JS(void, CNA_PixiJs_CreateTextureWithPixels, (int id, int width, int height, const uint8_t* rgba), {
    if (!Module['cnaPixiTextures']) Module['cnaPixiTextures'] = {};
    const buffer = new Uint8Array(HEAPU8.subarray(rgba, rgba + width * height * 4));
    const baseTexture = new PIXI.BaseTexture(new PIXI.BufferResource(buffer, { width: width, height: height }), {
        width: width, height: height,
        scaleMode: PIXI.SCALE_MODES.NEAREST,
        // REMED-PIXIJS-4 (found via the same real-browser Opaque-blend test, 2026-08-17):
        // ALPHA_MODES.UNPACK is NOT "leave the data alone" -- it is a synonym for
        // PREMULTIPLY_ON_UPLOAD (confirmed live: PIXI.ALPHA_MODES.UNPACK === 1 ===
        // PIXI.ALPHA_MODES.PREMULTIPLY_ON_UPLOAD), so every texture uploaded this way was silently
        // having its RGB multiplied by its own alpha at the GPU level -- invisible for the fully
        // opaque (alpha=255) fixtures every earlier test used, but a real, silent corruption for
        // any genuinely semi-transparent texture. ALPHA_MODES.NPM (No Premultiplied Alpha) is the
        // real "upload exactly as given" mode and is what Design decision 8's synchronous
        // straight-RGBA8 upload path actually needs -- confirmed empirically to make both
        // BLEND_MODES.NONE (Opaque) and BLEND_MODES.NORMAL (AlphaBlend/NonPremultiplied) produce
        // the mathematically correct straight-alpha compositing result.
        alphaMode: PIXI.ALPHA_MODES.NPM,
    });
    const texture = new PIXI.Texture(baseTexture);
    Module['cnaPixiTextures'][id] = { baseTexture: baseTexture, texture: texture, buffer: buffer, isRenderTarget: false };
});

// Same registration as above but blank (fully transparent) -- used by PixiJsRenderTargetRenderer,
// which has no initial pixel data to upload.
EM_JS(void, CNA_PixiJs_CreateBlankTexture, (int id, int width, int height), {
    if (!Module['cnaPixiTextures']) Module['cnaPixiTextures'] = {};
    const buffer = new Uint8Array(width * height * 4);
    const baseTexture = new PIXI.BaseTexture(new PIXI.BufferResource(buffer, { width: width, height: height }), {
        width: width, height: height,
        scaleMode: PIXI.SCALE_MODES.NEAREST,
        // REMED-PIXIJS-4 (found via the same real-browser Opaque-blend test, 2026-08-17):
        // ALPHA_MODES.UNPACK is NOT "leave the data alone" -- it is a synonym for
        // PREMULTIPLY_ON_UPLOAD (confirmed live: PIXI.ALPHA_MODES.UNPACK === 1 ===
        // PIXI.ALPHA_MODES.PREMULTIPLY_ON_UPLOAD), so every texture uploaded this way was silently
        // having its RGB multiplied by its own alpha at the GPU level -- invisible for the fully
        // opaque (alpha=255) fixtures every earlier test used, but a real, silent corruption for
        // any genuinely semi-transparent texture. ALPHA_MODES.NPM (No Premultiplied Alpha) is the
        // real "upload exactly as given" mode and is what Design decision 8's synchronous
        // straight-RGBA8 upload path actually needs -- confirmed empirically to make both
        // BLEND_MODES.NONE (Opaque) and BLEND_MODES.NORMAL (AlphaBlend/NonPremultiplied) produce
        // the mathematically correct straight-alpha compositing result.
        alphaMode: PIXI.ALPHA_MODES.NPM,
    });
    const texture = new PIXI.Texture(baseTexture);
    Module['cnaPixiTextures'][id] = { baseTexture: baseTexture, texture: texture, buffer: buffer, isRenderTarget: false };
});

// plan_pixijs.md PIXIJS-31: full level-0 re-upload -- mutate the same backing buffer in place and
// call baseTexture.update(), PixiJS's own synchronous "re-upload this resource to the GPU" call.
EM_JS(void, CNA_PixiJs_UpdatePixels, (int id, int width, int height, const uint8_t* rgba), {
    const entry = Module['cnaPixiTextures'] && Module['cnaPixiTextures'][id];
    if (!entry) { console.error('[CNA] PixiJS: UpdatePixels on unknown texture id', id); return; }
    entry.buffer.set(HEAPU8.subarray(rgba, rgba + width * height * 4));
    entry.baseTexture.update();
});

EM_JS(void, CNA_PixiJs_DestroyTexture, (int id), {
    const entry = Module['cnaPixiTextures'] && Module['cnaPixiTextures'][id];
    if (entry) {
        entry.texture.destroy();
        entry.baseTexture.destroy();
        delete Module['cnaPixiTextures'][id];
    }
});
#endif

namespace CNA::Internal::Renderers::PixiJs
{
    namespace
    {
        int NextPixiTextureId()
        {
            static int next = 1;
            return next++;
        }
    }

    PixiJsTextureRenderer::PixiJsTextureRenderer(const ImageData& data)
        : id_(NextPixiTextureId())
        , width_(data.width)
        , height_(data.height)
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_CreateTextureWithPixels(id_, width_, height_, data.pixels.data());
#endif
    }

    PixiJsTextureRenderer::PixiJsTextureRenderer(int width, int height)
        : id_(NextPixiTextureId())
        , width_(width)
        , height_(height)
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_CreateBlankTexture(id_, width_, height_);
#endif
    }

    PixiJsTextureRenderer::~PixiJsTextureRenderer()
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_DestroyTexture(id_);
#endif
    }

    void PixiJsTextureRenderer::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        if (!rgba) return;
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_UpdatePixels(id_, width_, height_, rgba);
#endif
    }

    void PixiJsTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw std::runtime_error(
                "PixiJS (v1 scope) does not support mip-level texture uploads (level " +
                std::to_string(level) + ") yet -- plan_pixijs.md PIXIJS-31 has not decided the real "
                "policy here (PixiJS textures can carry real mipmaps, unlike Canvas2D, so this is "
                "not assumed to be a permanent boundary). Use Texture2D::SetData(level=0, ...) only.");
        (void)levelW; (void)levelH;
        UpdatePixels(rgba, width_ * 4);
    }
}
