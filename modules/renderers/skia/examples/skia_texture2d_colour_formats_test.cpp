// SPDX-License-Identifier: MS-PL
// SKIA-136: BGRA/sRGB Texture2D transfer bytes, colour-space routing, mips and refusal gates.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <utility>

using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Internal::Renderers::Skia::SkiaResourceStats;
using CNA::Internal::Renderers::Skia::SkiaSourceAlphaConvention;
using CNA::Internal::Renderers::Skia::SkiaTextureRenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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

    [[nodiscard]] bool SameBytes(const Color& left, const Color& right)
    {
        return left.getRProperty() == right.getRProperty()
            && left.getGProperty() == right.getGProperty()
            && left.getBProperty() == right.getBProperty()
            && left.getAProperty() == right.getAProperty();
    }

    template<std::size_t Size>
    [[nodiscard]] bool SameBytes(const std::array<Color, Size>& left,
                                 const std::array<Color, Size>& right)
    {
        for (std::size_t index = 0; index < Size; ++index)
        {
            if (!SameBytes(left[index], right[index]))
                return false;
        }
        return true;
    }

    [[nodiscard]] bool NearChannel(std::uint8_t actual, int expected, int tolerance = 1)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }

    [[nodiscard]] bool NearPixel(const std::array<std::uint8_t, 4>& actual,
                                 const std::array<int, 4>& expected, int tolerance = 1)
    {
        return NearChannel(actual[0], expected[0], tolerance)
            && NearChannel(actual[1], expected[1], tolerance)
            && NearChannel(actual[2], expected[2], tolerance)
            && NearChannel(actual[3], expected[3], tolerance);
    }

    [[nodiscard]] std::array<std::uint8_t, 4> DrawToColourSpace(
        const sk_sp<SkImage>& image, sk_sp<SkColorSpace> colourSpace)
    {
        const SkImageInfo info = SkImageInfo::Make(
            1, 1, kRGBA_8888_SkColorType, kPremul_SkAlphaType, std::move(colourSpace));
        const sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
        if (!surface || !image)
            return {};
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        surface->getCanvas()->drawImage(
            image, 0.0f, 0.0f, SkSamplingOptions(SkFilterMode::kNearest), &paint);
        SkPixmap pixels;
        if (!surface->peekPixels(&pixels) || pixels.addr() == nullptr)
            return {};
        const auto* bytes = static_cast<const std::uint8_t*>(pixels.addr());
        return {bytes[0], bytes[1], bytes[2], bytes[3]};
    }
}

class InspectableColourTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureRenderer* SkiaRenderer() const
    {
        return dynamic_cast<SkiaTextureRenderer*>(GetRendererRaw());
    }
};

class SkiaTexture2DColourFormatsTest final : public Game
{
public:
    SkiaTexture2DColourFormatsTest()
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
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* graphicsRenderer = dynamic_cast<SkiaRenderer*>(&device.GetRenderer());
        Check(graphicsRenderer != nullptr, "public GraphicsDevice owns the Skia renderer");
        if (!graphicsRenderer)
        {
            Exit();
            return;
        }

        const SkiaResourceStats baseline = graphicsRenderer->GetResourceStatsEXT();
        ExerciseRawTransfers(device, *graphicsRenderer, SurfaceFormat::ColorBgraEXT,
                             "ColorBgraEXT");
        ExerciseRawTransfers(device, *graphicsRenderer, SurfaceFormat::ColorSrgbEXT,
                             "ColorSrgbEXT");
        CheckMipGeneration(device);
        CheckColourMetadata(device);
        CheckSampling(device);
        CheckRefusals(device, *graphicsRenderer);
        CheckSrgbRenderTargetPromotion(device, *graphicsRenderer);
        Check(SameTextureStats(graphicsRenderer->GetResourceStatsEXT(), baseline),
              "all colour-format fixtures return resource counters to baseline");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

private:
    void ExerciseRawTransfers(GraphicsDevice& device, SkiaRenderer& graphicsRenderer,
                              SurfaceFormat format, const char* name)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        {
            InspectableColourTexture2D texture(device, 3, 2, true, format);
            SkiaTextureRenderer* renderer = texture.SkiaRenderer();
            const SkiaResourceStats allocated = graphicsRenderer.GetResourceStatsEXT();
            Check(texture.getFormatProperty() == format && texture.getLevelCountProperty() == 2
                      && renderer && renderer->FormatEXT() == format,
                  std::string(name) + " constructs the exact public/renderer format and mip count");
            Check(allocated.textureRenderers == before.textureRenderers + 1u
                      && allocated.textureImageViews == before.textureImageViews + 2u
                      && allocated.textureImageBytes == before.textureImageBytes + 48u
                      && allocated.mipChains2D == before.mipChains2D + 1u
                      && allocated.mipChain2DStorageBytes
                          == before.mipChain2DStorageBytes + 28u,
                  std::string(name) + " accounts two 32-bit image views and its mip chain");

            std::array<Color, 6> expected{
                Color(1, 2, 3, 4), Color(17, 33, 65, 129), Color(250, 7, 91, 255),
                Color(11, 99, 201, 13), Color(128, 127, 126, 125), Color(0, 255, 37, 211),
            };
            const Color guard(77, 66, 55, 44);
            std::array<Color, 8> upload{guard, guard, guard, guard, guard, guard, guard, guard};
            std::copy(expected.begin(), expected.end(), upload.begin() + 1);
            texture.SetData(0, nullptr, upload.data(), 1, 7);

            std::array<Color, 8> read{guard, guard, guard, guard, guard, guard, guard, guard};
            texture.GetData(read.data(), 1, 7);
            bool exact = SameBytes(read.front(), guard) && SameBytes(read.back(), guard);
            for (std::size_t index = 0; index < expected.size(); ++index)
                exact = exact && SameBytes(read[index + 1u], expected[index]);
            Check(exact, std::string(name) + " full transfer preserves exact caller bytes and guards");

            bool rawExact = renderer != nullptr;
            if (renderer)
            {
                const std::uint8_t* raw = renderer->MipChainEXT().LevelData(0);
                for (std::size_t index = 0; index < expected.size(); ++index)
                {
                    rawExact = rawExact
                        && raw[index * 4u + 0u] == expected[index].getRProperty()
                        && raw[index * 4u + 1u] == expected[index].getGProperty()
                        && raw[index * 4u + 2u] == expected[index].getBProperty()
                        && raw[index * 4u + 3u] == expected[index].getAProperty();
                }
            }
            Check(rawExact, std::string(name) + " renderer storage retains the public raw byte order");

            const Rectangle patchRectangle(1, 0, 1, 2);
            std::array<Color, 4> patchUpload{guard, Color(9, 19, 29, 39),
                                             Color(49, 59, 69, 79), guard};
            texture.SetData(0, &patchRectangle, patchUpload.data(), 1, 3);
            expected[1] = patchUpload[1];
            expected[4] = patchUpload[2];

            std::array<Color, 4> patchRead{guard, guard, guard, guard};
            texture.GetData(0, &patchRectangle, patchRead.data(), 1, 3);
            Check(SameBytes(patchRead[0], guard) && SameBytes(patchRead[1], patchUpload[1])
                      && SameBytes(patchRead[2], patchUpload[2])
                      && SameBytes(patchRead[3], guard),
                  std::string(name) + " partial rectangle is exact, row-major and guard-safe");

            std::array<Color, 6> whole{
                Color::Transparent, Color::Transparent, Color::Transparent,
                Color::Transparent, Color::Transparent, Color::Transparent,
            };
            texture.GetData(whole.data(), static_cast<int>(whole.size()));
            Check(SameBytes(whole, expected),
                  std::string(name) + " partial update preserves every neighbouring raw texel");
            const auto beforeFailure = whole;
            Check(Throws([&] { texture.SetData(0, nullptr, upload.data(), 0, 5); }),
                  std::string(name) + " rejects an undersized full upload");
            texture.GetData(whole.data(), static_cast<int>(whole.size()));
            Check(SameBytes(whole, beforeFailure),
                  std::string(name) + " rejected upload leaves all raw bytes unchanged");
        }
        Check(SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              std::string(name) + " destruction releases exact resource accounting");
    }

    void CheckMipGeneration(GraphicsDevice& device)
    {
        InspectableColourTexture2D bgra(device, 2, 2, true, SurfaceFormat::ColorBgraEXT);
        const std::array<Color, 4> bgraBase{
            Color(10, 20, 30, 0), Color(20, 30, 40, 64),
            Color(30, 40, 50, 128), Color(40, 50, 60, 255),
        };
        bgra.SetData(bgraBase.data(), static_cast<int>(bgraBase.size()));
        Color bgraMip = Color::Transparent;
        bgra.GetData(1, nullptr, &bgraMip, 0, 1);
        Check(SameBytes(bgraMip, Color(25, 35, 45, 112))
                  && bgra.SkiaRenderer() && bgra.SkiaRenderer()->MipGenerationCountEXT(1) == 1u,
              "ColorBgraEXT mip generation averages its four raw byte channels once");

        InspectableColourTexture2D srgb(device, 2, 2, true, SurfaceFormat::ColorSrgbEXT);
        const std::array<Color, 4> srgbBase{
            Color(0, 0, 0, 0), Color(255, 255, 255, 64),
            Color(255, 255, 255, 128), Color(0, 0, 0, 255),
        };
        srgb.SetData(srgbBase.data(), static_cast<int>(srgbBase.size()));
        Color srgbMip = Color::Transparent;
        srgb.GetData(1, nullptr, &srgbMip, 0, 1);
        Check(SameBytes(srgbMip, Color(188, 188, 188, 112))
                  && srgb.SkiaRenderer() && srgb.SkiaRenderer()->MipGenerationCountEXT(1) == 1u,
              "ColorSrgbEXT mip RGB averages in linear light while alpha averages linearly");
    }

    void CheckColourMetadata(GraphicsDevice& device)
    {
        InspectableColourTexture2D bgra(device, 1, 1, false, SurfaceFormat::ColorBgraEXT);
        InspectableColourTexture2D srgb(device, 1, 1, false, SurfaceFormat::ColorSrgbEXT);
        const sk_sp<SkImage> bgraImage = bgra.SkiaRenderer()->SnapshotImage(
            SkiaSourceAlphaConvention::Straight);
        const sk_sp<SkImage> srgbImage = srgb.SkiaRenderer()->SnapshotImage(
            SkiaSourceAlphaConvention::Straight);
        Check(bgraImage && bgraImage->colorType() == kBGRA_8888_SkColorType
                  && bgraImage->colorSpace() == nullptr,
              "ColorBgraEXT exposes native BGRA storage without an invented colour space");
        Check(srgbImage && srgbImage->colorType() == kSRGBA_8888_SkColorType
                  && srgbImage->colorSpace() && srgbImage->colorSpace()->gammaIsLinear(),
              "ColorSrgbEXT labels encoded bytes for one decode into linear-sRGB working space");

        const Color encoded(128, 64, 32, 255);
        srgb.SetData(&encoded, 1);
        const sk_sp<SkImage> encodedImage = srgb.SkiaRenderer()->SnapshotImage(
            SkiaSourceAlphaConvention::Straight);
        const auto linear = DrawToColourSpace(encodedImage, SkColorSpace::MakeSRGBLinear());
        const auto encodedAgain = DrawToColourSpace(encodedImage, SkColorSpace::MakeSRGB());
        Check(NearPixel(linear, {55, 13, 4, 255}),
              "explicit linear destination receives exactly one sRGB decode");
        Check(NearPixel(encodedAgain, {128, 64, 32, 255}),
              "explicit sRGB destination re-encodes once without double conversion");
    }

    void CheckSampling(GraphicsDevice& device)
    {
        Texture2D bgra(device, 1, 1, false, SurfaceFormat::ColorBgraEXT);
        Texture2D srgb(device, 1, 1, false, SurfaceFormat::ColorSrgbEXT);
        const Color bgraRaw(10, 20, 240, 255);
        const Color srgbEncoded(128, 64, 32, 255);
        bgra.SetData(&bgraRaw, 1);
        srgb.SetData(&srgbEncoded, 1);

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            point, nullptr, nullptr);
        spriteBatch_->Draw(bgra, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(srgb, Rectangle(4, 0, 4, 4), Color::White);
        spriteBatch_->End();

        const auto sample = [&](int x) {
            const Rectangle region(x, 2, 1, 1);
            Color result = Color::Transparent;
            device.GetBackBufferData(&region, &result, 0, 1);
            return result;
        };
        const Color bgraPixel = sample(2);
        const Color srgbPixel = sample(6);
        Check(SameBytes(bgraPixel, Color(240, 20, 10, 255)),
              "ColorBgraEXT sampling interprets exact public bytes as BGRA channels");
        Check(NearChannel(srgbPixel.getRProperty(), 55)
                  && NearChannel(srgbPixel.getGProperty(), 13)
                  && NearChannel(srgbPixel.getBProperty(), 4)
                  && srgbPixel.getAProperty() == 255,
              "ColorSrgbEXT SpriteBatch sampling decodes RGB exactly once to linear output");
    }

    void CheckRefusals(GraphicsDevice& device, SkiaRenderer& graphicsRenderer)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        Check(Throws([&] {
                  RenderTarget2D target(device, 2, 2, false, SurfaceFormat::ColorBgraEXT,
                                        DepthFormat::None);
                  (void)target;
              }) && SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              "ColorBgraEXT RenderTarget2D requests reject transactionally");

        Texture2D srgb(device, 1, 1, false, SurfaceFormat::ColorSrgbEXT);
        PackedVector::Rgba1010102 wrong;
        wrong.setPackedValueProperty(0xFFFFFFFFu);
        Check(Throws([&] { srgb.SetData(&wrong, 1); }),
              "packed transfer overload cannot reinterpret ColorSrgbEXT storage");
        const std::array<std::uint8_t, 4> raw{1, 2, 3, 4};
        Check(Throws([&] { srgb.SetDataRGBA(raw.data(), 1); }),
              "raw-RGBA convenience overload remains restricted to SurfaceFormat::Color");
    }

    void CheckSrgbRenderTargetPromotion(GraphicsDevice& device, SkiaRenderer& graphicsRenderer)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::ColorSrgbEXT,
                                  DepthFormat::None);
            const SkiaResourceStats allocated = graphicsRenderer.GetResourceStatsEXT();
            Check(allocated.renderTargets == before.renderTargets + 1u
                      && allocated.targetSurfaceBytes == before.targetSurfaceBytes + 16u,
                  "SKIA-142 promotes ColorSrgbEXT to a constructible RenderTarget2D");
        }
        Check(SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              "ColorSrgbEXT RenderTarget2D destruction releases its surface accounting");
    }

    [[nodiscard]] static bool SameTextureStats(const SkiaResourceStats& left,
                                               const SkiaResourceStats& right)
    {
        return left.textureRenderers == right.textureRenderers
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
    SkiaTexture2DColourFormatsTest game;
    game.Run();
    return game.Result();
}
