// plan_runtimerenderer.md RTR-P1-D10: the PortableGL family's pre-construction contract.
//
// PortableGL (rswinkle/PortableGL) is a CPU software OpenGL 3.x implementation writing into its own
// buffer -- the same "no window, no GPU library, no SDL video subsystem" shape as
// HEADLESS/SOFTWARE/STUB, despite the OpenGL name.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::PortableGL
{
    /**
     * @brief The PortableGL family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::PortableGL.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::PortableGL,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::PortableGL),
            .windowKind               = RendererWindowKind::None,
            .needsWindow              = false,
            .needsVideoSubsystem      = false,
            .prepareWindowFlags       = &NoWindowFlags,
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
        return PortableGL::GetDescriptor();
    }
}
