// plan_runtimerenderer.md RTR-P1-D36: the Diligent family's pre-construction contract.
//
// DiligentCore picks its concrete device type (D3D12/Vulkan/D3D11/OpenGL) at RUNTIME, after the
// window already exists -- but unlike BGFX's identical problem, SDL3 rejects a window created with
// BOTH SDL_WINDOW_VULKAN and SDL_WINDOW_OPENGL ("Conflicting window graphics flags specified"), so
// only one can ever be requested.
//
// DILIGENT-57: this calls the SAME shared parser DiligentRenderer::ParseDeviceTypeOverride() uses,
// rather than a narrower re-implementation. An earlier "opengl"/"gl"-only check here silently
// disagreed with that parser's full alias set (gles, vk, dx11/direct3d11, dx12/direct3d12, ...),
// so e.g. CNA_DILIGENT_DEVICE=gles created a Vulkan-flagged window and the renderer then failed
// with "the specified window isn't an OpenGL window".
//
// CNA_DILIGENT_DEVICE=auto (or unset) resolves to the first entry of the preference order, matching
// the first candidate TryCreateDevice() itself will attempt. Because only one SDL flag can be
// requested, an auto build whose first preference (Vulkan) fails at runtime cannot then fall
// through to OpenGL against this already-created window -- a known, documented limitation
// (plan_diligent.md DILIGENT-57), not a silently broken promise.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Diligent/DiligentDeviceSelection.hpp"
#include "CNA/GraphicsRendererType.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::Diligent
{
    namespace
    {
        [[nodiscard]] std::uint32_t PrepareWindowFlags()
        {
            const char* override = SDL_getenv("CNA_DILIGENT_DEVICE");
            const std::vector<DiligentDeviceType> resolved =
                ParseDeviceTypeOverride(override != nullptr ? override : "");
            const DiligentDeviceType chosen =
                !resolved.empty() ? resolved.front() : DiligentDeviceType::Vulkan;
            return static_cast<std::uint32_t>(
                chosen == DiligentDeviceType::OpenGL ? SDL_WINDOW_OPENGL : SDL_WINDOW_VULKAN);
        }
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
            .prepareWindowFlags       = &PrepareWindowFlags,
            .applyPreWindowAttributes = &NoPreWindowAttributes,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
        };
        return descriptor;
    }
}

namespace CNA::Internal::Renderers
{
    const GraphicsRendererDescriptor& ActiveDescriptor()
    {
        return Diligent::GetDescriptor();
    }
}
