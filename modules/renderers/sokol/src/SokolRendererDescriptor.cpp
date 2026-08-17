// plan_runtimerenderer.md RTR-P1-D35: the Sokol family's pre-construction contract.
//
// sokol_gfx creates no window or context of its own -- this renderer calls the platform window request on
// CNA's window (plan_sokol.md SOKOL-4, design decision 1). CNA_SOKOL_API stays a compile-time
// choice; the GL APIs are the only ones that context path can currently reach.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


#include <cstdint>

namespace CNA::Internal::Renderers::Sokol
{
    /**
     * @brief Creates this family's renderer instance.
     *
     * Defined in the family's own renderer translation unit. Declared here because the descriptor
     * below takes its address, and because plan_runtimerenderer.md design decision 4 moved it out
     * of the shared CNA::Internal::Renderers namespace so that several renderer archives can link
     * into one binary.
     *
     * @param args Construction arguments, already populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    namespace
    {
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
            .glFramebuffer            = { .depthBits = 24, .stencilBits = 8, .doubleBuffered = true, .wantsMultiSample = true },
            .needsGlContext           = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

