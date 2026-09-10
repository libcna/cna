// SPDX-License-Identifier: MS-PL
// DX-225: native DirectX storage and transfer contract for XNA Dxt1, Dxt3, and Dxt5.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#if defined(CNA_RENDERER_DIRECTX11)
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#elif defined(CNA_RENDERER_DIRECTX12)
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture3D.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#else
#error This contract is for the DirectX renderer family.
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr std::uint16_t kRed565 = 0xF800;
    constexpr std::uint16_t kGreen565 = 0x07E0;
    constexpr std::uint16_t kBlue565 = 0x001F;

    struct FormatCase
    {
        SurfaceFormat surfaceFormat;
        DXGI_FORMAT dxgiFormat;
        int bytesPerBlock;
        const char* name;
    };

    constexpr std::array<FormatCase, 3> kFormats{{
        {SurfaceFormat::Dxt1, DXGI_FORMAT_BC1_UNORM, 8, "Dxt1"},
        {SurfaceFormat::Dxt3, DXGI_FORMAT_BC2_UNORM, 16, "Dxt3"},
        {SurfaceFormat::Dxt5, DXGI_FORMAT_BC3_UNORM, 16, "Dxt5"},
    }};

    void AppendSolidBlock(std::vector<std::uint8_t>& out, SurfaceFormat format,
                          std::uint16_t colour)
    {
        if (format == SurfaceFormat::Dxt3)
        {
            out.insert(out.end(), 8, 0xFF);
        }
        else if (format == SurfaceFormat::Dxt5)
        {
            out.push_back(0xFF);
            out.push_back(0xFF);
            out.insert(out.end(), 6, 0x00);
        }
        out.push_back(static_cast<std::uint8_t>(colour & 0xFF));
        out.push_back(static_cast<std::uint8_t>(colour >> 8));
        out.push_back(0x00);
        out.push_back(0x00);
        out.insert(out.end(), 4, 0x00);
    }

    std::vector<std::uint8_t> MakeBlocks(SurfaceFormat format,
                                         std::initializer_list<std::uint16_t> colours)
    {
        std::vector<std::uint8_t> result;
        for (const std::uint16_t colour : colours)
            AppendSolidBlock(result, format, colour);
        return result;
    }

#if defined(CNA_RENDERER_DIRECTX11)
    using ActiveRenderer = CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
    using NativeTexture2D = CNA::Internal::Renderers::DirectX11::D3D11TextureRenderer;
    using NativeTextureCube = CNA::Internal::Renderers::DirectX11::D3D11TextureCubeRenderer;

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

    std::pair<bool, bool> QueryTexture3DSupport(ActiveRenderer& renderer, DXGI_FORMAT format)
    {
        UINT support = 0;
        const HRESULT hr = renderer.GetDeviceEXT()->CheckFormatSupport(format, &support);
        return {SUCCEEDED(hr), (support & D3D11_FORMAT_SUPPORT_TEXTURE3D) != 0};
    }
#else
    using ActiveRenderer = CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
    using NativeTexture2D = CNA::Internal::Renderers::DirectX12::D3D12TextureRenderer;
    using NativeTextureCube = CNA::Internal::Renderers::DirectX12::D3D12TextureCubeRenderer;

    DXGI_FORMAT NativeFormat(const NativeTexture2D& texture)
    {
        return texture.GetResourceEXT()->GetDesc().Format;
    }

    DXGI_FORMAT NativeFormat(const NativeTextureCube& texture)
    {
        return texture.GetResourceEXT()->GetDesc().Format;
    }

    std::pair<bool, bool> QueryTexture3DSupport(ActiveRenderer& renderer, DXGI_FORMAT format)
    {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = format;
        const HRESULT hr = renderer.GetDeviceEXT()->CheckFeatureSupport(
            D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));
        return {SUCCEEDED(hr), (support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE3D) != 0};
    }
#endif
}

class D3DCompressedTextureContract final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        ok ? ++pass_ : ++fail_;
    }

    void RunTexture2D(GraphicsDevice& device, const FormatCase& formatCase)
    {
        Texture2D texture(device, 8, 4, false, formatCase.surfaceFormat);
        auto* native = dynamic_cast<NativeTexture2D*>(&texture.GetRenderer());
        Check(native != nullptr && NativeFormat(*native) == formatCase.dxgiFormat,
              std::string(formatCase.name) + " Texture2D uses its native BC DXGI format");

        const auto initial = MakeBlocks(formatCase.surfaceFormat, {kRed565, kBlue565});
        texture.SetData(initial.data(), static_cast<int>(initial.size()));
        std::vector<std::uint8_t> read(initial.size(), 0xCD);
        texture.GetData(read.data(), static_cast<int>(read.size()));
        Check(read == initial,
              std::string(formatCase.name) + " Texture2D returns exact uploaded block bytes");

        const auto green = MakeBlocks(formatCase.surfaceFormat, {kGreen565});
        const Microsoft::Xna::Framework::Rectangle rightBlock(4, 0, 4, 4);
        texture.SetData(0, &rightBlock, green.data(), 0, static_cast<int>(green.size()));
        std::vector<std::uint8_t> expected = MakeBlocks(
            formatCase.surfaceFormat, {kRed565, kGreen565});
        std::fill(read.begin(), read.end(), 0xCD);
        texture.GetData(read.data(), static_cast<int>(read.size()));
        Check(read == expected,
              std::string(formatCase.name) + " partial update preserves the adjacent BC block");

        bool rejected = false;
        try
        {
            const Microsoft::Xna::Framework::Rectangle unaligned(2, 0, 4, 4);
            texture.SetData(0, &unaligned, green.data(), 0, static_cast<int>(green.size()));
        }
        catch (const std::out_of_range& e)
        {
            rejected = std::string(e.what()).find("block-aligned") != std::string::npos;
        }
        Check(rejected,
              std::string(formatCase.name) + " rejects a non-block-aligned partial update");

        Texture2D mipTexture(device, 8, 8, true, formatCase.surfaceFormat);
        bool mipRoundTrip = true;
        for (int level = 0; level < mipTexture.getLevelCountProperty(); ++level)
        {
            const int extent = std::max(1, 8 >> level);
            const int blockCount = ((extent + 3) / 4) * ((extent + 3) / 4);
            std::vector<std::uint8_t> mipBytes;
            for (int block = 0; block < blockCount; ++block)
                AppendSolidBlock(mipBytes, formatCase.surfaceFormat,
                                 block % 2 == 0 ? kRed565 : kBlue565);
            mipTexture.SetData(level, nullptr, mipBytes.data(), 0,
                               static_cast<int>(mipBytes.size()));
            std::vector<std::uint8_t> mipRead(mipBytes.size(), 0xCD);
            mipTexture.GetData(level, nullptr, mipRead.data(), 0,
                               static_cast<int>(mipRead.size()));
            mipRoundTrip = mipRoundTrip && mipRead == mipBytes;
        }
        Check(mipRoundTrip,
              std::string(formatCase.name) + " round-trips every mip including sub-4x4 tails");
    }

    void RunTextureCube(GraphicsDevice& device, const FormatCase& formatCase)
    {
        TextureCube texture(device, 4, true, formatCase.surfaceFormat);
        auto* native = dynamic_cast<NativeTextureCube*>(&texture.GetRenderer());
        Check(native != nullptr && NativeFormat(*native) == formatCase.dxgiFormat,
              std::string(formatCase.name) + " TextureCube uses its native BC DXGI format");

        const auto red = MakeBlocks(formatCase.surfaceFormat, {kRed565});
        texture.SetData(CubeMapFace::PositiveX, red.data(), static_cast<int>(red.size()));
        std::array<Color, 16> pixels{};
        texture.GetData(CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
        const bool decoded = std::all_of(pixels.begin(), pixels.end(), [](const Color& pixel) {
            return pixel.getRProperty() == 255 && pixel.getGProperty() == 0 &&
                   pixel.getBProperty() == 0 && pixel.getAProperty() == 255;
        });
        const auto green = MakeBlocks(formatCase.surfaceFormat, {kGreen565});
        texture.SetData(CubeMapFace::PositiveX, 2, nullptr, green.data(), 0,
                        static_cast<int>(green.size()));
        Color tail;
        texture.GetData(CubeMapFace::PositiveX, 2, nullptr, &tail, 0, 1);
        const bool tailDecoded = tail.getRProperty() == 0 && tail.getGProperty() == 255 &&
            tail.getBProperty() == 0 && tail.getAProperty() == 255;
        Check(decoded && tailDecoded,
              std::string(formatCase.name) +
                  " TextureCube decodes level 0 and its sub-4x4 mip tail");
    }

    void RunTexture3D(ActiveRenderer& renderer, GraphicsDevice& device,
                      const FormatCase& formatCase)
    {
        const auto [querySucceeded, supportsTexture3D] =
            QueryTexture3DSupport(renderer, formatCase.dxgiFormat);
        std::printf("[MEASURE] %s format-support query: success=%d texture3D=%d\n",
                    formatCase.name, querySucceeded ? 1 : 0, supportsTexture3D ? 1 : 0);
        Check(querySucceeded && supportsTexture3D,
              std::string(formatCase.name) + " native feature query includes 3D BC resources");

        try
        {
            Texture3D texture(device, 4, 4, 2, true, formatCase.surfaceFormat);
#if defined(CNA_RENDERER_DIRECTX11)
            auto* native = dynamic_cast<CNA::Internal::Renderers::DirectX11::D3D11Texture3DRenderer*>(
                &texture.GetRenderer());
            D3D11_TEXTURE3D_DESC desc{};
            if (native != nullptr) native->GetTextureEXT()->GetDesc(&desc);
            const bool nativeFormat = native != nullptr && desc.Format == formatCase.dxgiFormat;
#else
            auto* native = dynamic_cast<CNA::Internal::Renderers::DirectX12::D3D12Texture3DRenderer*>(
                &texture.GetRenderer());
            const bool nativeFormat = native != nullptr &&
                native->GetResourceEXT()->GetDesc().Format == formatCase.dxgiFormat;
#endif
            Check(nativeFormat,
                  std::string(formatCase.name) + " Texture3D uses its native BC DXGI format");

            const auto slices = MakeBlocks(formatCase.surfaceFormat, {kRed565, kGreen565});
            texture.SetDataPointerEXT(0, 0, 0, 4, 4, 0, 2,
                                     slices.data(), static_cast<int>(slices.size()));
            std::array<Color, 32> pixels{};
            texture.GetData(pixels.data(), static_cast<int>(pixels.size()));
            bool decoded = true;
            for (std::size_t i = 0; i < pixels.size(); ++i)
            {
                const bool redSlice = i < 16;
                decoded = decoded && pixels[i].getRProperty() == (redSlice ? 255 : 0) &&
                    pixels[i].getGProperty() == (redSlice ? 0 : 255) &&
                    pixels[i].getBProperty() == 0 && pixels[i].getAProperty() == 255;
            }
            const auto tailBlock = MakeBlocks(formatCase.surfaceFormat, {kBlue565});
            texture.SetDataPointerEXT(2, 0, 0, 1, 1, 0, 1,
                                     tailBlock.data(), static_cast<int>(tailBlock.size()));
            Color tail;
            texture.GetData(2, 0, 0, 1, 1, 0, 1, &tail, 0, 1);
            const bool tailDecoded = tail.getRProperty() == 0 && tail.getGProperty() == 0 &&
                tail.getBProperty() == 255 && tail.getAProperty() == 255;
            Check(decoded && tailDecoded,
                  std::string(formatCase.name) +
                      " Texture3D decodes slices and its sub-4x4 mip tail");
        }
        catch (const std::exception& e)
        {
            Check(false, std::string(formatCase.name) +
                             " Texture3D compressed transfer threw: " + e.what());
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool ran = false;
        if (ran) return;
        ran = true;

        auto& device = getGraphicsDeviceProperty();
        auto* renderer = dynamic_cast<ActiveRenderer*>(&device.GetRenderer());
        Check(renderer != nullptr, "GraphicsDevice exposes the active DirectX renderer");
        if (renderer != nullptr)
        {
            const bool coreCapabilities = renderer->LoadsCompressedContentNativelyEXT() &&
                std::all_of(kFormats.begin(), kFormats.end(), [&](const FormatCase& formatCase) {
                    const int ordinal = static_cast<int>(formatCase.surfaceFormat);
                    return renderer->IsCompressedTransferFormatEXT(ordinal) &&
                           renderer->IsCompressedCubeTransferFormatEXT(ordinal);
                });
            Check(coreCapabilities,
                  "only the implemented core DXT family advertises native compressed loading");
            const bool modernExcluded =
                !renderer->IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt5SrgbEXT)) &&
                !renderer->IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Bc7EXT)) &&
                !renderer->IsCompressedTransferFormatEXT(static_cast<int>(SurfaceFormat::Bc7SrgbEXT));
            Check(modernExcluded, "DX-225 does not advertise modern CNAEXT compressed formats");

            for (const auto& formatCase : kFormats)
            {
                RunTexture2D(device, formatCase);
                RunTextureCube(device, formatCase);
                RunTexture3D(*renderer, device, formatCase);
            }
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    D3DCompressedTextureContract()
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
    D3DCompressedTextureContract game;
    game.Run();
    return game.Result();
}
