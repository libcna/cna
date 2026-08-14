// plan_runtimerenderer.md RTR-P1-D35: the Sokol family's pre-construction contract.
//
// sokol_gfx creates no window or context of its own -- this renderer calls SDL_GL_CreateContext on
// CNA's window (plan_sokol.md SOKOL-4, design decision 1). CNA_SOKOL_API stays a compile-time
// choice; the GL APIs are the only ones that context path can currently reach.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Sokol
{
    namespace
    {
        /// SDL refuses to attach a GL context to a window that was not created with this flag.
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);
        }
    }

    /**
     * @brief The Sokol family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Sokol.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Sokol,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Sokol),
            .windowKind               = RendererWindowKind::OpenGL,
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
        return Sokol::GetDescriptor();
    }
}
