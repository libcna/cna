// plan_runtimerenderer.md RTR-P1-D12: the DirectX12 family's pre-construction contract.
//
// Direct3D 12 creates its swap chain from the window's HWND. It is also the one renderer that can
// genuinely run without one -- PresentationParameters::HeadlessEXT is that runtime opt-in, handled
// by GraphicsDevice rather than by this flag, which is why needsWindow stays true here.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


namespace CNA::Internal::Renderers::DirectX12
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
     * @brief The DirectX12 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX12.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX12,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX12),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

