// RTR-P1-D05: the WebGPU family's pre-construction contract.
//
// wgpu-native builds its surface from the native window handle, so no graphics-API window intent is
// required (docs/webgpu-renderer.md).

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::WebGPU
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
     * @brief The WebGPU family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::WebGPU.
     */
    namespace
    {
        /// What WebGPURenderer::PickSampleCount applies: WebGPU guarantees exactly 1 and 4
        /// samples for every renderable format, so a request of four or more is 4 and anything
        /// smaller is single-sampled (reported as XNA's 0).
        int ClampMultiSampleCount(int /*surfaceFormat*/, int requestedMultiSampleCount)
        {
            return requestedMultiSampleCount >= 4 ? 4 : 0;
        }
    }

    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::WebGPU,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::WebGPU),
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

