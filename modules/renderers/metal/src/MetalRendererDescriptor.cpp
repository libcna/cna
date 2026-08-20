// plans/plan_runtimerenderer.md RTR-P1-D40: the Metal family's pre-construction contract.
//
// Metal owns the renderer directly; the platform provides only the native macOS window and its CAMetalLayer.
// a high-density backing request comes with it so the layer is sized in real backing pixels.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"


#include <cstdint>

namespace CNA::Internal::Renderers::Metal
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
        /// a Metal window intent creates the CAMetalLayer this renderer draws into;
        /// the high-density request sizes it in real backing pixels rather than points.
    }

    /**
     * @brief The Metal family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Metal.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Metal,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Metal),
            .windowKind               = RendererWindowKind::Metal,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .wantsHighDpi             = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

