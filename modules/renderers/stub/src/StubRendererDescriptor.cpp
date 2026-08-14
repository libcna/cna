// plan_runtimerenderer.md RTR-P1-D09: the Stub family's pre-construction contract.
//
// Deliberately minimal no-op renderer (plan_stub.md design decision 1): renders nothing, touches no
// SDL window or video subsystem, keeps no bookkeeping of any kind.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Stub
{
    /**
     * @brief The Stub family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Stub.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Stub,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Stub),
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
        return Stub::GetDescriptor();
    }
}
