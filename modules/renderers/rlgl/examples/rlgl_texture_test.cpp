// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-023/RLGL-026/RLGL-027: focused native Texture2D storage evidence. Public
// Texture2D construction and SetData feed rlgl or the documented bridge; direct renderer readback
// prevents the public CPU shadow from hiding an upload defect. Exit 77 means no usable GL context.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "RlglBridge.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <type_traits>
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

    template <typename Texel>
    [[nodiscard]] bool FloatFormatRoundTrips(
        GraphicsDevice& device, const SurfaceFormat format,
        std::array<Texel, 6> expected, const Texel& patch)
    {
        static_assert(std::is_trivially_copyable_v<Texel>);
        Texture2D texture(device, 3, 2, false, format);
        texture.SetData(expected.data(), static_cast<int>(expected.size()));
        auto& native = texture.GetRenderer();
        std::array<std::uint8_t, sizeof(Texel) * 6u> actual{};
        if (!native.GetData(
                0, 0, 0, 3, 2, actual.data(), static_cast<int>(actual.size())) ||
            std::memcmp(actual.data(), expected.data(), actual.size()) != 0)
        {
            return false;
        }

        const Rectangle patchRectangle(1, 0, 1, 1);
        texture.SetData(0, &patchRectangle, &patch, 0, 1);
        expected[1] = patch;
        actual.fill(0);
        return native.GetData(
                   0, 0, 0, 3, 2, actual.data(), static_cast<int>(actual.size()))
            && std::memcmp(actual.data(), expected.data(), actual.size()) == 0;
    }
}

class RlglTextureTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglTextureTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
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

        Check(PackedFormatRoundTrips<Packed::Rgba1010102, std::uint32_t>(
                  device, SurfaceFormat::Rgba1010102,
                  {0xC00003FFu, 0xC00FFC00u, 0xFFF00000u, 0xFFFFFFFFu,
                   0x12345678u, 0x89ABCDEFu}, 0xA5C39E71u),
              "Rgba1010102 preserves XNA's least-significant-red packed layout");
        Check(PackedFormatRoundTrips<Packed::Rg32, std::uint32_t>(
                  device, SurfaceFormat::Rg32,
                  {0x0000FFFFu, 0xFFFF0000u, 0x12345678u, 0x89ABCDEFu,
                   0x00000000u, 0xFFFFFFFFu}, 0xA5C39E71u),
              "Rg32 preserves exact two-channel unsigned-normalized words");
        Check(PackedFormatRoundTrips<Packed::Rgba64, std::uint64_t>(
                  device, SurfaceFormat::Rgba64,
                  {0x000000000000FFFFull, 0x00000000FFFF0000ull,
                   0x0000FFFF00000000ull, 0xFFFF000000000000ull,
                   0x123456789ABCDEFull, 0xFEDCBA9876543210ull},
                  0xA5C39E717E2D4B08ull),
              "Rgba64 preserves exact four-channel unsigned-normalized words");
        Check(PackedFormatRoundTrips<Packed::Alpha8, std::uint8_t>(
                  device, SurfaceFormat::Alpha8,
                  {0x00u, 0x20u, 0x40u, 0x80u, 0xC0u, 0xFFu}, 0xA5u),
              "Alpha8 preserves exact one-byte texels across odd rows");
        Check(PackedFormatRoundTrips<Packed::HalfSingle, std::uint16_t>(
                  device, SurfaceFormat::HalfSingle,
                  {0x0000u, 0x3C00u, 0xBC00u, 0x3800u, 0x4000u, 0xC100u}, 0x3400u),
              "HalfSingle preserves exact IEEE binary16 words");
        Check(PackedFormatRoundTrips<Packed::HalfVector2, std::uint32_t>(
                  device, SurfaceFormat::HalfVector2,
                  {0x3C000000u, 0x00003C00u, 0x3800BC00u, 0x40003400u,
                   0xC1004200u, 0x4400C400u}, 0xB8003A00u),
              "HalfVector2 preserves exact two-component binary16 words");
        const std::array<std::uint64_t, 6> halfVector4Words{
            0x3C00000000000000ull, 0x00003C0000000000ull,
            0x000000003C000000ull, 0x0000000000003C00ull,
            0x3C00380034003000ull, 0xBC00B800B400B000ull};
        Check(PackedFormatRoundTrips<Packed::HalfVector4, std::uint64_t>(
                  device, SurfaceFormat::HalfVector4, halfVector4Words,
                  0x40003C0038003400ull),
              "HalfVector4 preserves exact four-component binary16 words");
        Check(PackedFormatRoundTrips<Packed::HalfVector4, std::uint64_t>(
                  device, SurfaceFormat::HdrBlendable, halfVector4Words,
                  0x4400420040003C00ull),
              "HdrBlendable uses XNA-compatible HalfVector4 storage");

        Check(FloatFormatRoundTrips<float>(
                  device, SurfaceFormat::Single,
                  {-4.0f, -0.5f, 0.0f, 0.25f, 1.0f, 8.0f}, 3.5f),
              "Single preserves exact finite IEEE binary32 values");
        Check(FloatFormatRoundTrips<Vector2>(
                  device, SurfaceFormat::Vector2,
                  {Vector2(-4.0f, 8.0f), Vector2(-0.5f, 0.25f), Vector2(0.0f, 1.0f),
                   Vector2(2.0f, -2.0f), Vector2(3.5f, 7.25f), Vector2(-8.0f, 16.0f)},
                  Vector2(0.125f, -0.25f)),
              "Vector2 preserves exact two-component finite binary32 values");
        Check(FloatFormatRoundTrips<Vector4>(
                  device, SurfaceFormat::Vector4,
                  {Vector4(-4.0f, -2.0f, -1.0f, 0.0f),
                   Vector4(0.125f, 0.25f, 0.5f, 1.0f),
                   Vector4(2.0f, 4.0f, 8.0f, 16.0f),
                   Vector4(-0.5f, 3.5f, -7.25f, 9.0f),
                   Vector4(11.0f, 12.0f, 13.0f, 14.0f),
                   Vector4(-16.0f, -15.0f, -14.0f, -13.0f)},
                  Vector4(0.125f, -0.25f, 0.5f, -1.0f)),
              "Vector4 preserves exact four-component finite binary32 values");

        Texture2D vectorMip(device, 4, 4, true, SurfaceFormat::Vector2);
        const std::array<Vector2, 4> vectorMipValues{
            Vector2(0.125f, 0.25f), Vector2(0.5f, 1.0f),
            Vector2(-2.0f, 4.0f), Vector2(8.0f, -16.0f)};
        vectorMip.SetData(1, nullptr, vectorMipValues.data(), 0,
                          static_cast<int>(vectorMipValues.size()));
        std::array<std::uint8_t, sizeof(Vector2) * 4u> vectorMipBytes{};
        auto& nativeVectorMip = vectorMip.GetRenderer();
        Check(nativeVectorMip.GetData(
                  1, 0, 0, 2, 2, vectorMipBytes.data(),
                  static_cast<int>(vectorMipBytes.size())) &&
                  std::memcmp(vectorMipBytes.data(), vectorMipValues.data(),
                              vectorMipBytes.size()) == 0,
              "two-channel float storage supports exact non-zero mip updates");

        const auto sample = [&](Texture2D& source) {
            device.Clear(Color(17, 29, 43, 255));
            source.GetRenderer().BindGL(0);
            renderer.SetBlendEnabled(false);
            CNA::Internal::Renderers::Rlgl::Bridge::DrawBoundTextureSampleForTesting(
                0.5f, 0.5f, 64, 48);
            Color pixel;
            const Rectangle center(32, 24, 1, 1);
            device.GetBackBufferData(&center, &pixel, 0, 1);
            CNA::Internal::Renderers::Rlgl::Bridge::BindTexture2D(0, 0);
            return pixel;
        };
        Texture2D alphaTexture(device, 1, 1, false, SurfaceFormat::Alpha8);
        const Packed::Alpha8 alphaValue(0.5f);
        alphaTexture.SetData(&alphaValue, 1);
        const Color sampledAlpha = sample(alphaTexture);
        Check(sampledAlpha.getRProperty() <= 1 && sampledAlpha.getGProperty() <= 1 &&
                  sampledAlpha.getBProperty() <= 1 &&
                  sampledAlpha.getAProperty() >= 127 && sampledAlpha.getAProperty() <= 129,
              "Alpha8 sampling expands its stored red byte to alpha over zero RGB");

        Texture2D singleTexture(device, 1, 1, false, SurfaceFormat::Single);
        const float singleValue = 0.25f;
        singleTexture.SetData(&singleValue, 1);
        const Color sampledSingle = sample(singleTexture);
        Check(sampledSingle.getRProperty() >= 63 && sampledSingle.getRProperty() <= 65 &&
                  sampledSingle.getGProperty() == 255 && sampledSingle.getBProperty() == 255 &&
                  sampledSingle.getAProperty() == 255,
              "Single sampling applies XNA's (R,1,1,1) missing-channel expansion");

        Texture2D vector2Texture(device, 1, 1, false, SurfaceFormat::Vector2);
        const Vector2 vector2Value(0.25f, 0.5f);
        vector2Texture.SetData(&vector2Value, 1);
        const Color sampledVector2 = sample(vector2Texture);
        Check(sampledVector2.getRProperty() >= 63 && sampledVector2.getRProperty() <= 65 &&
                  sampledVector2.getGProperty() >= 127 && sampledVector2.getGProperty() <= 129 &&
                  sampledVector2.getBProperty() == 255 && sampledVector2.getAProperty() == 255,
              "Vector2 sampling applies XNA's (R,G,1,1) missing-channel expansion");

        Texture2D normalized2Texture(
            device, 1, 1, false, SurfaceFormat::NormalizedByte2);
        const Packed::NormalizedByte2 normalized2Value(0.25f, 0.5f);
        normalized2Texture.SetData(&normalized2Value, 1);
        const Color sampledNormalized2 = sample(normalized2Texture);
        Check(sampledNormalized2.getRProperty() >= 62 &&
                  sampledNormalized2.getRProperty() <= 66 &&
                  sampledNormalized2.getGProperty() >= 126 &&
                  sampledNormalized2.getGProperty() <= 130 &&
                  sampledNormalized2.getBProperty() == 255 &&
                  sampledNormalized2.getAProperty() == 255,
              "NormalizedByte2 sampling applies XNA's (R,G,1,1) channel expansion");

        const std::array supportedFormats{
            SurfaceFormat::Color, SurfaceFormat::Bgr565, SurfaceFormat::Bgra5551,
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
                renderer.ClassifySurfaceFormatEXT(static_cast<int>(format)) ==
                    CNA::Internal::Renderers::RendererFormatVerdict::Supported;
        }
        classificationsExact = classificationsExact &&
            renderer.ClassifySurfaceFormatEXT(static_cast<int>(SurfaceFormat::Dxt1)) ==
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
