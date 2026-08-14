// plan_runtimerenderer.md RTR-P1-D39: the Llgl family's pre-construction contract.
//
// LLGL is itself an abstraction layer and picks its renderer module (OpenGL or Vulkan) at RUNTIME,
// so the window flag has to follow that decision rather than a compile-time renderer choice. Only
// the OpenGL module needs one: it creates a GL context on this very window, which therefore has to
// have been created with a visual that can carry one. LLGL's Vulkan module builds its surface from
// the native window handle alone and needs no SDL flag -- which is just as well, since SDL refuses
// to create a window that is both.
//
// Only the selection header is included, deliberately not the renderer header: this keeps the
// descriptor free of LLGL (and therefore of Xlib) includes.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Llgl/LlglRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Llgl
{
    namespace
    {
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            if (Detail::RendererModuleNeedsOpenGLWindow(Detail::ResolveRendererModule()))
            {
                return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);
            }
            return 0;
        }
    }

    /**
     * @brief The Llgl family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Llgl.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Llgl,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Llgl),
            // LLGL's module is a runtime choice; OpenGL is the one that constrains the window, so
            // that is the kind recorded here. Its Vulkan module needs no flag at all.
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
        return Llgl::GetDescriptor();
    }
}
