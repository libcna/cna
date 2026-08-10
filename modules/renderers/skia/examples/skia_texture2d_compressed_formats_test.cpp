// SPDX-License-Identifier: MS-PL
// SKIA-140: Dxt1/Dxt3/Dxt5 preserved compressed CPU blocks, block-alignment/range validation,
// alpha-mode fidelity, NPOT edge policy, authored (never generated) mip chains, exact compressed
// GetData, decoded public sampling, malformed-data rejection, memory accounting, and continued
// RenderTarget2D refusal pending SKIA-142.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
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

using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Internal::Renderers::Skia::SkiaResourceStats;
using CNA::Internal::Renderers::Skia::SkiaTextureRenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // Reference blocks verified against CNA/Internal/Graphics/DxtUtilTests.cpp.

    // DXT1, c0=0xF800 (red) > c1=0x0000 (black): 4-color opaque mode, all index 0 -> solid red.
    constexpr std::array<std::uint8_t, 8> kDxt1SolidRed{
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // DXT1, c0=0x07E0 (green) > c1=0x0000: solid green.
    constexpr std::array<std::uint8_t, 8> kDxt1SolidGreen{
        0xE0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // DXT1, c0=0x0000 <= c1=0xF800: 3-color + transparent mode, all index 3 -> alpha 0 everywhere.
    constexpr std::array<std::uint8_t, 8> kDxt1Transparent{
        0x00, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF};
    // DXT1, c0=0x0000 <= c1=0xF800, all index 2 (average of black/red, opaque): (127 or 128,0,0,255).
    constexpr std::array<std::uint8_t, 8> kDxt1AverageOpaque{
        0x00, 0x00, 0x00, 0xF8, 0xAA, 0xAA, 0xAA, 0xAA};

    // DXT3, explicit 4-bit alpha (all nibbles 0xF -> 255), colour = solid red.
    constexpr std::array<std::uint8_t, 16> kDxt3SolidRed{
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // DXT3, alpha nibble 0x8 replicated (0x88 = 136 decoded), colour = solid red.
    constexpr std::array<std::uint8_t, 16> kDxt3HalfAlpha{
        0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88,
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // DXT5, alpha0=255 > alpha1=0, mask 0 -> every texel selects index 0 -> alpha 255 everywhere.
    constexpr std::array<std::uint8_t, 16> kDxt5SolidRed{
        0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    // DXT5, alpha0=0 <= alpha1=255: 6-interpolated + explicit {index6->0, index7->255} mode.
    // Every texel packs a distinct 3-bit index 0..7 (16 texels, indices cycle 0..7 twice) so both
    // the six interpolated codes and the two explicit codes are exercised in one block.
    constexpr std::array<std::uint8_t, 16> kDxt5InterpolatedAlpha{
        0x00, 0xFF,             // alpha0=0, alpha1=255
        0x88, 0xC6, 0xFA, 0x88, 0xC6, 0xFA, // indices 0,1,2,3,4,5,6,7 repeated twice, LSB-first
        0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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
        return left.textureRenderers == right.textureRenderers
            && left.textureImageViews == right.textureImageViews
            && left.textureImageBytes == right.textureImageBytes
            && left.mipChains2D == right.mipChains2D
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes
            && left.renderTargets == right.renderTargets
            && left.targetSurfaceBytes == right.targetSurfaceBytes;
    }
}

class InspectableCompressedTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureRenderer* SkiaRenderer() const
    {
        return dynamic_cast<SkiaTextureRenderer*>(GetRendererRaw());
    }
};

class SkiaTexture2DCompressedFormatsTest final : public Game
{
public:
    SkiaTexture2DCompressedFormatsTest()
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
        auto* graphicsRenderer = dynamic_cast<SkiaRenderer*>(&device.GetRenderer());
        Check(checks_, failures_, graphicsRenderer != nullptr,
              "public GraphicsDevice owns the Skia renderer");
        if (!graphicsRenderer) { Exit(); return; }

        const SkiaResourceStats baseline = graphicsRenderer->GetResourceStatsEXT();

        CheckExactTransferAndMetadata(device);
        CheckAlphaModes(device);
        CheckBlockAlignmentAndRange(device);
        CheckNpotEdgePolicy(device);
        CheckAuthoredMipChain(device);
        CheckDecodedSampling(device);
        CheckMalformedData(device);
        CheckTargetRefusal(device, *graphicsRenderer);

        Check(checks_, failures_, SameStats(graphicsRenderer->GetResourceStatsEXT(), baseline),
              "every compressed texture releases its exact resource accounting on disposal");
        Exit();
    }

private:
    void CheckExactTransferAndMetadata(GraphicsDevice& device)
    {
        {
            InspectableCompressedTexture2D texture(device, 4, 4, false, SurfaceFormat::Dxt1);
            texture.SetData(kDxt1SolidRed.data(), static_cast<int>(kDxt1SolidRed.size()));
            std::array<std::uint8_t, 8> readback{};
            texture.GetData(readback.data(), static_cast<int>(readback.size()));
            Check(checks_, failures_, readback == kDxt1SolidRed,
                  "Dxt1 4x4 SetData/GetData round-trips the exact 8-byte block");
            auto* renderer = texture.SkiaRenderer();
            Check(checks_, failures_,
                  renderer && renderer->SnapshotImage(
                                 CNA::Internal::Renderers::Skia::SkiaSourceAlphaConvention::Straight)
                                 ->colorType() == kRGBA_8888_SkColorType,
                  "Dxt1 exposes a decoded kRGBA_8888 sampling image");
        }
        {
            InspectableCompressedTexture2D texture(device, 4, 4, false, SurfaceFormat::Dxt3);
            texture.SetData(kDxt3SolidRed.data(), static_cast<int>(kDxt3SolidRed.size()));
            std::array<std::uint8_t, 16> readback{};
            texture.GetData(readback.data(), static_cast<int>(readback.size()));
            Check(checks_, failures_, readback == kDxt3SolidRed,
                  "Dxt3 4x4 SetData/GetData round-trips the exact 16-byte block");
        }
        {
            InspectableCompressedTexture2D texture(device, 4, 4, false, SurfaceFormat::Dxt5);
            texture.SetData(kDxt5SolidRed.data(), static_cast<int>(kDxt5SolidRed.size()));
            std::array<std::uint8_t, 16> readback{};
            texture.GetData(readback.data(), static_cast<int>(readback.size()));
            Check(checks_, failures_, readback == kDxt5SolidRed,
                  "Dxt5 4x4 SetData/GetData round-trips the exact 16-byte block");
        }
    }

    [[nodiscard]] static std::array<std::uint8_t, 4> ReadTexel(
        SkiaTextureRenderer& renderer, int x, int y)
    {
        auto image = renderer.SnapshotImage(
            CNA::Internal::Renderers::Skia::SkiaSourceAlphaConvention::Straight);
        std::array<std::uint8_t, 4> pixel{};
        const SkImageInfo info =
            SkImageInfo::Make(1, 1, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
        image->readPixels(info, pixel.data(), 4, x, y);
        return pixel;
    }

    void CheckAlphaModes(GraphicsDevice& device)
    {
        {
            InspectableCompressedTexture2D opaque(device, 4, 4, false, SurfaceFormat::Dxt1);
            opaque.SetData(kDxt1SolidRed.data(), static_cast<int>(kDxt1SolidRed.size()));
            auto* renderer = opaque.SkiaRenderer();
            const auto pixel = ReadTexel(*renderer, 0, 0);
            Check(checks_, failures_,
                  pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255,
                  "Dxt1 4-colour opaque mode decodes RGBA(255,0,0,255)");
        }
        {
            InspectableCompressedTexture2D transparent(device, 4, 4, false, SurfaceFormat::Dxt1);
            transparent.SetData(kDxt1Transparent.data(),
                                static_cast<int>(kDxt1Transparent.size()));
            auto* renderer = transparent.SkiaRenderer();
            const auto pixel = ReadTexel(*renderer, 0, 0);
            Check(checks_, failures_, pixel[3] == 0,
                  "Dxt1 index-3 in 3-colour mode decodes exactly alpha 0 (one-bit alpha)");
        }
        {
            InspectableCompressedTexture2D average(device, 4, 4, false, SurfaceFormat::Dxt1);
            average.SetData(kDxt1AverageOpaque.data(),
                            static_cast<int>(kDxt1AverageOpaque.size()));
            auto* renderer = average.SkiaRenderer();
            const auto pixel = ReadTexel(*renderer, 0, 0);
            Check(checks_, failures_, pixel[3] == 255,
                  "Dxt1 index-2 average colour in 3-colour mode stays fully opaque");
        }
        {
            InspectableCompressedTexture2D texture(device, 4, 4, false, SurfaceFormat::Dxt3);
            texture.SetData(kDxt3HalfAlpha.data(), static_cast<int>(kDxt3HalfAlpha.size()));
            auto* renderer = texture.SkiaRenderer();
            const auto pixel = ReadTexel(*renderer, 0, 0);
            Check(checks_, failures_, pixel[3] == 0x88,
                  "Dxt3 explicit 4-bit alpha nibble 0x8 decodes to replicated byte 0x88");
        }
        {
            InspectableCompressedTexture2D texture(device, 4, 4, false, SurfaceFormat::Dxt5);
            texture.SetData(kDxt5InterpolatedAlpha.data(),
                            static_cast<int>(kDxt5InterpolatedAlpha.size()));
            // Texel (x,y) maps to alpha-mask linear position p = 4y + x; the packed mask below
            // sets alphaIndex = p mod 8, so (0,0)->0, (2,1)->6, (3,1)->7.
            auto* renderer = texture.SkiaRenderer();
            const auto index0 = ReadTexel(*renderer, 0, 0);
            const auto index6 = ReadTexel(*renderer, 2, 1);
            const auto index7 = ReadTexel(*renderer, 3, 1);
            Check(checks_, failures_,
                  index0[3] == 0 && index6[3] == 0 && index7[3] == 255,
                  "Dxt5 alpha0<alpha1 mode decodes explicit index 6->0 and index 7->255");
        }
    }

    void CheckBlockAlignmentAndRange(GraphicsDevice& device)
    {
        InspectableCompressedTexture2D texture(device, 8, 8, false, SurfaceFormat::Dxt1);
        std::array<std::uint8_t, 32> full{};
        for (int block = 0; block < 4; ++block)
            std::copy(kDxt1SolidRed.begin(), kDxt1SolidRed.end(), full.begin() + block * 8);
        texture.SetData(full.data(), static_cast<int>(full.size()));

        Check(checks_, failures_,
              !Throws([&] {
                  const Rectangle rect(0, 0, 4, 4);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "block-aligned interior 4x4 rect on an 8x8 level is accepted");
        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(2, 0, 4, 4);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "rect with x not a multiple of 4 is rejected");
        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(0, 2, 4, 4);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "rect with y not a multiple of 4 is rejected");
        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(4, 4, 8, 8);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "rect exceeding the level bounds is rejected");

        std::array<std::uint8_t, 8> readback{};
        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(1, 0, 4, 4);
                  texture.GetData(0, &rect, readback.data(), 0,
                                  static_cast<int>(readback.size()));
              }),
              "GetData enforces the same block-alignment policy as SetData");
    }

    void CheckNpotEdgePolicy(GraphicsDevice& device)
    {
        // 6x6 Dxt1: blockCols=blockRows=ceil(6/4)=2 -> padded level bytes = 2*2*8 = 32.
        InspectableCompressedTexture2D texture(device, 6, 6, false, SurfaceFormat::Dxt1);
        std::array<std::uint8_t, 32> full{};
        for (int block = 0; block < 4; ++block)
            std::copy(kDxt1SolidRed.begin(), kDxt1SolidRed.end(), full.begin() + block * 8);
        texture.SetData(full.data(), static_cast<int>(full.size()));
        std::array<std::uint8_t, 32> readback{};
        texture.GetData(readback.data(), static_cast<int>(readback.size()));
        Check(checks_, failures_, readback == full,
              "6x6 NPOT Dxt1 full-level transfer uses the exact 32-byte padded block count");

        // x=4 is block-aligned; w=2 does not reach a multiple of 4 but does touch the true
        // right edge (4+2 == 6) -- the NPOT edge exception. h=4 is already block-aligned, so
        // this rect covers exactly the single block at (blockX=1, blockY=0).
        Check(checks_, failures_,
              !Throws([&] {
                  const Rectangle rect(4, 0, 2, 4);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "a rect reaching the true NPOT right edge is accepted without being block-multiple");
        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(0, 0, 2, 2);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "an interior rect that is neither block-aligned nor edge-touching is rejected");
    }

    void CheckAuthoredMipChain(GraphicsDevice& device)
    {
        InspectableCompressedTexture2D texture(device, 8, 8, true, SurfaceFormat::Dxt1);
        Check(checks_, failures_, texture.getLevelCountProperty() == 4,
              "8x8 mip-mapped Dxt1 texture reports the expected four levels");

        std::array<std::uint8_t, 32> level0{};
        for (int block = 0; block < 4; ++block)
            std::copy(kDxt1SolidRed.begin(), kDxt1SolidRed.end(), level0.begin() + block * 8);
        texture.SetData(0, nullptr, level0.data(), 0, static_cast<int>(level0.size()));

        std::array<std::uint8_t, 8> level1Readback{};
        texture.GetData(1, nullptr, level1Readback.data(), 0,
                        static_cast<int>(level1Readback.size()));
        Check(checks_, failures_,
              level1Readback == std::array<std::uint8_t, 8>{0, 0, 0, 0, 0, 0, 0, 0},
              "an unauthored descendant mip level stays exactly zero -- never generated");

        texture.SetData(1, nullptr, kDxt1SolidGreen.data(), 0,
                        static_cast<int>(kDxt1SolidGreen.size()));
        std::array<std::uint8_t, 8> authoredReadback{};
        texture.GetData(1, nullptr, authoredReadback.data(), 0,
                        static_cast<int>(authoredReadback.size()));
        Check(checks_, failures_, authoredReadback == kDxt1SolidGreen,
              "explicitly authoring a descendant mip level makes it exactly readable");

        auto* renderer = texture.SkiaRenderer();
        Check(checks_, failures_,
              renderer && renderer->MipGenerationCountEXT(0) == 0
                  && renderer->MipGenerationCountEXT(1) == 0
                  && renderer->MipGenerationCountEXT(2) == 0
                  && renderer->MipGenerationCountEXT(3) == 0,
              "no compressed mip level is ever counted as generated");
    }

    void CheckDecodedSampling(GraphicsDevice& device)
    {
        InspectableCompressedTexture2D red(device, 4, 4, false, SurfaceFormat::Dxt1);
        red.SetData(kDxt1SolidRed.data(), static_cast<int>(kDxt1SolidRed.size()));
        InspectableCompressedTexture2D green(device, 4, 4, false, SurfaceFormat::Dxt1);
        green.SetData(kDxt1SolidGreen.data(), static_cast<int>(kDxt1SolidGreen.size()));

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, point, nullptr, nullptr);
        spriteBatch_->Draw(red, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(green, Rectangle(4, 0, 4, 4), Color::White);
        spriteBatch_->End();

        const auto sample = [&](int x) {
            const Rectangle region(x, 2, 1, 1);
            Color result = Color::Transparent;
            device.GetBackBufferData(&region, &result, 0, 1);
            return result;
        };
        const Color redPixel = sample(2);
        const Color greenPixel = sample(6);
        Check(checks_, failures_,
              redPixel.getRProperty() == 255 && redPixel.getGProperty() == 0
                  && redPixel.getBProperty() == 0,
              "Dxt1 solid-red texture samples through public SpriteBatch drawing");
        Check(checks_, failures_,
              greenPixel.getRProperty() == 0 && greenPixel.getGProperty() == 255
                  && greenPixel.getBProperty() == 0,
              "Dxt1 solid-green texture samples through public SpriteBatch drawing");
    }

    void CheckMalformedData(GraphicsDevice& device)
    {
        InspectableCompressedTexture2D texture(device, 4, 4, false, SurfaceFormat::Dxt1);
        texture.SetData(kDxt1SolidRed.data(), static_cast<int>(kDxt1SolidRed.size()));

        Check(checks_, failures_,
              Throws([&] { texture.SetData(kDxt1SolidGreen.data(), 4); }),
              "SetData with fewer bytes than one full Dxt1 block is rejected");
        std::array<std::uint8_t, 8> unchanged{};
        texture.GetData(unchanged.data(), static_cast<int>(unchanged.size()));
        Check(checks_, failures_, unchanged == kDxt1SolidRed,
              "a rejected undersized SetData leaves the previous block bytes exactly unchanged");

        Check(checks_, failures_,
              Throws([&] {
                  const Rectangle rect(-4, 0, 4, 4);
                  texture.SetData(0, &rect, kDxt1SolidGreen.data(), 0,
                                  static_cast<int>(kDxt1SolidGreen.size()));
              }),
              "a negative rect origin is rejected");
    }

    void CheckTargetRefusal(GraphicsDevice& device, SkiaRenderer& graphicsRenderer)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        const std::array formats{SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5};
        bool allRejected = true;
        for (const SurfaceFormat format : formats)
        {
            allRejected = allRejected && Throws([&] {
                RenderTarget2D target(device, 4, 4, false, format, DepthFormat::None);
                (void)target;
            });
        }
        Check(checks_, failures_,
              allRejected && SameStats(graphicsRenderer.GetResourceStatsEXT(), before),
              "Dxt1/Dxt3/Dxt5 RenderTarget2D construction rejects transactionally "
              "(block-compressed formats are never FNA-renderable)");
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DCompressedFormatsTest game;
    game.Run();
    return game.Result();
}
