// plans/plan_runtimerenderer.md RTR-P1-D41: the Fna3d family's pre-construction contract.
//
// FNA3D selects its driver (SDL_GPU / Direct3D 11 / OpenGL) inside FNA3D_PrepareWindowAttributes
// and returns the window attributes that driver needs -- the platform window intent for the GL driver, none
// for the others. That call must happen before the window is created, because it also primes the GL
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
            .glFramebuffer            = { .depthBits = 24, .stencilBits = 8, .doubleBuffered = true },
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

