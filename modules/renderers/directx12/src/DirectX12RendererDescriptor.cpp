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
        return DirectX12::GetDescriptor();
    }
}
