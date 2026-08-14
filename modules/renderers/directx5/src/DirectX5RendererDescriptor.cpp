// plan_runtimerenderer.md RTR-P1-D24: the DirectX5 family's pre-construction contract.
//
// Real DirectDraw v4 + Direct3D v3 FVF DrawPrimitive.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::DirectX5
{
    /**
     * @brief The DirectX5 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX5.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX5,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX5),
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
        return DirectX5::GetDescriptor();
    }
}
