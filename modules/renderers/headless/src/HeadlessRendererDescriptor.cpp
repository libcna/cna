// plan_runtimerenderer.md RTR-P1-D07: the Headless family's pre-construction contract.
//
// The Headless renderer never creates a window and never touches SDL's video subsystem at all
// (plan_headless.md design decision 2), which is what lets it run in a CI container with no display
// server present -- not merely without a visible window.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Headless
{
    /**
     * @brief The Headless family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Headless.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Headless,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Headless),
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
        return Headless::GetDescriptor();
    }
}
