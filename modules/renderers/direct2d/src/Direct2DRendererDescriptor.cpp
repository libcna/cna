// plan_runtimerenderer.md RTR-P1-D13: the Direct2D family's pre-construction contract.
//
// Direct2D binds to the window's HWND; no SDL graphics-API flag applies.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Direct2D
{
    /**
     * @brief The Direct2D family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Direct2D.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Direct2D,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Direct2D),
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
        return Direct2D::GetDescriptor();
    }
}
