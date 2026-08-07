// SPDX-License-Identifier: MS-PL
// SKIA-138: exact float/half Texture2D transfer, mip, sampling, HDR and refusal gates.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;
using CNA::Internal::Backends::Skia::SkiaSourceAlphaConvention;
using CNA::Internal::Backends::Skia::SkiaTextureBackend;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    [[nodiscard]] float FloatFromBits(std::uint32_t bits) noexcept
    {
        return std::bit_cast<float>(bits);
    }

    template<typename Element>
    struct FloatTraits;

    template<>
    struct FloatTraits<float>
    {
        static constexpr int Components = 1;
        [[nodiscard]] static float Get(const float& value, int) noexcept { return value; }
        static void Set(float& value, int, float channel) noexcept { value = channel; }
    };

    template<>
    struct FloatTraits<Vector2>
    {
        static constexpr int Components = 2;
        [[nodiscard]] static float Get(const Vector2& value, int channel) noexcept
        {
            return channel == 0 ? value.X : value.Y;
        }
        static void Set(Vector2& value, int channel, float component) noexcept
        {
            if (channel == 0) value.X = component;
            else value.Y = component;
        }
    };

    template<>
    struct FloatTraits<Vector4>
    {
        static constexpr int Components = 4;
        [[nodiscard]] static float Get(const Vector4& value, int channel) noexcept
        {
            switch (channel)
            {
                case 0: return value.X;
                case 1: return value.Y;
                case 2: return value.Z;
                default: return value.W;
            }
        }
        static void Set(Vector4& value, int channel, float component) noexcept
        {
            switch (channel)
            {
                case 0: value.X = component; break;
                case 1: value.Y = component; break;
                case 2: value.Z = component; break;
                default: value.W = component; break;
            }
        }
    };

    template<typename Element>
    [[nodiscard]] Element MakeFloatElement(
        const std::array<std::uint32_t, 4>& bits) noexcept
    {
        Element result{};
        for (int channel = 0; channel < FloatTraits<Element>::Components; ++channel)
            FloatTraits<Element>::Set(result, channel, FloatFromBits(bits[channel]));
        return result;
    }

    template<typename Element>
    [[nodiscard]] bool SameFloatBits(const Element& left, const Element& right) noexcept
    {
        for (int channel = 0; channel < FloatTraits<Element>::Components; ++channel)
        {
            if (std::bit_cast<std::uint32_t>(FloatTraits<Element>::Get(left, channel))
                != std::bit_cast<std::uint32_t>(FloatTraits<Element>::Get(right, channel)))
            {
                return false;
            }
        }
        return true;
    }

    template<typename Packed, typename Word>
    [[nodiscard]] Packed MakePacked(Word word)
    {
        Packed result;
        result.setPackedValueProperty(word);
        return result;
    }

    template<typename Callable>
    [[nodiscard]] bool Throws(Callable&& callable)
    {
        try { callable(); }
        catch (const std::exception&) { return true; }
        return false;
    }

    [[nodiscard]] bool SameStats(const SkiaResourceStats& left,
                                 const SkiaResourceStats& right) noexcept
    {
        return left.textureBackends == right.textureBackends
            && left.textureImageViews == right.textureImageViews
            && left.textureImageBytes == right.textureImageBytes
            && left.mipChains2D == right.mipChains2D
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes
            && left.renderTargets == right.renderTargets
            && left.targetSurfaceBytes == right.targetSurfaceBytes;
    }
}

class InspectableFloatTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureBackend* SkiaBackend() const
    {
        return dynamic_cast<SkiaTextureBackend*>(GetBackendRaw());
    }
};

class SkiaTexture2DFloatFormatsTest final : public Game
{
public:
    SkiaTexture2DFloatFormatsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(28);
        graphics_->setPreferredBackBufferHeightProperty(4);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* graphicsBackend = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(graphicsBackend != nullptr, "public GraphicsDevice owns the Skia backend");
        if (!graphicsBackend)
        {
            Exit();
            return;
        }

        const SkiaResourceStats baseline = graphicsBackend->GetResourceStatsEXT();
        ExerciseFloat32<float>(device, *graphicsBackend, SurfaceFormat::Single, "Single");
        ExerciseFloat32<Vector2>(device, *graphicsBackend, SurfaceFormat::Vector2, "Vector2");
        ExerciseFloat32<Vector4>(device, *graphicsBackend, SurfaceFormat::Vector4, "Vector4");
        ExerciseHalf<HalfSingle, std::uint16_t>(
            device, *graphicsBackend, SurfaceFormat::HalfSingle, "HalfSingle", 1);
        ExerciseHalf<HalfVector2, std::uint32_t>(
            device, *graphicsBackend, SurfaceFormat::HalfVector2, "HalfVector2", 2);
        ExerciseHalf<HalfVector4, std::uint64_t>(
            device, *graphicsBackend, SurfaceFormat::HalfVector4, "HalfVector4", 4);
        ExerciseHalf<HalfVector4, std::uint64_t>(
            device, *graphicsBackend, SurfaceFormat::HdrBlendable, "HdrBlendable", 4);
        CheckSpecialMipPolicy(device);
        CheckMetadataSamplingAndHdr(device);
        CheckTypedRefusalAndTargetPromotion(device, *graphicsBackend);
        Check(SameStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "all float fixtures return Skia resource counters to baseline");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

private:
    template<typename Element>
    void ExerciseFloat32(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend,
                         SurfaceFormat format, const char* name)
    {
        constexpr int components = FloatTraits<Element>::Components;
        const std::array bitRows{
            std::array<std::uint32_t, 4>{0x80000000u, 0x3E800000u, 0x3F000000u, 0x3F800000u},
            std::array<std::uint32_t, 4>{0x7F800000u, 0xFF800000u, 0x7FC12345u, 0xBF800000u},
            std::array<std::uint32_t, 4>{0x7FA54321u, 0x00000001u, 0x00800000u, 0x40000000u},
            std::array<std::uint32_t, 4>{0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u},
        };
        std::array<Element, 4> source{};
        for (std::size_t index = 0; index < source.size(); ++index)
            source[index] = MakeFloatElement<Element>(bitRows[index]);

        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        {
            InspectableFloatTexture2D texture(device, 2, 2, false, format);
            SkiaTextureBackend* backend = texture.SkiaBackend();
            const std::size_t transferBytes = static_cast<std::size_t>(components) * 4u;
            const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
            Check(backend && backend->FormatEXT() == format
                      && allocated.textureBackends == before.textureBackends + 1u
                      && allocated.textureImageBytes == before.textureImageBytes + 128u
                      && allocated.mipChain2DStorageBytes
                          == before.mipChain2DStorageBytes + 4u * transferBytes,
                  std::string(name) + " constructs with exact transfer/working accounting");

            texture.SetData(source.data(), static_cast<int>(source.size()));
            std::array<Element, 6> destination{};
            destination.front() = MakeFloatElement<Element>(
                {0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu, 0xDEADBEEFu});
            destination.back() = destination.front();
            texture.GetData(destination.data(), 1, 5);
            bool exact = SameFloatBits(destination.front(), destination.back());
            for (std::size_t index = 0; index < source.size(); ++index)
                exact = exact && SameFloatBits(destination[index + 1u], source[index]);
            Check(exact, std::string(name)
                      + " round-trips signed zero, subnormal, infinity and NaN payload bits");

            bool littleEndian = backend != nullptr;
            if (backend)
            {
                const std::uint8_t* raw = backend->MipChainEXT().LevelData(0);
                for (std::size_t texel = 0; texel < source.size(); ++texel)
                {
                    for (int channel = 0; channel < components; ++channel)
                    {
                        const std::uint32_t bits = bitRows[texel][channel];
                        for (std::size_t byte = 0; byte < 4u; ++byte)
                        {
                            littleEndian = littleEndian
                                && raw[texel * transferBytes + channel * 4u + byte]
                                    == static_cast<std::uint8_t>(bits >> (byte * 8u));
                        }
                    }
                }
            }
            Check(littleEndian, std::string(name) + " stores explicit little-endian IEEE words");

            const Rectangle patchRect(1, 0, 1, 2);
            const std::array<Element, 2> patch{
                MakeFloatElement<Element>({0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3ECCCCCDu}),
                MakeFloatElement<Element>({0x40A00000u, 0x40C00000u, 0x40E00000u, 0x41000000u}),
            };
            texture.SetData(0, &patchRect, patch.data(), 0, 2);
            std::array<Element, 2> patchRead{};
            texture.GetData(0, &patchRect, patchRead.data(), 0, 2);
            Check(SameFloatBits(patchRead[0], patch[0])
                      && SameFloatBits(patchRead[1], patch[1]),
                  std::string(name) + " partial rectangle preserves exact component words");
        }
        Check(SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              std::string(name) + " destruction releases transfer and working storage");
    }

    template<typename Packed, typename Word>
    void ExerciseHalf(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend,
                      SurfaceFormat format, const char* name, int channels)
    {
        const Word channelWords[4] = {
            static_cast<Word>(0x8000u), static_cast<Word>(0x7C00u),
            static_cast<Word>(0xFC00u), static_cast<Word>(0x7E55u)};
        std::array<Packed, 4> source{};
        for (std::size_t texel = 0; texel < source.size(); ++texel)
        {
            Word word = 0;
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto half = static_cast<Word>(
                    channelWords[(texel + static_cast<std::size_t>(channel)) % 4u]);
                word |= static_cast<Word>(half << (channel * 16));
            }
            source[texel] = MakePacked<Packed>(word);
        }

        const std::size_t bytesPerTexel = static_cast<std::size_t>(channels) * 2u;
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        {
            InspectableFloatTexture2D texture(device, 2, 2, false, format);
            SkiaTextureBackend* backend = texture.SkiaBackend();
            const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
            Check(backend && backend->FormatEXT() == format
                      && allocated.textureImageBytes
                          == before.textureImageBytes + 8u * bytesPerTexel
                      && allocated.mipChain2DStorageBytes
                          == before.mipChain2DStorageBytes + 4u * bytesPerTexel,
                  std::string(name) + " uses native half-float image/storage accounting");
            texture.SetData(source.data(), 4);
            std::array<Packed, 6> destination{};
            destination.front() = MakePacked<Packed>(static_cast<Word>(0x55u));
            destination.back() = destination.front();
            texture.GetData(destination.data(), 1, 5);
            bool exact = destination.front().getPackedValueProperty()
                == destination.back().getPackedValueProperty();
            for (std::size_t index = 0; index < source.size(); ++index)
            {
                exact = exact && destination[index + 1u].getPackedValueProperty()
                    == source[index].getPackedValueProperty();
            }
            Check(exact, std::string(name)
                      + " round-trips signed zero, infinities and NaN payload bits");

            bool littleEndian = backend != nullptr;
            if (backend)
            {
                const std::uint8_t* raw = backend->MipChainEXT().LevelData(0);
                for (std::size_t texel = 0; texel < source.size(); ++texel)
                {
                    const Word word = source[texel].getPackedValueProperty();
                    for (std::size_t byte = 0; byte < bytesPerTexel; ++byte)
                    {
                        littleEndian = littleEndian
                            && raw[texel * bytesPerTexel + byte]
                                == static_cast<std::uint8_t>(word >> (byte * 8u));
                    }
                }
            }
            Check(littleEndian, std::string(name) + " stores explicit little-endian half words");
        }
        Check(SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              std::string(name) + " destruction releases native image and mip storage");
    }

    void CheckSpecialMipPolicy(GraphicsDevice& device)
    {
        InspectableFloatTexture2D finite(device, 2, 2, true, SurfaceFormat::Vector4);
        const std::array<Vector4, 4> finiteSource{
            Vector4(0.0f, 2.0f, -4.0f, 1.0f), Vector4(2.0f, 4.0f, -2.0f, 0.5f),
            Vector4(4.0f, 6.0f, 0.0f, 0.0f), Vector4(6.0f, 8.0f, 2.0f, -0.5f),
        };
        finite.SetData(finiteSource.data(), 4);
        Vector4 average;
        finite.GetData(1, nullptr, &average, 0, 1);
        Check(average.X == 3.0f && average.Y == 5.0f
                  && average.Z == -1.0f && average.W == 0.25f,
              "float mip generation averages finite components in extended range");

        InspectableFloatTexture2D special(device, 2, 2, true, SurfaceFormat::Single);
        const std::array<float, 4> specialSource{
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity(), 1.0f, 2.0f};
        special.SetData(specialSource.data(), 4);
        float specialAverage = 0.0f;
        special.GetData(1, nullptr, &specialAverage, 0, 1);
        Check(std::bit_cast<std::uint32_t>(specialAverage) == 0x7FC00000u,
              "opposing infinities generate canonical positive quiet NaN in float mips");

        InspectableFloatTexture2D halfSpecial(
            device, 2, 2, true, SurfaceFormat::HalfSingle);
        const std::array<HalfSingle, 4> halfSource{
            MakePacked<HalfSingle>(static_cast<std::uint16_t>(0x7D55u)),
            HalfSingle(1.0f), HalfSingle(2.0f), HalfSingle(3.0f)};
        halfSpecial.SetData(halfSource.data(), 4);
        HalfSingle halfAverage;
        halfSpecial.GetData(1, nullptr, &halfAverage, 0, 1);
        Check(halfAverage.getPackedValueProperty() == 0x7E00u,
              "any half NaN generates canonical positive quiet binary16 NaN");
    }

    void CheckMetadataSamplingAndHdr(GraphicsDevice& device)
    {
        InspectableFloatTexture2D single(device, 1, 1, false, SurfaceFormat::Single);
        InspectableFloatTexture2D vector2(device, 1, 1, false, SurfaceFormat::Vector2);
        InspectableFloatTexture2D vector4(device, 1, 1, false, SurfaceFormat::Vector4);
        InspectableFloatTexture2D halfSingle(device, 1, 1, false, SurfaceFormat::HalfSingle);
        InspectableFloatTexture2D halfVector2(device, 1, 1, false, SurfaceFormat::HalfVector2);
        InspectableFloatTexture2D halfVector4(device, 1, 1, false, SurfaceFormat::HalfVector4);
        InspectableFloatTexture2D hdr(device, 1, 1, false, SurfaceFormat::HdrBlendable);
        const float singleValue = 0.5f;
        const Vector2 vector2Value(1.0f, 0.5f);
        const Vector4 vector4Value(1.0f, 0.5f, 0.25f, 1.0f);
        const HalfSingle halfSingleValue(0.5f);
        const HalfVector2 halfVector2Value(1.0f, 0.5f);
        const HalfVector4 halfVector4Value(1.0f, 0.5f, 0.25f, 1.0f);
        const HalfVector4 hdrValue(2.0f, -1.0f, 0.5f, 1.0f);
        single.SetData(&singleValue, 1);
        vector2.SetData(&vector2Value, 1);
        vector4.SetData(&vector4Value, 1);
        halfSingle.SetData(&halfSingleValue, 1);
        halfVector2.SetData(&halfVector2Value, 1);
        halfVector4.SetData(&halfVector4Value, 1);
        hdr.SetData(&hdrValue, 1);

        const auto image = [&](InspectableFloatTexture2D& texture) {
            return texture.SkiaBackend()->SnapshotImage(SkiaSourceAlphaConvention::Straight);
        };
        Check(image(single)->colorType() == kRGBA_F32_SkColorType
                  && image(single)->alphaType() == kOpaque_SkAlphaType
                  && image(vector2)->colorType() == kRGBA_F32_SkColorType
                  && image(vector2)->alphaType() == kOpaque_SkAlphaType
                  && image(vector4)->colorType() == kRGBA_F32_SkColorType
                  && image(halfSingle)->colorType() == kR16_float_SkColorType
                  && image(halfVector2)->colorType() == kR16G16_float_SkColorType
                  && image(halfVector4)->colorType() == kRGBA_F16_SkColorType
                  && image(hdr)->colorType() == kRGBA_F16_SkColorType,
              "seven formats expose pinned F32/R16F/RG16F/RGBA16F Skia metadata");

        std::array<float, 4> hdrRead{};
        const SkImageInfo floatInfo = SkImageInfo::Make(
            1, 1, kRGBA_F32_SkColorType, kUnpremul_SkAlphaType);
        const bool readHdr = image(hdr)->readPixels(
            floatInfo, hdrRead.data(), sizeof(hdrRead), 0, 0);
        Check(readHdr && hdrRead[0] == 2.0f && hdrRead[1] == -1.0f
                  && hdrRead[2] == 0.5f && hdrRead[3] == 1.0f,
              "HdrBlendable sampling preserves unclamped negative and greater-than-one values");

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            point, nullptr, nullptr);
        std::array<Texture2D*, 7> textures{
            &single, &vector2, &vector4, &halfSingle, &halfVector2, &halfVector4, &hdr};
        for (int index = 0; index < static_cast<int>(textures.size()); ++index)
            spriteBatch_->Draw(*textures[index], Rectangle(index * 4, 0, 4, 4), Color::White);
        spriteBatch_->End();
        const auto sample = [&](int index) {
            const Rectangle region(index * 4 + 2, 2, 1, 1);
            Color color = Color::Transparent;
            device.GetBackBufferData(&region, &color, 0, 1);
            return color;
        };
        const Color singlePixel = sample(0);
        const Color vector2Pixel = sample(1);
        const Color vector4Pixel = sample(2);
        const Color halfSinglePixel = sample(3);
        const Color halfVector2Pixel = sample(4);
        const Color halfVector4Pixel = sample(5);
        const Color hdrPixel = sample(6);
        const auto near = [](std::uint8_t actual, int expected) {
            return std::abs(static_cast<int>(actual) - expected) <= 1;
        };
        Check(near(singlePixel.getRProperty(), 128) && singlePixel.getGProperty() == 0
                  && singlePixel.getBProperty() == 0 && singlePixel.getAProperty() == 255
                  && vector2Pixel.getRProperty() == 255
                  && near(vector2Pixel.getGProperty(), 128)
                  && vector2Pixel.getBProperty() == 0 && vector2Pixel.getAProperty() == 255
                  && near(halfSinglePixel.getRProperty(), 128)
                  && halfSinglePixel.getGProperty() == 0
                  && halfVector2Pixel.getRProperty() == 255
                  && near(halfVector2Pixel.getGProperty(), 128),
              "single/RG float formats sample missing channels as zero and alpha as opaque");
        Check(vector4Pixel.getRProperty() == 255 && near(vector4Pixel.getGProperty(), 128)
                  && near(vector4Pixel.getBProperty(), 64) && vector4Pixel.getAProperty() == 255
                  && halfVector4Pixel.getRProperty() == 255
                  && near(halfVector4Pixel.getGProperty(), 128)
                  && near(halfVector4Pixel.getBProperty(), 64)
                  && hdrPixel.getRProperty() == 255 && hdrPixel.getGProperty() == 0
                  && near(hdrPixel.getBProperty(), 128),
              "RGBA float sampling is channel-correct and RGBA8 presentation clamps HDR");

        const HalfVector4 blendValue(0.5f, 0.25f, 0.75f, 0.5f);
        hdr.SetData(&blendValue, 1);
        device.Clear(Color(64, 128, 192, 255));
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied,
                            point, nullptr, nullptr);
        spriteBatch_->Draw(hdr, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        const Rectangle blendRegion(2, 2, 1, 1);
        Color blended = Color::Transparent;
        device.GetBackBufferData(&blendRegion, &blended, 0, 1);
        Check(near(blended.getRProperty(), 96) && near(blended.getGProperty(), 96)
                  && near(blended.getBProperty(), 192) && near(blended.getAProperty(), 191),
              "HdrBlendable public non-premultiplied blend matches (96,96,192,191), got ("
                  + std::to_string(blended.getRProperty()) + ","
                  + std::to_string(blended.getGProperty()) + ","
                  + std::to_string(blended.getBProperty()) + ","
                  + std::to_string(blended.getAProperty()) + ")");
    }

    void CheckTypedRefusalAndTargetPromotion(GraphicsDevice& device,
                                    SkiaGraphicsBackend& graphicsBackend)
    {
        InspectableFloatTexture2D single(device, 1, 1, false, SurfaceFormat::Single);
        const float original = 0.25f;
        single.SetData(&original, 1);
        const Vector2 wrong(1.0f, 2.0f);
        Check(Throws([&] { single.SetData(&wrong, 1); }),
              "mismatched Vector2 upload rejects before Single mutation");
        float unchanged = 0.0f;
        single.GetData(&unchanged, 1);
        Check(std::bit_cast<std::uint32_t>(unchanged)
                  == std::bit_cast<std::uint32_t>(original),
              "mismatched typed upload leaves existing float bits unchanged");

        // SKIA-142: all seven float/half formats are now constructible RenderTarget2D targets.
        // Single/Vector2 have no native 1/2-channel 32-bit-float SkColorType, so their surfaces
        // widen to kRGBA_F32 (16 bytes/pixel) even though their public transfer width stays
        // narrow -- `nativeBytesPerPixel` below is the surface accounting truth, not the public
        // GetData/SetData element size exercised elsewhere in this file.
        const std::array<std::pair<SurfaceFormat, std::size_t>, 7> promoted{{
            {SurfaceFormat::Single, 16u}, {SurfaceFormat::Vector2, 16u},
            {SurfaceFormat::Vector4, 16u}, {SurfaceFormat::HalfSingle, 2u},
            {SurfaceFormat::HalfVector2, 4u}, {SurfaceFormat::HalfVector4, 8u},
            {SurfaceFormat::HdrBlendable, 8u},
        }};
        for (const auto& [format, nativeBytesPerPixel] : promoted)
        {
            const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
            {
                RenderTarget2D target(device, 2, 2, false, format, DepthFormat::None);
                const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
                Check(allocated.renderTargets == before.renderTargets + 1u
                          && allocated.targetSurfaceBytes
                              == before.targetSurfaceBytes + 4u * nativeBytesPerPixel,
                      "SKIA-142 promotes a constructible float/half RenderTarget2D with exact "
                      "native surface accounting");
            }
            Check(SameStats(graphicsBackend.GetResourceStatsEXT(), before),
                  "promoted float/half RenderTarget2D destruction releases its surface accounting");
        }
    }

    void Check(bool pass, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DFloatFormatsTest game;
    game.Run();
    return game.Result();
}
