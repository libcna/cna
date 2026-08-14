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
     * @brief Creates this family's renderer instance.
     *
     * Defined in the family's own renderer translation unit. Declared here because the descriptor
     * below takes its address, and because plan_runtimerenderer.md design decision 4 moved it out
     * of the shared CNA::Internal::Renderers namespace so that several renderer archives can link
     * into one binary.
     *
     * @param args Construction arguments, already populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

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

