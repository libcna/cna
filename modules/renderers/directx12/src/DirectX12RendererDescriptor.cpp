// plans/plan_runtimerenderer.md RTR-P1-D12: the DirectX12 family's pre-construction contract.
//
// Direct3D 12 creates its swap chain from the window's HWND. It is also the one renderer that can
// genuinely run without one -- PresentationParameters::HeadlessEXT is that runtime opt-in, handled
// by GraphicsDevice rather than by this flag, which is why needsWindow stays true here.

#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptor.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererDescriptorHelpers.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Configuration.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>


namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        using Microsoft::WRL::ComPtr;
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        ComPtr<ID3D12Device> TryCreateQueryDevice(IDXGIAdapter1* adapter)
        {
            static constexpr D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_12_1,
                D3D_FEATURE_LEVEL_12_0,
                D3D_FEATURE_LEVEL_11_1,
                D3D_FEATURE_LEVEL_11_0,
            };
            for (const D3D_FEATURE_LEVEL level : levels)
            {
                ComPtr<ID3D12Device> device;
                if (SUCCEEDED(D3D12CreateDevice(adapter, level, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()))))
                    return device;
            }
            return {};
        }

        ComPtr<ID3D12Device> CreateAdapterQueryDevice()
        {
            // plans/plan_directx12_parity.md DX12-0031: the queries a game makes before its device exists
            // (render-target formats, back-buffer format, MSAA clamps) are answered by the adapter the
            // renderer will actually use, chosen by the same rules as
            // DirectX12Renderer::CreateDeviceOnConfiguredAdapter. This used to take the first hardware
            // adapter whatever CNA_D3D12_ADAPTER said, and a null adapter -- "the default adapter", which
            // may be WARP -- when there was none; on a machine whose first hardware adapter refuses
            // Direct3D 12 every query answered "unsupported" and every MSAA clamp 0 while the renderer
            // itself ran on WARP.
            ComPtr<IDXGIFactory4> factory;
            if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()))))
                return {};

            if (CaptureD3D12ConfigurationFromEnvironment().adapter == D3D12AdapterPreference::Warp)
            {
                ComPtr<IDXGIAdapter1> warp;
                if (FAILED(factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()))))
                    return {};
                return TryCreateQueryDevice(warp.Get());
            }

            ComPtr<IDXGIFactory6> factory6;
            const bool byPreference = SUCCEEDED(factory.As(&factory6));
            for (UINT index = 0; ; ++index)
            {
                ComPtr<IDXGIAdapter1> adapter;
                const HRESULT hr = byPreference
                    ? factory6->EnumAdapterByGpuPreference(
                          index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(adapter.GetAddressOf()))
                    : factory->EnumAdapters1(index, adapter.GetAddressOf());
                if (FAILED(hr))
                    return {};

                DXGI_ADAPTER_DESC1 description{};
                adapter->GetDesc1(&description);
                if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
                    continue;
                if (ComPtr<ID3D12Device> device = TryCreateQueryDevice(adapter.Get()))
                    return device;
            }
        }

        const ComPtr<ID3D12Device>& GetAdapterQueryDevice()
        {
            static const ComPtr<ID3D12Device> device = CreateAdapterQueryDevice();
            return device;
        }

        bool HasFormatSupport(DXGI_FORMAT format, D3D12_FORMAT_SUPPORT1 required)
        {
            const ComPtr<ID3D12Device>& device = GetAdapterQueryDevice();
            if (!device || format == DXGI_FORMAT_UNKNOWN) return false;

            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
            support.Format = format;
            return SUCCEEDED(device->CheckFeatureSupport(
                       D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support))) &&
                (support.Support1 & required) == required;
        }

        bool IsRenderTargetFormatSupported(int graphicsProfile, int surfaceFormat)
        {
            (void) graphicsProfile;
            if (!D3DCommon::IsXnaRenderTargetSurfaceFormat(surfaceFormat)) return false;

            const auto required = static_cast<D3D12_FORMAT_SUPPORT1>(
                D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
                D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE);
            return HasFormatSupport(D3DCommon::SurfaceFormatToDxgi(surfaceFormat), required);
        }

        bool IsBackBufferFormatSupported(int graphicsProfile, int surfaceFormat)
        {
            (void) graphicsProfile;
            if (surfaceFormat != static_cast<int>(SurfaceFormat::Color)) return false;

            const auto required = static_cast<D3D12_FORMAT_SUPPORT1>(
                D3D12_FORMAT_SUPPORT1_TEXTURE2D |
                D3D12_FORMAT_SUPPORT1_RENDER_TARGET |
                D3D12_FORMAT_SUPPORT1_DISPLAY);
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
            const ComPtr<ID3D12Device>& device = GetAdapterQueryDevice();
            if (!device || format == DXGI_FORMAT_UNKNOWN) return 0;

            int candidate = 1;
            while (candidate <= requestedMultiSampleCount / 2) candidate *= 2;
            while (candidate > 1)
            {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS support{};
                support.Format = format;
                support.SampleCount = static_cast<UINT>(candidate);
                support.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
                if (SUCCEEDED(device->CheckFeatureSupport(
                        D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                        &support, sizeof(support))) &&
                    support.NumQualityLevels > 0)
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
     * @brief The DirectX12 family's descriptor.
     *
     * @return The descriptor for GraphicsRendererType::DirectX12.
     */
    const GraphicsRendererDescriptor& GetDescriptor()
    {
        static const GraphicsRendererDescriptor descriptor{
            .type                     = CNA::GraphicsRendererType::DirectX12,
            .name                     = CNA::getGraphicsRendererName(CNA::GraphicsRendererType::DirectX12),
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
