// SPDX-License-Identifier: MS-PL
// SKIA-137: exact one/two/four-channel UNORM transfers, mips, sampling, and refusal gates.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "include/core/SkImage.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>
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
    template<typename Transfer>
    [[nodiscard]] Transfer MakeTransfer(std::uint64_t word)
    {
        if constexpr (std::is_integral_v<Transfer>)
            return static_cast<Transfer>(word);
        else
        {
            Transfer result;
            result.setPackedValueProperty(
                static_cast<decltype(result.getPackedValueProperty())>(word));
            return result;
        }
    }

    template<typename Transfer>
    [[nodiscard]] std::uint64_t TransferWord(const Transfer& value)
    {
        if constexpr (std::is_integral_v<Transfer>)
            return static_cast<std::uint64_t>(value);
        else
            return static_cast<std::uint64_t>(value.getPackedValueProperty());
    }

    template<typename Transfer>
    [[nodiscard]] std::vector<Transfer> MakeTransfers(const std::vector<std::uint64_t>& words)
    {
        std::vector<Transfer> result;
        result.reserve(words.size());
        for (const std::uint64_t word : words)
            result.push_back(MakeTransfer<Transfer>(word));
        return result;
    }

    template<typename Transfer>
    [[nodiscard]] bool SameWords(const std::vector<Transfer>& actual,
                                 const std::vector<std::uint64_t>& expected,
                                 std::size_t actualOffset = 0u)
    {
        if (actual.size() < actualOffset + expected.size())
            return false;
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            if (TransferWord(actual[actualOffset + index]) != expected[index])
                return false;
        }
        return true;
    }

    [[nodiscard]] std::uint64_t AverageUnormWords(
        SurfaceFormat format, const std::vector<std::uint64_t>& words)
    {
        std::uint64_t sums[4] = {0u, 0u, 0u, 0u};
        int channels = 1;
        int channelBits = 8;
        if (format == SurfaceFormat::UShortEXT)
            channelBits = 16;
        else if (format == SurfaceFormat::Rg32)
        {
            channels = 2;
            channelBits = 16;
        }
        else if (format == SurfaceFormat::Rgba64)
        {
            channels = 4;
            channelBits = 16;
        }

        const std::uint64_t mask = channelBits == 8 ? 0xFFu : 0xFFFFu;
        for (const std::uint64_t word : words)
        {
            for (int channel = 0; channel < channels; ++channel)
                sums[channel] += (word >> (channel * channelBits)) & mask;
        }
        std::uint64_t result = 0u;
        const std::uint64_t count = words.size();
        for (int channel = 0; channel < channels; ++channel)
            result |= ((sums[channel] + count / 2u) / count) << (channel * channelBits);
        return result;
    }

    template<typename Callable>
    [[nodiscard]] bool Throws(Callable&& callable)
    {
        try
        {
            callable();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }

    [[nodiscard]] bool NearChannel(std::uint8_t actual, int expected, int tolerance = 1)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }

    [[nodiscard]] SkColorType ExpectedColorType(SurfaceFormat format)
    {
        switch (format)
        {
            case SurfaceFormat::Alpha8: return kAlpha_8_SkColorType;
            case SurfaceFormat::ByteEXT: return kR8_unorm_SkColorType;
            case SurfaceFormat::UShortEXT: return kR16_unorm_SkColorType;
            case SurfaceFormat::Rg32: return kR16G16_unorm_SkColorType;
            case SurfaceFormat::Rgba64: return kR16G16B16A16_unorm_SkColorType;
            default: return kUnknown_SkColorType;
        }
    }
}

class InspectableUnormTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureBackend* SkiaBackend() const
    {
        return dynamic_cast<SkiaTextureBackend*>(GetBackendRaw());
    }
};

class SkiaTexture2DUnormFormatsTest final : public Game
{
public:
    SkiaTexture2DUnormFormatsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(20);
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
        if (done_)
            return;
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
        ExerciseTransfer<Alpha8>(device, *graphicsBackend, SurfaceFormat::Alpha8, "Alpha8", 1u,
            {0u, 32u, 96u, 160u, 224u, 255u}, {17u, 239u});
        ExerciseTransfer<std::uint8_t>(device, *graphicsBackend, SurfaceFormat::ByteEXT,
            "ByteEXT", 1u, {3u, 37u, 91u, 151u, 203u, 251u}, {29u, 227u});
        ExerciseTransfer<std::uint16_t>(device, *graphicsBackend, SurfaceFormat::UShortEXT,
            "UShortEXT", 2u, {0u, 8192u, 24576u, 40960u, 57344u, 65535u},
            {0x1234u, 0xFEDCu});
        ExerciseTransfer<Rg32>(device, *graphicsBackend, SurfaceFormat::Rg32, "Rg32", 4u,
            {0x00000000u, 0x20004000u, 0x40008000u, 0x8000C000u, 0xC000FFFFu,
             0xFFFF1234u},
            {0x3456ABCDu, 0xFEDC0123u});
        ExerciseTransfer<Rgba64>(device, *graphicsBackend, SurfaceFormat::Rgba64, "Rgba64", 8u,
            {0x0000000000000000ull, 0x200040008000FFFFull, 0x40008000FFFF0000ull,
             0x8000C00040002000ull, 0xC000FFFF80004000ull, 0xFFFF123456789ABCull},
            {0x13572468ACE0FDB9ull, 0xFEDCBA9876543210ull});

        Check(SameTextureStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "UNORM transfer fixtures release every image view and mip chain");
        CheckTypedRefusalIsAtomic(device);
        CheckMetadataAndSampling(device, *graphicsBackend);
        CheckRenderTargetRefusal(device, *graphicsBackend);
        CheckRenderTargetPromotion(device, *graphicsBackend);
        Check(SameTextureStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "all UNORM fixtures return resource counters to baseline");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

private:
    template<typename Transfer>
    void ExerciseTransfer(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend,
                          SurfaceFormat format, const char* name, std::size_t bytesPerTexel,
                          std::vector<std::uint64_t> expected,
                          const std::vector<std::uint64_t>& patch)
    {
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        {
            InspectableUnormTexture2D texture(device, 3, 2, true, format);
            SkiaTextureBackend* backend = texture.SkiaBackend();
            const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
            Check(texture.getFormatProperty() == format && texture.getLevelCountProperty() == 2
                      && backend && backend->FormatEXT() == format,
                  std::string(name) + " constructs the exact public/backend format and mip count");
            Check(allocated.textureBackends == before.textureBackends + 1u
                      && allocated.textureImageViews == before.textureImageViews + 2u
                      && allocated.textureImageBytes
                          == before.textureImageBytes + 12u * bytesPerTexel
                      && allocated.mipChains2D == before.mipChains2D + 1u
                      && allocated.mipChain2DStorageBytes
                          == before.mipChain2DStorageBytes + 7u * bytesPerTexel,
                  std::string(name) + " accounts native transfer, image-view and mip bytes");

            constexpr std::uint64_t kGuard = 0x5AA55AA55AA55AA5ull;
            std::vector<Transfer> upload(8, MakeTransfer<Transfer>(kGuard));
            const auto expectedTransfers = MakeTransfers<Transfer>(expected);
            for (std::size_t index = 0; index < expectedTransfers.size(); ++index)
                upload[index + 1u] = expectedTransfers[index];
            texture.SetData(0, nullptr, upload.data(), 1, 7);

            std::vector<Transfer> read(8, MakeTransfer<Transfer>(kGuard));
            texture.GetData(read.data(), 1, 7);
            const std::uint64_t truncatedGuard = TransferWord(MakeTransfer<Transfer>(kGuard));
            Check(SameWords(read, expected, 1u)
                      && TransferWord(read.front()) == truncatedGuard
                      && TransferWord(read.back()) == truncatedGuard,
                  std::string(name) + " full transfer is exact and preserves caller guards");

            bool littleEndian = backend != nullptr;
            if (backend)
            {
                const std::uint8_t* raw = backend->MipChainEXT().LevelData(0);
                for (std::size_t index = 0; index < expected.size(); ++index)
                {
                    for (std::size_t byte = 0; byte < bytesPerTexel; ++byte)
                    {
                        littleEndian = littleEndian
                            && raw[index * bytesPerTexel + byte]
                                == static_cast<std::uint8_t>(expected[index] >> (byte * 8u));
                    }
                }
            }
            Check(littleEndian,
                  std::string(name) + " backend storage uses the pinned little-endian layout");

            std::array<Transfer, 1> generated{MakeTransfer<Transfer>(0u)};
            texture.GetData(1, nullptr, generated.data(), 0, 1);
            const auto generatedImage = backend ? backend->SnapshotMipLevelEXT(
                1, SkiaSourceAlphaConvention::Straight) : nullptr;
            Check(TransferWord(generated[0]) == AverageUnormWords(format, expected)
                      && backend && backend->MipGenerationCountEXT(1) == 1u
                      && generatedImage && generatedImage->colorType() == ExpectedColorType(format),
                  std::string(name)
                      + " mip generation averages native UNORM channels and exposes a typed image");

            const Rectangle patchRectangle(1, 0, 1, 2);
            std::vector<Transfer> patchUpload(4, MakeTransfer<Transfer>(kGuard));
            patchUpload[1] = MakeTransfer<Transfer>(patch[0]);
            patchUpload[2] = MakeTransfer<Transfer>(patch[1]);
            texture.SetData(0, &patchRectangle, patchUpload.data(), 1, 3);
            expected[1] = patch[0];
            expected[4] = patch[1];

            std::vector<Transfer> patchRead(4, MakeTransfer<Transfer>(kGuard));
            texture.GetData(0, &patchRectangle, patchRead.data(), 1, 3);
            Check(TransferWord(patchRead[0]) == truncatedGuard
                      && TransferWord(patchRead[1]) == patch[0]
                      && TransferWord(patchRead[2]) == patch[1]
                      && TransferWord(patchRead[3]) == truncatedGuard,
                  std::string(name) + " partial rectangle is exact, row-major and guard-safe");

            std::vector<Transfer> whole(6, MakeTransfer<Transfer>(0u));
            texture.GetData(whole.data(), 6);
            texture.GetData(1, nullptr, generated.data(), 0, 1);
            Check(SameWords(whole, expected)
                      && TransferWord(generated[0]) == AverageUnormWords(format, expected)
                      && backend && backend->MipGenerationCountEXT(1) == 2u,
                  std::string(name) + " parent patch preserves neighbours and rebuilds its mip");

            const auto beforeFailure = whole;
            Check(Throws([&] { texture.SetData(0, nullptr, upload.data(), 0, 5); }),
                  std::string(name) + " rejects an undersized full upload");
            texture.GetData(whole.data(), 6);
            Check(SameWords(whole, expected) && SameWords(beforeFailure, expected),
                  std::string(name) + " rejected upload leaves all texture words unchanged");
        }
        Check(SameTextureStats(graphicsBackend.GetResourceStatsEXT(), before),
              std::string(name) + " destruction releases exact resource accounting");
    }

    void CheckTypedRefusalIsAtomic(GraphicsDevice& device)
    {
        Texture2D bytes(device, 1, 1, false, SurfaceFormat::ByteEXT);
        const std::uint8_t original = 77u;
        bytes.SetData(&original, 1);
        const Alpha8 wrong = MakeTransfer<Alpha8>(211u);
        Check(Throws([&] { bytes.SetData(&wrong, 1); }),
              "mismatched Alpha8 SetData rejects before ByteEXT storage mutation");
        std::uint8_t actual = 0u;
        bytes.GetData(&actual, 1);
        Check(actual == original, "mismatched SetData leaves the previous ByteEXT value intact");

        Alpha8 untouched = MakeTransfer<Alpha8>(123u);
        Check(Throws([&] { bytes.GetData(&untouched, 1); })
                  && TransferWord(untouched) == 123u,
              "mismatched packed GetData rejects without touching caller memory");
        Color colorGuard(1, 2, 3, 4);
        Check(Throws([&] { bytes.GetData(&colorGuard, 1); })
                  && colorGuard == Color(1, 2, 3, 4),
              "Color transfer overload cannot reinterpret single-channel storage");
    }

    void CheckMetadataAndSampling(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend)
    {
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        {
            InspectableUnormTexture2D alpha(device, 1, 1, false, SurfaceFormat::Alpha8);
            InspectableUnormTexture2D byte(device, 1, 1, false, SurfaceFormat::ByteEXT);
            InspectableUnormTexture2D ushort(device, 1, 1, false, SurfaceFormat::UShortEXT);
            InspectableUnormTexture2D rg(device, 1, 1, false, SurfaceFormat::Rg32);
            InspectableUnormTexture2D rgba(device, 1, 1, false, SurfaceFormat::Rgba64);

            const Alpha8 alphaValue = MakeTransfer<Alpha8>(128u);
            const std::uint8_t byteValue = 128u;
            const std::uint16_t ushortValue = 0x8080u;
            const Rg32 rgValue = MakeTransfer<Rg32>(0x8000FFFFu);
            const Rgba64 rgbaValue = MakeTransfer<Rgba64>(0xFFFF40008000FFFFull);
            alpha.SetData(&alphaValue, 1);
            byte.SetData(&byteValue, 1);
            ushort.SetData(&ushortValue, 1);
            rg.SetData(&rgValue, 1);
            rgba.SetData(&rgbaValue, 1);

            const auto alphaImage = alpha.SkiaBackend()->SnapshotImage(
                SkiaSourceAlphaConvention::Straight);
            const auto byteImage = byte.SkiaBackend()->SnapshotImage(
                SkiaSourceAlphaConvention::Straight);
            const auto ushortImage = ushort.SkiaBackend()->SnapshotImage(
                SkiaSourceAlphaConvention::Straight);
            const auto rgImage = rg.SkiaBackend()->SnapshotImage(
                SkiaSourceAlphaConvention::Straight);
            const auto rgbaImage = rgba.SkiaBackend()->SnapshotImage(
                SkiaSourceAlphaConvention::Straight);
            Check(alphaImage && alphaImage->colorType() == kAlpha_8_SkColorType
                      && alphaImage->alphaType() == kPremul_SkAlphaType
                      && byteImage && byteImage->colorType() == kR8_unorm_SkColorType
                      && byteImage->alphaType() == kOpaque_SkAlphaType
                      && ushortImage && ushortImage->colorType() == kR16_unorm_SkColorType
                      && ushortImage->alphaType() == kOpaque_SkAlphaType
                      && rgImage && rgImage->colorType() == kR16G16_unorm_SkColorType
                      && rgImage->alphaType() == kOpaque_SkAlphaType
                      && rgbaImage
                      && rgbaImage->colorType() == kR16G16B16A16_unorm_SkColorType
                      && rgbaImage->alphaType() == kUnpremul_SkAlphaType,
                  "all five formats expose their exact pinned Skia colour/alpha metadata");

            device.Clear(Color::Transparent);
            auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                point, nullptr, nullptr);
            spriteBatch_->Draw(alpha, Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->Draw(byte, Rectangle(4, 0, 4, 4), Color::White);
            spriteBatch_->Draw(ushort, Rectangle(8, 0, 4, 4), Color::White);
            spriteBatch_->Draw(rg, Rectangle(12, 0, 4, 4), Color::White);
            spriteBatch_->Draw(rgba, Rectangle(16, 0, 4, 4), Color::White);
            spriteBatch_->End();

            const auto sample = [&](int x) {
                const Rectangle region(x, 2, 1, 1);
                Color result = Color::Transparent;
                device.GetBackBufferData(&region, &result, 0, 1);
                return result;
            };
            const Color alphaPixel = sample(2);
            const Color bytePixel = sample(6);
            const Color ushortPixel = sample(10);
            const Color rgPixel = sample(14);
            const Color rgbaPixel = sample(18);
            Check(NearChannel(alphaPixel.getRProperty(), 0)
                      && NearChannel(alphaPixel.getGProperty(), 0)
                      && NearChannel(alphaPixel.getBProperty(), 0)
                      && NearChannel(alphaPixel.getAProperty(), 128),
                  "Alpha8 samples as zero RGB plus normalized alpha");
            Check(NearChannel(bytePixel.getRProperty(), 128)
                      && bytePixel.getGProperty() == 0 && bytePixel.getBProperty() == 0
                      && bytePixel.getAProperty() == 255
                      && NearChannel(ushortPixel.getRProperty(), 128)
                      && ushortPixel.getGProperty() == 0 && ushortPixel.getBProperty() == 0
                      && ushortPixel.getAProperty() == 255,
                  "ByteEXT/UShortEXT sample normalized red with zero GB and opaque alpha");
            Check(rgPixel.getRProperty() == 255 && NearChannel(rgPixel.getGProperty(), 128)
                      && rgPixel.getBProperty() == 0 && rgPixel.getAProperty() == 255,
                  "Rg32 samples normalized RG with zero blue and opaque alpha");
            Check(rgbaPixel.getRProperty() == 255
                      && NearChannel(rgbaPixel.getGProperty(), 128)
                      && NearChannel(rgbaPixel.getBProperty(), 64)
                      && rgbaPixel.getAProperty() == 255,
                  "Rgba64 samples all four normalized 16-bit channels in RGBA order");
        }
        Check(SameTextureStats(graphicsBackend.GetResourceStatsEXT(), before),
              "sampled UNORM textures release native image views and mip storage");
    }

    void CheckRenderTargetRefusal(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend)
    {
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        Check(Throws([&] {
                  RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Alpha8,
                                        DepthFormat::None);
                  (void)target;
              }) && SameTextureStats(graphicsBackend.GetResourceStatsEXT(), before),
              "Alpha8 RenderTarget2D requests reject transactionally");
    }

    void CheckRenderTargetPromotion(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend)
    {
        const std::array<std::pair<SurfaceFormat, std::size_t>, 4> promoted{{
            {SurfaceFormat::ByteEXT, 1u},
            {SurfaceFormat::UShortEXT, 2u},
            {SurfaceFormat::Rg32, 4u},
            {SurfaceFormat::Rgba64, 8u},
        }};
        for (const auto& [format, bytesPerTexel] : promoted)
        {
            const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
            {
                RenderTarget2D target(device, 2, 2, false, format, DepthFormat::None);
                const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
                Check(allocated.renderTargets == before.renderTargets + 1u
                          && allocated.targetSurfaceBytes
                              == before.targetSurfaceBytes + 4u * bytesPerTexel,
                      "SKIA-142 promotes a constructible UNORM RenderTarget2D with exact "
                      "surface accounting");
            }
            Check(SameTextureStats(graphicsBackend.GetResourceStatsEXT(), before),
                  "promoted UNORM RenderTarget2D destruction releases its surface accounting");
        }
    }

    [[nodiscard]] static bool SameTextureStats(const SkiaResourceStats& left,
                                               const SkiaResourceStats& right)
    {
        return left.textureBackends == right.textureBackends
            && left.textureImageViews == right.textureImageViews
            && left.textureImageBytes == right.textureImageBytes
            && left.mipChains2D == right.mipChains2D
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes
            && left.renderTargets == right.renderTargets
            && left.targetSurfaceBytes == right.targetSurfaceBytes;
    }

    void Check(bool pass, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DUnormFormatsTest game;
    game.Run();
    return game.Result();
}
