// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-044: focused plain SurfaceFormat.Color TextureCube validation.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
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
                      CNA::Internal::Renderers::RendererFormatVerdict::Unsupported &&
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
