// plan_runtimerenderer.md RTR-P1-D08: the Software family's pre-construction contract.
//
// A real CPU rasterizer that owns its own framebuffer (plan_software.md design decision 4): no
// window, no GPU library, no SDL video subsystem.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Software
{
    /**
     * @brief The Software family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Software.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Software,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Software),
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
        return Software::GetDescriptor();
    }
}
