// plan_runtimerenderer.md RTR-P1-D34: the Wicked family's pre-construction contract.
//
// Wicked Engine's RHI creates its own Vulkan (or D3D12) device from the native window handle, like
// LLGL's Vulkan module -- no graphics-API window intent, which is just as well since the platform refuses a window that is
// both OpenGL and Vulkan.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Wicked
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
     * @brief The Wicked family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Wicked.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Wicked,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Wicked),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

