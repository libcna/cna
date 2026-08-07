// SPDX-License-Identifier: MS-PL
// SKIA-139: exact Bgra5551/SNORM shadows, deterministic mips and RGBA32F sampling.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra5551.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/NormalizedByte4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>
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
    template<typename Packed, typename Word>
    [[nodiscard]] Packed MakePacked(Word word)
    {
        Packed result;
        result.setPackedValueProperty(word);
        return result;
    }

    template<typename Packed, typename Word>
    [[nodiscard]] bool SameWords(const std::vector<Packed>& actual,
                                 const std::vector<Word>& expected,
                                 std::size_t offset = 0u)
    {
        if (actual.size() < offset + expected.size()) return false;
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            if (actual[offset + index].getPackedValueProperty() != expected[index]) return false;
        }
        return true;
    }

    [[nodiscard]] int DecodeSignedByte(std::uint8_t value) noexcept
    {
        return value < 0x80u ? static_cast<int>(value) : static_cast<int>(value) - 256;
    }

    [[nodiscard]] int AverageSnormComponent(
        std::int64_t sum, std::uint32_t sampleCount) noexcept
    {
        const std::int64_t half = static_cast<std::int64_t>(sampleCount / 2u);
        return static_cast<int>(sum >= 0
            ? (sum + half) / static_cast<std::int64_t>(sampleCount)
            : (sum - half) / static_cast<std::int64_t>(sampleCount));
    }

    [[nodiscard]] std::uint16_t AverageBgra5551(const std::vector<std::uint16_t>& words)
    {
        std::uint32_t sums[4] = {0u, 0u, 0u, 0u};
        for (const std::uint16_t word : words)
        {
            sums[0] += (word >> 10u) & 0x1Fu;
            sums[1] += (word >> 5u) & 0x1Fu;
            sums[2] += word & 0x1Fu;
            sums[3] += (word >> 15u) & 0x1u;
        }
        const auto average = [count = static_cast<std::uint32_t>(words.size())](
                                 std::uint32_t sum) {
            return (sum + count / 2u) / count;
        };
        return static_cast<std::uint16_t>(
            (average(sums[3]) << 15u) | (average(sums[0]) << 10u)
            | (average(sums[1]) << 5u) | average(sums[2]));
    }

    template<typename Word, int Channels>
    [[nodiscard]] Word AverageSnorm(const std::vector<Word>& words)
    {
        std::int64_t sums[4] = {0, 0, 0, 0};
        for (const Word word : words)
        {
            for (int channel = 0; channel < Channels; ++channel)
            {
                sums[channel] += std::max(-127, DecodeSignedByte(
                    static_cast<std::uint8_t>(
                        word >> (static_cast<unsigned>(channel) * 8u))));
            }
        }
        Word result = 0;
        for (int channel = 0; channel < Channels; ++channel)
        {
            const Word encoded = static_cast<Word>(static_cast<std::uint8_t>(
                AverageSnormComponent(sums[channel],
                                      static_cast<std::uint32_t>(words.size()))));
            result |= static_cast<Word>(encoded << (static_cast<unsigned>(channel) * 8u));
        }
        return result;
    }

    template<typename Callable>
    [[nodiscard]] bool Throws(Callable&& callable)
    {
        try { callable(); }
        catch (const std::exception&) { return true; }
        return false;
    }

    [[nodiscard]] bool Near(float actual, float expected, float tolerance = 0.00001f) noexcept
    {
        return std::abs(actual - expected) <= tolerance;
    }

    [[nodiscard]] bool NearByte(std::uint8_t actual, int expected, int tolerance = 1) noexcept
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
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

class InspectableShadowTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureBackend* SkiaBackend() const
    {
        return dynamic_cast<SkiaTextureBackend*>(GetBackendRaw());
    }
};

class SkiaTexture2DShadowFormatsTest final : public Game
{
public:
    SkiaTexture2DShadowFormatsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(12);
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
        ExercisePacked<std::uint16_t, Bgra5551>(
            device, *graphicsBackend, SurfaceFormat::Bgra5551, "Bgra5551",
            {0xFC00u, 0x83E0u, 0x801Fu, 0xFFFFu, 0x0000u, 0xE210u},
            {0x9123u, 0xE456u}, AverageBgra5551, 14u);
        ExercisePacked<std::uint16_t, NormalizedByte2>(
            device, *graphicsBackend, SurfaceFormat::NormalizedByte2, "NormalizedByte2",
            {0x007Fu, 0x7F00u, 0x0081u, 0x8100u, 0x8181u, 0x4080u},
            {0x2040u, 0xE0C0u},
            [](const std::vector<std::uint16_t>& words) {
                return AverageSnorm<std::uint16_t, 2>(words);
            }, 14u);
        ExercisePacked<std::uint32_t, NormalizedByte4>(
            device, *graphicsBackend, SurfaceFormat::NormalizedByte4, "NormalizedByte4",
            {0x7F00007Fu, 0x7F007F00u, 0x7F7F0000u, 0x7F404040u, 0x81818181u,
             0x4080817Fu},
            {0x10203040u, 0xE0D0C0B0u},
            [](const std::vector<std::uint32_t>& words) {
                return AverageSnorm<std::uint32_t, 4>(words);
            }, 28u);
        CheckExactDecodeAndMetadata(device);
        CheckPublicSampling(device);
        CheckTypedAndTargetRefusal(device, *graphicsBackend);
        Check(SameStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "all shadow-format fixtures return Skia resource counters to baseline");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

private:
    template<typename Word, typename Packed, typename Average>
    void ExercisePacked(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend,
                        SurfaceFormat format, const char* name,
                        std::vector<Word> expected, const std::vector<Word>& patch,
                        Average&& average, std::size_t expectedMipBytes)
    {
        static_assert(std::is_unsigned_v<Word>);
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        {
            InspectableShadowTexture2D texture(device, 3, 2, true, format);
            SkiaTextureBackend* backend = texture.SkiaBackend();
            const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
            Check(texture.getFormatProperty() == format && texture.getLevelCountProperty() == 2
                      && backend && backend->FormatEXT() == format,
                  std::string(name) + " constructs exact format and complete mip chain");
            Check(allocated.textureBackends == before.textureBackends + 1u
                      && allocated.textureImageViews == before.textureImageViews + 2u
                      && allocated.textureImageBytes == before.textureImageBytes + 192u
                      && allocated.mipChains2D == before.mipChains2D + 1u
                      && allocated.mipChain2DStorageBytes
                          == before.mipChain2DStorageBytes + expectedMipBytes,
                  std::string(name) + " accounts exact shadow and two RGBA32F views");

            const Word guard = static_cast<Word>(~Word{0} - Word{1});
            std::vector<Packed> upload(8, MakePacked<Packed>(guard));
            for (std::size_t index = 0; index < expected.size(); ++index)
                upload[index + 1u] = MakePacked<Packed>(expected[index]);
            texture.SetData(0, nullptr, upload.data(), 1, 7);

            std::vector<Packed> read(8, MakePacked<Packed>(guard));
            texture.GetData(read.data(), 1, 7);
            Check(SameWords<Packed>(read, expected, 1u)
                      && read.front().getPackedValueProperty() == guard
                      && read.back().getPackedValueProperty() == guard,
                  std::string(name) + " full transfer preserves exact words and caller guards");

            bool littleEndian = backend != nullptr;
            if (backend)
            {
                const std::uint8_t* raw = backend->MipChainEXT().LevelData(0);
                for (std::size_t index = 0; index < expected.size(); ++index)
                {
                    for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                    {
                        littleEndian = littleEndian
                            && raw[index * sizeof(Word) + byte]
                                == static_cast<std::uint8_t>(expected[index] >> (byte * 8u));
                    }
                }
            }
            Check(littleEndian, std::string(name) + " shadow uses explicit little-endian words");

            Packed generated{};
            texture.GetData(1, nullptr, &generated, 0, 1);
            Check(generated.getPackedValueProperty() == average(expected)
                      && backend && backend->MipGenerationCountEXT(1) == 1u,
                  std::string(name) + " generated mip averages native normalized components");

            const Rectangle patchRectangle(1, 0, 1, 2);
            std::array<Packed, 4> patchUpload{
                MakePacked<Packed>(guard), MakePacked<Packed>(patch[0]),
                MakePacked<Packed>(patch[1]), MakePacked<Packed>(guard)};
            texture.SetData(0, &patchRectangle, patchUpload.data(), 1, 3);
            expected[1] = patch[0];
            expected[4] = patch[1];

            std::array<Packed, 4> patchRead{
                MakePacked<Packed>(guard), MakePacked<Packed>(Word{0}),
                MakePacked<Packed>(Word{0}), MakePacked<Packed>(guard)};
            texture.GetData(0, &patchRectangle, patchRead.data(), 1, 3);
            Check(patchRead[0].getPackedValueProperty() == guard
                      && patchRead[1].getPackedValueProperty() == patch[0]
                      && patchRead[2].getPackedValueProperty() == patch[1]
                      && patchRead[3].getPackedValueProperty() == guard,
                  std::string(name) + " partial rectangle is exact and guard-safe");

            std::vector<Packed> whole(6);
            texture.GetData(whole.data(), 6);
            texture.GetData(1, nullptr, &generated, 0, 1);
            Check(SameWords<Packed>(whole, expected)
                      && generated.getPackedValueProperty() == average(expected)
                      && backend && backend->MipGenerationCountEXT(1) == 2u,
                  std::string(name) + " partial write preserves neighbours and regenerates mip"
                      + " (expected=" + std::to_string(average(expected))
                      + ", actual="
                      + std::to_string(generated.getPackedValueProperty()) + ")");

            const std::vector<Packed> beforeFailure = whole;
            const bool rejected = Throws([&] {
                texture.SetData(0, nullptr, upload.data(), 0, 5);
            });
            texture.GetData(whole.data(), 6);
            Check(rejected && whole == beforeFailure,
                  std::string(name) + " undersized upload is failure-atomic");
        }
        Check(SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              std::string(name) + " destruction releases shadow and working views");
    }

    void CheckExactDecodeAndMetadata(GraphicsDevice& device)
    {
        InspectableShadowTexture2D bgra(device, 1, 1, false, SurfaceFormat::Bgra5551);
        InspectableShadowTexture2D norm2(
            device, 1, 1, false, SurfaceFormat::NormalizedByte2);
        InspectableShadowTexture2D norm4(
            device, 1, 1, false, SurfaceFormat::NormalizedByte4);
        const Bgra5551 bgraValue = MakePacked<Bgra5551>(static_cast<std::uint16_t>(0xC104u));
        const NormalizedByte2 norm2Value =
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x807Fu));
        const NormalizedByte4 norm4Value =
            MakePacked<NormalizedByte4>(static_cast<std::uint32_t>(0x4080817Fu));
        bgra.SetData(&bgraValue, 1);
        norm2.SetData(&norm2Value, 1);
        norm4.SetData(&norm4Value, 1);

        const auto image = [](InspectableShadowTexture2D& texture) {
            return texture.SkiaBackend()->SnapshotImage(SkiaSourceAlphaConvention::Straight);
        };
        Check(image(bgra)->colorType() == kRGBA_F32_SkColorType
                  && image(norm2)->colorType() == kRGBA_F32_SkColorType
                  && image(norm2)->alphaType() == kOpaque_SkAlphaType
                  && image(norm4)->colorType() == kRGBA_F32_SkColorType,
              "three shadow formats expose pinned RGBA32F working metadata");

        const SkImageInfo info = SkImageInfo::Make(
            1, 1, kRGBA_F32_SkColorType, kUnpremul_SkAlphaType);
        std::array<float, 4> rgba{};
        const bool readBgra = image(bgra)->readPixels(info, rgba.data(), sizeof(rgba), 0, 0);
        Check(readBgra && Near(rgba[0], 16.0f / 31.0f) && Near(rgba[1], 8.0f / 31.0f)
                  && Near(rgba[2], 4.0f / 31.0f) && rgba[3] == 1.0f,
              "Bgra5551 decodes A:R:G:B bit fields at native 5/5/5/1 precision");

        const bool readNorm2 = image(norm2)->readPixels(info, rgba.data(), sizeof(rgba), 0, 0);
        Check(readNorm2 && rgba[0] == 1.0f && rgba[1] == -1.0f
                  && rgba[2] == 0.0f && rgba[3] == 1.0f,
              "NormalizedByte2 maps raw -128 to -1 with zero B and opaque alpha");

        const bool readNorm4 = image(norm4)->readPixels(info, rgba.data(), sizeof(rgba), 0, 0);
        Check(readNorm4 && rgba[0] == 1.0f && rgba[1] == -1.0f && rgba[2] == -1.0f
                  && Near(rgba[3], 64.0f / 127.0f),
              "NormalizedByte4 applies standard SNORM endpoints to all four channels");

        InspectableShadowTexture2D endpoint(
            device, 2, 2, true, SurfaceFormat::NormalizedByte2);
        const std::array<NormalizedByte2, 4> rawMinus128{
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x0080u)),
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x0080u)),
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x0080u)),
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x0080u)),
        };
        endpoint.SetData(rawMinus128.data(), 4);
        NormalizedByte2 canonical;
        endpoint.GetData(1, nullptr, &canonical, 0, 1);
        Check(canonical.getPackedValueProperty() == 0x0081u,
              "generated SNORM mip canonicalizes sampled -1 from raw 0x80 to packed 0x81");
    }

    void CheckPublicSampling(GraphicsDevice& device)
    {
        Texture2D bgra(device, 1, 1, false, SurfaceFormat::Bgra5551);
        Texture2D norm2(device, 1, 1, false, SurfaceFormat::NormalizedByte2);
        Texture2D norm4(device, 1, 1, false, SurfaceFormat::NormalizedByte4);
        const Bgra5551 bgraValue = MakePacked<Bgra5551>(static_cast<std::uint16_t>(0xFE08u));
        const NormalizedByte2 norm2Value =
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x4081u));
        const NormalizedByte4 norm4Value =
            MakePacked<NormalizedByte4>(static_cast<std::uint32_t>(0x7F40817Fu));
        bgra.SetData(&bgraValue, 1);
        norm2.SetData(&norm2Value, 1);
        norm4.SetData(&norm4Value, 1);

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            point, nullptr, nullptr);
        spriteBatch_->Draw(bgra, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(norm2, Rectangle(4, 0, 4, 4), Color::White);
        spriteBatch_->Draw(norm4, Rectangle(8, 0, 4, 4), Color::White);
        spriteBatch_->End();

        const auto sample = [&](int x) {
            const Rectangle region(x, 2, 1, 1);
            Color result = Color::Transparent;
            device.GetBackBufferData(&region, &result, 0, 1);
            return result;
        };
        const Color bgraPixel = sample(2);
        const Color norm2Pixel = sample(6);
        const Color norm4Pixel = sample(10);
        Check(bgraPixel.getRProperty() == 255 && NearByte(bgraPixel.getGProperty(), 132)
                  && NearByte(bgraPixel.getBProperty(), 66) && bgraPixel.getAProperty() == 255,
              "Bgra5551 public sampling preserves channel order and 5-bit normalization");
        Check(norm2Pixel.getRProperty() == 0 && NearByte(norm2Pixel.getGProperty(), 129)
                  && norm2Pixel.getBProperty() == 0 && norm2Pixel.getAProperty() == 255,
              "NormalizedByte2 public sampling clamps negative X and supplies missing channels");
        Check(norm4Pixel.getRProperty() == 255 && norm4Pixel.getGProperty() == 0
                  && NearByte(norm4Pixel.getBProperty(), 129)
                  && norm4Pixel.getAProperty() == 255,
              "NormalizedByte4 public sampling distinguishes signed bytes from UNORM");
    }

    void CheckTypedAndTargetRefusal(GraphicsDevice& device,
                                    SkiaGraphicsBackend& graphicsBackend)
    {
        Texture2D texture(device, 1, 1, false, SurfaceFormat::NormalizedByte2);
        const NormalizedByte2 original =
            MakePacked<NormalizedByte2>(static_cast<std::uint16_t>(0x2040u));
        texture.SetData(&original, 1);
        const Bgra5551 wrong = MakePacked<Bgra5551>(static_cast<std::uint16_t>(0xFFFFu));
        const bool rejected = Throws([&] { texture.SetData(&wrong, 1); });
        NormalizedByte2 unchanged;
        texture.GetData(&unchanged, 1);
        Check(rejected && unchanged == original,
              "mismatched typed upload rejects before mutating the exact SNORM shadow");

        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        const std::array formats{
            SurfaceFormat::Bgra5551,
            SurfaceFormat::NormalizedByte2,
            SurfaceFormat::NormalizedByte4,
        };
        bool allRejected = true;
        for (const SurfaceFormat format : formats)
        {
            allRejected = allRejected && Throws([&] {
                RenderTarget2D target(device, 2, 2, false, format, DepthFormat::None);
                (void)target;
            });
        }
        Check(allRejected && SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              "Bgra5551/NormalizedByte2/NormalizedByte4 RenderTarget2D requests reject "
              "transactionally (not FNA-renderable)");
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
    SkiaTexture2DShadowFormatsTest game;
    game.Run();
    return game.Result();
}
