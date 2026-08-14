// plan_runtimerenderer.md RTR-P1-D16: the SvgDom family's pre-construction contract.
//
// Emscripten-only. Renders into real SVG DOM elements over SDL's canvas (plan_svg_dom.md design
// decision 1); no GPU flag.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::SvgDom
{
    /**
     * @brief The SvgDom family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::SvgDom.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::SvgDom,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::SvgDom),
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
        return SvgDom::GetDescriptor();
    }
}
