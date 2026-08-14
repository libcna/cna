#pragma once

// plan_runtimerenderer.md RTR-P1: the hooks most renderer families have nothing to say about.
//
// GraphicsRendererDescriptor requires every function pointer to be non-null, so that no call site
// needs a null check (see that header's own note). These are the shared implementations a family
// uses when a hook does not apply to it -- one definition each, rather than 42 identical lambdas.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers
{
    /**
     * @brief A prepareWindowFlags() hook for a renderer that needs no window at all.
     *
     * Used by HEADLESS, SOFTWARE, STUB and PORTABLEGL, whose descriptors also set
     * needsWindow = false -- so GraphicsDevice never calls this. It exists to keep the pointer
     * non-null rather than to be invoked.
     *
     * @return 0.
     */
    [[nodiscard]] inline std::uint32_t NoWindowFlags()
    {
        return 0;
    }

    /**
     * @brief A prepareWindowFlags() hook for a renderer that wants an ordinary window.
     *
     * The renderer needs a real window but no graphics-API flag on it: SDL_RENDERER, the whole
     * DirectX family, GDI, Direct2D, Skia, Blend2D and the browser-DOM renderers.
     *
     * @return 0 -- GraphicsDevice supplies SDL_WINDOW_RESIZABLE itself.
     */
    [[nodiscard]] inline std::uint32_t PlainWindowFlags()
    {
        return 0;
    }

    /**
     * @brief An applyPreWindowAttributes() hook for a renderer with no pre-window requirements.
     *
     * Every family except OPENGL1 uses this. OPENGL1 is the exception because GLX fixes its
     * window's visual -- and therefore its depth/stencil/multisample bits -- at SDL_CreateWindow()
     * time, so those attributes cannot be requested from the renderer's own constructor.
     *
     * @param request Ignored.
     */
    inline void NoPreWindowAttributes(const RendererPreWindowRequest& request)
    {
        (void)request;
    }

    /**
     * @brief An isAvailable() hook for a renderer with no meaningful cheap probe.
     *
     * Returning true is not a promise that construction will succeed -- it states only that there
     * is no cheap, side-effect-free way to know otherwise, which is the honest answer for most
     * families. A renderer that CAN be probed (a loadable driver library, an enumerable device)
     * should implement a real probe instead of reusing this.
     *
     * @return true.
     */
    [[nodiscard]] inline bool AlwaysAvailable()
    {
        return true;
    }
}
