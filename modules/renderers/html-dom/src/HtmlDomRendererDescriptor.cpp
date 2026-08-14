// plan_runtimerenderer.md RTR-P1-D15: the HtmlDom family's pre-construction contract.
//
// Emscripten-only. Renders into pooled CSS-transformed <div>s over SDL's canvas; no GPU flag.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::HtmlDom
{
    /**
     * @brief The HtmlDom family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::HtmlDom.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::HtmlDom,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::HtmlDom),
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
        return HtmlDom::GetDescriptor();
    }
}
