// SPDX-License-Identifier: MS-PL
//
// The one bindable form of a sampled texture in the WebGPU renderer (REMED-GFX-167), shared by the
// classic renderer and the modern-resource records (plans/plan_webgpu_modern_graphics.md).

#pragma once

#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#elif __has_include(<webgpu-headers/webgpu.h>)
#include <webgpu-headers/webgpu.h>
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#else
#error "CNA WebGPU renderer requires webgpu.h from wgpu-native"
#endif

#include <memory>

namespace CNA::Internal::Renderers::WebGPU
{
    /**
     * @brief One native reference on a sampleable texture and its view (REMED-GFX-167). CNAEXT.
     *
     * This renderer records a whole frame and replays it later — a draw queued now is issued at
     * `SetRenderTarget()`'s flush or, for a backbuffer destination, not until `Present()`. The
     * public `Texture2D`/`RenderTarget2D` it sampled may be a short-lived local that is already
     * destroyed by then, and its renderer releases its `WGPUTexture`/`WGPUTextureView` in its own
     * destructor. Holding this object keeps both native handles valid for exactly as long as some
     * queued command can still bind them.
     *
     * `wgpuTextureRelease`/`wgpuTextureViewRelease` are refcount decrements, not the destructive
     * `wgpuTextureDestroy`, so an extra reference genuinely keeps the resource usable rather than
     * merely keeping a freed handle addressable. Both handles are referenced: a view alone would
     * rely on wgpu-native's internal parent reference, which this renderer does not need to assume.
     */
    class WebGPUSampledResourceEXT
    {
    public:
        /** @brief Takes one native reference on each of @p texture and @p view. */
        WebGPUSampledResourceEXT(WGPUTexture texture, WGPUTextureView view);
        /** @brief Releases the reference taken on each handle by the constructor. */
        ~WebGPUSampledResourceEXT();

        WebGPUSampledResourceEXT(const WebGPUSampledResourceEXT&) = delete;
        WebGPUSampledResourceEXT& operator=(const WebGPUSampledResourceEXT&) = delete;

        /** @brief The sampleable view this object keeps alive. */
        [[nodiscard]] WGPUTextureView View() const noexcept { return view_; }

    private:
        WGPUTexture texture_ = nullptr;
        WGPUTextureView view_ = nullptr;
    };

    /**
     * @brief The one bindable form of a sampled texture in this renderer (REMED-GFX-167). CNAEXT.
     *
     * Every deferred command stores this by value instead of a pointer to the resource's renderer
     * object. That closes both halves of the same defect at once: the view is resolved once, at
     * the public draw call, so replay never dereferences a wrapper that may since have been
     * destroyed; and @ref keepAlive holds the native handles open until the last command carrying
     * it is gone.
     *
     * Copying one costs a refcount increment; it allocates nothing (each samplable renderer builds
     * its @ref keepAlive once, at construction).
     */
    struct WebGPUSampledTextureEXT
    {
        /** @brief The already-resolved sampleable view, or null for an unbound slot. */
        WGPUTextureView view = nullptr;
        /** @brief Keeps @ref view valid while a queued command can still bind it. */
        std::shared_ptr<const WebGPUSampledResourceEXT> keepAlive;

        /** @brief Whether a texture was actually resolved (an absent optional slot yields false). */
        [[nodiscard]] explicit operator bool() const noexcept { return view != nullptr; }
        /** @brief The resolved view, for the bind-group entry that samples it. */
        [[nodiscard]] WGPUTextureView View() const noexcept { return view; }
    };
}
