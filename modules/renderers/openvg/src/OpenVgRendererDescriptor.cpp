// plan_runtimerenderer.md RTR-P1-D42: the OpenVg family's pre-construction contract.
//
// OpenVG 1.1 via ShivaVG, which runs on top of a real desktop OpenGL context this renderer creates
// itself through SDL -- same "own GL context, no EasyGL" shape as OPENGL1/OPENGL2/OPENGL4.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::OpenVg
{
    namespace
    {
        /// SDL refuses to attach a GL context to a window that was not created with this flag.
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);
        }
    }

    /**
     * @brief The OpenVg family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::OpenVg.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::OpenVg,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::OpenVg),
            .windowKind               = RendererWindowKind::OpenGL,
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
        return OpenVg::GetDescriptor();
    }
}
