// plan_runtimerenderer.md RTR-P1-D20: the DirectX9 family's pre-construction contract.
//
// Real Direct3D 9 against the window's HWND.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9FormatMapping.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9ProfileCapabilities.hpp"


namespace CNA::Internal::Renderers::DirectX9
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
        // plan_runtimerenderer.md design decision 9: GraphicsAdapter's profile/format queries run
        // BEFORE any device exists, so they cannot go through an IGraphicsRenderer virtual. D3D9 is
        // the only renderer with a real capability structure (D3DCAPS9 / IDirect3D9::CheckDevice*)
        // to answer them from; every other family leaves these null and GraphicsAdapter keeps its
        // own honest framework rule.

        /// D9-32: Reach has no floor worth checking -- every real D3D9 HAL device already exceeds
        /// vs_2_0/ps_2_0 and every other Reach minimum. Only HiDef's floor is hardware-dependent.
        [[nodiscard]] bool IsProfileSupported(int graphicsProfile)
        {
            if (graphicsProfile == 0)
                return true;
            return MeetsHiDefFloorEXT(QueryAdapterCapsEXT());
        }

        /// D9-102: valid for the profile (D9-100's whitelist) AND supported by the real device
        /// (IDirect3D9::CheckDeviceFormat). Either failing makes the caller fall back to Color.
        [[nodiscard]] bool IsRenderTargetFormatSupported(int graphicsProfile, int surfaceFormat)
        {
            if (!IsValidTextureFormatForProfileEXT(graphicsProfile, surfaceFormat))
                return false;
            const D3DFORMAT requested = SurfaceFormatToD3D9(surfaceFormat);
            return requested != D3DFMT_UNKNOWN
                && IsRenderTargetFormatSupportedByHardwareEXT(requested);
        }

        /// D9-102: the back buffer has its own, stricter display-compatibility restriction --
        /// probed via IDirect3D9::CheckDeviceType, not CheckDeviceFormat.
        [[nodiscard]] bool IsBackBufferFormatSupported(int graphicsProfile, int surfaceFormat)
        {
            if (!IsValidTextureFormatForProfileEXT(graphicsProfile, surfaceFormat))
                return false;
            const D3DFORMAT requested = SurfaceFormatToD3D9(surfaceFormat);
            return requested != D3DFMT_UNKNOWN
                && IsBackBufferFormatSupportedByHardwareEXT(requested);
        }

        [[nodiscard]] int ClampMultiSampleCount(int surfaceFormat, int requestedMultiSampleCount)
        {
            return ClampMultiSampleCountForFormatEXT(
                SurfaceFormatToD3D9(surfaceFormat), requestedMultiSampleCount);
        }
    }

    /**
     * @brief The DirectX9 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX9.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX9,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX9),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
            .adapterQueries           = {
                .isProfileSupported            = &IsProfileSupported,
                .isRenderTargetFormatSupported = &IsRenderTargetFormatSupported,
                .isBackBufferFormatSupported   = &IsBackBufferFormatSupported,
                .clampMultiSampleCount         = &ClampMultiSampleCount,
            },
        };
        return descriptor;
    }
}

