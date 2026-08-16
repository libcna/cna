// plan_runtimerenderer.md RTR-P1-D09: the Stub family's pre-construction contract.
//
// Deliberately minimal no-op renderer (plan_stub.md design decision 1): renders nothing, touches no
// platform window or video subsystem, keeps no bookkeeping of any kind.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Stub
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
     * @brief The Stub family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Stub.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Stub,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Stub),
            .windowKind               = RendererWindowKind::None,
            .needsWindow              = false,
            .needsVideoSubsystem      = false,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

