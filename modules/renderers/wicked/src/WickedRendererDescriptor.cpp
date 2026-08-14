// plan_runtimerenderer.md RTR-P1-D34: the Wicked family's pre-construction contract.
//
// Wicked Engine's RHI creates its own Vulkan (or D3D12) device from the native window handle, like
// LLGL's Vulkan module -- no SDL flag, which is just as well since SDL refuses a window that is
// both OpenGL and Vulkan.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::Wicked
{
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
        return Wicked::GetDescriptor();
    }
}
