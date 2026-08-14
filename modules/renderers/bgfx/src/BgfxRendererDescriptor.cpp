// plan_runtimerenderer.md RTR-P1-D03: the Bgfx family's pre-construction contract.
//
// BGFX is one of the four families that genuinely decide their window flags at RUNTIME: bgfx picks
// its own native renderer (Vulkan, OpenGL, OpenGL ES, ...) and CNA has to create a window that
// matches whatever it will pick. That decision was already runtime before this file existed --
// GraphicsDevice.cpp called the same ResolveRendererType() through an #ifdef. Only the call site
// moved.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Bgfx
{
    namespace
    {
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            const auto rendererType = Detail::ResolveRendererType(SDL_getenv("CNA_BGFX_RENDERER"));
            switch (rendererType)
            {
            case bgfx::RendererType::Vulkan:
                return static_cast<std::uint32_t>(SDL_WINDOW_VULKAN);

            case bgfx::RendererType::OpenGL:
            case bgfx::RendererType::OpenGLES:
            case bgfx::RendererType::Count:
                return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);

            default:
                return 0;
            }
        }
    }

    /**
     * @brief The Bgfx family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Bgfx.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Bgfx,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Bgfx),
            // bgfx's own native API is a runtime choice, so no single RendererWindowKind describes
            // it. Vulkan is the kind its default preference order resolves to; a fallback crossing
            // this boundary is refused rather than guessed (design decision 8).
            .windowKind               = RendererWindowKind::Vulkan,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .prepareWindowFlags       = &PrepareWindowFlags,
            .applyPreWindowAttributes = &NoPreWindowAttributes,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

namespace CNA::Internal::Renderers
{
    const GraphicsRendererDescriptor& ActiveDescriptor()
    {
        return Bgfx::GetDescriptor();
    }
}
