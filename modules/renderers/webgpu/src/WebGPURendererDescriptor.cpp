// plan_runtimerenderer.md RTR-P1-D05: the WebGPU family's pre-construction contract.
//
// wgpu-native builds its surface from the native window handle, so no SDL graphics-API flag is
// required (plan_webgpu.md).

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::WebGPU
{
    /**
     * @brief The WebGPU family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::WebGPU.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::WebGPU,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::WebGPU),
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
        return WebGPU::GetDescriptor();
    }
}
