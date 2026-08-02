// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-153: pixel-exact SpriteBatch source-rectangle orientation contract.
//
// The oracle keeps three coordinate spaces independent:
//
//   1. source storage/readback -- RenderTarget2D::GetData must expose the same top-left bytes that
//      were uploaded to the ordinary Texture2D used to produce it;
//   2. source selection -- a partial source rectangle must select those exact logical rows for an
//      ordinary Texture2D and for a RenderTarget2D used as a texture;
//   3. destination orientation -- the selected pixels must land top-to-bottom in the requested
//      destination on both another render target and the backbuffer.
//
// Every failure is compared with mutually distinct candidates: the source rectangle mirrored
// around the whole texture (the recorded GFX-153 defect), the correct source with the final
// destination vertically flipped, an uncompensated bottom-left texture origin, the previous
// producer generation, and the destination poison colour. The row-marker source gives every
// logical row a unique identity, while the second source has four asymmetric corner markers.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_BACKEND_BGFX)
    constexpr const char* kBackendName = "BGFX";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadbackRequired = true;
#elif defined(CNA_BACKEND_EASYGL)
    constexpr const char* kBackendName = "EASYGL";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadbackRequired = true;
#elif defined(CNA_BACKEND_VULKAN)
    constexpr const char* kBackendName = "VULKAN";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadbackRequired = true;
#elif defined(CNA_BACKEND_WEBGPU)
    constexpr const char* kBackendName = "WEBGPU";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadbackRequired = true;
#elif defined(CNA_BACKEND_SDL_GPU)
    constexpr const char* kBackendName = "SDL_GPU";
    constexpr bool kRasterizes = true;
    // REMED-GFX-165 supplies SDL_GPU's lazy readback proxy, so its backbuffer is measurable here.
    constexpr bool kBackbufferReadbackRequired = true;
#elif defined(CNA_BACKEND_SOFTWARE)
    constexpr const char* kBackendName = "SOFTWARE";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadbackRequired = true;
#elif defined(CNA_BACKEND_HEADLESS)
    constexpr const char* kBackendName = "HEADLESS";
    constexpr bool kRasterizes = false;
    constexpr bool kBackbufferReadbackRequired = false;
#else
#error "REMED-GFX-153 fixture requires a declared backend contract."
#endif

    constexpr int kRowW = 8;
    constexpr int kRowH = 7;
    constexpr int kCornerW = 6;
    constexpr int kCornerH = 5;
    constexpr int kBackbufferW = 96;
    constexpr int kBackbufferH = 64;

    const Color kPoison(3, 5, 7, 255);

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() &&
               a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    Color RowColor(int x, int y, int generation)
    {
        if (generation == 0)
        {
            return Color(17 + x * 27, 19 + y * 31, 23 + x * 7 + y * 13, 255);
        }
        return Color(229 - x * 23, 211 - y * 27, 31 + x * 11 + y * 9, 255);
    }

    std::vector<Color> MakeRows(int generation)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kRowW) * kRowH, Color(0, 0, 0, 0));
        for (int y = 0; y < kRowH; ++y)
            for (int x = 0; x < kRowW; ++x)
                pixels[static_cast<std::size_t>(y) * kRowW + x] = RowColor(x, y, generation);
        return pixels;
    }

    std::vector<Color> MakeCorners()
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kCornerW) * kCornerH,
                                  Color(0, 0, 0, 0));
        for (int y = 0; y < kCornerH; ++y)
            for (int x = 0; x < kCornerW; ++x)
                pixels[static_cast<std::size_t>(y) * kCornerW + x] =
                    Color(41 + x * 17, 37 + y * 23, 29 + x * 9 + y * 11, 255);

        pixels[0] = Color(241, 17, 31, 255);                                      // top-left
        pixels[kCornerW - 1] = Color(19, 229, 47, 255);                           // top-right
        pixels[static_cast<std::size_t>(kCornerH - 1) * kCornerW] =
            Color(29, 53, 239, 255);                                               // bottom-left
        pixels[static_cast<std::size_t>(kCornerH) * kCornerW - 1] =
            Color(233, 211, 37, 255);                                              // bottom-right
        return pixels;
    }

    SpriteEffects ToggleVertical(SpriteEffects effects)
    {
        using Underlying = std::underlying_type_t<SpriteEffects>;
        return static_cast<SpriteEffects>(static_cast<Underlying>(effects) ^
                                          static_cast<Underlying>(SpriteEffects::FlipVertically));
    }

    std::vector<Color> ExpectedRegion(const std::vector<Color>& source, int sourceW, int sourceH,
                                      Rectangle rect, int scaleX, int scaleY, SpriteEffects effects)
    {
        const int outW = rect.Width * scaleX;
        const int outH = rect.Height * scaleY;
        std::vector<Color> result(static_cast<std::size_t>(outW) * outH, kPoison);
        const bool flipX = (effects & SpriteEffects::FlipHorizontally) != SpriteEffects::None;
        const bool flipY = (effects & SpriteEffects::FlipVertically) != SpriteEffects::None;
        for (int y = 0; y < outH; ++y)
        {
            int sourceY = y / scaleY;
            if (flipY) sourceY = rect.Height - 1 - sourceY;
            for (int x = 0; x < outW; ++x)
            {
                int sourceX = x / scaleX;
                if (flipX) sourceX = rect.Width - 1 - sourceX;
                result[static_cast<std::size_t>(y) * outW + x] =
                    source[static_cast<std::size_t>(rect.Y + sourceY) * sourceW + rect.X + sourceX];
            }
        }
        (void)sourceH;
        return result;
    }

    std::vector<Color> FlipDestinationVertically(const std::vector<Color>& pixels, int width, int height)
    {
        std::vector<Color> result(pixels.size(), Color(0, 0, 0, 0));
        for (int y = 0; y < height; ++y)
            std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(y * width), width,
                        result.begin() + static_cast<std::ptrdiff_t>((height - 1 - y) * width));
        return result;
    }

    int MatchCount(const std::vector<Color>& actual, const std::vector<Color>& expected)
    {
        const std::size_t count = std::min(actual.size(), expected.size());
        int matches = 0;
        for (std::size_t i = 0; i < count; ++i)
            if (Same(actual[i], expected[i])) ++matches;
        return matches;
    }

    std::vector<Color> Extract(const std::vector<Color>& pixels, int surfaceW,
                               Rectangle region)
    {
        std::vector<Color> result(static_cast<std::size_t>(region.Width) * region.Height,
                                  Color(0, 0, 0, 0));
        for (int y = 0; y < region.Height; ++y)
            for (int x = 0; x < region.Width; ++x)
                result[static_cast<std::size_t>(y) * region.Width + x] =
                    pixels[static_cast<std::size_t>(region.Y + y) * surfaceW + region.X + x];
        return result;
    }

    struct Readback
    {
        bool supported = true;
        bool wrongException = false;
        std::string what;
        int width = 0;
        int height = 0;
        std::vector<Color> pixels;
    };
}

class SourceRectangleOrientationTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> rowsTexture_;
    std::unique_ptr<Texture2D> cornersTexture_;
    std::unique_ptr<RenderTarget2D> rowsTarget_;
    std::unique_ptr<RenderTarget2D> cornersTarget_;
    std::vector<Color> rows_;
    std::vector<Color> corners_;
    bool done_ = false;
    int result_ = 1;
    int passed_ = 0;
    int total_ = 0;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok) ++passed_;
    }

    static void ResetState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    void BeginPoint()
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr,
                            const_cast<RasterizerState*>(&RasterizerState::CullNone));
    }

    void DrawSprite(const Texture2D& texture, Rectangle destination, Rectangle source,
                    SpriteEffects effects, bool implicitFull)
    {
        if (implicitFull)
            spriteBatch_->Draw(texture, destination, Color::White);
        else
            spriteBatch_->Draw(texture, destination, source, Color::White, 0.0f, Vector2::Zero,
                               effects, 0.0f);
    }

    void RenderFull(GraphicsDevice& device, const Texture2D& texture, RenderTarget2D& target,
                    int width, int height)
    {
        device.SetRenderTarget(&target);
        ResetState(device);
        device.Clear(kPoison);
        BeginPoint();
        spriteBatch_->Draw(texture, Rectangle(0, 0, width, height), Color::White);
        spriteBatch_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(device);
    }

    Readback ReadTarget(RenderTarget2D& target, int width, int height)
    {
        Readback result;
        result.width = width;
        result.height = height;
        result.pixels.assign(static_cast<std::size_t>(width) * height, Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            target.GetData(result.pixels.data(), 0, static_cast<int>(result.pixels.size()));
        }
        catch (const System::NotSupportedException&)
        {
            result.supported = false;
        }
        catch (const std::exception& e)
        {
            result.supported = false;
            result.wrongException = true;
            result.what = e.what();
        }
        catch (...)
        {
            result.supported = false;
            result.wrongException = true;
            result.what = "non-std exception";
        }
        return result;
    }

    Readback ReadBackbuffer(GraphicsDevice& device)
    {
        Readback result;
        result.width = kBackbufferW;
        result.height = kBackbufferH;
        result.pixels.assign(static_cast<std::size_t>(kBackbufferW) * kBackbufferH,
                             Color(0xCD, 0xCD, 0xCD, 0xCD));
        try
        {
            device.GetBackBufferData(result.pixels.data(), 0, static_cast<int>(result.pixels.size()));
        }
        catch (const System::NotSupportedException&)
        {
            result.supported = false;
        }
        catch (const std::exception& e)
        {
            result.supported = false;
            result.wrongException = true;
            result.what = e.what();
        }
        catch (...)
        {
            result.supported = false;
            result.wrongException = true;
            result.what = "non-std exception";
        }
        return result;
    }

    bool CheckExact(const std::vector<Color>& actual, const std::vector<Color>& expected,
                    const std::string& label)
    {
        const int matches = MatchCount(actual, expected);
        std::string first;
        if (matches != static_cast<int>(expected.size()))
        {
            const std::size_t count = std::min(actual.size(), expected.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!Same(actual[i], expected[i]))
                {
                    first = "; first index " + std::to_string(i) + " got=" + ColorText(actual[i]) +
                            " want=" + ColorText(expected[i]);
                    break;
                }
            }
        }
        const bool ok = actual.size() == expected.size() && matches == static_cast<int>(expected.size());
        Check(ok, label + " (" + std::to_string(matches) + "/" +
                  std::to_string(expected.size()) + " exact)" + first);
        return ok;
    }

    void PrintOrientationClassification(const std::vector<Color>& actual,
                                        const std::vector<Color>& source, int sourceW, int sourceH,
                                        Rectangle rect, int scaleX, int scaleY, SpriteEffects effects,
                                        const std::optional<std::vector<Color>>& stale = std::nullopt)
    {
        const auto expected = ExpectedRegion(source, sourceW, sourceH, rect, scaleX, scaleY, effects);
        Rectangle mirroredRect(rect.X, sourceH - rect.Y - rect.Height, rect.Width, rect.Height);
        const auto mirroredSelection =
            ExpectedRegion(source, sourceW, sourceH, mirroredRect, scaleX, scaleY, effects);
        const auto destinationFlip =
            FlipDestinationVertically(expected, rect.Width * scaleX, rect.Height * scaleY);
        const auto wrongOrigin =
            ExpectedRegion(source, sourceW, sourceH, mirroredRect, scaleX, scaleY,
                           ToggleVertical(effects));
        const std::vector<Color> poison(actual.size(), kPoison);

        std::printf("[INFO] orientation classification: expected=%d/%zu "
                    "mirrored-source-selection=%d/%zu destination-vflip=%d/%zu "
                    "uncompensated-origin=%d/%zu poison=%d/%zu",
                    MatchCount(actual, expected), expected.size(),
                    MatchCount(actual, mirroredSelection), mirroredSelection.size(),
                    MatchCount(actual, destinationFlip), destinationFlip.size(),
                    MatchCount(actual, wrongOrigin), wrongOrigin.size(),
                    MatchCount(actual, poison), poison.size());
        if (stale.has_value())
            std::printf(" stale-generation=%d/%zu", MatchCount(actual, stale.value()), stale->size());
        std::printf("\n");

        if (!actual.empty())
        {
            std::vector<int> rows;
            for (int y = 0; y < sourceH; ++y)
            {
                bool found = false;
                for (int x = 0; x < sourceW && !found; ++x)
                    found = Same(actual.front(), source[static_cast<std::size_t>(y) * sourceW + x]);
                if (found) rows.push_back(y);
            }
            std::printf("[INFO] first output texel %s belongs to logical source row(s):",
                        ColorText(actual.front()).c_str());
            for (int row : rows) std::printf(" %d", row);
            if (rows.empty()) std::printf(" none");
            std::printf("\n");
        }
        std::fflush(stdout);
    }

    struct Case
    {
        const char* name;
        const Texture2D* texture;
        const RenderTarget2D* target;
        const std::vector<Color>* source;
        int sourceW;
        int sourceH;
        Rectangle rect;
        int scaleX;
        int scaleY;
        SpriteEffects effects;
        bool implicitFull;
    };

    void RunTargetCase(GraphicsDevice& device, const Case& testCase)
    {
        const int drawW = testCase.rect.Width * testCase.scaleX;
        const int drawH = testCase.rect.Height * testCase.scaleY;
        const int gap = 2;
        const int targetW = drawW * 2 + gap;
        const int targetH = drawH + 2;
        RenderTarget2D destination(device, targetW, targetH, false, SurfaceFormat::Color,
                                   DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&destination);
        ResetState(device);
        device.Clear(kPoison);
        BeginPoint();
        DrawSprite(*testCase.texture, Rectangle(0, 1, drawW, drawH), testCase.rect,
                   testCase.effects, testCase.implicitFull);
        DrawSprite(*testCase.target, Rectangle(drawW + gap, 1, drawW, drawH), testCase.rect,
                   testCase.effects, testCase.implicitFull);
        spriteBatch_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Readback read = ReadTarget(destination, targetW, targetH);
        if (!read.supported)
        {
            Check(false, std::string(testCase.name) + ": destination target readback refused" +
                         (read.what.empty() ? "" : " (" + read.what + ")"));
            return;
        }

        const Rectangle ordinaryRegion(0, 1, drawW, drawH);
        const Rectangle targetRegion(drawW + gap, 1, drawW, drawH);
        const auto ordinary = Extract(read.pixels, targetW, ordinaryRegion);
        const auto rendered = Extract(read.pixels, targetW, targetRegion);
        const auto expected = ExpectedRegion(*testCase.source, testCase.sourceW, testCase.sourceH,
                                             testCase.rect, testCase.scaleX, testCase.scaleY,
                                             testCase.effects);
        const bool ordinaryOk = CheckExact(ordinary, expected,
                                            std::string(testCase.name) + " ordinary Texture2D");
        const bool targetOk = CheckExact(rendered, expected,
                                          std::string(testCase.name) + " RenderTarget2D source");
        CheckExact(rendered, ordinary,
                   std::string(testCase.name) + " source-kind identity into RenderTarget2D");
        if (!targetOk || !ordinaryOk)
            PrintOrientationClassification(rendered, *testCase.source, testCase.sourceW,
                                           testCase.sourceH, testCase.rect, testCase.scaleX,
                                           testCase.scaleY, testCase.effects);

        bool guards = true;
        for (int x = 0; x < targetW; ++x)
            guards = guards && Same(read.pixels[static_cast<std::size_t>(x)], kPoison) &&
                     Same(read.pixels[static_cast<std::size_t>(targetH - 1) * targetW + x], kPoison);
        for (int y = 0; y < targetH; ++y)
            for (int x = drawW; x < drawW + gap; ++x)
                guards = guards && Same(read.pixels[static_cast<std::size_t>(y) * targetW + x], kPoison);
        Check(guards, std::string(testCase.name) +
                      ": destination guards prove selection did not move or overwrite adjacent pixels");
    }

    void RunBackbufferCase(GraphicsDevice& device, const Case& testCase)
    {
        const int drawW = testCase.rect.Width * testCase.scaleX;
        const int drawH = testCase.rect.Height * testCase.scaleY;
        const int left = 3;
        const int top = 4;
        const int gap = 3;

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(device);
        device.Clear(kPoison);
        BeginPoint();
        DrawSprite(*testCase.texture, Rectangle(left, top, drawW, drawH), testCase.rect,
                   testCase.effects, testCase.implicitFull);
        DrawSprite(*testCase.target, Rectangle(left + drawW + gap, top, drawW, drawH), testCase.rect,
                   testCase.effects, testCase.implicitFull);
        spriteBatch_->End();

        const Readback read = ReadBackbuffer(device);
        if (!read.supported)
        {
            Check(!kBackbufferReadbackRequired && !read.wrongException,
                  std::string(testCase.name) + ": backbuffer readback boundary on " + kBackendName +
                  (read.what.empty() ? "" : " (" + read.what + ")"));
            return;
        }

        const auto ordinary = Extract(read.pixels, kBackbufferW,
                                      Rectangle(left, top, drawW, drawH));
        const auto rendered = Extract(read.pixels, kBackbufferW,
                                      Rectangle(left + drawW + gap, top, drawW, drawH));
        const auto expected = ExpectedRegion(*testCase.source, testCase.sourceW, testCase.sourceH,
                                             testCase.rect, testCase.scaleX, testCase.scaleY,
                                             testCase.effects);
        const bool ordinaryOk = CheckExact(ordinary, expected,
                                            std::string(testCase.name) + " ordinary -> backbuffer");
        const bool targetOk = CheckExact(rendered, expected,
                                          std::string(testCase.name) + " target source -> backbuffer");
        CheckExact(rendered, ordinary,
                   std::string(testCase.name) + " source-kind identity on backbuffer");
        if (!targetOk || !ordinaryOk)
            PrintOrientationClassification(rendered, *testCase.source, testCase.sourceW,
                                           testCase.sourceH, testCase.rect, testCase.scaleX,
                                           testCase.scaleY, testCase.effects);
        Check(Same(read.pixels[0], kPoison) &&
              Same(read.pixels[static_cast<std::size_t>(kBackbufferH - 1) * kBackbufferW +
                               (kBackbufferW - 1)], kPoison),
              std::string(testCase.name) + ": backbuffer corners retain the asymmetric poison guard");
    }

    void RunRotationCase(GraphicsDevice& device)
    {
        constexpr int width = 28;
        constexpr int height = 22;
        const Rectangle source(1, 1, 5, 3);
        const Rectangle destination(14, 11, 10, 6); // X/Y are the rotation pivot for this overload.
        const Vector2 origin(2.5f, 1.5f);
        constexpr float pi = 3.14159265358979323846f;

        RenderTarget2D ordinaryDestination(device, width, height, false, SurfaceFormat::Color,
                                             DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        RenderTarget2D targetDestination(device, width, height, false, SurfaceFormat::Color,
                                           DepthFormat::None, 0, RenderTargetUsage::DiscardContents);

        const auto render = [&](RenderTarget2D& destinationTarget, const Texture2D& sourceTexture)
        {
            device.SetRenderTarget(&destinationTarget);
            ResetState(device);
            device.Clear(kPoison);
            BeginPoint();
            spriteBatch_->Draw(sourceTexture, destination, source, Color::White, pi, origin,
                               SpriteEffects::FlipHorizontally, 0.0f);
            spriteBatch_->End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        };

        render(ordinaryDestination, *rowsTexture_);
        render(targetDestination, *rowsTarget_);
        const Readback ordinary = ReadTarget(ordinaryDestination, width, height);
        const Readback rendered = ReadTarget(targetDestination, width, height);
        if (!ordinary.supported || !rendered.supported)
        {
            Check(false, "rotation: destination readback must be supported");
            return;
        }

        CheckExact(rendered.pixels, ordinary.pixels,
                   "rotation + partial source + horizontal flip is pixel-identical by source kind");
        int changed = 0;
        for (const Color& pixel : ordinary.pixels)
            if (!Same(pixel, kPoison)) ++changed;
        Check(changed > 0, "rotation control rendered non-poison pixels");
        Check(Same(ordinary.pixels.front(), kPoison) && Same(ordinary.pixels.back(), kPoison),
              "rotation leaves distant destination corners untouched");
    }

    void RunTransitionAndStaleCase(GraphicsDevice& device)
    {
        const auto generation0 = MakeRows(0);
        const auto generation1 = MakeRows(1);
        Texture2D texture0(device, kRowW, kRowH, false, SurfaceFormat::Color);
        Texture2D texture1(device, kRowW, kRowH, false, SurfaceFormat::Color);
        texture0.SetData(generation0.data(), static_cast<int>(generation0.size()));
        texture1.SetData(generation1.data(), static_cast<int>(generation1.size()));
        RenderTarget2D source(device, kRowW, kRowH, false, SurfaceFormat::Color, DepthFormat::None,
                              0, RenderTargetUsage::DiscardContents);
        const Rectangle sampleRect(1, 1, 5, 3);
        constexpr int scale = 2;
        const int outputW = sampleRect.Width * scale;
        const int outputH = sampleRect.Height * scale;
        RenderTarget2D generation0Destination(device, outputW, outputH, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0,
                                               RenderTargetUsage::DiscardContents);
        RenderTarget2D generation1Destination(device, outputW, outputH, false, SurfaceFormat::Color,
                                               DepthFormat::None, 0,
                                               RenderTargetUsage::DiscardContents);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(device);
        device.Clear(kPoison);

        // No readback, Present, wait, or dummy bind separates these producer/consumer segments.
        RenderFull(device, texture0, source, kRowW, kRowH);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        BeginPoint();
        spriteBatch_->Draw(source, Rectangle(2, 2, kRowW, 1), Rectangle(0, 0, kRowW, 1),
                           Color::White);
        spriteBatch_->End();

        device.SetRenderTarget(&generation0Destination);
        device.Clear(kPoison);
        BeginPoint();
        spriteBatch_->Draw(source, Rectangle(0, 0, outputW, outputH), sampleRect, Color::White);
        spriteBatch_->End();

        RenderFull(device, texture1, source, kRowW, kRowH);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        BeginPoint();
        spriteBatch_->Draw(source, Rectangle(2, 6, kRowW, 1),
                           Rectangle(0, kRowH - 1, kRowW, 1), Color::White);
        spriteBatch_->Draw(texture1, Rectangle(12, 6, kRowW, 1),
                           Rectangle(0, kRowH - 1, kRowW, 1), Color::White);
        spriteBatch_->End();

        device.SetRenderTarget(&generation1Destination);
        device.Clear(kPoison);
        BeginPoint();
        spriteBatch_->Draw(source, Rectangle(0, 0, outputW, outputH), sampleRect, Color::White);
        spriteBatch_->End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const auto expected0 = ExpectedRegion(generation0, kRowW, kRowH, sampleRect, scale, scale,
                                               SpriteEffects::None);
        const auto expected1 = ExpectedRegion(generation1, kRowW, kRowH, sampleRect, scale, scale,
                                               SpriteEffects::None);
        // Snapshot the swapchain result before target downloads can advance a backend's
        // readback/presentation machinery. All producer/consumer work is already queued, so this
        // remains a single ordering oracle rather than an intermediate synchronization point.
        const Readback backbuffer = ReadBackbuffer(device);
        const Readback destination0 = ReadTarget(generation0Destination, outputW, outputH);
        const Readback destination1 = ReadTarget(generation1Destination, outputW, outputH);
        const Readback sourceReadback = ReadTarget(source, kRowW, kRowH);
        if (destination0.supported)
            CheckExact(destination0.pixels, expected0,
                       "transition A: first producer generation reaches target consumer");
        else
            Check(false, "transition A: first destination readback refused");
        if (destination1.supported)
        {
            const bool ok = CheckExact(destination1.pixels, expected1,
                                       "transition B: replacement producer generation reaches target consumer");
            if (!ok)
                PrintOrientationClassification(destination1.pixels, generation1, kRowW, kRowH,
                                               sampleRect, scale, scale, SpriteEffects::None,
                                               expected0);
            Check(MatchCount(destination1.pixels, expected0) != static_cast<int>(expected0.size()),
                  "transition B is not stale generation A content");
        }
        else
            Check(false, "transition B: second destination readback refused");
        if (sourceReadback.supported)
            CheckExact(sourceReadback.pixels, generation1,
                       "source readback after repeated target/backbuffer transitions is latest generation");
        else
            Check(false, "transition source readback refused");

        if (backbuffer.supported)
        {
            const auto first = Extract(backbuffer.pixels, kBackbufferW, Rectangle(2, 2, kRowW, 1));
            const auto second = Extract(backbuffer.pixels, kBackbufferW, Rectangle(2, 6, kRowW, 1));
            const auto control = Extract(backbuffer.pixels, kBackbufferW, Rectangle(12, 6, kRowW, 1));
            CheckExact(first, ExpectedRegion(generation0, kRowW, kRowH,
                                             Rectangle(0, 0, kRowW, 1), 1, 1,
                                             SpriteEffects::None),
                       "backbuffer retains generation A across target/backbuffer transitions");
            CheckExact(second, ExpectedRegion(generation1, kRowW, kRowH,
                                              Rectangle(0, kRowH - 1, kRowW, 1), 1, 1,
                                              SpriteEffects::None),
                       "backbuffer receives generation B final row after returning from a target");
            CheckExact(second, control,
                       "backbuffer generation B RenderTarget2D and Texture2D controls agree");
        }
        else
        {
            Check(!kBackbufferReadbackRequired && !backbuffer.wrongException,
                  std::string("transition backbuffer readback boundary on ") + kBackendName);
        }

        bool disposeClean = true;
        try
        {
            generation0Destination.Dispose();
            generation1Destination.Dispose();
            source.Dispose();
            texture0.Dispose();
            texture1.Dispose();
            source.Dispose();
        }
        catch (...)
        {
            disposeClean = false;
        }
        Check(disposeClean, "explicit source/destination disposal and repeated Dispose are clean");
    }

    void RunHeadlessBoundary(GraphicsDevice& device)
    {
        Texture2D texture(device, kRowW, kRowH, false, SurfaceFormat::Color);
        texture.SetData(rows_.data(), static_cast<int>(rows_.size()));
        RenderTarget2D target(device, kRowW, kRowH, false, SurfaceFormat::Color, DepthFormat::None,
                              0, RenderTargetUsage::DiscardContents);
        RenderFull(device, texture, target, kRowW, kRowH);
        const Readback read = ReadTarget(target, kRowW, kRowH);
        Check(!read.supported && !read.wrongException,
              "Headless rejects nonexistent render-target pixels with NotSupportedException");
        bool poisonIntact = true;
        for (const Color& pixel : read.pixels)
            poisonIntact = poisonIntact && Same(pixel, Color(0xCD, 0xCD, 0xCD, 0xCD));
        Check(poisonIntact, "Headless refusal leaves the caller's destination byte-exact");
        bool disposeClean = true;
        try
        {
            target.Dispose();
            texture.Dispose();
            target.Dispose();
        }
        catch (...)
        {
            disposeClean = false;
        }
        Check(disposeClean, "Headless resource disposal and teardown preparation are clean");
    }

    void RunAll(GraphicsDevice& device)
    {
        std::printf("[INFO] REMED-GFX-153 source-rectangle orientation on %s\n", kBackendName);
        std::fflush(stdout);

        if (!kRasterizes)
        {
            RunHeadlessBoundary(device);
            return;
        }

        RenderFull(device, *rowsTexture_, *rowsTarget_, kRowW, kRowH);
        RenderFull(device, *cornersTexture_, *cornersTarget_, kCornerW, kCornerH);

        const Readback rowsReadback = ReadTarget(*rowsTarget_, kRowW, kRowH);
        const Readback cornersReadback = ReadTarget(*cornersTarget_, kCornerW, kCornerH);
        Check(rowsReadback.supported, "row-marker RenderTarget2D readback is supported");
        if (rowsReadback.supported)
        {
            CheckExact(rowsReadback.pixels, rows_,
                       "row-marker producer and source readback are top-left and byte-exact");
            Check(Same(rowsReadback.pixels.front(), rows_.front()) &&
                  Same(rowsReadback.pixels[static_cast<std::size_t>(kRowH - 1) * kRowW],
                       rows_[static_cast<std::size_t>(kRowH - 1) * kRowW]),
                  "row-marker readback keeps first and final logical rows distinct");
        }
        Check(cornersReadback.supported, "four-corner RenderTarget2D readback is supported");
        if (cornersReadback.supported)
            CheckExact(cornersReadback.pixels, corners_,
                       "asymmetric four-corner producer and readback are byte-exact");

        const std::vector<Case> targetCases = {
            { "T1 implicit full row-marker source", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(0, 0, kRowW, kRowH), 1, 1, SpriteEffects::None, true },
            { "T2 explicit full four-corner source scaled", cornersTexture_.get(), cornersTarget_.get(),
              &corners_, kCornerW, kCornerH, Rectangle(0, 0, kCornerW, kCornerH), 2, 2,
              SpriteEffects::None, false },
            { "T3 top one-row rectangle", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(0, 0, kRowW, 1), 2, 3, SpriteEffects::None, false },
            { "T4 middle one-row rectangle", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(1, 3, 6, 1), 3, 2, SpriteEffects::None, false },
            { "T5 final-row rectangle", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(0, kRowH - 1, kRowW, 1), 2, 2,
              SpriteEffects::None, false },
            { "T6 partial top rows", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(1, 0, 5, 2), 1, 2, SpriteEffects::None, false },
            { "T7 partial middle rows scaled", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(2, 2, 4, 3), 2, 2, SpriteEffects::None, false },
            { "T8 partial bottom rows", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(1, kRowH - 2, 6, 2), 2, 1,
              SpriteEffects::None, false },
            { "T9 partial horizontal flip", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(1, 1, 6, 3), 2, 2,
              SpriteEffects::FlipHorizontally, false },
            { "T10 partial vertical flip", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(1, 1, 6, 3), 2, 2,
              SpriteEffects::FlipVertically, false },
            { "T11 partial horizontal+vertical flips", rowsTexture_.get(), rowsTarget_.get(), &rows_,
              kRowW, kRowH, Rectangle(1, 0, 6, 5), 1, 2,
              SpriteEffects::FlipHorizontally | SpriteEffects::FlipVertically, false },
            { "T12 four-corner upper-left partial rectangle", cornersTexture_.get(),
              cornersTarget_.get(), &corners_, kCornerW, kCornerH, Rectangle(0, 0, 3, 3),
              2, 2, SpriteEffects::None, false },
            { "T13 four-corner lower-right final-row rectangle", cornersTexture_.get(),
              cornersTarget_.get(), &corners_, kCornerW, kCornerH,
              Rectangle(kCornerW - 3, kCornerH - 1, 3, 1), 3, 3,
              SpriteEffects::FlipHorizontally, false },
        };
        for (const Case& testCase : targetCases) RunTargetCase(device, testCase);

        const std::vector<Case> backbufferCases = {
            targetCases[0], targetCases[3], targetCases[4], targetCases[9], targetCases[12]
        };
        for (const Case& testCase : backbufferCases) RunBackbufferCase(device, testCase);

        RunRotationCase(device);
        RunTransitionAndStaleCase(device);

        bool persistentDisposeClean = true;
        try
        {
            rowsTarget_->Dispose();
            cornersTarget_->Dispose();
            rowsTexture_->Dispose();
            cornersTexture_->Dispose();
            spriteBatch_->Dispose();
            rowsTarget_->Dispose();
        }
        catch (...)
        {
            persistentDisposeClean = false;
        }
        Check(persistentDisposeClean,
              "persistent texture/target/SpriteBatch disposal is clean before Game teardown");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        rows_ = MakeRows(0);
        corners_ = MakeCorners();
        rowsTexture_ = std::make_unique<Texture2D>(device, kRowW, kRowH, false, SurfaceFormat::Color);
        cornersTexture_ =
            std::make_unique<Texture2D>(device, kCornerW, kCornerH, false, SurfaceFormat::Color);
        rowsTexture_->SetData(rows_.data(), static_cast<int>(rows_.size()));
        cornersTexture_->SetData(corners_.data(), static_cast<int>(corners_.size()));
        rowsTarget_ = std::make_unique<RenderTarget2D>(
            device, kRowW, kRowH, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        cornersTarget_ = std::make_unique<RenderTarget2D>(
            device, kCornerW, kCornerH, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        try
        {
            RunAll(getGraphicsDeviceProperty());
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("uncaught fixture exception: ") + e.what());
        }
        catch (...)
        {
            Check(false, "uncaught non-std fixture exception");
        }
        std::printf("[INFO] %s: %d/%d checks passed\n", kBackendName, passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SourceRectangleOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBackbufferW);
        gdm_->setPreferredBackBufferHeightProperty(kBackbufferH);
    }

    int Result() const { return result_; }
};

int main()
{
    SourceRectangleOrientationTest test;
    test.Run();
    return test.Result();
}
