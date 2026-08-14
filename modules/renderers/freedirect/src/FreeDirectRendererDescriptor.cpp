// plan_runtimerenderer.md RTR-P1-D19: the FreeDirect family's pre-construction contract.
//
// DirectDraw semantics via the ../free-direct sibling reimplementation, which presents through SDL
// itself.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::FreeDirect
{
    /**
     * @brief The FreeDirect family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::FreeDirect.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::FreeDirect,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::FreeDirect),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .prepareWindowFlags       = &PlainWindowFlags,
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
        return FreeDirect::GetDescriptor();
    }
}
