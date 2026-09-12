// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-023: focused native RGBA8 Texture2D storage evidence. Public Texture2D
// construction and SetData feed rlgl's resource wrappers; direct renderer readback prevents the
// public CPU shadow from hiding an upload defect. Exit 77 means no usable GL context.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "RlglBridge.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    [[nodiscard]] std::vector<std::uint8_t> ToBytes(const std::vector<Color>& colors)
    {
        std::vector<std::uint8_t> bytes(colors.size() * 4u);
        for (std::size_t index = 0; index < colors.size(); ++index)
        {
            bytes[index * 4u + 0u] = colors[index].getRProperty();
            bytes[index * 4u + 1u] = colors[index].getGProperty();
            bytes[index * 4u + 2u] = colors[index].getBProperty();
            bytes[index * 4u + 3u] = colors[index].getAProperty();
        }
        return bytes;
    }
}

class RlglTextureTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglTextureTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(64);
        graphics_->setPreferredBackBufferHeightProperty(48);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Rlgl::RlglRenderer&>(
            device.GetRenderer());

        Check(renderer.GetMaxTextureDimension() > 0,
              "RLGL reports the live GL_MAX_TEXTURE_SIZE value");

        Texture2D texture(device, 4, 4);
        Check(texture.GetRenderer().GetWidth() == 4 && texture.GetRenderer().GetHeight() == 4 &&
                  texture.GetRenderer().GetSurfaceFormatEXT() ==
                      static_cast<int>(SurfaceFormat::Color),
              "Texture2D dimensions and SurfaceFormat.Color reach the renderer record");

        std::vector<Color> expected;
        expected.reserve(16);
        for (int index = 0; index < 16; ++index)
        {
            expected.emplace_back(
                static_cast<std::uint8_t>(10 + index),
                static_cast<std::uint8_t>(40 + index * 2),
                static_cast<std::uint8_t>(90 + index * 3),
                static_cast<std::uint8_t>(200 + index));
        }
        texture.SetData(expected.data(), static_cast<int>(expected.size()));
        // A full ordinary Texture2D upload deliberately detaches shared renderer storage, so take
        // the private record only after SetData has installed that new native texture.
        auto& native = texture.GetRenderer();

        std::vector<std::uint8_t> nativePixels(4u * 4u * 4u);
        const bool readWhole = native.GetData(
            0, 0, 0, 4, 4, nativePixels.data(), static_cast<int>(nativePixels.size()));
        Check(readWhole && nativePixels == ToBytes(expected),
              "rlgl level-zero upload round-trips exact RGBA8 bytes from native storage");

        const Rectangle patchRect(1, 1, 2, 2);
        const std::array<Color, 4> patch{
            Color(240, 10, 20, 30), Color(210, 40, 50, 60),
            Color(180, 70, 80, 90), Color(150, 100, 110, 120)};
        texture.SetData(0, &patchRect, patch.data(), 0, static_cast<int>(patch.size()));
        expected[5] = patch[0];
        expected[6] = patch[1];
        expected[9] = patch[2];
        expected[10] = patch[3];
        nativePixels.assign(nativePixels.size(), 0);
        const bool readPatched = native.GetData(
            0, 0, 0, 4, 4, nativePixels.data(), static_cast<int>(nativePixels.size()));
        Check(readPatched && nativePixels == ToBytes(expected),
              "public partial SetData preserves untouched native level-zero texels");

        std::array<std::uint8_t, 16> subRectangle{};
        const bool readSubRectangle = native.GetData(
            0, 1, 1, 2, 2, subRectangle.data(), static_cast<int>(subRectangle.size()));
        const auto expectedPatch = ToBytes(std::vector<Color>(patch.begin(), patch.end()));
        Check(readSubRectangle &&
                  std::equal(subRectangle.begin(), subRectangle.end(), expectedPatch.begin()),
              "native subrectangle readback keeps top-row-first upload order");

        native.BindGL(3);
        Check(CNA::Internal::Renderers::Rlgl::Bridge::GetBoundTexture2DForTesting(3) != 0,
              "ITextureRenderer::BindGL reaches rlgl texture-unit binding");
        CNA::Internal::Renderers::Rlgl::Bridge::BindTexture2D(0, 3);

        Texture2D mipmapped(device, 4, 4, true, SurfaceFormat::Color);
        auto& nativeMip = mipmapped.GetRenderer();
        Check(mipmapped.getLevelCountProperty() == 3 &&
                  nativeMip.HasDefinedMipLevel(0) && !nativeMip.HasDefinedMipLevel(1),
              "declared mip storage distinguishes allocated from defined levels");

        const std::array<Color, 4> levelOne{
            Color(1, 2, 3, 4), Color(5, 6, 7, 8),
            Color(9, 10, 11, 12), Color(13, 14, 15, 16)};
        mipmapped.SetData(1, nullptr, levelOne.data(), 0, 4);
        std::array<std::uint8_t, 16> levelOneBytes{};
        const bool readLevelOne = nativeMip.GetData(
            1, 0, 0, 2, 2, levelOneBytes.data(), static_cast<int>(levelOneBytes.size()));
        const auto expectedLevelOne = ToBytes(
            std::vector<Color>(levelOne.begin(), levelOne.end()));
        Check(nativeMip.HasDefinedMipLevel(1) && readLevelOne &&
                  std::equal(levelOneBytes.begin(), levelOneBytes.end(), expectedLevelOne.begin()),
              "non-zero mip upload and readback use the declared native level");

        bool rejectedInvalidLevel = false;
        try
        {
            (void)nativeMip.GetData(3, 0, 0, 1, 1, levelOneBytes.data(), 4);
        }
        catch (const std::out_of_range&)
        {
            rejectedInvalidLevel = true;
        }
        Check(rejectedInvalidLevel, "native readback rejects an out-of-range mip level");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglTextureTest game;
    game.Run();
    return game.getResultProperty();
}
