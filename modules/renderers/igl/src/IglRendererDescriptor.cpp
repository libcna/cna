// plan_runtimerenderer.md RTR-P1: the Igl family's pre-construction contract.
//
// IGL is itself a graphics abstraction (igl::IDevice fronts real OpenGL and Vulkan
// implementations) and picks its backend at RUNTIME (CNA_IGL_BACKEND), the same shape LLGL,
// FNA3D and Diligent already have here -- OpenGL is what its default preference order resolves
// to (see IglRendererSelection.hpp's GetDefaultRendererPreference()) and therefore the window
// kind this build will normally create; an explicit CNA_IGL_BACKEND=vulkan request still works
// because IGL's Vulkan backend builds its surface from the native window handle rather than the
// platform's OpenGL context service (see IglPlatformSurface.cpp).
//
// Only the renderer-selection header is included, deliberately not the renderer header: this
// keeps the descriptor free of IGL (and therefore of Xlib and Vulkan) includes.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Igl/IglRendererSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <cstdint>

namespace CNA::Internal::Renderers::Igl
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
    }

    /**
     * @brief The Igl family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Igl.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Igl,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Igl),
            // IGL's backend is a runtime choice; OpenGL is the one that constrains the window and
            // the one its own default preference order resolves to first.
            .windowKind               = RendererWindowKind::OpenGL,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            // Context-backed only in the sense that its OpenGL backend adopts the platform's GL
            // context; the Vulkan backend ignores this service entirely (it builds its surface from
            // the native window handle instead), so handing it out unconditionally is harmless.
            .needsGlContext           = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}
