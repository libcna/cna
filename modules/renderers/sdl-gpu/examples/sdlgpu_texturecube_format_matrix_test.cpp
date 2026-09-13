// SPDX-License-Identifier: MS-PL
// SDLGPU-70: truthful classic TextureCube format classification plus DXT1/3/5 face, partial,
// authored-mip, decoded-readback and physical native/fallback storage verification.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using CNA::Internal::Renderers::RendererFormatVerdict;
using CNA::Internal::Renderers::SdlGpu::SdlGpuRenderer;
using CNA::Internal::Renderers::SdlGpu::SdlGpuTextureCubeRenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    [[nodiscard]] std::vector<std::uint8_t> SolidDxtBlock(
        SurfaceFormat format, std::uint16_t rgb565)
    {
        std::vector<std::uint8_t> bytes;
        if (format == SurfaceFormat::Dxt3)
            bytes.insert(bytes.end(), 8, 0xFFu);
        else if (format == SurfaceFormat::Dxt5)
        {
            bytes.push_back(0xFFu);
            bytes.push_back(0xFFu);
            bytes.insert(bytes.end(), 6, 0u);
        }
        bytes.push_back(static_cast<std::uint8_t>(rgb565 & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(rgb565 >> 8));
        bytes.push_back(static_cast<std::uint8_t>(rgb565 & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>(rgb565 >> 8));
        bytes.insert(bytes.end(), 4, 0u);
        return bytes;
    }

    [[nodiscard]] bool AllExact(const std::vector<Color>& colors, const Color& expected)
    {
        for (const Color& color : colors)
            if (color != expected)
                return false;
        return true;
    }
}

class SdlGpuTextureCubeFormatMatrixTest final : public Game
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

    void CheckClassifiers(SdlGpuRenderer& renderer, GraphicsDevice& device)
    {
        const std::array supported{
            SurfaceFormat::Color, SurfaceFormat::Dxt1,
            SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
        };
        bool supportedExact = true;
        for (const SurfaceFormat format : supported)
        {
            supportedExact &= renderer.ClassifyTextureCubeFormatEXT(
                static_cast<int>(format)) == RendererFormatVerdict::Supported;
        }
        Check(supportedExact,
              "the exact usable Color/DXT1/DXT3/DXT5 cube set is explicitly Supported");

        const std::array unsupported{
            SurfaceFormat::Bgr565,
            SurfaceFormat::Bgra5551,
            SurfaceFormat::Bgra4444,
            SurfaceFormat::NormalizedByte2,
            SurfaceFormat::NormalizedByte4,
            SurfaceFormat::Rgba1010102,
            SurfaceFormat::Rg32,
            SurfaceFormat::Rgba64,
            SurfaceFormat::Alpha8,
            SurfaceFormat::Single,
            SurfaceFormat::Vector2,
            SurfaceFormat::Vector4,
            SurfaceFormat::HalfSingle,
            SurfaceFormat::HalfVector2,
            SurfaceFormat::HalfVector4,
            SurfaceFormat::HdrBlendable,
        };
        bool unsupportedExact = true;
        for (const SurfaceFormat format : unsupported)
        {
            unsupportedExact &= renderer.ClassifyTextureCubeFormatEXT(
                static_cast<int>(format)) == RendererFormatVerdict::Unsupported;
        }
        Check(unsupportedExact,
              "all 16 classic formats without a truthful cube transfer are Unsupported");

        const bool compressedExact =
            renderer.IsCompressedCubeTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt1)) &&
            renderer.IsCompressedCubeTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt3)) &&
            renderer.IsCompressedCubeTransferFormatEXT(static_cast<int>(SurfaceFormat::Dxt5)) &&
            !renderer.IsCompressedCubeTransferFormatEXT(static_cast<int>(SurfaceFormat::Color));
        Check(compressedExact, "only DXT1/3/5 use the compressed cube transfer route");

        bool packedRefused = true;
        for (const SurfaceFormat format : {SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551,
                                           SurfaceFormat::Bgra4444})
        {
            try
            {
                TextureCube invalid(device, 4, false, format);
                packedRefused = false;
            }
            catch (const System::NotSupportedException&)
            {
            }
        }
        Check(packedRefused,
              "public packed cube construction refuses instead of silently allocating RGBA8");
    }

    void CheckEveryDxtFormat(GraphicsDevice& device)
    {
        for (const auto [format, name] :
             {std::pair{SurfaceFormat::Dxt1, "DXT1"},
              std::pair{SurfaceFormat::Dxt3, "DXT3"},
              std::pair{SurfaceFormat::Dxt5, "DXT5"}})
        {
            TextureCube cube(device, 4, false, format);
            const std::vector<std::uint8_t> block = SolidDxtBlock(format, 0xF800u);
            cube.SetData(CubeMapFace::PositiveX, block.data(),
                         static_cast<int>(block.size()));
            std::vector<Color> readBack(16, Color::Transparent);
            cube.GetData(CubeMapFace::PositiveX, readBack.data(),
                         static_cast<int>(readBack.size()));
            Check(AllExact(readBack, Color::Red),
                  std::string(name) + " block upload decodes to exact red readback");
        }
    }

    void CheckSixFaces(GraphicsDevice& device)
    {
        constexpr std::array faces{
            CubeMapFace::PositiveX, CubeMapFace::NegativeX,
            CubeMapFace::PositiveY, CubeMapFace::NegativeY,
            CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        constexpr std::array<std::uint16_t, 6> packed{
            0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u, 0xF81Fu, 0x07FFu,
        };
        const std::array expected{
            Color(255, 0, 0, 255), Color(0, 255, 0, 255),
            Color(0, 0, 255, 255), Color(255, 255, 0, 255),
            Color(255, 0, 255, 255), Color(0, 255, 255, 255),
        };

        TextureCube cube(device, 4, false, SurfaceFormat::Dxt1);
        for (std::size_t face = 0; face < faces.size(); ++face)
        {
            const std::vector<std::uint8_t> block =
                SolidDxtBlock(SurfaceFormat::Dxt1, packed[face]);
            cube.SetData(faces[face], block.data(), static_cast<int>(block.size()));
        }

        bool exact = true;
        for (std::size_t face = 0; face < faces.size(); ++face)
        {
            std::vector<Color> readBack(16, Color::Transparent);
            cube.GetData(faces[face], readBack.data(), static_cast<int>(readBack.size()));
            exact &= AllExact(readBack, expected[face]);
        }
        Check(exact, "six compressed faces retain six deliberately distinct colors");
    }

    void CheckNpotPartialAndMip(GraphicsDevice& device)
    {
        TextureCube cube(device, 7, true, SurfaceFormat::Dxt1);
        std::vector<std::uint8_t> level0;
        for (int block = 0; block < 4; ++block)
        {
            const auto red = SolidDxtBlock(SurfaceFormat::Dxt1, 0xF800u);
            level0.insert(level0.end(), red.begin(), red.end());
        }
        cube.SetData(CubeMapFace::NegativeZ, level0.data(),
                     static_cast<int>(level0.size()));

        const std::vector<std::uint8_t> blue =
            SolidDxtBlock(SurfaceFormat::Dxt1, 0x001Fu);
        const Rectangle topRight(4, 0, 3, 4);
        cube.SetData(CubeMapFace::NegativeZ, 0, &topRight, blue.data(), 0,
                     static_cast<int>(blue.size()));

        std::vector<Color> readBack(49, Color::Transparent);
        cube.GetData(CubeMapFace::NegativeZ, readBack.data(),
                     static_cast<int>(readBack.size()));
        bool partialExact = true;
        for (int y = 0; y < 7; ++y)
        {
            for (int x = 0; x < 7; ++x)
            {
                const Color expected = x >= 4 && y < 4 ? Color::Blue : Color::Red;
                partialExact &= readBack[static_cast<std::size_t>(y * 7 + x)] == expected;
            }
        }
        Check(partialExact,
              "NPOT partial DXT update changes one edge block and preserves three blocks");

        TextureCube authored(device, 7, true, SurfaceFormat::Dxt5);
        const std::vector<std::uint8_t> green =
            SolidDxtBlock(SurfaceFormat::Dxt5, 0x07E0u);
        authored.SetData(CubeMapFace::PositiveY, 1, nullptr, green.data(), 0,
                         static_cast<int>(green.size()));
        std::vector<Color> mipRead(9, Color::Transparent);
        authored.GetData(CubeMapFace::PositiveY, 1, nullptr, mipRead.data(), 0,
                         static_cast<int>(mipRead.size()));
        Check(AllExact(mipRead, Color(0, 255, 0, 255)),
              "authored sub-4x4 DXT5 mip preserves and decodes its padded block");
    }

    void CheckStorageMode(GraphicsDevice& device)
    {
        TextureCube dxt1(device, 4, false, SurfaceFormat::Dxt1);
        TextureCube dxt3(device, 4, false, SurfaceFormat::Dxt3);
        TextureCube dxt5(device, 4, false, SurfaceFormat::Dxt5);
        const bool anyNative =
            dynamic_cast<const SdlGpuTextureCubeRenderer&>(dxt1.GetRenderer())
                .UsesNativeCompressionEXT() ||
            dynamic_cast<const SdlGpuTextureCubeRenderer&>(dxt3.GetRenderer())
                .UsesNativeCompressionEXT() ||
            dynamic_cast<const SdlGpuTextureCubeRenderer&>(dxt5.GetRenderer())
                .UsesNativeCompressionEXT();
        const char* forceFallback = std::getenv("CNA_SDLGPU_FORCE_DXT_FALLBACK");
        const bool fallbackRequested =
            forceFallback != nullptr && std::string(forceFallback) != "0";
        Check(!fallbackRequested || !anyNative,
              "forced DXT fallback creates no native BC cube storage");
        std::printf("[INFO] cube DXT storage path: %s\n",
                    anyNative ? "at least one native BC format" : "renderer RGBA8 decode");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto& renderer = dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());
        CheckClassifiers(renderer, device);
        CheckEveryDxtFormat(device);
        CheckSixFaces(device);
        CheckNpotPartialAndMip(device);
        CheckStorageMode(device);

        std::printf("=== %d/%d PASS ===\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    SdlGpuTextureCubeFormatMatrixTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuTextureCubeFormatMatrixTest game;
    game.Run();
    return game.Result();
}
