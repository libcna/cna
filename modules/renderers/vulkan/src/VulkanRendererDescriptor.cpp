// plans/plan_runtimerenderer.md RTR-P1-D04: the Vulkan family's pre-construction contract.
//
// Vulkan builds its surface from the platform window, which must have been created with
// the platform window intent.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"


#include <cstdint>

namespace CNA::Internal::Renderers::Vulkan
{
    /**
     * @brief Creates this family's renderer instance.
     *
     * Defined in the family's own renderer translation unit. Declared here because the descriptor
     * below takes its address, and because plans/plan_runtimerenderer.md design decision 4 moved it out
     * of the shared CNA::Internal::Renderers namespace so that several renderer archives can link
     * into one binary.
     *
     * @param args Construction arguments, already populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    namespace
    {
        /// the platform refuses to build a Vulkan surface from a window that was not created with this flag.
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
            .needsVulkanSurface       = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
            // plan_vulkan.md VULKAN-187 (F-34): GraphicsAdapter::QueryRenderTargetFormat runs
            // before any GraphicsDevice exists and, with no hook, reported 0 samples on a device
            // with 4x or 8x. Only the MSAA clamp is registered: the shared format table already
            // agrees with this renderer's own verdict (Vulkan_AdapterQueryContract leg B), and a
            // second answer where the first is right is a way to make them disagree later.
            .adapterQueries           = RendererAdapterQueries{
                .clampMultiSampleCount = &VulkanRenderer::ClampAdapterMultiSampleCountEXT,
            },
        };
        return descriptor;
    }
}

