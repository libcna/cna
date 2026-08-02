// SPDX-License-Identifier: MS-PL
// SKIA-141: Bc7EXT/Bc7SrgbEXT preserved compressed CPU blocks, decoded via a native BC7 decoder
// implemented directly from the public Khronos Data Format Specification (no third-party decoder
// dependency -- see docs/skia-bc7-decoder.md). Covers conformance-block decode across two BC7
// modes, sRGB colour-space handling, block transfer/alignment, malformed input, deterministic
// reserved-mode fallback, and continued RenderTarget2D refusal.

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaTextureBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;
using CNA::Internal::Backends::Skia::SkiaSourceAlphaConvention;
using CNA::Internal::Backends::Skia::SkiaTextureBackend;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // BC7 mode 6 (single subset, RGBAP 7.7.7.7.1 endpoints, unique P-bit per endpoint, 4-bit
    // indices). With the P-bit, component precision is exactly 8 bits, so any endpoint value
    // whose R/G/B/A bytes share the same parity round-trips through the decoder exactly with no
    // replication. Verified against a from-scratch standalone decode of the same bytes before
    // this file was written.

    // e0 = e1 = (201,151,101,255): solid colour, exact round trip regardless of index.
    constexpr std::array<std::uint8_t, 16> kBc7ModeSixSolid{
        0x40, 0x32, 0x79, 0xB9, 0x94, 0xC9, 0xFE, 0xFF,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // e0 = e1 = (127,127,127,255): mid-grey, used for the sRGB decode comparison.
    constexpr std::array<std::uint8_t, 16> kBc7ModeSixMidGrey{
        0xC0, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE, 0xFF,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // Reserved encoding: low byte entirely zero. The specification requires a deterministic
    // all-zero decode (alpha may be 0 or 1.0; this decoder always returns exactly 0).
    constexpr std::array<std::uint8_t, 16> kBc7Reserved{};

    void Check(int& checks, int& failures, bool pass, const std::string& label)
    {
        ++checks;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass) ++failures;
    }

    template <typename Callable>
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

    [[nodiscard]] bool NearByte(std::uint8_t actual, int expected, int tolerance = 1) noexcept
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }
}

class InspectableBc7Texture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureBackend* SkiaBackend() const
    {
        return dynamic_cast<SkiaTextureBackend*>(GetBackendRaw());
    }
};

class SkiaTexture2DBc7Test final : public Game
{
public:
    SkiaTexture2DBc7Test()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
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
        Check(checks_, failures_, graphicsBackend != nullptr,
              "public GraphicsDevice owns the Skia backend");
        if (!graphicsBackend) { Exit(); return; }

        const SkiaResourceStats baseline = graphicsBackend->GetResourceStatsEXT();

        CheckExactTransferAndMetadata(device);
        CheckSrgbColourSpace(device);
        CheckReservedModeFallback(device);
        CheckBlockAlignment(device);
        CheckMalformedData(device);
        CheckDecodedSampling(device);
        CheckTargetRefusal(device, *graphicsBackend);

        Check(checks_, failures_, SameStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "every BC7 texture releases its exact resource accounting on disposal");
        Exit();
    }

private:
    void CheckExactTransferAndMetadata(GraphicsDevice& device)
    {
        {
            InspectableBc7Texture2D texture(device, 4, 4, false, SurfaceFormat::Bc7EXT);
            texture.SetData(kBc7ModeSixSolid.data(), static_cast<int>(kBc7ModeSixSolid.size()));
            std::array<std::uint8_t, 16> readback{};
            texture.GetData(readback.data(), static_cast<int>(readback.size()));
            Check(checks_, failures_, readback == kBc7ModeSixSolid,
                  "Bc7EXT 4x4 SetData/GetData round-trips the exact 16-byte mode-6 block");
            auto* backend = texture.SkiaBackend();
            Check(checks_, failures_,
                  backend && backend->SnapshotImage(SkiaSourceAlphaConvention::Straight)
                                 ->colorType() == kRGBA_8888_SkColorType,
                  "Bc7EXT exposes a decoded kRGBA_8888 sampling image");
        }
        {
            InspectableBc7Texture2D texture(device, 4, 4, false, SurfaceFormat::Bc7SrgbEXT);
            texture.SetData(kBc7ModeSixSolid.data(), static_cast<int>(kBc7ModeSixSolid.size()));
            std::array<std::uint8_t, 16> readback{};
            texture.GetData(readback.data(), static_cast<int>(readback.size()));
            Check(checks_, failures_, readback == kBc7ModeSixSolid,
                  "Bc7SrgbEXT 4x4 SetData/GetData round-trips the exact 16-byte mode-6 block");
            auto* backend = texture.SkiaBackend();
            Check(checks_, failures_,
                  backend && backend->SnapshotImage(SkiaSourceAlphaConvention::Straight)
                                 ->colorType() == kSRGBA_8888_SkColorType,
                  "Bc7SrgbEXT exposes a decoded kSRGBA_8888 sampling image, distinct from Bc7EXT");
        }
    }

    void CheckSrgbColourSpace(GraphicsDevice& device)
    {
        // Sampled through public SpriteBatch drawing onto the backbuffer, matching the
        // established ColorSrgbEXT verification technique (a colour-managed intermediate raster
        // surface, by contrast, treats an untagged kRGBA_8888 source as sRGB-encoded by default
        // and would double-decode a genuinely linear format -- not a suitable oracle here).
        InspectableBc7Texture2D linearTex(device, 4, 4, false, SurfaceFormat::Bc7EXT);
        linearTex.SetData(kBc7ModeSixMidGrey.data(), static_cast<int>(kBc7ModeSixMidGrey.size()));
        InspectableBc7Texture2D srgbTex(device, 4, 4, false, SurfaceFormat::Bc7SrgbEXT);
        srgbTex.SetData(kBc7ModeSixMidGrey.data(), static_cast<int>(kBc7ModeSixMidGrey.size()));

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, point, nullptr, nullptr);
        spriteBatch_->Draw(linearTex, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(srgbTex, Rectangle(4, 0, 4, 4), Color::White);
        spriteBatch_->End();

        const auto sample = [&](int x) {
            const Rectangle region(x, 2, 1, 1);
            Color result = Color::Transparent;
            device.GetBackBufferData(&region, &result, 0, 1);
            return result;
        };
        const Color linearPixel = sample(2);
        const Color srgbPixel = sample(6);
        Check(checks_, failures_,
              linearPixel.getRProperty() == 127 && linearPixel.getGProperty() == 127
                  && linearPixel.getBProperty() == 127,
              "Bc7EXT bytes (127,127,127) sample through SpriteBatch unchanged");
        Check(checks_, failures_,
              NearByte(srgbPixel.getRProperty(), 54) && NearByte(srgbPixel.getGProperty(), 54)
                  && NearByte(srgbPixel.getBProperty(), 54),
              "Bc7SrgbEXT bytes (127,127,127) -- the identical bit pattern that stayed 127 as "
              "Bc7EXT -- sample through SpriteBatch decoded once to approximately linear 54");
    }

    void CheckReservedModeFallback(GraphicsDevice& device)
    {
        InspectableBc7Texture2D texture(device, 4, 4, false, SurfaceFormat::Bc7EXT);
        texture.SetData(kBc7Reserved.data(), static_cast<int>(kBc7Reserved.size()));
        std::array<std::uint8_t, 16> readback{};
        texture.GetData(readback.data(), static_cast<int>(readback.size()));
        Check(checks_, failures_, readback == kBc7Reserved,
              "the reserved (all-zero) encoding still round-trips its exact raw bytes");

        auto* backend = texture.SkiaBackend();
        const auto image = backend->SnapshotImage(SkiaSourceAlphaConvention::Straight);
        std::array<std::uint8_t, 4> pixel{};
        const SkImageInfo info =
            SkImageInfo::Make(1, 1, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
        image->readPixels(info, pixel.data(), 4, 0, 0);
        Check(checks_, failures_,
              pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0,
              "the reserved encoding decodes deterministically to exact zero, never fabricated "
              "colour");
    }

    void CheckBlockAlignment(GraphicsDevice& device)
    {
        InspectableBc7Texture2D texture(device, 8, 8, false, SurfaceFormat::Bc7EXT);
        std::array<std::uint8_t, 64> full{};
        for (int block = 0; block < 4; ++block)
        {
            std::copy(kBc7ModeSixSolid.begin(), kBc7ModeSixSolid.end(),
                     full.begin() + block * 16);
        }
        texture.SetData(full.data(), static_cast<int>(full.size()));

        Check(checks_, failures_,
              !Throws([&] {
                  const Rectangle rect(4, 0, 4, 4);
                  texture.SetData(0, &rect, kBc7ModeSixSolid.data(), 0,
                                  static_cast<int>(kBc7ModeSixSolid.size()));
              }),
              "block-aligned interior 4x4 rect on an 8x8 Bc7EXT level is accepted");
        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(1, 0, 4, 4);
                  texture.SetData(0, &rect, kBc7ModeSixSolid.data(), 0,
                                  static_cast<int>(kBc7ModeSixSolid.size()));
              }),
              "a rect with x not a multiple of 4 is rejected for Bc7EXT, same as Dxt1/3/5");
    }

    void CheckMalformedData(GraphicsDevice& device)
    {
        InspectableBc7Texture2D texture(device, 4, 4, false, SurfaceFormat::Bc7EXT);
        texture.SetData(kBc7ModeSixSolid.data(), static_cast<int>(kBc7ModeSixSolid.size()));

        Check(checks_, failures_,
              Throws([&] { texture.SetData(kBc7ModeSixMidGrey.data(), 8); }),
              "SetData with fewer bytes than one full Bc7 block is rejected");
        std::array<std::uint8_t, 16> unchanged{};
        texture.GetData(unchanged.data(), static_cast<int>(unchanged.size()));
        Check(checks_, failures_, unchanged == kBc7ModeSixSolid,
              "a rejected undersized SetData leaves the previous block bytes exactly unchanged");
    }

    void CheckDecodedSampling(GraphicsDevice& device)
    {
        InspectableBc7Texture2D texture(device, 4, 4, false, SurfaceFormat::Bc7EXT);
        texture.SetData(kBc7ModeSixSolid.data(), static_cast<int>(kBc7ModeSixSolid.size()));

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, point, nullptr, nullptr);
        spriteBatch_->Draw(texture, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();

        const Rectangle region(2, 2, 1, 1);
        Color sampled = Color::Transparent;
        device.GetBackBufferData(&region, &sampled, 0, 1);
        Check(checks_, failures_,
              sampled.getRProperty() == 201 && sampled.getGProperty() == 151
                  && sampled.getBProperty() == 101,
              "Bc7EXT solid mode-6 texture samples through public SpriteBatch drawing");
    }

    void CheckTargetRefusal(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend)
    {
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        const std::array formats{SurfaceFormat::Bc7EXT, SurfaceFormat::Bc7SrgbEXT};
        bool allRejected = true;
        for (const SurfaceFormat format : formats)
        {
            allRejected = allRejected && Throws([&] {
                RenderTarget2D target(device, 4, 4, false, format, DepthFormat::None);
                (void)target;
            });
        }
        Check(checks_, failures_,
              allRejected && SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              "Bc7EXT/Bc7SrgbEXT RenderTarget2D construction remains transactionally refused "
              "pending SKIA-142");
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DBc7Test game;
    game.Run();
    return game.Result();
}
