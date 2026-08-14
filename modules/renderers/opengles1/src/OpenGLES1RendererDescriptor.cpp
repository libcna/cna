// plan_runtimerenderer.md RTR-P1-D30: the OpenGLES1 family's pre-construction contract.
//
// OpenGL ES 1.1 fixed-function "Common" profile against a real system GLESv1_CM library
// (plan_opengles1.md design decision 1). SDL still needs SDL_WINDOW_OPENGL to attach the context.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::OpenGLES1
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
    }

    /**
     * @brief The OpenGLES1 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::OpenGLES1.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::OpenGLES1,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::OpenGLES1),
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

