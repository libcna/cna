// plan_runtimerenderer.md RTR-P1: the Igl family's pre-construction contract.
//
// IGL is itself a graphics abstraction (igl::IDevice fronts real OpenGL and Vulkan
// implementations) and picks its backend at RUNTIME (CNA_IGL_BACKEND), the same shape LLGL,
// FNA3D and Diligent already have here -- so, exactly like BGFX (RTR-P10-9), everything in this
// descriptor that describes the WINDOW has to follow IGL's own resolution rather than the
// build-time fact that CNA picked IGL. The two backends need genuinely different windows: the
// OpenGL one adopts a GL context created on this very window by CNA's platform GL service, while
// the Vulkan one builds its surface from the native window handle (see IglPlatformSurface.cpp),
// and a native window cannot be created for both.
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
        /**
         * @brief Builds the descriptor from IGL's own resolved backend.
         *
         * Resolved ONCE, through the same cached answer `IglPlatformSurface` reads when it builds
         * the device, so the window CNA creates and the device IGL brings up on it can never
         * describe different APIs. The non-throwing form is required here: the generated registry
         * publishes the compiled-in set from a static initializer, so an exception raised while
         * this descriptor is built would terminate the process rather than report a bad
         * CNA_IGL_BACKEND value.
         *
         * @return The fully populated descriptor.
         */
        [[nodiscard]] GraphicsRendererDescriptor MakeDescriptor()
        {
            const Detail::RendererBackend backend = Detail::ResolveRendererBackendForWindow();

            return GraphicsRendererDescriptor{
                .type                = CNA::GraphicsRendererType::Igl,
                .name                = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Igl),
                // Was hardcoded to RendererWindowKind::OpenGL with a comment saying that is what
                // IGL's default preference resolves to. That is only true when nothing overrides
                // it: CNA_IGL_BACKEND=vulkan resolved the Vulkan backend while CNA recorded --
                // and requested -- an OpenGL window, so the fallback rule (design decision 8)
                // compared the wrong kind and the window carried the wrong render intent.
                .windowKind          = Detail::GetRendererBackendWindowKind(backend),
                .needsWindow         = true,
                .needsVideoSubsystem = true,
                // GLX fixes the visual when the window is created, so the depth/stencil/double
                // buffer/multisample request has to be stated here rather than when the context is
                // created. Zero for the Vulkan backend, whose window carries no GL visual.
                .glFramebuffer       = Detail::GetRendererBackendGlFramebufferRequest(backend),
                // Only the OpenGL backend adopts the platform's GL context; the Vulkan backend
                // never reads the service, and asking for one on a Vulkan-intent window would ask
                // the platform for something that window cannot provide.
                .needsGlContext      = Detail::RendererBackendNeedsOpenGLWindow(backend),
                .isAvailable         = &AlwaysAvailable,
                .create              = &CreateGraphicsRenderer,
            };
        }
    }

    /**
     * @brief The Igl family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Igl.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor = MakeDescriptor();
        return descriptor;
    }
}
