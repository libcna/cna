#include "CNA/Internal/Renderers/PixiJs/PixiJsTextureRenderer.hpp"

#include <cstdint>
#include <stdexcept>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_pixijs.md PIXIJS-30/Design decision 8: synchronous, buffer-backed PIXI.Texture upload --
// PIXI.BufferResource wraps a plain typed array directly as the GPU texture source, avoiding the
// async Image/ImageBitmap decode path PixiJS's own Texture.from() otherwise uses. Registered by
// integer id in Module['cnaPixi'].textures -- PixiJsSpriteBatchRenderer's flush resolves the
// sampled texture from that registry by id.
//
// ALPHA_MODES.NPM ("no premultiplied alpha") is load-bearing in two independent ways, both
// verified in a real browser:
//   1. Upload. ALPHA_MODES.UNPACK is a synonym for PREMULTIPLY_ON_UPLOAD, so every texture
//      uploaded that way silently had its RGB multiplied by its own alpha at the GPU level
//      (REMED-PIXIJS-4). NPM uploads the bytes exactly as CNA supplied them, which is what lets an
//      application choose its own alpha convention through the BlendState it draws with.
//   2. Vertex-colour packing. PixiJS premultiplies a sprite's tint into the vertex colour only when
//      the sampled base texture is premultiplied; with NPM it packs a STRAIGHT tint, which is
//      exactly XNA's own SpriteBatch tint semantics (RGB scaled by colour.rgb, alpha carried
//      separately).
EM_JS(int, CNA_PixiJs_CreateTextureWithPixels, (int id, int width, int height, const uint8_t* rgba), {
    const state = Module['cnaPixi'];
    if (!state) return 0;
    try {
        const buffer = new Uint8Array(HEAPU8.subarray(rgba, rgba + width * height * 4));
        const baseTexture = new PIXI.BaseTexture(new PIXI.BufferResource(buffer, { width: width, height: height }), {
            width: width, height: height,
            scaleMode: PIXI.SCALE_MODES.NEAREST,
            alphaMode: PIXI.ALPHA_MODES.NPM,
        });
        state.textures[id] = {
            texture: new PIXI.Texture(baseTexture),
            buffer: buffer,
            isRenderTarget: false,
            // PIXIJS-94: cache of per-(frame, rotate) PIXI.Texture views onto this base texture,
            // owned by this entry so a view can never outlive the resource it describes.
            views: {},
        };
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: texture creation failed:', e);
        return 0;
    }
});

// plan_pixijs.md PIXIJS-31: full level-0 re-upload -- mutate the same backing buffer in place and
// call baseTexture.update(), PixiJS's own synchronous "re-upload this resource to the GPU" call.
EM_JS(int, CNA_PixiJs_UpdatePixels, (int id, int width, int height, const uint8_t* rgba), {
    const state = Module['cnaPixi'];
    const entry = state && state.textures[id];
    if (!entry) return 0;
    try {
        entry.buffer.set(HEAPU8.subarray(rgba, rgba + width * height * 4));
        entry.texture.baseTexture.update();
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: texture update failed:', e);
        return 0;
    }
});

// A destroy that runs after CNA_PixiJs_DestroyApp has already cleared the registry is a no-op, not
// an error: tearing the renderer down takes its WebGL context with it, so every texture created
// against that context is gone by then (PIXIJS-92).
EM_JS(void, CNA_PixiJs_DestroyTexture, (int id), {
    const state = Module['cnaPixi'];
    const entry = state && state.textures[id];
    if (!entry) return;
    for (const key of Object.keys(entry.views)) entry.views[key].destroy(false);
    entry.texture.destroy(true);
    delete state.textures[id];
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
        if (CNA_PixiJs_CreateTextureWithPixels(id_, width_, height_, data.pixels.data()) == 0)
            throw std::runtime_error(
                "PixiJS: could not create a texture; the PIXI.Application is not available.");
#endif
    }

    PixiJsTextureRenderer::PixiJsTextureRenderer(int width, int height)
        : id_(NextPixiTextureId())
        , width_(width)
        , height_(height)
    {
#if defined(__EMSCRIPTEN__)
        const std::vector<std::uint8_t> blank(static_cast<std::size_t>(width_) * height_ * 4, 0);
        if (CNA_PixiJs_CreateTextureWithPixels(id_, width_, height_, blank.data()) == 0)
            throw std::runtime_error(
                "PixiJS: could not create a blank texture; the PIXI.Application is not available.");
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
        if (CNA_PixiJs_UpdatePixels(id_, width_, height_, rgba) == 0)
            throw std::runtime_error("PixiJS: could not update texture pixels for an unknown texture id.");
#endif
    }

    void PixiJsTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw std::runtime_error(
                "PixiJS does not support custom mip-level texture uploads (level " +
                std::to_string(level) + ") -- plan_pixijs.md PIXIJS-31: PIXI.BufferResource and "
                "PIXI.BaseTexture expose no per-level CPU upload API at all; mipmaps are "
                "GPU-auto-generated from level 0 only. Use Texture2D::SetData(level=0, ...) only.");
        (void)levelW; (void)levelH;
        UpdatePixels(rgba, width_ * 4);
    }
}
