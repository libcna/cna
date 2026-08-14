// plan_runtimerenderer.md RTR-P1-D25: the DirectX6 family's pre-construction contract.
//
// Real DirectDraw v4 + Direct3D v3, with real stencil buffer operations.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::DirectX6
{
    /**
     * @brief The DirectX6 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX6.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX6,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX6),
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
        return DirectX6::GetDescriptor();
    }
}
