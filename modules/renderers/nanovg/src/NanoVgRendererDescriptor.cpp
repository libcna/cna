// plan_runtimerenderer.md RTR-P1-D42-style pre-construction contract for the NanoVg family.
//
// NanoVG's GL2 backend runs on top of a real desktop OpenGL context this renderer creates itself
// through the platform -- same "own GL context, no EasyGL" shape as OPENGL1/OPENGL2/OPENVG.
// NVG_STENCIL_STROKES (the flag this renderer's constructor passes to nvgCreateGL2) needs a real
// stencil plane at window-creation time -- GLX fixes the window's visual (and therefore its
// stencil bits) before any renderer object exists, so this is requested here, not in the renderer
// constructor (see GraphicsRendererDescriptor::glFramebuffer's own doc comment for why).

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers::NanoVg
{
    /**
     * @brief Creates this family's renderer instance.
     *
     * Defined in the family's own renderer translation unit; declared here because the
     * descriptor below takes its address.
     *
     * @param args Construction arguments, already populated by GraphicsDevice.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    /**
     * @brief The NanoVg family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::NanoVg.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::NanoVg,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::NanoVg),
            .windowKind               = RendererWindowKind::OpenGL,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .glFramebuffer            = { .stencilBits = 8, .doubleBuffered = true },
            .needsGlContext           = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}
