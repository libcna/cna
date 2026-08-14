// plan_runtimerenderer.md RTR-P1-D17: the Skia family's pre-construction contract.
//
// CPU raster presented through a streaming SDL_Renderer texture -- no GPU context on the window
// itself.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Skia
{
    /**
     * @brief The Skia family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Skia.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Skia,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Skia),
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
        return Skia::GetDescriptor();
    }
}
