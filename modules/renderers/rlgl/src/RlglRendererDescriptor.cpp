// SPDX-License-Identifier: MS-PL

// plans/plan_rlgl.md RLGL-005: RLGL consumes a CNA-owned desktop OpenGL context. The renderer
// implementation does not create a raylib window or initialize any other raylib subsystem.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

namespace CNA::Internal::Renderers::Rlgl
{
    /**
     * @brief Creates the RLGL renderer instance.
     *
     * @param args Construction arguments populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    /**
     * @brief Returns the RLGL renderer's pre-construction contract.
     *
     * @return The descriptor for GraphicsRendererType::Rlgl.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                = CNA::GraphicsRendererType::Rlgl,
            .name                = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Rlgl),
            .windowKind          = RendererWindowKind::OpenGL,
            .needsWindow         = true,
            .needsVideoSubsystem = true,
            .glFramebuffer       = {
                .depthBits = 24,
                .stencilBits = 8,
                .doubleBuffered = true,
                .wantsMultiSample = true,
            },
            .needsGlContext      = true,
            .isAvailable         = &AlwaysAvailable,
            .create              = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}
