// SPDX-License-Identifier: MS-PL
// plans/plan_runtimerenderer.md RTR-P1-D32: the OpenGL1 family's pre-construction contract.
//
// OPENGL1 is the ONE family that needs a non-default framebuffer fixed before its window exists.
// It requests a legacy/compatibility (non-ES) GL context, which on X11 goes through GLX rather
// than EasyGL's EGL path (requesting an ES context profile steers the platform to EGL, where the
// framebuffer config can still be chosen at context-creation time). GLX fixes the window's X
// visual -- and therefore its depth/stencil buffer bits -- when the window is created, so setting
// those attributes afterward (as OpenGL1Renderer's own constructor also does, for
// self-containment) is too late and silently produces a 0-bit stencil buffer, making every
// DepthStencilState::StencilEnable a permanent no-op. Confirmed empirically before this code moved
// here: GL_STENCIL_BITS read back 0 without it and 8 with it. The same constraint applies to the
// multisample attributes, hence wantsMultiSample.
//
// MERGE (plans/plan_platform.md PLAT-8): this was an applyPreWindowAttributes hook calling the windowing
// library directly. It is now descriptor data that GraphicsDevice hands to the platform contract;
// the finding above is unchanged, only the mechanism is.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers::OpenGL1
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

    /**
     * @brief The OpenGL1 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::OpenGL1.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::OpenGL1,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::OpenGL1),
            .windowKind               = RendererWindowKind::OpenGL,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .glFramebuffer            = { .depthBits = 24, .stencilBits = 8, .doubleBuffered = true, .wantsMultiSample = true },
            .needsGlContext           = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

