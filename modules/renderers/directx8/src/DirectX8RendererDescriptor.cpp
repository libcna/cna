// plan_runtimerenderer.md RTR-P1-D27: the DirectX8 family's pre-construction contract.
//
// Real Direct3D 8, DXVK-delivered: one CreateDevice call makes both device and swap chain.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::DirectX8
{
    /**
     * @brief The DirectX8 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX8.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX8,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX8),
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
        return DirectX8::GetDescriptor();
    }
}
