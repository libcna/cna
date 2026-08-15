// plan_runtimerenderer.md RTR-P1-D36: the Diligent family's pre-construction contract.
//
// DiligentCore picks its concrete device type (D3D12/Vulkan/D3D11/OpenGL) at RUNTIME, after the
// window already exists -- but unlike BGFX's identical problem, SDL3 rejects a window created with
// BOTH the platform window intent and the platform window intent ("Conflicting window graphics flags specified"), so
// only one can ever be requested.
//
// DILIGENT-57: this calls the SAME shared parser DiligentRenderer::ParseDeviceTypeOverride() uses,
// rather than a narrower re-implementation. An earlier "opengl"/"gl"-only check here silently
// disagreed with that parser's full alias set (gles, vk, dx11/direct3d11, dx12/direct3d12, ...),
// so e.g. CNA_DILIGENT_DEVICE=gles created a Vulkan-flagged window and the renderer then failed
// with "the specified window isn't an OpenGL window".
//
// CNA_DILIGENT_DEVICE=auto (or unset) resolves to the first entry of the preference order, matching
// the first candidate TryCreateDevice() itself will attempt. Because only one the platform flag can be
// requested, an auto build whose first preference (Vulkan) fails at runtime cannot then fall
// through to OpenGL against this already-created window -- a known, documented limitation
// (plan_diligent.md DILIGENT-57), not a silently broken promise.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Diligent/DiligentDeviceSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"


#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::Diligent
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
     * @brief The Diligent family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::Diligent.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::Diligent,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::Diligent),
            // Diligent's device type is a runtime choice; Vulkan is what its default preference
            // order resolves to and therefore the window kind this build will normally create.
            .windowKind               = RendererWindowKind::Vulkan,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .needsGlContext           = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

