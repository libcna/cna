// SPDX-License-Identifier: MS-PL
// plans/plan_graphics_shared_cleanup.md GSC-0006: end to end, a real Direct3D API misuse on the renderer's
// own device reaches D3DDebugLayerLog as an ERROR from the right debug layer, through the live-queue drain
// the test listener runs at the end of every test -- the path that makes such a message fail its test.
//
// Runs only where validation was explicitly enabled (CNA_D3D11_DEBUG_LAYER=1 / CNA_D3D12_DEBUG_LAYER=1).
// The test installs its own observer for the duration of the misuse, so the error it provokes on purpose is
// checked here instead of failing it through the listener.

#if defined(_WIN32) && (defined(CNA_RENDERER_DIRECTX11) || defined(CNA_RENDERER_DIRECTX12))

#include "CNA/Internal/Renderers/D3DCommon/D3DDebugLayerLog.hpp"
#if defined(CNA_RENDERER_DIRECTX11)
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#else
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#endif
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace
{
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerApi;
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerLog;
    using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerMessage;

    class ScopedObserver
    {
    public:
        explicit ScopedObserver(std::vector<D3DDebugLayerMessage>& seen)
            : previous_(D3DDebugLayerLog::SetObserver(
                  [&seen](const D3DDebugLayerMessage& message) { seen.push_back(message); }))
        {
        }

        ~ScopedObserver() { D3DDebugLayerLog::SetObserver(std::move(previous_)); }

        ScopedObserver(const ScopedObserver&) = delete;
        ScopedObserver& operator=(const ScopedObserver&) = delete;

    private:
        D3DDebugLayerLog::Observer previous_;
    };
}

TEST(D3DDebugLayerCaptureTest, AnApiMisuseOnTheRenderersDeviceIsRecordedAsAnError)
{
    Microsoft::Xna::Framework::Graphics::GraphicsDevice device;
#if defined(CNA_RENDERER_DIRECTX11)
    auto* renderer = dynamic_cast<CNA::Internal::Renderers::DirectX11::DirectX11Renderer*>(&device.GetRenderer());
    const D3DDebugLayerApi expectedApi = D3DDebugLayerApi::Direct3D11;
#else
    auto* renderer = dynamic_cast<CNA::Internal::Renderers::DirectX12::DirectX12Renderer*>(&device.GetRenderer());
    const D3DDebugLayerApi expectedApi = D3DDebugLayerApi::Direct3D12;
#endif
    if (renderer == nullptr || !renderer->IsDebugLayerEnabledEXT())
        GTEST_SKIP() << "needs the Direct3D renderer with its debug layer explicitly enabled";

    std::vector<D3DDebugLayerMessage> seen;
    {
        ScopedObserver observer(seen);
        D3DDebugLayerLog::DrainLiveQueues();
        seen.clear();

        // A zero-sized texture is invalid on both APIs; the debug layer stores an ERROR for it.
#if defined(CNA_RENDERER_DIRECTX11)
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 0;
        desc.Height = 0;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        EXPECT_FALSE(SUCCEEDED(renderer->GetDeviceEXT()->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())));
#else
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 0;
        desc.Height = 0;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        EXPECT_FALSE(SUCCEEDED(renderer->GetDeviceEXT()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(resource.GetAddressOf()))));
#endif
        D3DDebugLayerLog::DrainLiveQueues();
    }

    const bool sawError = std::any_of(seen.begin(), seen.end(), [&](const D3DDebugLayerMessage& message) {
        return message.api == expectedApi && message.severity == 1;
    });
    EXPECT_TRUE(sawError) << "the misuse produced " << seen.size()
                          << " recorded message(s) and no error from the renderer's debug layer";
}

#endif
