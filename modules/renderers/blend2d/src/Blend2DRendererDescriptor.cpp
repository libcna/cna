// plan_runtimerenderer.md RTR-P1-D18: the Blend2D family's pre-construction contract.
//
// Same "CPU raster + SDL presentation" shape SKIA established (plan_blend2d.md).

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Blend2D
{
    /**
     * @brief The Blend2D family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Blend2D.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Blend2D,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Blend2D),
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
        return Blend2D::GetDescriptor();
    }
}
