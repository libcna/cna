// plans/plan_runtimerenderer.md RTR-P1-D11: the DirectX11 family's pre-construction contract.
//
// Direct3D 11 creates its swap chain from the window's HWND.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <d3d11.h>
#include <wrl/client.h>


namespace CNA::Internal::Renderers::DirectX11
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        ComPtr<ID3D11Device> CreateAdapterQueryDevice()
        {
            static constexpr D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
            };

            ComPtr<ID3D11Device> device;
            D3D_FEATURE_LEVEL appliedLevel = D3D_FEATURE_LEVEL_11_0;
            HRESULT result = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                levels, 2, D3D11_SDK_VERSION,
                device.ReleaseAndGetAddressOf(), &appliedLevel, nullptr);
            if (result == E_INVALIDARG)
            {
                result = D3D11CreateDevice(
                    nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                    levels + 1, 1, D3D11_SDK_VERSION,
                    device.ReleaseAndGetAddressOf(), &appliedLevel, nullptr);
            }
            if (FAILED(result) || appliedLevel < D3D_FEATURE_LEVEL_11_0)
                return {};
            return device;
        }

        const ComPtr<ID3D11Device>& GetAdapterQueryDevice()
        {
            static const ComPtr<ID3D11Device> device = CreateAdapterQueryDevice();
            return device;
        }

        bool HasFormatSupport(DXGI_FORMAT format, UINT required)
        {
            const ComPtr<ID3D11Device>& device = GetAdapterQueryDevice();
            if (!device || format == DXGI_FORMAT_UNKNOWN) return false;

            UINT support = 0;
            return SUCCEEDED(device->CheckFormatSupport(format, &support)) &&
                (support & required) == required;
        }

        bool IsRenderTargetFormatSupported(int graphicsProfile, int surfaceFormat)
        {
            (void) graphicsProfile;
            if (!D3DCommon::IsXnaRenderTargetSurfaceFormat(surfaceFormat)) return false;

            constexpr UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D |
                                      D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                                      D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
            return HasFormatSupport(D3DCommon::SurfaceFormatToDxgi(surfaceFormat), required);
        }

        bool IsBackBufferFormatSupported(int graphicsProfile, int surfaceFormat)
        {
            (void) graphicsProfile;
            if (surfaceFormat != static_cast<int>(SurfaceFormat::Color)) return false;

            constexpr UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D |
                                      D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                                      D3D11_FORMAT_SUPPORT_DISPLAY;
            return HasFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, required);
        }

        int SelectBackBufferDepthStencilFormat(int requestedDepthFormat)
        {
            (void) requestedDepthFormat;
            return static_cast<int>(DepthFormat::Depth24Stencil8);
        }

        int ClampMultiSampleCount(int surfaceFormat, int requestedMultiSampleCount)
        {
            if (requestedMultiSampleCount <= 1) return 0;

            const DXGI_FORMAT format = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
            const ComPtr<ID3D11Device>& device = GetAdapterQueryDevice();
            if (!device || format == DXGI_FORMAT_UNKNOWN) return 0;

            int candidate = 1;
            while (candidate <= requestedMultiSampleCount / 2) candidate *= 2;
            while (candidate > 1)
            {
                UINT qualityLevels = 0;
                if (SUCCEEDED(device->CheckMultisampleQualityLevels(
                        format, static_cast<UINT>(candidate), &qualityLevels)) &&
                    qualityLevels > 0)
                {
                    return candidate;
                }
                candidate >>= 1;
            }
            return 0;
        }
    }

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

    /**
     * @brief The DirectX11 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX11.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX11,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX11),
            .windowKind               = RendererWindowKind::Plain,
            .needsWindow              = true,
            .needsVideoSubsystem      = true,
            .isAvailable              = &AlwaysAvailable,
            .create                   = &CreateGraphicsRenderer,
            .adapterQueries           = {
                .isRenderTargetFormatSupported      = &IsRenderTargetFormatSupported,
                .isBackBufferFormatSupported        = &IsBackBufferFormatSupported,
                .selectBackBufferDepthStencilFormat = &SelectBackBufferDepthStencilFormat,
                .clampMultiSampleCount              = &ClampMultiSampleCount,
            },
        };
        return descriptor;
    }
}
