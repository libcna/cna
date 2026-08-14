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

    namespace
    {
        /// Which of the five public GL identities this archive was compiled for.
        ///
        /// Deliberately derived from THIS TARGET's own CNA_GL_PROFILE_* define rather than from
        /// CNA::getCurrentGraphicsRendererType(). That accessor reads the project-wide renderer
        /// macros, which in a multi-renderer build name the BUILD DEFAULT -- so in a
        /// SDL_RENDERER;OPENGLES3 build this descriptor reported SDL_RENDERER, the registry held
        /// SDL_RENDERER twice, and OPENGLES3 was unreachable. Found by actually selecting each
        /// renderer in a five-renderer binary.
        [[nodiscard]] constexpr CNA::GraphicsRendererType ProfileIdentity()
        {
#if defined(CNA_GL_PROFILE_OPENGL33)
            return CNA::GraphicsRendererType::OpenGL33;
#elif defined(CNA_GL_PROFILE_WEBGL1)
            return CNA::GraphicsRendererType::WebGL1;
#elif defined(CNA_GL_PROFILE_WEBGL2)
            return CNA::GraphicsRendererType::WebGL2;
#elif defined(CNA_GL_PROFILE_OPENGLES2)
            return CNA::GraphicsRendererType::OpenGLES2;
#else // CNA_GL_PROFILE_OPENGLES3 -- the default within the EasyGL family
            return CNA::GraphicsRendererType::OpenGLES3;
#endif
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
            .type                     = ProfileIdentity(),
            .name                     = CNA::getGraphicsRendererName(ProfileIdentity()),
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

