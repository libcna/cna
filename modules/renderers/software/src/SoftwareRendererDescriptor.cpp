// RTR-P1-D08: the Software family's pre-construction contract.
//
// A real CPU rasterizer that owns its own framebuffer (plans/plan_software.md design decision 4): no
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
     * below takes its address, and because docs/runtime-renderer-selection.md design decision 4 moved it out
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
    namespace
    {
        /// What SoftwareFramebuffer::SetMultiSampleCount applies: the CPU rasterizer has one
        /// multisample mode, a 2x2 grid, so four or more samples become 4 and fewer become 0.
        int ClampMultiSampleCount(int /*surfaceFormat*/, int requestedMultiSampleCount)
        {
            return requestedMultiSampleCount >= 4 ? 4 : 0;
        }
    }

    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Software,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Software),
            .windowKind               = RendererWindowKind::None,
            .needsWindow              = false,
            .needsVideoSubsystem      = false,
            // This family owns no swap chain, so a finished frame reaches a screen only through
            // IPlatformSurfacePresenter. Setting this is a statement about the renderer, not a
            // demand on the host: GraphicsDevice::createOrAttachWindow() builds a window and a
            // presenter only where the platform says the presenter is the ONLY route to the screen
            // (TERMINAL on a TTY). On a windowing platform, and with no presentation at all, this
            // family stays exactly as off-screen as it has always been -- `needsWindow` is still
            // false and nothing else in the descriptor changed.
            .needsSurfacePresenter    = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
            .adapterQueries           = {
                .clampMultiSampleCount = &ClampMultiSampleCount,
            },
        };
        return descriptor;
    }
}

