#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderTargetRenderer.hpp"

#include <cstdint>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_pixijs.md PIXIJS-32/Design decision 8: a real PIXI.RenderTexture, registered in the same
// Module['cnaPixiTextures'] id space plain buffer-backed textures use (PixiJsTextureRenderer.cpp)
// so PixiJsSpriteBatchRenderer's sampling path can treat either uniformly by id, but created via
// PIXI.RenderTexture.create() rather than a PIXI.BufferResource -- a render texture is a real
// GPU framebuffer target, not a CPU-buffer-backed sampling source.
EM_JS(void, CNA_PixiJs_CreateRenderTexture, (int id, int width, int height), {
    if (!Module['cnaPixiTextures']) Module['cnaPixiTextures'] = {};
    const renderTexture = PIXI.RenderTexture.create({ width: width, height: height });
    Module['cnaPixiTextures'][id] = { texture: renderTexture, isRenderTarget: true };
});

EM_JS(void, CNA_PixiJs_DestroyRenderTexture, (int id), {
    const entry = Module['cnaPixiTextures'] && Module['cnaPixiTextures'][id];
    if (entry) {
        entry.texture.destroy(true);
        delete Module['cnaPixiTextures'][id];
    }
});

// plan_pixijs.md PIXIJS-32: switches Module['cnaPixiActiveContainer']/['cnaPixiActiveRenderTexture']
// (see PixiJsRenderer.cpp) to this target's own container/RenderTexture pair, so subsequent
// Clear()/Draw()/Present() calls target it instead of the main stage. Each render target gets its
// own fresh PIXI.Container to draw sprites into (kept alive on the registry entry) so its content
// is independent of app.stage's own pooled sprites.
EM_JS(void, CNA_PixiJs_BindRenderTarget, (int id), {
    const app = Module['cnaPixiApp'];
    const entry = Module['cnaPixiTextures'] && Module['cnaPixiTextures'][id];
    if (!app || !entry) { console.error('[CNA] PixiJS: BindRenderTarget on unknown id', id); return; }
    if (!entry.container) entry.container = new PIXI.Container();
    Module['cnaPixiActiveContainer'] = entry.container;
    Module['cnaPixiActiveRenderTexture'] = entry.texture;
});

// plan_pixijs.md Design decision 9: app.renderer.extract.pixels() reads back synchronously from
// whichever target is passed -- ONE target's own RenderTexture here, unlike
// CNA_PixiJs_ReadCurrentPixels in PixiJsRenderer.cpp, which reads whichever target is current.
//
// REMED-PIXIJS-1 (see PixiJsRenderer.cpp's own CNA_PixiJs_ReadCurrentPixels comment): re-render
// this target's own container into its own texture before reading -- PixiJS is retained-mode, so a
// Draw() into this target queues sprite-property changes without painting anything. Safe even when
// this target isn't the currently-bound one: entry.container's children are exactly whatever was
// last drawn into it, so re-rendering is an idempotent refresh, not a content change.
//
// REMED-PIXIJS-5 (part 3, found by the render-target GetData test, 2026-08-17): `clear` was left
// at its default here, which clears the renderTexture to transparent black before drawing --
// harmless when the container has pending sprites to draw, but this re-render commonly runs
// against an EMPTY container (nothing queued since the last Clear()/End()), and an empty render
// with the default clear WIPED OUT whatever Clear() itself had just painted a moment earlier
// (REMED-PIXIJS-5 part 2's own clear-sprite fix), discovered because the "sample this target as an
// ordinary texture" path (which never calls this function) kept the correct colour while
// GetData() read back blank. `clear: false` makes this purely additive: draw whatever is queued
// (possibly nothing) ON TOP of the target's existing content instead of erasing it first.
EM_JS(int, CNA_PixiJs_ReadTexturePixels, (int id, int x, int y, int w, int h, uint8_t* outPixels), {
    const app = Module['cnaPixiApp'];
    const entry = Module['cnaPixiTextures'] && Module['cnaPixiTextures'][id];
    if (!app || !entry) return 0;
    if (entry.container) app.renderer.render(entry.container, { renderTexture: entry.texture, clear: false });
    const pixels = app.renderer.extract.pixels(entry.texture);
    const fullWidth = entry.texture.width;
    const bytesPerRow = w * 4;
    for (let row = 0; row < h; ++row) {
        const srcOffset = ((y + row) * fullWidth + x) * 4;
        HEAPU8.set(pixels.subarray(srcOffset, srcOffset + bytesPerRow), outPixels + row * bytesPerRow);
    }
    return 1;
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
        CNA_PixiJs_CreateRenderTexture(id_, width_, height_);
#endif
    }

    PixiJsRenderTargetRenderer::~PixiJsRenderTargetRenderer()
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_DestroyRenderTexture(id_);
#endif
    }

    void PixiJsRenderTargetRenderer::UpdatePixels(const uint8_t*, int)
    {
        throw std::runtime_error(
            "PixiJS: direct CPU pixel upload into a bound PIXI.RenderTexture is not yet "
            "implemented (plan_pixijs.md PIXIJS-32's own doc comment) -- draw into this render "
            "target via SpriteBatch instead of Texture2D::SetData.");
    }

    void PixiJsRenderTargetRenderer::BindAsRenderTarget()
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_BindRenderTarget(id_);
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
        // BindAsRenderTarget() is idempotent/absolute, so switching directly from this target to
        // another (or back to the main stage via PixiJsRenderer::SetRenderTarget2D(nullptr)'s
        // CNA_PixiJs_UnbindRenderTarget()) never needs this target to clean up first.
    }
}
