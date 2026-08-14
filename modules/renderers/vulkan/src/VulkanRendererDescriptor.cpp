// plan_runtimerenderer.md RTR-P1-D04: the Vulkan family's pre-construction contract.
//
// Vulkan builds its surface from the SDL window, which must have been created with
// SDL_WINDOW_VULKAN.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Vulkan
{
    namespace
    {
        /// SDL refuses to build a Vulkan surface from a window that was not created with this flag.
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(SDL_WINDOW_VULKAN);
        }
    }

    /**
     * @brief The Vulkan family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Vulkan.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Vulkan,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Vulkan),
            .windowKind               = RendererWindowKind::Vulkan,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .prepareWindowFlags       = &PrepareWindowFlags,
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
        return Vulkan::GetDescriptor();
    }
}
