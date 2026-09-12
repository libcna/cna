// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-044: focused plain SurfaceFormat.Color TextureCube validation.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr std::array<CubeMapFace, 6> kFaces{
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};

    [[nodiscard]] bool Matches(const Color& actual, const Color& expected)
    {
        return actual.getRProperty() == expected.getRProperty() &&
            actual.getGProperty() == expected.getGProperty() &&
            actual.getBProperty() == expected.getBProperty() &&
            actual.getAProperty() == expected.getAProperty();
    }

    [[nodiscard]] std::vector<std::uint8_t> SolidDxtBlock(
        const SurfaceFormat format, const std::uint16_t rgb565)
    {
        std::vector<std::uint8_t> result;
        if (format == SurfaceFormat::Dxt3)
        {
            result.insert(result.end(), 8u, 0xFFu);
        }
        else if (format == SurfaceFormat::Dxt5)
        {
            result.push_back(0xFFu);
            result.push_back(0xFFu);
            result.insert(result.end(), 6u, 0u);
        }
        result.push_back(static_cast<std::uint8_t>(rgb565));
        result.push_back(static_cast<std::uint8_t>(rgb565 >> 8u));
        result.push_back(static_cast<std::uint8_t>(rgb565));
        result.push_back(static_cast<std::uint8_t>(rgb565 >> 8u));
        result.insert(result.end(), 4u, 0u);
        return result;
    }

    [[nodiscard]] bool AllMatch(
        const std::vector<Color>& actual, const Color& expected)
    {
        const bool matches = std::all_of(
            actual.begin(), actual.end(),
            [&](const Color& value) { return Matches(value, expected); });
        if (!matches && !actual.empty())
        {
            std::printf(
                "[INFO] cube mismatch actual=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n",
                actual.front().getRProperty(), actual.front().getGProperty(),
                actual.front().getBProperty(), actual.front().getAProperty(),
                expected.getRProperty(), expected.getGProperty(),
                expected.getBProperty(), expected.getAProperty());
        }
        return matches;
    }
}

class RlglTextureCubeTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglTextureCubeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        graphics_->setPreferredBackBufferWidthProperty(32);
        graphics_->setPreferredBackBufferHeightProperty(32);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Rlgl::RlglRenderer&>(
            device.GetRenderer());
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;
        namespace Rlgl = CNA::Internal::Renderers::Rlgl;

        Check(renderer.GetMaxCubeSizeForProfileEXT(0) ==
                  std::min(renderer.GetMaxTextureDimension(), 512) &&
                  renderer.GetMaxCubeSizeForProfileEXT(1) ==
                      std::min(renderer.GetMaxTextureDimension(), 4096) &&
                  renderer.ClassifyTextureCubeFormatEXT(
                      static_cast<int>(SurfaceFormat::Color)) ==
                      CNA::Internal::Renderers::RendererFormatVerdict::Supported &&
                  renderer.ClassifyTextureCubeFormatEXT(
                      static_cast<int>(SurfaceFormat::Dxt1)) ==
                      CNA::Internal::Renderers::RendererFormatVerdict::Supported &&
                  renderer.ClassifyTextureCubeFormatEXT(
                      static_cast<int>(SurfaceFormat::Vector4)) ==
                      CNA::Internal::Renderers::RendererFormatVerdict::Unsupported,
              "TextureCube limits and format classification expose only the validated baseline");

        TextureCube mipmapped(device, 8, true, SurfaceFormat::Color);
        const auto resource = Rlgl::GetTextureCubeResourceSnapshotForTesting(
            mipmapped.GetRenderer());
        const auto native = Bridge::GetTextureCubeSnapshotForTesting(
            resource.texture, resource.levelCount);
        Check(resource.texture != 0 && resource.size == 8 && resource.levelCount == 4 &&
                  resource.surfaceFormat == static_cast<int>(SurfaceFormat::Color) &&
                  std::all_of(
                      native.levelZeroWidths.begin(), native.levelZeroWidths.end(),
                      [](const int width) { return width == 8; }) &&
                  std::all_of(
                      native.finalLevelWidths.begin(), native.finalLevelWidths.end(),
                      [](const int width) { return width == 1; }) &&
                  native.baseLevel == 0 && native.maxLevel == 3 &&
                  native.internalFormat == 0x8058,
              "rlLoadTextureCubemap allocates RGBA8 storage for every face and mip level");

        TextureCube bound(device, 2, false, SurfaceFormat::Color);
        const auto boundResource = Rlgl::GetTextureCubeResourceSnapshotForTesting(
            bound.GetRenderer());
        bound.GetRenderer().BindGL(2);
        TextureCube scratch(device, 2, false, SurfaceFormat::Color);
        const std::array<Color, 4> scratchPixels{
            Color(1, 2, 3, 4), Color(5, 6, 7, 8),
            Color(9, 10, 11, 12), Color(13, 14, 15, 16)};
        scratch.SetData(
            CubeMapFace::PositiveX, scratchPixels.data(),
            static_cast<int>(scratchPixels.size()));
        std::array<Color, 4> scratchRead{};
        scratch.GetData(
            CubeMapFace::PositiveX, scratchRead.data(),
            static_cast<int>(scratchRead.size()));
        Check(Bridge::GetBoundTextureCubeForTesting(2) == boundResource.texture,
              "cube allocation, upload, and readback preserve the caller's active cube binding");

        TextureCube faces(device, 2, false, SurfaceFormat::Color);
        bool allFacesExact = true;
        for (std::size_t face = 0; face < kFaces.size(); ++face)
        {
            const Color expected(
                static_cast<std::uint8_t>(20 + face * 30),
                static_cast<std::uint8_t>(230 - face * 20),
                static_cast<std::uint8_t>(40 + face * 17),
                static_cast<std::uint8_t>(100 + face * 19));
            const std::array<Color, 6> source{
                Color(250, 1, 2, 3), expected, expected, expected, expected,
                Color(4, 5, 6, 7)};
            faces.SetData(kFaces[face], source.data(), 1, 4);
            std::array<Color, 6> actual{
                Color(91, 92, 93, 94), Color(), Color(), Color(), Color(),
                Color(95, 96, 97, 98)};
            faces.GetData(kFaces[face], actual.data(), 1, 4);
            allFacesExact = allFacesExact && Matches(actual.front(), Color(91, 92, 93, 94)) &&
                Matches(actual.back(), Color(95, 96, 97, 98));
            for (int index = 1; index <= 4; ++index)
                allFacesExact = allFacesExact && Matches(actual[index], expected);
        }
        Check(allFacesExact,
              "all six XNA faces round-trip exact RGBA including alpha and startIndex isolation");

        TextureCube partial(device, 4, false, SurfaceFormat::Color);
        const Color background(9, 19, 29, 39);
        const std::vector<Color> backgroundPixels(16, background);
        partial.SetData(
            CubeMapFace::PositiveZ, backgroundPixels.data(),
            static_cast<int>(backgroundPixels.size()));
        const Rectangle patchRectangle(1, 0, 2, 3);
        const std::array<Color, 8> patchSource{
            Color(1, 1, 1, 1), Color(101, 11, 21, 31), Color(102, 12, 22, 32),
            Color(103, 13, 23, 33), Color(104, 14, 24, 34),
            Color(105, 15, 25, 35), Color(106, 16, 26, 36), Color(2, 2, 2, 2)};
        partial.SetData(
            CubeMapFace::PositiveZ, 0, &patchRectangle,
            patchSource.data(), 1, 6);
        std::array<Color, 8> patchRead{
            Color(71, 72, 73, 74), Color(), Color(), Color(), Color(), Color(), Color(),
            Color(75, 76, 77, 78)};
        partial.GetData(
            CubeMapFace::PositiveZ, 0, &patchRectangle,
            patchRead.data(), 1, 6);
        std::array<Color, 16> completeRead{};
        partial.GetData(
            CubeMapFace::PositiveZ, completeRead.data(),
            static_cast<int>(completeRead.size()));
        bool partialExact = Matches(patchRead.front(), Color(71, 72, 73, 74)) &&
            Matches(patchRead.back(), Color(75, 76, 77, 78));
        for (int index = 0; index < 6; ++index)
            partialExact = partialExact && Matches(patchRead[index + 1], patchSource[index + 1]);
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const bool inPatch = x >= 1 && x < 3 && y < 3;
                const Color expected = inPatch
                    ? patchSource[1 + y * 2 + (x - 1)]
                    : background;
                partialExact = partialExact && Matches(completeRead[y * 4 + x], expected);
            }
        }
        Check(partialExact,
              "asymmetric top-edge rectangle preserves row order, untouched texels, and destinations");

        const Color finalMipColor(17, 117, 217, 67);
        mipmapped.SetData(CubeMapFace::NegativeY, 3, nullptr, &finalMipColor, 0, 1);
        Color finalMipRead(0, 0, 0, 0);
        mipmapped.GetData(CubeMapFace::NegativeY, 3, nullptr, &finalMipRead, 0, 1);
        Check(Matches(finalMipRead, finalMipColor),
              "the final 1x1 mip level is independently writable and readable");

        mipmapped.GetRenderer().BindGL(5);
        Check(Bridge::GetBoundTextureCubeForTesting(5) == resource.texture,
              "ITextureCubeRenderer::BindGL reaches rlgl's cube-map binding path");
        Bridge::BindTextureCube(0, 5);
        Bridge::BindTextureCube(0, 2);

        const std::array supportedFormats{
            SurfaceFormat::Color, SurfaceFormat::Dxt1,
            SurfaceFormat::Dxt3, SurfaceFormat::Dxt5};
        const std::array unsupportedFormats{
            SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551,
            SurfaceFormat::Bgra4444, SurfaceFormat::NormalizedByte2,
            SurfaceFormat::NormalizedByte4, SurfaceFormat::Rgba1010102,
            SurfaceFormat::Rg32, SurfaceFormat::Rgba64, SurfaceFormat::Alpha8,
            SurfaceFormat::Single, SurfaceFormat::Vector2, SurfaceFormat::Vector4,
            SurfaceFormat::HalfSingle, SurfaceFormat::HalfVector2,
            SurfaceFormat::HalfVector4, SurfaceFormat::HdrBlendable};
        bool classificationsExact = true;
        for (const SurfaceFormat format : supportedFormats)
        {
            classificationsExact = classificationsExact &&
                renderer.ClassifyTextureCubeFormatEXT(static_cast<int>(format)) ==
                    CNA::Internal::Renderers::RendererFormatVerdict::Supported;
        }
        for (const SurfaceFormat format : unsupportedFormats)
        {
            classificationsExact = classificationsExact &&
                renderer.ClassifyTextureCubeFormatEXT(static_cast<int>(format)) ==
                    CNA::Internal::Renderers::RendererFormatVerdict::Unsupported;
        }
        classificationsExact = classificationsExact &&
            renderer.IsCompressedCubeTransferFormatEXT(
                static_cast<int>(SurfaceFormat::Dxt1)) &&
            renderer.IsCompressedCubeTransferFormatEXT(
                static_cast<int>(SurfaceFormat::Dxt3)) &&
            renderer.IsCompressedCubeTransferFormatEXT(
                static_cast<int>(SurfaceFormat::Dxt5)) &&
            !renderer.IsCompressedCubeTransferFormatEXT(
                static_cast<int>(SurfaceFormat::Color));
        Check(classificationsExact,
              "cube classification exposes exactly Color plus DXT1/3/5 transfer-complete formats");

        bool unsupportedConstructionRefused = true;
        for (const SurfaceFormat format : unsupportedFormats)
        {
            try
            {
                TextureCube unsupported(device, 2, false, format);
                unsupportedConstructionRefused = false;
            }
            catch (const System::NotSupportedException&)
            {
            }
        }
        Check(unsupportedConstructionRefused,
              "all classic formats without an exact public cube transfer are refused at creation");

        const bool fallbackRequested = [] {
            const char* value = std::getenv("CNA_RLGL_FORCE_DXT_FALLBACK");
            return value != nullptr && value[0] != '\0' && value[0] != '0';
        }();
        const std::array dxtCases{
            std::pair{SurfaceFormat::Dxt1, std::pair{std::uint16_t{0xF800u}, Color::Red}},
            std::pair{SurfaceFormat::Dxt3,
                      std::pair{std::uint16_t{0x07E0u}, Color(0, 255, 0, 255)}},
            std::pair{SurfaceFormat::Dxt5, std::pair{std::uint16_t{0x001Fu}, Color::Blue}}};
        bool allDxtExact = true;
        bool storageModesExact = true;
        for (const auto& [format, sample] : dxtCases)
        {
            TextureCube cube(device, 4, false, format);
            const std::vector<std::uint8_t> block = SolidDxtBlock(format, sample.first);
            cube.SetData(CubeMapFace::PositiveX, block.data(), static_cast<int>(block.size()));
            std::vector<Color> pixels(16, Color::Transparent);
            cube.GetData(
                CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
            allDxtExact = allDxtExact && AllMatch(pixels, sample.second);

            const auto cubeResource = Rlgl::GetTextureCubeResourceSnapshotForTesting(
                cube.GetRenderer());
            const auto cubeNative = Bridge::GetTextureCubeSnapshotForTesting(
                cubeResource.texture, cubeResource.levelCount);
            int compressedInternalFormat = 0;
            if (format == SurfaceFormat::Dxt1) compressedInternalFormat = 0x83F1;
            else if (format == SurfaceFormat::Dxt3) compressedInternalFormat = 0x83F2;
            else compressedInternalFormat = 0x83F3;
            storageModesExact = storageModesExact &&
                (!fallbackRequested || !cubeResource.nativeCompressed) &&
                cubeNative.internalFormat ==
                    (cubeResource.nativeCompressed ? compressedInternalFormat : 0x8058);
        }
        Check(allDxtExact,
              "DXT1/3/5 block uploads read back exact decoded red, green, and blue texels");
        Check(storageModesExact,
              "each DXT cube reports exact native S3TC or forced RGBA8 fallback storage");

        bool compressedAlphaExact = true;
        {
            TextureCube dxt1(device, 4, false, SurfaceFormat::Dxt1);
            std::vector<std::uint8_t> transparentDxt1(8u, 0u);
            transparentDxt1[4] = transparentDxt1[5] =
                transparentDxt1[6] = transparentDxt1[7] = 0xFFu;
            dxt1.SetData(
                CubeMapFace::PositiveX, transparentDxt1.data(),
                static_cast<int>(transparentDxt1.size()));
            std::vector<Color> pixels(16, Color::White);
            dxt1.GetData(
                CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
            compressedAlphaExact = compressedAlphaExact &&
                AllMatch(pixels, Color(0, 0, 0, 0));
        }
        {
            TextureCube dxt3(device, 4, false, SurfaceFormat::Dxt3);
            auto quarterAlpha = SolidDxtBlock(SurfaceFormat::Dxt3, 0xF800u);
            std::fill_n(quarterAlpha.begin(), 8, 0x44u);
            dxt3.SetData(
                CubeMapFace::PositiveX, quarterAlpha.data(),
                static_cast<int>(quarterAlpha.size()));
            std::vector<Color> pixels(16, Color::White);
            dxt3.GetData(
                CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
            compressedAlphaExact = compressedAlphaExact &&
                AllMatch(pixels, Color(255, 0, 0, 68));
        }
        {
            TextureCube dxt5(device, 4, false, SurfaceFormat::Dxt5);
            auto zeroAlpha = SolidDxtBlock(SurfaceFormat::Dxt5, 0x001Fu);
            zeroAlpha[0] = 0u;
            zeroAlpha[1] = 255u;
            dxt5.SetData(
                CubeMapFace::PositiveX, zeroAlpha.data(),
                static_cast<int>(zeroAlpha.size()));
            std::vector<Color> pixels(16, Color::White);
            dxt5.GetData(
                CubeMapFace::PositiveX, pixels.data(), static_cast<int>(pixels.size()));
            compressedAlphaExact = compressedAlphaExact &&
                AllMatch(pixels, Color(0, 0, 255, 0));
        }
        Check(compressedAlphaExact,
              "DXT1 transparency plus DXT3/DXT5 alpha decode exactly on both storage paths");

        TextureCube compressedFaces(device, 4, false, SurfaceFormat::Dxt1);
        const std::array<std::uint16_t, 6> faceColors{
            0xF800u, 0x07E0u, 0x001Fu, 0xFFE0u, 0xF81Fu, 0x07FFu};
        const std::array decodedFaceColors{
            Color::Red, Color(0, 255, 0, 255), Color::Blue, Color::Yellow,
            Color::Magenta, Color::Cyan};
        for (std::size_t face = 0; face < kFaces.size(); ++face)
        {
            const auto block = SolidDxtBlock(SurfaceFormat::Dxt1, faceColors[face]);
            compressedFaces.SetData(
                kFaces[face], block.data(), static_cast<int>(block.size()));
        }
        bool compressedFacesExact = true;
        for (std::size_t face = 0; face < kFaces.size(); ++face)
        {
            std::vector<Color> pixels(16, Color::Transparent);
            compressedFaces.GetData(
                kFaces[face], pixels.data(), static_cast<int>(pixels.size()));
            compressedFacesExact = compressedFacesExact &&
                AllMatch(pixels, decodedFaceColors[face]);
        }
        Check(compressedFacesExact,
              "six DXT1 faces preserve six distinct block-compressed colors");

        TextureCube compressedPartial(device, 7, true, SurfaceFormat::Dxt1);
        const auto redBlock = SolidDxtBlock(SurfaceFormat::Dxt1, 0xF800u);
        const auto blueBlock = SolidDxtBlock(SurfaceFormat::Dxt1, 0x001Fu);
        std::vector<std::uint8_t> fourRedBlocks;
        for (int block = 0; block < 4; ++block)
            fourRedBlocks.insert(fourRedBlocks.end(), redBlock.begin(), redBlock.end());
        compressedPartial.SetData(
            CubeMapFace::NegativeZ, fourRedBlocks.data(),
            static_cast<int>(fourRedBlocks.size()));
        const Rectangle topRight(4, 0, 3, 4);
        compressedPartial.SetData(
            CubeMapFace::NegativeZ, 0, &topRight,
            blueBlock.data(), 0, static_cast<int>(blueBlock.size()));
        std::vector<Color> compressedPartialRead(49, Color::Transparent);
        compressedPartial.GetData(
            CubeMapFace::NegativeZ, compressedPartialRead.data(),
            static_cast<int>(compressedPartialRead.size()));
        bool compressedPartialExact = true;
        for (int y = 0; y < 7; ++y)
        {
            for (int x = 0; x < 7; ++x)
            {
                compressedPartialExact = compressedPartialExact && Matches(
                    compressedPartialRead[static_cast<std::size_t>(y * 7 + x)],
                    x >= 4 && y < 4 ? Color::Blue : Color::Red);
            }
        }
        Check(compressedPartialExact,
              "NPOT DXT partial update changes only its edge block on native and fallback paths");

        TextureCube compressedMip(device, 7, true, SurfaceFormat::Dxt5);
        const auto greenBlock = SolidDxtBlock(SurfaceFormat::Dxt5, 0x07E0u);
        compressedMip.SetData(
            CubeMapFace::PositiveY, 1, nullptr,
            greenBlock.data(), 0, static_cast<int>(greenBlock.size()));
        std::vector<Color> compressedMipRead(9, Color::Transparent);
        compressedMip.GetData(
            CubeMapFace::PositiveY, 1, nullptr,
            compressedMipRead.data(), 0, static_cast<int>(compressedMipRead.size()));
        Check(AllMatch(compressedMipRead, Color(0, 255, 0, 255)),
              "a sub-4x4 NPOT DXT5 mip retains and decodes its padded block");

        bool colorUploadRefused = false;
        try
        {
            const std::vector<Color> colors(16, Color::White);
            compressedFaces.SetData(
                CubeMapFace::PositiveX, colors.data(), static_cast<int>(colors.size()));
        }
        catch (const System::NotSupportedException&)
        {
            colorUploadRefused = true;
        }
        Check(colorUploadRefused,
              "a Color payload cannot silently replace the compressed cube transfer contract");

        bool contentLoaderExact = false;
        try
        {
            using Microsoft::Xna::Framework::Content::ContentManager;
            using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterTextureCubeXnbReader();
            ContentManager content(
                nullptr, "tests/assets/xnb/monogame/windows/uncompressed");
            content.setGraphicsDevice(device);
            TextureCube loaded = content.Load<TextureCube>("SampleCube64DXT1Mips");
            const auto loadedResource = Rlgl::GetTextureCubeResourceSnapshotForTesting(
                loaded.GetRenderer());
            const Color sentinel(0xA5, 0xA5, 0xA5, 0xA5);
            std::vector<Color> finalLevel(1, sentinel);
            loaded.GetData(
                CubeMapFace::NegativeZ, 6, nullptr,
                finalLevel.data(), 0, static_cast<int>(finalLevel.size()));
            contentLoaderExact = loaded.getFormatProperty() == SurfaceFormat::Dxt1 &&
                loaded.getLevelCountProperty() == 7 && loadedResource.size == 64 &&
                loadedResource.surfaceFormat == static_cast<int>(SurfaceFormat::Dxt1) &&
                finalLevel[0].getPackedValueProperty() != sentinel.getPackedValueProperty();
            ContentTypeReaderManager::ClearTypeCreators();
        }
        catch (...)
        {
            Microsoft::Xna::Framework::Content::ContentTypeReaderManager::ClearTypeCreators();
        }
        Check(contentLoaderExact,
              "the external XNB DXT1 cube keeps all six compressed seven-level mip chains");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglTextureCubeTest game;
    game.Run();
    return game.getResultProperty();
}
