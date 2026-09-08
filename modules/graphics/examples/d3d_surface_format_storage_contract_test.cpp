// SPDX-License-Identifier: MS-PL
// DX-214: renderer-family contract for native uncompressed XNA SurfaceFormat storage.

#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#if defined(CNA_RENDERER_DIRECTX11)
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#elif defined(CNA_RENDERER_DIRECTX12)
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture3D.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#else
#error This contract is for the DirectX renderer family.
#endif

#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Internal::Graphics;
using namespace CNA::Internal::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct FormatCase
    {
        SurfaceFormat surfaceFormat;
        DXGI_FORMAT dxgiFormat;
        const char* name;
    };

    constexpr std::array<FormatCase, 17> kUncompressedFormats{{
        {SurfaceFormat::Color, DXGI_FORMAT_R8G8B8A8_UNORM, "Color"},
        {SurfaceFormat::Bgr565, DXGI_FORMAT_B5G6R5_UNORM, "Bgr565"},
        {SurfaceFormat::Bgra5551, DXGI_FORMAT_B5G5R5A1_UNORM, "Bgra5551"},
        {SurfaceFormat::Bgra4444, DXGI_FORMAT_B4G4R4A4_UNORM, "Bgra4444"},
        {SurfaceFormat::NormalizedByte2, DXGI_FORMAT_R8G8_SNORM, "NormalizedByte2"},
        {SurfaceFormat::NormalizedByte4, DXGI_FORMAT_R8G8B8A8_SNORM, "NormalizedByte4"},
        {SurfaceFormat::Rgba1010102, DXGI_FORMAT_R10G10B10A2_UNORM, "Rgba1010102"},
        {SurfaceFormat::Rg32, DXGI_FORMAT_R16G16_UNORM, "Rg32"},
        {SurfaceFormat::Rgba64, DXGI_FORMAT_R16G16B16A16_UNORM, "Rgba64"},
        {SurfaceFormat::Alpha8, DXGI_FORMAT_A8_UNORM, "Alpha8"},
        {SurfaceFormat::Single, DXGI_FORMAT_R32_FLOAT, "Single"},
        {SurfaceFormat::Vector2, DXGI_FORMAT_R32G32_FLOAT, "Vector2"},
        {SurfaceFormat::Vector4, DXGI_FORMAT_R32G32B32A32_FLOAT, "Vector4"},
        {SurfaceFormat::HalfSingle, DXGI_FORMAT_R16_FLOAT, "HalfSingle"},
        {SurfaceFormat::HalfVector2, DXGI_FORMAT_R16G16_FLOAT, "HalfVector2"},
        {SurfaceFormat::HalfVector4, DXGI_FORMAT_R16G16B16A16_FLOAT, "HalfVector4"},
        {SurfaceFormat::HdrBlendable, DXGI_FORMAT_R16G16B16A16_FLOAT, "HdrBlendable"},
    }};

    template <std::size_t N>
    std::vector<uint8_t> Bytes(const std::array<float, N>& values)
    {
        std::vector<uint8_t> bytes(sizeof(values));
        std::memcpy(bytes.data(), values.data(), bytes.size());
        return bytes;
    }

#if defined(CNA_RENDERER_DIRECTX11)
    using NativeTexture2D = CNA::Internal::Renderers::DirectX11::D3D11TextureRenderer;
    using NativeTextureCube = CNA::Internal::Renderers::DirectX11::D3D11TextureCubeRenderer;
    using NativeTexture3D = CNA::Internal::Renderers::DirectX11::D3D11Texture3DRenderer;
    using NativeRenderTarget2D = CNA::Internal::Renderers::DirectX11::D3D11RenderTargetRenderer;
    using NativeRenderTargetCube = CNA::Internal::Renderers::DirectX11::D3D11RenderTargetCubeRenderer;

    DXGI_FORMAT NativeFormat(const NativeTexture2D& texture)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture.GetTextureEXT()->GetDesc(&desc);
        return desc.Format;
    }

    DXGI_FORMAT NativeFormat(const NativeTextureCube& texture)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture.GetTextureEXT()->GetDesc(&desc);
        return desc.Format;
    }

    DXGI_FORMAT NativeFormat(const NativeTexture3D& texture)
    {
        D3D11_TEXTURE3D_DESC desc{};
        texture.GetTextureEXT()->GetDesc(&desc);
        return desc.Format;
    }

    DXGI_FORMAT NativeFormat(const NativeRenderTarget2D& target)
    {
        D3D11_TEXTURE2D_DESC desc{};
        target.GetSampleableTextureEXT()->GetDesc(&desc);
        return desc.Format;
    }

    DXGI_FORMAT NativeFormat(const NativeRenderTargetCube& target)
    {
        D3D11_TEXTURE2D_DESC desc{};
        target.GetSampleableTextureEXT()->GetDesc(&desc);
        return desc.Format;
    }
#else
    using NativeTexture2D = CNA::Internal::Renderers::DirectX12::D3D12TextureRenderer;
    using NativeTextureCube = CNA::Internal::Renderers::DirectX12::D3D12TextureCubeRenderer;
    using NativeTexture3D = CNA::Internal::Renderers::DirectX12::D3D12Texture3DRenderer;
    using NativeRenderTarget2D = CNA::Internal::Renderers::DirectX12::D3D12RenderTargetRenderer;
    using NativeRenderTargetCube = CNA::Internal::Renderers::DirectX12::D3D12RenderTargetCubeRenderer;

    DXGI_FORMAT NativeFormat(const NativeTexture2D& texture)
    {
        return texture.GetResourceEXT()->GetDesc().Format;
    }

    DXGI_FORMAT NativeFormat(const NativeTextureCube& texture)
    {
        return texture.GetResourceEXT()->GetDesc().Format;
    }

    DXGI_FORMAT NativeFormat(const NativeTexture3D& texture)
    {
        return texture.GetResourceEXT()->GetDesc().Format;
    }

    DXGI_FORMAT NativeFormat(const NativeRenderTarget2D& target)
    {
        return target.GetSampleableColorResourceEXT()->GetDesc().Format;
    }

    DXGI_FORMAT NativeFormat(const NativeRenderTargetCube& target)
    {
        return target.GetSampleableColorResourceEXT()->GetDesc().Format;
    }
#endif
}

class D3DSurfaceFormatStorageContract final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++pass_ : ++fail_;
    }

    void RunTexture2D(GraphicsDevice& device)
    {
        Texture2D texture(device, 2, 2, true, SurfaceFormat::Single);
        const std::array<float, 4> level0{{0.125f, -2.5f, 17.0f, 0.0f}};
        const std::array<float, 1> level1{{3.25f}};
        texture.SetData(level0.data(), static_cast<int>(level0.size()));
        texture.SetData(1, nullptr, level1.data(), 0, static_cast<int>(level1.size()));

        auto* native = dynamic_cast<NativeTexture2D*>(&texture.GetRenderer());
        Check(native != nullptr, "Texture2D uses the active DirectX native texture renderer");
        if (native == nullptr) return;

        Check(native->GetSurfaceFormatEXT() == static_cast<int>(SurfaceFormat::Single),
              "Texture2D reports SurfaceFormat::Single rather than Color");
        Check(NativeFormat(*native) == DXGI_FORMAT_R32_FLOAT,
              "Texture2D creates DXGI_FORMAT_R32_FLOAT storage");

        std::vector<uint8_t> read0(sizeof(level0), 0xCD);
        const bool read0Ok = native->GetData(0, 0, 0, 2, 2, read0.data(),
                                             static_cast<int>(read0.size()));
        Check(read0Ok && read0 == Bytes(level0),
              "Texture2D level 0 round-trips exact float bytes through GPU readback");

        std::vector<uint8_t> read1(sizeof(level1), 0xCD);
        const bool read1Ok = native->GetData(1, 0, 0, 1, 1, read1.data(),
                                             static_cast<int>(read1.size()));
        Check(read1Ok && read1 == Bytes(level1),
              "Texture2D mip upload uses the Single-format row pitch");
    }

    void RunUncompressedFormatTable(GraphicsDevice& device)
    {
        for (const auto& formatCase : kUncompressedFormats)
        {
            try
            {
                Texture2D texture(device, 1, 1, false, formatCase.surfaceFormat);
                auto* native = dynamic_cast<NativeTexture2D*>(&texture.GetRenderer());
                Check(native != nullptr &&
                          native->GetSurfaceFormatEXT() == static_cast<int>(formatCase.surfaceFormat) &&
                          NativeFormat(*native) == formatCase.dxgiFormat,
                      std::string("Texture2D maps SurfaceFormat::") + formatCase.name +
                          " to its declared DXGI storage");
            }
            catch (const std::exception& e)
            {
                Check(false, std::string("Texture2D SurfaceFormat::") + formatCase.name +
                                 " construction threw: " + e.what());
            }
        }
    }

    void RunTextureCube(GraphicsDevice& device)
    {
        auto texture = device.GetRenderer().CreateTextureCube(
            2, false, static_cast<int>(SurfaceFormat::Single));
        auto* native = dynamic_cast<NativeTextureCube*>(texture.get());
        Check(native != nullptr, "TextureCube uses the active DirectX native texture renderer");
        if (native == nullptr) return;

        Check(native->GetSurfaceFormatEXT() == static_cast<int>(SurfaceFormat::Single) &&
                  NativeFormat(*native) == DXGI_FORMAT_R32_FLOAT,
              "TextureCube preserves SurfaceFormat::Single in metadata and DXGI storage");

        const std::array<float, 4> values{{-1.0f, 0.5f, 42.0f, 9.25f}};
        const auto bytes = Bytes(values);
        const bool wrote = native->SetData(4, 0, 0, 0, 2, 2, bytes.data(),
                                           static_cast<int>(bytes.size()));
        std::vector<uint8_t> read(bytes.size(), 0xCD);
        const bool readOk = native->GetData(4, 0, 0, 0, 2, 2, read.data(),
                                            static_cast<int>(read.size()));
        Check(wrote && readOk && read == bytes,
              "TextureCube round-trips exact Single bytes on an isolated face");
    }

    void RunTexture3D(GraphicsDevice& device)
    {
        auto texture = device.GetRenderer().CreateTexture3D(
            2, 2, 2, false, static_cast<int>(SurfaceFormat::Single));
        auto* native = dynamic_cast<NativeTexture3D*>(texture.get());
        Check(native != nullptr, "Texture3D uses the active DirectX native texture renderer");
        if (native == nullptr) return;

        Check(native->GetSurfaceFormatEXT() == static_cast<int>(SurfaceFormat::Single) &&
                  NativeFormat(*native) == DXGI_FORMAT_R32_FLOAT,
              "Texture3D preserves SurfaceFormat::Single in metadata and DXGI storage");

        const std::array<float, 8> values{{0.0f, 1.0f, 2.0f, 3.0f, -4.0f, -5.0f, 6.5f, 7.75f}};
        const auto bytes = Bytes(values);
        const bool wrote = native->SetData(0, 0, 0, 0, 2, 2, 2, bytes.data(),
                                           static_cast<int>(bytes.size()));
        std::vector<uint8_t> read(bytes.size(), 0xCD);
        const bool readOk = native->GetData(0, 0, 0, 0, 2, 2, 2, read.data(),
                                            static_cast<int>(read.size()));
        Check(wrote && readOk && read == bytes,
              "Texture3D round-trips exact Single bytes across row and slice pitches");
    }

    void RunRenderTargets(GraphicsDevice& device)
    {
        RenderTarget2D target2D(device, 4, 4, false, SurfaceFormat::Single,
                                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        auto* native2D = dynamic_cast<NativeRenderTarget2D*>(target2D.GetRenderTargetRenderer());
        Check(native2D != nullptr &&
                  native2D->GetSurfaceFormatEXT() == static_cast<int>(SurfaceFormat::Single) &&
                  NativeFormat(*native2D) == DXGI_FORMAT_R32_FLOAT,
              "RenderTarget2D creates and reports native DXGI_FORMAT_R32_FLOAT storage");

        RenderTargetCube targetCube(device, 4, false, SurfaceFormat::HalfVector4,
                                    DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        auto* nativeCube = dynamic_cast<NativeRenderTargetCube*>(
            targetCube.GetRenderTargetCubeRenderer());
        Check(nativeCube != nullptr &&
                  nativeCube->GetSurfaceFormatEXT() ==
                      static_cast<int>(SurfaceFormat::HalfVector4) &&
                  NativeFormat(*nativeCube) == DXGI_FORMAT_R16G16B16A16_FLOAT,
              "RenderTargetCube creates and reports native DXGI_FORMAT_R16G16B16A16_FLOAT storage");
    }

    void RunInvalidOrdinal(GraphicsDevice& device)
    {
        ImageData invalid{2, 2, {}, 1, 999};
        try
        {
            auto texture = device.GetRenderer().CreateTexture(invalid);
            (void)texture;
            Check(false, "invalid SurfaceFormat ordinal is refused");
        }
        catch (const std::invalid_argument& e)
        {
            const std::string message = e.what();
            Check(message.find("Unknown") != std::string::npos &&
                      message.find("999") != std::string::npos,
                  "invalid SurfaceFormat is refused by diagnostic name and ordinal");
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("invalid SurfaceFormat threw the wrong exception: ") + e.what());
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool ran = false;
        if (ran) return;
        ran = true;

        auto& device = getGraphicsDeviceProperty();
        RunTexture2D(device);
        RunUncompressedFormatTable(device);
        RunTextureCube(device);
        RunTexture3D(device);
        RunRenderTargets(device);
        RunInvalidOrdinal(device);
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    D3DSurfaceFormatStorageContract()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int Result() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    D3DSurfaceFormatStorageContract game;
    game.Run();
    return game.Result();
}
