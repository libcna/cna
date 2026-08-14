// plan_runtimerenderer.md RTR-P1-D14: the Canvas family's pre-construction contract.
//
// Emscripten-only. SDL3's Emscripten video driver already creates and sizes the <canvas>; this
// renderer takes its 2D context from it and needs no graphics-API flag.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Canvas
{
    /**
     * @brief The Canvas family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Canvas.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Canvas,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Canvas),
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
        return Canvas::GetDescriptor();
    }
}
