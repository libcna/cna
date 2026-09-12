// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-023/RLGL-026: focused native Texture2D storage evidence. Public Texture2D
// construction and SetData feed rlgl or the documented bridge; direct renderer readback prevents
// the public CPU shadow from hiding an upload defect. Exit 77 means no usable GL context.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
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

    template <typename Packed, typename Word>
    [[nodiscard]] bool PackedFormatRoundTrips(
        GraphicsDevice& device, const SurfaceFormat format,
        std::array<Word, 6> words, const Word patch)
    {
        std::array<Packed, 6> values{};
        for (std::size_t index = 0; index < values.size(); ++index)
            values[index].setPackedValueProperty(words[index]);

        Texture2D texture(device, 3, 2, false, format);
        texture.SetData(values.data(), static_cast<int>(values.size()));
        auto& native = texture.GetRenderer();
        std::array<std::uint8_t, sizeof(Word) * 6u> actual{};
        if (native.GetSurfaceFormatEXT() != static_cast<int>(format) ||
            !native.GetData(
                0, 0, 0, 3, 2, actual.data(), static_cast<int>(actual.size())))
        {
            return false;
        }

        const auto encoded = [](const std::array<Word, 6>& source) {
            std::array<std::uint8_t, sizeof(Word) * 6u> result{};
            for (std::size_t index = 0; index < source.size(); ++index)
            {
                for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                {
                    result[index * sizeof(Word) + byte] = static_cast<std::uint8_t>(
                        source[index] >> (byte * 8u));
                }
            }
            return result;
        };
        if (actual != encoded(words)) return false;

        Packed patchValue;
        patchValue.setPackedValueProperty(patch);
        const Rectangle patchRectangle(1, 0, 1, 1);
        texture.SetData(0, &patchRectangle, &patchValue, 0, 1);
        words[1] = patch;
        actual.fill(0);
        return native.GetData(
                   0, 0, 0, 3, 2, actual.data(), static_cast<int>(actual.size()))
            && actual == encoded(words);
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

        namespace Packed = Microsoft::Xna::Framework::Graphics::PackedVector;
        Check(PackedFormatRoundTrips<Packed::Bgr565, std::uint16_t>(
                  device, SurfaceFormat::Bgr565,
                  {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0x39E7u, 0xA55Au}, 0x5AA5u),
              "Bgr565 exact odd-row packed storage survives full and partial public updates");
        Check(PackedFormatRoundTrips<Packed::Bgra5551, std::uint16_t>(
                  device, SurfaceFormat::Bgra5551,
                  {0xFC00u, 0x83E0u, 0x801Fu, 0xFFFFu, 0x4211u, 0xDEE5u}, 0xA55Au),
              "Bgra5551 XNA-to-GL bit rotation reverses exactly on native readback");
        Check(PackedFormatRoundTrips<Packed::Bgra4444, std::uint16_t>(
                  device, SurfaceFormat::Bgra4444,
                  {0xFF00u, 0xF0F0u, 0xF00Fu, 0xFFFFu, 0x1357u, 0xECA8u}, 0xA5C3u),
              "Bgra4444 XNA-to-GL nibble rotation reverses exactly on native readback");
        Check(PackedFormatRoundTrips<Packed::NormalizedByte2, std::uint16_t>(
                  device, SurfaceFormat::NormalizedByte2,
                  {0x817Fu, 0x0040u, 0xC020u, 0x7F01u, 0x1030u, 0xE151u}, 0x21DFu),
              "NormalizedByte2 uses exact signed RG8 storage for native transfer");
        Check(PackedFormatRoundTrips<Packed::NormalizedByte4, std::uint32_t>(
                  device, SurfaceFormat::NormalizedByte4,
                  {0x7F0181FFu, 0x2040607Fu, 0x81C0E001u, 0x10203040u,
                   0x11223344u, 0xE1D1C1B1u}, 0x7F41DF01u),
              "NormalizedByte4 uses exact signed RGBA8 storage for native transfer");

        const std::array supportedFormats{
            SurfaceFormat::Color, SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551,
            SurfaceFormat::Bgra4444, SurfaceFormat::NormalizedByte2,
            SurfaceFormat::NormalizedByte4};
        bool classificationsExact = true;
        for (const SurfaceFormat format : supportedFormats)
        {
            classificationsExact = classificationsExact &&
                renderer.ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
                    CNA::Internal::Renderers::RendererFormatVerdict::Supported;
        }
        classificationsExact = classificationsExact &&
            renderer.ClassifySurfaceFormatEXT(static_cast<int>(SurfaceFormat::Rgba1010102)) ==
                CNA::Internal::Renderers::RendererFormatVerdict::Unsupported;
        Check(classificationsExact,
              "Texture2D format classification claims only completed native layouts");
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
