// plan_runtimerenderer.md RTR-P1-D32: the OpenGL1 family's pre-construction contract.
//
// OPENGL1 is the ONE family with real work in applyPreWindowAttributes. It requests a
// legacy/compatibility (non-ES) GL context, which on X11 goes through GLX rather than EasyGL's EGL
// path (SDL_GL_CONTEXT_PROFILE_MASK=ES steers SDL to EGL, where the framebuffer config can still be
// chosen at SDL_GL_CreateContext() time). GLX fixes the window's X visual -- and therefore its
// depth/stencil buffer bits -- at SDL_CreateWindow() time, so setting SDL_GL_STENCIL_SIZE afterward
// (as OpenGL1Renderer's own constructor also does, for self-containment) is too late and silently
// produces a 0-bit stencil buffer, making every DepthStencilState.StencilEnable a permanent no-op.
// Confirmed empirically before this code moved here: GL_STENCIL_BITS read back 0 without it and 8
// with it.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::OpenGL1
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
        /// SDL refuses to attach a GL context to a window that was not created with this flag.
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);
        }

        /// Must run before SDL_CreateWindow -- see this file's own header comment for why.
        void ApplyPreWindowAttributes(const RendererPreWindowRequest& request)
        {
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

            // plan_opengl1.md item 22 (EasyGL parity): the same GLX-visual-fixed-at-window-creation
            // constraint applies to the multisample attributes. Requested here or not at all --
            // OpenGL1Renderer's own constructor runs after the window already exists and can never
            // recover a visual that was fixed without a multisample buffer.
            if (request.multiSampleCount > 1)
            {
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
                SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, request.multiSampleCount);
            }
        }
    }

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
            .prepareWindowFlags       = &PrepareWindowFlags,
            .applyPreWindowAttributes = &ApplyPreWindowAttributes,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

