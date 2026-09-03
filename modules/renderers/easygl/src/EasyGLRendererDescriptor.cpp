// plans/plan_runtimerenderer.md RTR-P1-D02: the EasyGL family's pre-construction contract.
//
// EasyGL is the one family serving MORE than one public identity: OPENGLES2, OPENGLES3, OPENGL33
// (desktop/mobile) and WEBGL1, WEBGL2 (Emscripten) all compile into this same archive, told apart
// by the CNA_GL_PROFILE_* compile definition (plans/plan_glbackends.md). getCurrentGraphicsRendererType()
// already resolves that to the right one of the five, so the descriptor's identity follows it
// rather than being hardcoded here. Making the profile itself a RUNTIME choice -- which is what
// would let two GL profiles coexist in one binary -- is plans/plan_runtimerenderer.md phase P11, not
// this task.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/EasyGL/GlProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"


#include <cstdint>

namespace CNA::Internal::Renderers::EasyGL
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
     * @brief Creates an EasyGL renderer for a specific GL profile.
     *
     * plans/plan_runtimerenderer.md P11. Defined in the family's renderer translation unit alongside
     * CreateGraphicsRenderer, which is this with the build's default profile.
     *
     * @param args Construction arguments.
     * @param profile Which of the five GL profiles to create.
     * @return The new renderer.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRendererForProfile(
        const GraphicsRendererCreateArgs& args, GlProfile profile);

    namespace
    {
        bool IsGuaranteedGlesRenderTargetFormat(
            [[maybe_unused]] const int graphicsProfile,
            const int surfaceFormat)
        {
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

            // An adapter query runs before there is a context whose extensions can be probed.
            // RGBA8 Color is the only format in XNA's preferred render-target set that every
            // EasyGL GLES/Web profile can promise. In particular, RGBA16 UNORM (Rgba64) needs
            // EXT_texture_norm16 and is absent on conforming GLES 3/WebGL 2 implementations.
            return surfaceFormat == static_cast<int>(SurfaceFormat::Color);
        }

        template <CNA::GraphicsRendererType Identity>
        RendererAdapterQueries AdapterQueriesFor()
        {
            if constexpr (Identity == CNA::GraphicsRendererType::OpenGL33)
                return {};

            return {
                .isRenderTargetFormatSupported =
                    &IsGuaranteedGlesRenderTargetFormat,
            };
        }

        /// plans/plan_runtimerenderer.md P11: one descriptor per public GL identity, all served by the
        /// same EasyGL archive. Each pins its own GlProfile, which the renderer publishes as the
        /// thread's active profile on construction -- that is what lets several of the five be
        /// compiled in at once, where before the choice was a compile definition.
        template <CNA::GraphicsRendererType Identity>
        const GraphicsRendererDescriptor& DescriptorFor()
        {
            static const GraphicsRendererDescriptor descriptor{
                .type                     = Identity,
                .name                     = CNA::getGraphicsRendererName(Identity),
                .windowKind               = RendererWindowKind::OpenGL,
                .needsWindow              = true,
                .needsVideoSubsystem      = true,
                .needsGlContext           = true,
                .isAvailable              = &AlwaysAvailable,
                .create                   = +[](const GraphicsRendererCreateArgs& args)
                    -> std::unique_ptr<IGraphicsRenderer>
                {
                    return CreateGraphicsRendererForProfile(args, ToGlProfile(Identity));
                },
                .adapterQueries           = AdapterQueriesFor<Identity>(),
            };
            return descriptor;
        }
    }

    /** @brief Descriptor for the OPENGLES2 identity (native GLES 2.0 context, GLSL ES 1.00). */
    const GraphicsRendererDescriptor& GetDescriptorOpenGLES2()
    {
        return DescriptorFor<CNA::GraphicsRendererType::OpenGLES2>();
    }

    /** @brief Descriptor for the OPENGLES3 identity (native GLES 3.0 context, GLSL ES 3.00). */
    const GraphicsRendererDescriptor& GetDescriptorOpenGLES3()
    {
        return DescriptorFor<CNA::GraphicsRendererType::OpenGLES3>();
    }

    /** @brief Descriptor for the OPENGL33 identity (desktop GL 3.3 core, GLSL 3.30). */
    const GraphicsRendererDescriptor& GetDescriptorOpenGL33()
    {
        return DescriptorFor<CNA::GraphicsRendererType::OpenGL33>();
    }

    /** @brief Descriptor for the WEBGL1 identity (browser WebGL 1, GLSL ES 1.00). */
    const GraphicsRendererDescriptor& GetDescriptorWebGL1()
    {
        return DescriptorFor<CNA::GraphicsRendererType::WebGL1>();
    }

    /** @brief Descriptor for the WEBGL2 identity (browser WebGL 2, GLSL ES 3.00). */
    const GraphicsRendererDescriptor& GetDescriptorWebGL2()
    {
        return DescriptorFor<CNA::GraphicsRendererType::WebGL2>();
    }
}
