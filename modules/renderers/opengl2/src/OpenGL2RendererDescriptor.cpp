// plans/plan_runtimerenderer.md RTR-P1-D33: the OpenGL2 family's pre-construction contract.
//
// Native desktop OpenGL 2.1 (compatibility profile): creates its GL context on CNA's window, which
// the platform refuses unless the window carries the platform window intent.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


#include <cstdint>

namespace CNA::Internal::Renderers::OpenGL2
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

    namespace
    {
    }

    /**
     * @brief The OpenGL2 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::OpenGL2.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::OpenGL2,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::OpenGL2),
            .windowKind               = RendererWindowKind::OpenGL,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .glFramebuffer            = { .depthBits = 24, .stencilBits = 8, .doubleBuffered = true },
            .needsGlContext           = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

