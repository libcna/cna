// plan_runtimerenderer.md RTR-P1-D28: the DirectX10 family's pre-construction contract.
//
// Real ID3D10Device, DXVK-delivered via d3d10core.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::DirectX10
{
    /**
     * @brief The DirectX10 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX10.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX10,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX10),
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
        return DirectX10::GetDescriptor();
    }
}
