// plan_runtimerenderer.md RTR-P1-D41: the Fna3d family's pre-construction contract.
//
// FNA3D selects its driver (SDL_GPU / Direct3D 11 / OpenGL) inside FNA3D_PrepareWindowAttributes
// and returns the SDL window flags that driver needs -- SDL_WINDOW_OPENGL for the GL driver, none
// for the others. That call must happen before SDL_CreateWindow, because it also primes the GL
// attributes the window's visual is chosen from. Same runtime-decides-the-flag shape as
// BGFX/LLGL/DILIGENT, except that FNA3D makes the decision itself rather than CNA re-deriving it.
//
// Only the window-flag header is included, deliberately not the renderer header: this keeps the
// descriptor free of FNA3D (and therefore of MojoShader) includes.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dWindowFlags.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers::Fna3d
{
    namespace
    {
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(Detail::PrepareWindowFlags());
        }
    }

    /**
     * @brief The Fna3d family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Fna3d.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Fna3d,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Fna3d),
            // FNA3D's driver is a runtime choice; OpenGL is the one that constrains the window.
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
        return Fna3d::GetDescriptor();
    }
}
