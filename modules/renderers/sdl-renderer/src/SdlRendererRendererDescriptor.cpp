// plan_runtimerenderer.md RTR-P1-D01: the SdlRenderer family's pre-construction contract.
//
// SDL_Renderer picks its own backend behind SDL's API and needs no graphics-API flag on the window.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::SdlRenderer
{
    /**
     * @brief The SdlRenderer family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::SdlRenderer.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::SdlRenderer,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::SdlRenderer),
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
        return SdlRenderer::GetDescriptor();
    }
}
