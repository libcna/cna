// plan_runtimerenderer.md design decision 2 / plan_pixijs.md Design decision 2: the PixiJs
// family's pre-construction contract.
//
// Emscripten-only. The browser platform already creates and sizes the <canvas>; this renderer
// builds its PIXI.Application against that element and needs no graphics-API window flag, exactly
// like the CANVAS and HTML_DOM families it sits beside.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::PixiJs
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
     * @brief The PixiJs family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::PixiJs.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::PixiJs,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::PixiJs),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}
