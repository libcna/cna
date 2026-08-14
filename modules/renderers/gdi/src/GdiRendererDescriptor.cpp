// plan_runtimerenderer.md RTR-P1-D38: the Gdi family's pre-construction contract.
//
// Classic Win32 GDI blits into the window's own DC -- an ordinary window, no graphics-API flag.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Gdi
{
    /**
     * @brief The Gdi family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Gdi.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Gdi,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Gdi),
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
        return Gdi::GetDescriptor();
    }
}
