// plan_runtimerenderer.md RTR-P1-D03: the Bgfx family's pre-construction contract.
//
// BGFX is one of the four families that genuinely decide their window flags at RUNTIME: bgfx picks
// its own native renderer (Vulkan, OpenGL, OpenGL ES, ...) and CNA has to create a window that
// matches whatever it will pick. That decision was already runtime before this file existed --
// GraphicsDevice.cpp called the same ResolveRendererType() through an #ifdef. Only the call site
// moved.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Internal::Renderers::Bgfx
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
        /// plan_runtimerenderer.md RTR-P10-9: the window kind bgfx's OWN runtime choice implies.
        ///
        /// This renderer has two levels of selection above it: CNA picks BGFX, and bgfx then picks
        /// its native API. Both the descriptor's windowKind and the SDL window flags have to follow
        /// the SECOND one, and they have to agree with each other -- they are two statements about
        /// the same window.
        [[nodiscard]] RendererWindowKind ResolvedWindowKind()
        {
            switch (Detail::ResolveRendererType(SDL_getenv("CNA_BGFX_RENDERER")))
            {
            case bgfx::RendererType::Vulkan:
                return RendererWindowKind::Vulkan;

            case bgfx::RendererType::OpenGL:
            case bgfx::RendererType::OpenGLES:
            case bgfx::RendererType::Count:
                return RendererWindowKind::OpenGL;

            default:
                return RendererWindowKind::Plain;
            }
        }

        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            switch (ResolvedWindowKind())
            {
            case RendererWindowKind::Vulkan:
                return static_cast<std::uint32_t>(SDL_WINDOW_VULKAN);

            case RendererWindowKind::OpenGL:
                return static_cast<std::uint32_t>(SDL_WINDOW_OPENGL);

            default:
                return 0;
            }
        }
    }

    /**
     * @brief The Bgfx family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Bgfx.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Bgfx,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Bgfx),
            // plan_runtimerenderer.md RTR-P10-9: taken from bgfx's OWN resolution, not assumed.
            //
            // This was hardcoded to Vulkan, with a comment saying that is what bgfx's default
            // preference order resolves to. On Linux that is simply untrue: Detail::
            // GetDefaultRendererType() returns bgfx::RendererType::OpenGL unconditionally there,
            // and PrepareWindowFlags() below accordingly asked SDL for SDL_WINDOW_OPENGL. So CNA
            // recorded the window as Vulkan-kind while creating a GL window -- two contradictory
            // statements about one window, on the platform this renderer is primarily tested on.
            //
            // Harmless while the cross-window-kind branch was unreachable; RTR-P5-13 made it live,
            // and it decides whether a later candidate may reuse this window. A GL candidate would
            // have been refused against a window that is in fact GL, and a Vulkan candidate
            // accepted against one that is not.
            //
            // Resolved once, when this descriptor is first requested. CNA_BGFX_RENDERER is read
            // again per attempt by PrepareWindowFlags(), so a value changed mid-process would still
            // move the flags without moving this field; that is out of scope here and no worse than
            // before, where the field could never be right at all.
            .windowKind               = ResolvedWindowKind(),
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

