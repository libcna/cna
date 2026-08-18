#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderTargetRenderer.hpp"

#include "CNA/Logger.hpp"

#include <cstdint>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_pixijs.md PIXIJS-32/Design decision 8: a real PIXI.RenderTexture, registered in the same
// Module['cnaPixi'].textures id space plain buffer-backed textures use (PixiJsTextureRenderer.cpp)
// so the SpriteBatch sampling path can treat either uniformly by id, but created via
// PIXI.RenderTexture.create() rather than a PIXI.BufferResource -- a render texture is a real GPU
// framebuffer target, not a CPU-buffer-backed sampling source.
//
// alphaMode = NPM matters here for a reason specific to readback: PIXI.Extract un-premultiplies a
// render texture's pixels whenever its base texture's alphaMode is non-zero, and
// RenderTexture.create() defaults it to 1. This renderer's content is straight-alpha throughout
// (PixiJsTextureRenderer.cpp's own NPM note), so that division would corrupt every GetData() of a
// target holding partially transparent pixels. Setting NPM makes readback verbatim, and matches how
// the target behaves when it is later SAMPLED as an ordinary texture.
EM_JS(int, CNA_PixiJs_CreateRenderTexture, (int id, int width, int height), {
    const state = Module['cnaPixi'];
    if (!state) return 0;
    try {
        const renderTexture = PIXI.RenderTexture.create({ width: width, height: height });
        renderTexture.baseTexture.alphaMode = PIXI.ALPHA_MODES.NPM;
        state.textures[id] = { texture: renderTexture, isRenderTarget: true, views: {} };
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: render-texture creation failed:', e);
        return 0;
    }
});

// PIXIJS-92: destroying a target that is still bound would leave every later draw pointed at a
// dead framebuffer. Fall back to the back buffer and report it, rather than continuing into
// undefined behaviour -- the caller is a destructor, so this cannot throw.
EM_JS(int, CNA_PixiJs_DestroyRenderTexture, (int id), {
    const state = Module['cnaPixi'];
    const entry = state && state.textures[id];
    if (!entry) return 0;
    let wasBound = 0;
    if (state.activeTargetId === id) {
        state.activeTarget = null;
        state.activeTargetId = 0;
        wasBound = 1;
    }
    for (const key of Object.keys(entry.views)) entry.views[key].destroy(false);
    if (entry.container) entry.container.destroy({ children: false });
    entry.texture.destroy(true);
    delete state.textures[id];
    return wasBound;
});

// plan_pixijs.md PIXIJS-32: switches which target subsequent Clear()/Draw() calls render into.
// PIXIJS-87: a render target no longer owns a retained PIXI.Container of pending sprites -- every
// submitted batch is rasterized into the target's RenderTexture before the submitting call
// returns. Binding therefore only has to redirect the next render, which is what makes
// A -> B -> A -> back buffer switching safe: no scene node ever belongs to a target, so none can
// be moved out of one.
EM_JS(int, CNA_PixiJs_BindRenderTarget, (int id), {
    const state = Module['cnaPixi'];
    const entry = state && state.textures[id];
    if (!state || !state.app || !entry) return 0;
    state.activeTarget = entry.texture;
    state.activeTargetId = id;
    return 1;
});

// plan_pixijs.md Design decision 9: app.renderer.extract.pixels() reads back synchronously from
// the passed target. PIXIJS-87: no re-render first -- everything drawn into this target was already
// rasterized at its own submission point, so a read is just a read. (The previous implementation
// re-rendered the target's pending container before reading, which is what forced the `clear:false`
// correction of REMED-PIXIJS-5 parts 3/4; with nothing pending there is nothing to correct.)
EM_JS(int, CNA_PixiJs_ReadTexturePixels, (int id, int x, int y, int w, int h, uint8_t* outPixels), {
    const state = Module['cnaPixi'];
    const entry = state && state.textures[id];
    if (!state || !state.app || !entry) return 0;
    try {
        const pixels = state.app.renderer.extract.pixels(entry.texture);
        const fullWidth = entry.texture.width;
        const bytesPerRow = w * 4;
        for (let row = 0; row < h; ++row) {
            const srcOffset = ((y + row) * fullWidth + x) * 4;
            HEAPU8.set(pixels.subarray(srcOffset, srcOffset + bytesPerRow), outPixels + row * bytesPerRow);
        }
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: render-target readback failed:', e);
        return 0;
    }
});

// plan_pixijs.md PIXIJS-32: direct CPU pixel upload (Texture2D::SetData) into a PIXI.RenderTexture.
// A render texture has no synchronous CPU-buffer upload path the way a plain buffer-backed texture
// does, so this builds a throwaway buffer-backed texture from the given pixels, paints it over the
// whole target with BLEND_MODES.NONE (a real unconditional overwrite), then discards it.
EM_JS(int, CNA_PixiJs_UpdateRenderTexturePixels, (int id, int width, int height, const uint8_t* rgba), {
    const state = Module['cnaPixi'];
    const entry = state && state.textures[id];
    if (!state || !state.app || !entry) return 0;
    let tempBaseTexture = null;
    let tempTexture = null;
    try {
        const buffer = new Uint8Array(HEAPU8.subarray(rgba, rgba + width * height * 4));
        tempBaseTexture = new PIXI.BaseTexture(new PIXI.BufferResource(buffer, { width: width, height: height }), {
            width: width, height: height,
            scaleMode: PIXI.SCALE_MODES.NEAREST,
            alphaMode: PIXI.ALPHA_MODES.NPM,
        });
        tempTexture = new PIXI.Texture(tempBaseTexture);
        const sprite = new PIXI.Sprite(tempTexture);
        sprite.anchor.set(0, 0);
        sprite.position.set(0, 0);
        sprite.blendMode = PIXI.BLEND_MODES.NONE;
        // The renderer's own scratch container, emptied on both sides: reusing it keeps this path
        // from allocating a Container per SetData call.
        state.scratch.removeChildren();
        state.scratch.addChild(sprite);
        state.gl.colorMask(true, true, true, true);
        state.app.renderer.render(state.scratch, { renderTexture: entry.texture, clear: false });
        state.scratch.removeChildren();
        sprite.destroy({ children: false, texture: false, baseTexture: false });
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: render-target SetData failed:', e);
        try { state.scratch.removeChildren(); } catch (ignored) { /* the scene graph is unusable */ }
        return 0;
    } finally {
        if (tempTexture) tempTexture.destroy(false);
        if (tempBaseTexture) tempBaseTexture.destroy(true);
    }
});
#endif

namespace CNA::Internal::Renderers::PixiJs
{
    namespace
    {
        int NextPixiRenderTextureId()
        {
            static int next = 1'000'000; // Disjoint from PixiJsTextureRenderer's own id counter.
            return next++;
        }
    }

    PixiJsRenderTargetRenderer::PixiJsRenderTargetRenderer(int w, int h)
        : id_(NextPixiRenderTextureId())
        , width_(w)
        , height_(h)
    {
#if defined(__EMSCRIPTEN__)
        if (CNA_PixiJs_CreateRenderTexture(id_, width_, height_) == 0)
            throw std::runtime_error(
                "PixiJS: could not create a render target; the PIXI.Application is not available.");
#endif
    }

    PixiJsRenderTargetRenderer::~PixiJsRenderTargetRenderer()
    {
#if defined(__EMSCRIPTEN__)
        // PIXIJS-92: destroying a still-bound target is a real application error, but a destructor
        // cannot throw. The JS side has already restored the back buffer so nothing renders into a
        // dead framebuffer; say so loudly rather than letting it pass unremarked.
        if (CNA_PixiJs_DestroyRenderTexture(id_) != 0)
            CNA::Logger::Warn(
                "PixiJS: a RenderTarget2D was destroyed while it was still the active render "
                "target. The back buffer has been restored, but the drawing that followed its "
                "destruction was not what the application asked for -- call "
                "GraphicsDevice::SetRenderTarget(nullptr) before releasing a bound target.");
#endif
    }

    void PixiJsRenderTargetRenderer::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        if (!rgba) return;
#if defined(__EMSCRIPTEN__)
        if (CNA_PixiJs_UpdateRenderTexturePixels(id_, width_, height_, rgba) == 0)
            throw std::runtime_error("PixiJS: could not upload pixels into a render target.");
#endif
    }

    void PixiJsRenderTargetRenderer::BindAsRenderTarget()
    {
#if defined(__EMSCRIPTEN__)
        if (CNA_PixiJs_BindRenderTarget(id_) == 0)
            throw std::runtime_error(
                "PixiJS: could not bind a render target; it is unknown to the renderer, which "
                "usually means the graphics device that created it has already been destroyed.");
#endif
    }

    bool PixiJsRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level > 0)
            throw System::NotSupportedException(
                "PixiJsRenderTargetRenderer::GetData: PixiJS render textures have no mip chain in "
                "this v1 scope; level " + std::to_string(level) + " was requested.");
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(width_) ||
            bottom > static_cast<std::int64_t>(height_))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(width_) + "x" +
                    std::to_string(height_) + " render target.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");
        if (data == nullptr) return false;

#if defined(__EMSCRIPTEN__)
        return CNA_PixiJs_ReadTexturePixels(id_, x, y, w, h, static_cast<std::uint8_t*>(data)) != 0;
#else
        return false;
#endif
    }

    void PixiJsRenderTargetRenderer::UnbindAsRenderTarget()
    {
        // A genuine no-op, same reasoning as CanvasRenderTargetRenderer's own UnbindAsRenderTarget:
        // BindAsRenderTarget() is absolute, so switching directly from this target to another (or
        // back to the back buffer via PixiJsRenderer::SetRenderTarget2D(nullptr)) never needs this
        // target to clean up first.
    }
}
