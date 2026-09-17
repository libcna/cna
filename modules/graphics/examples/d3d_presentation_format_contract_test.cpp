// SPDX-License-Identifier: MS-PL
// DX-213: public DirectX presentation reporting must match the actual default resources.
//
// plans/plan_graphics_shared_cleanup.md GSC-0005: the depth half of this contract predated the back
// buffer honouring PresentationParameters.DepthStencilFormat (WINCLOSE-0012 on DirectX11, DX12-0019 on
// DirectX12) and demanded DX-213's fixed Depth24Stencil8 whatever was requested. The rule now checked:
//   * a Reset applies and reports the requested depth format, and the native depth resource is its DXGI
//     format (Depth24 and Depth24Stencil8 share D24_UNORM_S8_UINT; None allocates no depth resource);
//   * GraphicsDeviceManager's default request is XNA's Depth24;
//   * the CNAEXT store-only SetPresentationParameters records the request without reallocating --
//     the native resources stay those of the last Reset, the documented store-only contract EasyGL's
//     depth-format fixture also relies on.
// The colour half (always Color / R8G8B8A8) and the sample-count half are unchanged.

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

    DXGI_FORMAT ExpectedNativeDepth(DepthFormat format)
    {
        switch (format)
        {
            case DepthFormat::Depth16:         return DXGI_FORMAT_D16_UNORM;
            case DepthFormat::Depth24:
            case DepthFormat::Depth24Stencil8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
            default:                           return DXGI_FORMAT_UNKNOWN;
        }
    }

    NativeDefaultSurface QueryNativeDefaultSurface(ActiveRenderer& renderer)
    {
        NativeDefaultSurface result;
#if defined(CNA_RENDERER_DIRECTX11)
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        renderer.GetContextEXT()->OMGetRenderTargets(
            1, rtv.GetAddressOf(), dsv.GetAddressOf());
        if (!rtv) return result;

        Microsoft::WRL::ComPtr<ID3D11Resource> colorResource;
        rtv->GetResource(colorResource.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTexture;
        if (FAILED(colorResource.As(&colorTexture))) return result;
        D3D11_TEXTURE2D_DESC colorDesc{};
        colorTexture->GetDesc(&colorDesc);
        result.colorFormat = colorDesc.Format;
        result.colorSamples = colorDesc.SampleDesc.Count;

        if (dsv)
        {
            Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
            dsv->GetResource(depthResource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
            if (FAILED(depthResource.As(&depthTexture))) return result;
            D3D11_TEXTURE2D_DESC depthDesc{};
            depthTexture->GetDesc(&depthDesc);
            result.depthFormat = depthDesc.Format;
            result.depthSamples = depthDesc.SampleDesc.Count;
        }
#else
        ID3D12Resource* const colorResource = renderer.GetBoundColorResourceEXT();
        ID3D12Resource* const depthResource = renderer.GetDefaultDepthStencilResourceEXT();
        if (colorResource == nullptr) return result;

        const D3D12_RESOURCE_DESC colorDesc = colorResource->GetDesc();
        result.colorFormat = colorDesc.Format;
        result.colorSamples = colorDesc.SampleDesc.Count;
        if (depthResource != nullptr)
        {
            const D3D12_RESOURCE_DESC depthDesc = depthResource->GetDesc();
            result.depthFormat = depthDesc.Format;
            result.depthSamples = depthDesc.SampleDesc.Count;
        }
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

    // `reportedDepth` is what PresentationParameters must say; `nativeDepth` is the depth format whose
    // resource must be bound. They differ only after the store-only request.
    void VerifyAppliedState(GraphicsDevice& device, ActiveRenderer& renderer,
                            int expectedSampleCount, DepthFormat reportedDepth,
                            DepthFormat nativeDepth, const char* leg)
    {
        const PresentationParameters& parameters =
            device.getPresentationParametersProperty();
        const NativeDefaultSurface native = QueryNativeDefaultSurface(renderer);
        const unsigned nativeSampleCount =
            static_cast<unsigned>(expectedSampleCount > 1 ? expectedSampleCount : 1);
        const DXGI_FORMAT expectedNativeDepth = ExpectedNativeDepth(nativeDepth);

        Check(parameters.getBackBufferFormatProperty() == SurfaceFormat::Color,
              std::string(leg) + ": public back-buffer format is the applied Color format");
        Check(parameters.getDepthStencilFormatProperty() == reportedDepth,
              std::string(leg) + ": public depth format is the requested format");
        Check(parameters.getMultiSampleCountProperty() == expectedSampleCount,
              std::string(leg) + ": public sample count is the applied count");
        Check(native.valid,
              std::string(leg) + ": native default color resource is bound");
        Check(native.colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM &&
                  native.depthFormat == expectedNativeDepth,
              std::string(leg) + ": native DXGI color and depth formats are the applied ones");
        Check(native.colorSamples == nativeSampleCount &&
                  native.depthSamples ==
                      (expectedNativeDepth == DXGI_FORMAT_UNKNOWN ? 0u : nativeSampleCount),
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
        VerifyAppliedState(device, *renderer, startupSamples, DepthFormat::Depth16,
                           DepthFormat::Depth16, "GDM Depth16 request");

        PresentationParameters reset =
            device.getPresentationParametersProperty().Clone();
        reset.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
        reset.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        reset.setMultiSampleCountProperty(4);
        device.Reset(reset);
        VerifyAppliedState(device, *renderer, 4, DepthFormat::Depth24Stencil8,
                           DepthFormat::Depth24Stencil8, "Reset Depth24Stencil8 request");

        reset.setDepthStencilFormatProperty(DepthFormat::Depth16);
        device.Reset(reset);
        VerifyAppliedState(device, *renderer, 4, DepthFormat::Depth16, DepthFormat::Depth16,
                           "Reset Depth16 request");

        PresentationParameters storeOnly =
            device.getPresentationParametersProperty().Clone();
        storeOnly.setBackBufferFormatProperty(SurfaceFormat::Bgra4444);
        storeOnly.setDepthStencilFormatProperty(DepthFormat::None);
        storeOnly.setMultiSampleCountProperty(1024);
        device.SetPresentationParameters(storeOnly);
        VerifyAppliedState(device, *renderer, 4, DepthFormat::None, DepthFormat::Depth16,
                           "store-only request (recorded, not reallocated)");

        PresentationParameters noDepth =
            device.getPresentationParametersProperty().Clone();
        noDepth.setDepthStencilFormatProperty(DepthFormat::None);
        noDepth.setMultiSampleCountProperty(4);
        device.Reset(noDepth);
        VerifyAppliedState(device, *renderer, 4, DepthFormat::None, DepthFormat::None,
                           "Reset None request");

        PresentationParameters disable =
            device.getPresentationParametersProperty().Clone();
        disable.setBackBufferFormatProperty(SurfaceFormat::Bgr565);
        disable.setDepthStencilFormatProperty(DepthFormat::Depth16);
        disable.setMultiSampleCountProperty(0);
        device.Reset(disable);
        VerifyAppliedState(device, *renderer, 0, DepthFormat::Depth16, DepthFormat::Depth16,
                           "single-sample Reset request");

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
