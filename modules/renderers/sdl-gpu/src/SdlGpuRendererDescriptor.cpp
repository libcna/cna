// plan_runtimerenderer.md RTR-P1-D29: the SdlGpu family's pre-construction contract.
//
// SDL_GPU claims the window through SDL_ClaimWindowForGPUDevice, which needs no creation-time flag.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::SdlGpu
{
    /**
     * @brief The SdlGpu family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::SdlGpu.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::SdlGpu,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::SdlGpu),
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
        return SdlGpu::GetDescriptor();
    }
}
