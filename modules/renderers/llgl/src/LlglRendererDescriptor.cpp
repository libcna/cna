// plans/plan_runtimerenderer.md RTR-P1-D39: the Llgl family's pre-construction contract.
//
// LLGL is itself an abstraction layer and picks its renderer module (OpenGL or Vulkan) at RUNTIME,
// so the window flag has to follow that decision rather than a compile-time renderer choice. Only
// the OpenGL module needs one: it creates a GL context on this very window, which therefore has to
// have been created with a visual that can carry one. LLGL's Vulkan module builds its surface from
// the native window handle alone and needs no graphics-API window intent -- which is just as well, since the platform refuses
// to create a window that is both.
//
// Only the selection header is included, deliberately not the renderer header: this keeps the
// descriptor free of LLGL (and therefore of Xlib) includes.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Llgl/LlglRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"


#include <cstdint>

namespace CNA::Internal::Renderers::Llgl
{
    /**
     * @brief Creates this family's renderer instance.
     *
     * Defined in the family's own renderer translation unit. Declared here because the descriptor
     * below takes its address, and because plans/plan_runtimerenderer.md design decision 4 moved it out
     * of the shared CNA::Internal::Renderers namespace so that several renderer archives can link
     * into one binary.
     *
     * @param args Construction arguments, already populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

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
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

