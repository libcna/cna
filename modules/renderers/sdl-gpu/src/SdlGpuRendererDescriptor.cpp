// RTR-P1-D29: the SdlGpu family's pre-construction contract.
//
// SDLGPU claims the window through SDL_ClaimWindowForGPUDevice, which needs no creation-time flag.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::SdlGpu
{
    /**
     * @brief Creates this family's renderer instance.
     *
     * Defined in the family's own renderer translation unit. Declared here because the descriptor
     * below takes its address, and because docs/runtime-renderer-selection.md design decision 4 moved it out
     * of the shared CNA::Internal::Renderers namespace so that several renderer archives can link
     * into one binary.
     *
     * @param args Construction arguments, already populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    /**
     * @brief The SdlGpu family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::SdlGpu.
     */
    namespace
    {
        /// The candidate SdlGpuRenderer::ClampSampleCount starts from: the largest of 2, 4 and 8
        /// not above the request. The device may still step down when a format lacks that count;
        /// 8 is what every Vulkan device this renderer has run on supports.
        int ClampMultiSampleCount(int /*surfaceFormat*/, int requestedMultiSampleCount)
        {
            if (requestedMultiSampleCount >= 8) return 8;
            if (requestedMultiSampleCount >= 4) return 4;
            if (requestedMultiSampleCount >= 2) return 2;
            return 0;
        }
    }

    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::SdlGpu,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::SdlGpu),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
            .adapterQueries           = {
                .clampMultiSampleCount = &ClampMultiSampleCount,
            },
        };
        return descriptor;
    }
}

