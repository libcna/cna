// plan_runtimerenderer.md RTR-P1-D08: the Software family's pre-construction contract.
//
// A real CPU rasterizer that owns its own framebuffer (plan_software.md design decision 4): no
// window, no GPU library, no platform video subsystem.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Software
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
     * @brief The Software family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Software.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Software,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Software),
            .windowKind               = RendererWindowKind::None,
            .needsWindow              = false,
            .needsVideoSubsystem      = false,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

