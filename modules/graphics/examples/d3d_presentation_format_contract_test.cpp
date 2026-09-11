// SPDX-License-Identifier: MS-PL
// DX-213: public DirectX presentation reporting must match the actual default resources.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#if defined(CNA_RENDERER_DIRECTX11)
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#elif defined(CNA_RENDERER_DIRECTX12)
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#else
#error This contract is for the DirectX renderer family.
#endif

#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_RENDERER_DIRECTX11)
    using ActiveRenderer = CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
#else
    using ActiveRenderer = CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
#endif

    struct NativeDefaultSurface
    {
        DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
        unsigned colorSamples = 0;
        unsigned depthSamples = 0;
        bool valid = false;
    };

    NativeDefaultSurface QueryNativeDefaultSurface(ActiveRenderer& renderer)
    {
        NativeDefaultSurface result;
#if defined(CNA_RENDERER_DIRECTX11)
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        renderer.GetContextEXT()->OMGetRenderTargets(
            1, rtv.GetAddressOf(), dsv.GetAddressOf());
        if (!rtv || !dsv) return result;

        Microsoft::WRL::ComPtr<ID3D11Resource> colorResource;
        Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
        rtv->GetResource(colorResource.GetAddressOf());
        dsv->GetResource(depthResource.GetAddressOf());

        Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
        if (FAILED(colorResource.As(&colorTexture)) || FAILED(depthResource.As(&depthTexture)))
            return result;

        D3D11_TEXTURE2D_DESC colorDesc{};
        D3D11_TEXTURE2D_DESC depthDesc{};
        colorTexture->GetDesc(&colorDesc);
        depthTexture->GetDesc(&depthDesc);
        result.colorFormat = colorDesc.Format;
        result.depthFormat = depthDesc.Format;
        result.colorSamples = colorDesc.SampleDesc.Count;
        result.depthSamples = depthDesc.SampleDesc.Count;
#else
        ID3D12Resource* const colorResource = renderer.GetBoundColorResourceEXT();
        ID3D12Resource* const depthResource = renderer.GetDefaultDepthStencilResourceEXT();
        if (colorResource == nullptr || depthResource == nullptr) return result;

        const D3D12_RESOURCE_DESC colorDesc = colorResource->GetDesc();
        const D3D12_RESOURCE_DESC depthDesc = depthResource->GetDesc();
        result.colorFormat = colorDesc.Format;
        result.depthFormat = depthDesc.Format;
        result.colorSamples = colorDesc.SampleDesc.Count;
        result.depthSamples = depthDesc.SampleDesc.Count;
#endif
        result.valid = true;
        return result;
    }
}

class D3DPresentationFormatContract final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        condition ? ++passed_ : ++failed_;
    }

    void VerifyAppliedState(GraphicsDevice& device, ActiveRenderer& renderer,
                            int expectedSampleCount, const char* leg)
    {
        const PresentationParameters& parameters =
            device.getPresentationParametersProperty();
        const NativeDefaultSurface native = QueryNativeDefaultSurface(renderer);
        const unsigned nativeSampleCount =
            static_cast<unsigned>(expectedSampleCount > 1 ? expectedSampleCount : 1);

        Check(parameters.getBackBufferFormatProperty() == SurfaceFormat::Color,
              std::string(leg) + ": public back-buffer format is the applied Color format");
        Check(parameters.getDepthStencilFormatProperty() == DepthFormat::Depth24Stencil8,
              std::string(leg) + ": public depth format is the applied Depth24Stencil8 format");
        Check(parameters.getMultiSampleCountProperty() == expectedSampleCount,
              std::string(leg) + ": public sample count is the applied count");
        Check(native.valid,
              std::string(leg) + ": native default color and depth resources are bound");
        Check(native.colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM &&
                  native.depthFormat == DXGI_FORMAT_D24_UNORM_S8_UINT,
              std::string(leg) + ": native DXGI formats independently match the public report");
        Check(native.colorSamples == nativeSampleCount &&
                  native.depthSamples == nativeSampleCount,
              std::string(leg) + ": native color/depth sample counts match the public report");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<ActiveRenderer*>(&device.GetRenderer());
        Check(renderer != nullptr, "GraphicsDevice exposes the active DirectX renderer");
        if (renderer == nullptr)
        {
            Exit();
            return;
        }

        const int startupSamples =
            device.getPresentationParametersProperty().getMultiSampleCountProperty();
        Check(startupSamples > 1,
              "GraphicsDeviceManager multisampling request applies a real sample count");
        VerifyAppliedState(device, *renderer, startupSamples, "GDM request");

        PresentationParameters reset =
            device.getPresentationParametersProperty().Clone();
        reset.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
        reset.setDepthStencilFormatProperty(DepthFormat::Depth16);
        reset.setMultiSampleCountProperty(4);
        device.Reset(reset);
        VerifyAppliedState(device, *renderer, 4, "Reset request");

        PresentationParameters storeOnly =
            device.getPresentationParametersProperty().Clone();
        storeOnly.setBackBufferFormatProperty(SurfaceFormat::Bgra4444);
        storeOnly.setDepthStencilFormatProperty(DepthFormat::None);
        storeOnly.setMultiSampleCountProperty(1024);
        device.SetPresentationParameters(storeOnly);
        VerifyAppliedState(device, *renderer, 4, "store-only request");

        PresentationParameters disable =
            device.getPresentationParametersProperty().Clone();
        disable.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
        disable.setDepthStencilFormatProperty(DepthFormat::Depth16);
        disable.setMultiSampleCountProperty(0);
        device.Reset(disable);
        VerifyAppliedState(device, *renderer, 0, "single-sample Reset request");

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    D3DPresentationFormatContract()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(160);
        gdm_->setPreferredBackBufferHeightProperty(120);
        gdm_->setPreferredBackBufferFormatProperty(SurfaceFormat::Bgr565);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth16);
        gdm_->setPreferMultiSamplingProperty(true);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    D3DPresentationFormatContract game;
    game.Run();
    return game.Result();
}
