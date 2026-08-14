// plan_runtimerenderer.md RTR-P1-D02: the EasyGL family's pre-construction contract.
//
// EasyGL is the one family serving MORE than one public identity: OPENGLES2, OPENGLES3, OPENGL33
// (desktop/mobile) and WEBGL1, WEBGL2 (Emscripten) all compile into this same archive, told apart
// by the CNA_GL_PROFILE_* compile definition (plan_glbackends.md). getCurrentGraphicsRendererType()
// already resolves that to the right one of the five, so the descriptor's identity follows it
// rather than being hardcoded here. Making the profile itself a RUNTIME choice -- which is what
// would let two GL profiles coexist in one binary -- is plan_runtimerenderer.md phase P11, not
// this task.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::EasyGL
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

    namespace
    {
        /// EasyGL creates its GL context on the window CNA created, and SDL refuses to attach one
        /// to a window that was not created with SDL_WINDOW_OPENGL.
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);
        }
    }

    /**
     * @brief The EasyGL family's descriptor.
     *
     * @return A descriptor whose identity is whichever of the five GL profiles this build selected.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::getCurrentGraphicsRendererType(),
            .name                     = CNA::getCurrentGraphicsRendererName(),
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

