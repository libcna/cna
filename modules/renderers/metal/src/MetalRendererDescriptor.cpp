// plan_runtimerenderer.md RTR-P1-D40: the Metal family's pre-construction contract.
//
// Metal owns the renderer directly; SDL provides only the native macOS window and its CAMetalLayer.
// SDL_WINDOW_HIGH_PIXEL_DENSITY comes with it so the layer is sized in real backing pixels.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Metal
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
        /// SDL_WINDOW_METAL creates the CAMetalLayer this renderer draws into;
        /// SDL_WINDOW_HIGH_PIXEL_DENSITY sizes it in real backing pixels rather than points.
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            return static_cast<std::uint32_t>(SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        }
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
            .prepareWindowFlags       = &PrepareWindowFlags,
            .applyPreWindowAttributes = &NoPreWindowAttributes,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

