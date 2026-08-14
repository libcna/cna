// plan_runtimerenderer.md RTR-P1-D37: the Glide family's pre-construction contract.
//
// Glide 3.x is loaded dynamically from a caller-supplied glide3x.dll and takes the render window as
// a native handle.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Glide
{
    /**
     * @brief The Glide family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Glide.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Glide,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Glide),
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
        return Glide::GetDescriptor();
    }
}
