// SPDX-License-Identifier: MS-PL
// D2D-1: public-API pixel baseline for the Direct2D 2D-only renderer.
//
// This is deliberately a compact, real presentation test rather than a Direct2D implementation
// test. It validates four independent 2D contracts through GraphicsDevice:
//   1. Clear + exact top-left backbuffer readback,
//   2. Texture2D source selection and SpriteEffects::FlipHorizontally,
//   3. opaque SpriteBatch tinting, and
//   4. active RenderTarget2D readback, direct target readback, and RenderTarget2D -> Texture2D
//      sampling after the target is unbound, including the GPU-only decorated RT source path.
//
// All pixels use 0/255 or opaque values, avoiding gamma and premultiplied-alpha ambiguity. The
// target is NativeBackBuffer so GetBackBufferData probes physical and logical pixels 1:1.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
// D2D-40: CompositePorterDuffReference is the independent CPU oracle the composite-mode
// matrix below compares Direct2D output against; Direct2DBlendMode names the modes.
// NOGDI keeps <windows.h> (pulled in by d2d1_1.h) from declaring the global ::Rectangle
// GDI function, which would otherwise make every unqualified Rectangle in this file
// ambiguous against Microsoft::Xna::Framework::Rectangle. Direct2D itself never uses GDI.
#ifndef NOGDI
#define NOGDI
#endif
#include "CNA/Internal/Renderers/Direct2D/Direct2DRenderer.hpp"
#include "System/NotSupportedException.hpp"
#include "direct2d_test_support.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Direct2D::CompositePorterDuffReference;
using CNA::Internal::Renderers::Direct2D::Direct2DBlendMode;

// D2D-128: the tolerance comparison and the read-one-pixel-and-report step are shared by every
// Direct2D example test; they live in one header so two tests cannot drift apart about what a
// tolerance means.
using CnaDirect2DTestSupport::Matches;
using CnaDirect2DTestSupport::PixelChecker;

class Direct2D2DParityTest final : public Game
{
public:
    Direct2D2DParityTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(48);
        manager_->setPreferredBackBufferHeightProperty(32);
        manager_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{255, 255, 255, 255}));
        twoTexels_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 2, 1, std::vector<uint8_t>{
                255, 0, 0, 255, // left: red
                0, 255, 0, 255, // right: green
            }));
        twoRows_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 2, std::vector<uint8_t>{
                255, 0, 0, 255, // top: red
                0, 255, 0, 255, // bottom: green
            }));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        bool passed = true;
        // WineD3D also does not register Direct2D's built-in ColorMatrix effect.  The native
        // Windows test retains this full GPU-only RT decoration matrix.
        const bool verifyRenderTargetDecoration =
            std::getenv("CNA_DIRECT2D_SKIP_RENDER_TARGET_DECORATION") == nullptr;
        // WineD3D currently ignores Direct2D's non-source-over DrawImage composite modes,
        // including BOUNDED_SOURCE_COPY and Porter-Duff variants. Native Windows retains these
        // exact blend probes.
        const bool verifyAdvancedBlend =
            std::getenv("CNA_DIRECT2D_SKIP_ADVANCED_BLEND") == nullptr;

        PixelChecker checker(device, passed);
        const auto check = [&](const char* label, int x, int y, const Color& expected) {
            checker.Check(label, x, y, expected);
        };

        // D2D-89: the complete full/partial SetData matrix for every authored Texture2D mip and
        // for the render target. Everything here is a byte-exact transfer contract -- no
        // filtering, no blending -- so it holds identically on every runtime.
        {
            const auto transferCheck = [&](const char* label, bool condition) {
                std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
                passed = passed && condition;
            };
            const auto rejects = [&](const char* label, auto&& action) {
                bool threw = false;
                try
                {
                    action();
                }
                catch (const std::exception&)
                {
                    threw = true;
                }
                transferCheck(label, threw);
            };

            // 5x3 with one authored lower level (2x1); the odd width and the non-square shape make
            // a row-pitch or transposition mistake visible instead of self-cancelling.
            Texture2D transferTexture(device, 5, 3, true, SurfaceFormat::Color);
            std::vector<Color> levelZero(15, Color::Transparent);
            for (std::size_t index = 0; index < levelZero.size(); ++index)
            {
                const int value = static_cast<int>(index);
                levelZero[index] = Color(10 + value * 3, 200 - value * 5, 40 + value * 7,
                                         255 - value * 2);
            }
            transferTexture.SetData(levelZero.data(), static_cast<int>(levelZero.size()));
            std::vector<Color> readBack(15, Color::Transparent);
            transferTexture.GetData(readBack.data(), static_cast<int>(readBack.size()));
            transferCheck("SetData full level 0 round trip", readBack == levelZero);

            // Interior rectangle at a nonzero offset: every texel outside it must survive.
            const Rectangle interior(1, 1, 3, 1);
            const std::array<Color, 3> interiorPixels{
                Color(1, 2, 3, 4), Color(5, 6, 7, 8), Color(9, 10, 11, 12)};
            transferTexture.SetData(0, &interior, interiorPixels.data(), 0,
                                    static_cast<int>(interiorPixels.size()));
            std::vector<Color> afterInterior(15, Color::Transparent);
            transferTexture.GetData(afterInterior.data(), static_cast<int>(afterInterior.size()));
            bool interiorExact = true;
            for (std::size_t index = 0; index < afterInterior.size(); ++index)
            {
                const int x = static_cast<int>(index) % 5;
                const int y = static_cast<int>(index) / 5;
                const bool inside = y == 1 && x >= 1 && x <= 3;
                const Color expectedPixel =
                    inside ? interiorPixels[static_cast<std::size_t>(x - 1)] : levelZero[index];
                interiorExact = interiorExact && afterInterior[index] == expectedPixel;
            }
            transferCheck("SetData offset rectangle preserves every other texel", interiorExact);

            // Last pixel of the level: an off-by-one in the row offset lands outside it.
            const Rectangle lastPixel(4, 2, 1, 1);
            // Opaque on purpose: this texel is also the one sampled through SpriteBatch below,
            // where an AlphaBlend result would otherwise mix with the cleared background.
            const std::array<Color, 1> lastPixelData{Color(123, 45, 67, 255)};
            transferTexture.SetData(0, &lastPixel, lastPixelData.data(), 0, 1);
            std::array<Color, 1> lastPixelReadback{Color::Transparent};
            transferTexture.GetData(0, &lastPixel, lastPixelReadback.data(), 0, 1);
            transferCheck("SetData/GetData address the last texel",
                          lastPixelReadback[0] == lastPixelData[0]);

            // startIndex selects a window inside a larger buffer rather than its beginning.
            const Rectangle windowRect(0, 0, 2, 1);
            const std::array<Color, 4> windowSource{
                Color(200, 0, 0, 255), Color(0, 200, 0, 255),
                Color(0, 0, 200, 255), Color(200, 200, 0, 255)};
            transferTexture.SetData(0, &windowRect, windowSource.data(), 2, 2);
            std::array<Color, 2> windowReadback{Color::Transparent, Color::Transparent};
            transferTexture.GetData(0, &windowRect, windowReadback.data(), 0, 2);
            transferCheck("SetData honours startIndex",
                          windowReadback[0] == windowSource[2] && windowReadback[1] == windowSource[3]);

            // The authored lower level is 2x1 and is written and read independently of level 0.
            const std::array<Color, 2> levelOne{Color(0, 0, 255, 255), Color(255, 255, 0, 255)};
            transferTexture.SetData(1, nullptr, levelOne.data(), 0,
                                    static_cast<int>(levelOne.size()));
            std::array<Color, 2> levelOneReadback{Color::Transparent, Color::Transparent};
            transferTexture.GetData(1, nullptr, levelOneReadback.data(), 0, 2);
            transferCheck("SetData/GetData round trip for the authored mip",
                          levelOneReadback == levelOne);
            const Rectangle levelOneTail(1, 0, 1, 1);
            const std::array<Color, 1> levelOneTailData{Color(7, 8, 9, 10)};
            transferTexture.SetData(1, &levelOneTail, levelOneTailData.data(), 0, 1);
            std::array<Color, 2> levelOneAfterPartial{Color::Transparent, Color::Transparent};
            transferTexture.GetData(1, nullptr, levelOneAfterPartial.data(), 0, 2);
            transferCheck("partial SetData on the authored mip keeps its other texel",
                          levelOneAfterPartial[0] == levelOne[0] &&
                              levelOneAfterPartial[1] == levelOneTailData[0]);

            // Writing a lower level must not disturb level zero.
            std::vector<Color> levelZeroAfterMipWrite(15, Color::Transparent);
            transferTexture.GetData(levelZeroAfterMipWrite.data(),
                                    static_cast<int>(levelZeroAfterMipWrite.size()));
            transferCheck("authored mip writes leave level 0 untouched",
                          levelZeroAfterMipWrite[0] == afterInterior[0] &&
                              levelZeroAfterMipWrite[14] == lastPixelData[0]);

            // Invalid ranges. Each rejection must also leave the texture untouched, which the
            // full-level comparison after them asserts once for all of them.
            const Rectangle outsideRight(3, 0, 3, 1);
            const Rectangle negativeOrigin(-1, 0, 2, 1);
            const Rectangle emptyRegion(1, 1, 0, 0);
            rejects("SetData rejects a rectangle leaving the level", [&] {
                transferTexture.SetData(0, &outsideRight, interiorPixels.data(), 0, 3);
            });
            rejects("SetData rejects a negative origin", [&] {
                transferTexture.SetData(0, &negativeOrigin, interiorPixels.data(), 0, 2);
            });
            // An empty region is a no-op rather than an error: the shared transfer layer bounds-
            // checks the rectangle and the element count, and a zero-area region violates neither.
            // Pin that it really changes nothing instead of pretending it throws.
            std::vector<Color> beforeEmptyRegion(15, Color::Transparent);
            transferTexture.GetData(beforeEmptyRegion.data(),
                                    static_cast<int>(beforeEmptyRegion.size()));
            transferTexture.SetData(0, &emptyRegion, interiorPixels.data(), 0, 1);
            std::vector<Color> afterEmptyRegion(15, Color::Transparent);
            transferTexture.GetData(afterEmptyRegion.data(),
                                    static_cast<int>(afterEmptyRegion.size()));
            transferCheck("an empty region writes nothing", beforeEmptyRegion == afterEmptyRegion);
            rejects("SetData rejects an element count smaller than the region", [&] {
                transferTexture.SetData(0, &interior, interiorPixels.data(), 0, 2);
            });
            rejects("SetData rejects a level that does not exist", [&] {
                transferTexture.SetData(7, nullptr, levelOne.data(), 0, 2);
            });
            rejects("GetData rejects a rectangle leaving the level", [&] {
                std::vector<Color> destination(3, Color::Transparent);
                transferTexture.GetData(0, &outsideRight, destination.data(), 0, 3);
            });
            rejects("GetData rejects a destination smaller than the region", [&] {
                std::vector<Color> destination(1, Color::Transparent);
                transferTexture.GetData(0, &interior, destination.data(), 0, 1);
            });

            // Every rejection above must have left the texture exactly as it was.
            std::vector<Color> afterRejections(15, Color::Transparent);
            transferTexture.GetData(afterRejections.data(), static_cast<int>(afterRejections.size()));
            transferCheck("rejected transfers change nothing",
                          afterRejections == levelZeroAfterMipWrite);

            // The GPU side must agree with the CPU round trip: point-sample the updated texture
            // 1:1 and compare the exact texels.
            device.Clear(Color::Black);
            SamplerState transferSampler = SamplerState::PointClamp;
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &transferSampler,
                            nullptr, nullptr);
            sprites_->Draw(transferTexture, Rectangle(0, 0, 5, 3), Rectangle(4, 2, 1, 1),
                           Color::White);
            sprites_->End();
            check("SetData reaches the GPU (last texel)", 2, 1, lastPixelData[0]);

            // RenderTarget2D: level zero only, full and partial, with the same rejection contract.
            RenderTarget2D transferTarget(device, 4, 2, false, SurfaceFormat::Color,
                                          DepthFormat::None);
            std::vector<Color> targetPixels(8, Color::Transparent);
            for (std::size_t index = 0; index < targetPixels.size(); ++index)
            {
                const int value = static_cast<int>(index);
                targetPixels[index] = Color(20 * value, 255 - 20 * value, 30 + value, 255);
            }
            transferTarget.SetData(targetPixels.data(), static_cast<int>(targetPixels.size()));
            std::vector<Color> targetReadback(8, Color::Transparent);
            transferTarget.GetData(targetReadback.data(), static_cast<int>(targetReadback.size()));
            transferCheck("render-target full SetData round trip", targetReadback == targetPixels);

            const Rectangle targetRegion(2, 1, 2, 1);
            const std::array<Color, 2> targetRegionPixels{
                Color(11, 22, 33, 255), Color(44, 55, 66, 255)};
            transferTarget.SetData(0, &targetRegion, targetRegionPixels.data(), 0, 2);
            std::vector<Color> targetAfterPartial(8, Color::Transparent);
            transferTarget.GetData(targetAfterPartial.data(),
                                   static_cast<int>(targetAfterPartial.size()));
            bool targetPartialExact = true;
            for (std::size_t index = 0; index < targetAfterPartial.size(); ++index)
            {
                const int x = static_cast<int>(index) % 4;
                const int y = static_cast<int>(index) / 4;
                const bool inside = y == 1 && x >= 2;
                const Color expectedPixel =
                    inside ? targetRegionPixels[static_cast<std::size_t>(x - 2)] : targetPixels[index];
                targetPartialExact = targetPartialExact && targetAfterPartial[index] == expectedPixel;
            }
            transferCheck("render-target partial SetData preserves every other texel",
                          targetPartialExact);
            rejects("render-target rejects level 1 SetData", [&] {
                transferTarget.SetData(1, nullptr, targetRegionPixels.data(), 0, 1);
            });
            rejects("render-target rejects level 1 GetData", [&] {
                std::vector<Color> destination(1, Color::Transparent);
                transferTarget.GetData(1, nullptr, destination.data(), 0, 1);
            });
            rejects("render-target rejects a rectangle leaving level 0", [&] {
                const Rectangle outside(3, 1, 2, 1);
                transferTarget.SetData(0, &outside, targetRegionPixels.data(), 0, 2);
            });
            std::vector<Color> targetAfterRejections(8, Color::Transparent);
            transferTarget.GetData(targetAfterRejections.data(),
                                   static_cast<int>(targetAfterRejections.size()));
            transferCheck("rejected render-target transfers change nothing",
                          targetAfterRejections == targetAfterPartial);
        }

        // 1. Clear and native backbuffer readback.
        device.Clear(Color(12, 34, 56, 255));
        check("Clear + GetBackBufferData", 1, 1, Color(12, 34, 56, 255));

        // 2/3. Point-sampled source selection, horizontal flip, and an opaque tint.
        device.Clear(Color::Black);
        SamplerState point = SamplerState::PointClamp;
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(*twoTexels_, Rectangle(0, 0, 16, 8), Rectangle(0, 0, 2, 1), Color::White);
        sprites_->Draw(*twoTexels_, Rectangle(16, 0, 16, 8), Rectangle(0, 0, 2, 1), Color::White,
                       0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sprites_->Draw(*white_, Rectangle(32, 0, 16, 8), Rectangle(0, 0, 1, 1),
                       Color(128, 64, 32, 255));
        sprites_->End();
        check("Texture2D point source left", 3, 3, Color(255, 0, 0, 255));
        check("Texture2D point source right", 12, 3, Color(0, 255, 0, 255));
        check("SpriteEffects::FlipHorizontally left", 19, 3, Color(0, 255, 0, 255));
        check("SpriteEffects::FlipHorizontally right", 28, 3, Color(255, 0, 0, 255));
        check("SpriteBatch opaque tint", 35, 3, Color(128, 64, 32, 255));

        // Odd-width, asymmetric RGBA data exercises the renderer's RGBA<->BGRA mapping without a
        // symmetric-channel false positive. Repeated partial writes must preserve neighboring
        // pixels, and a rejected short write must be transactional.
        Texture2D updateTexture(device, 3, 2, false, SurfaceFormat::Color);
        const std::array<Color, 6> initialUpdatePixels{
            Color(1, 17, 231, 19), Color(2, 34, 210, 255), Color(3, 51, 189, 85),
            Color(4, 68, 168, 102), Color(5, 85, 147, 119), Color(6, 102, 126, 136)};
        updateTexture.SetData(initialUpdatePixels.data(), static_cast<int>(initialUpdatePixels.size()));
        const Rectangle updateColumn(1, 0, 1, 2);
        const std::array<Color, 2> replacementColumn{
            Color(9, 99, 199, 255), Color(222, 77, 13, 17)};
        updateTexture.SetData(0, &updateColumn, replacementColumn.data(), 0,
                              static_cast<int>(replacementColumn.size()));
        bool shortUpdateRejected = false;
        try
        {
            const std::array<Color, 1> shortUpdate{Color::White};
            updateTexture.SetData(shortUpdate.data(), static_cast<int>(shortUpdate.size()));
        }
        catch (const std::exception&)
        {
            shortUpdateRejected = true;
        }
        std::array<Color, 6> updatedReadback{
            Color::Transparent, Color::Transparent, Color::Transparent,
            Color::Transparent, Color::Transparent, Color::Transparent};
        updateTexture.GetData(updatedReadback.data(), static_cast<int>(updatedReadback.size()));
        const bool repeatedUpdateExact = updatedReadback[0] == initialUpdatePixels[0] &&
            updatedReadback[1] == replacementColumn[0] &&
            updatedReadback[2] == initialUpdatePixels[2] &&
            updatedReadback[3] == initialUpdatePixels[3] &&
            updatedReadback[4] == replacementColumn[1] &&
            updatedReadback[5] == initialUpdatePixels[5];
        std::printf("[%s] Direct2D odd-width repeated Texture2D updates preserve exact RGBA and reject short writes transactionally\n",
                    shortUpdateRejected && repeatedUpdateExact ? "PASS" : "FAIL");
        passed = passed && shortUpdateRejected && repeatedUpdateExact;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(updateTexture, Rectangle(0, 0, 3, 2), Rectangle(0, 0, 3, 2), Color::White);
        sprites_->End();
        check("Direct2D odd-width updated Texture2D reaches native draw", 1, 0,
              replacementColumn[0]);

        // D2D-88: SetData while an Immediate batch has outstanding native work commits the old
        // bitmap before replacement. The first sprite is therefore a stable old-data snapshot,
        // while the second sprite observes the successful update in the same Begin/End scope.
        Texture2D updateDuringDraw = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{211, 17, 39, 255});
        const std::array<Color, 1> updatedDuringDraw{Color(23, 197, 61, 255)};
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(updateDuringDraw, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1),
                       Color::White);
        updateDuringDraw.SetData(updatedDuringDraw.data(), 1);
        sprites_->Draw(updateDuringDraw, Rectangle(4, 0, 4, 4), Rectangle(0, 0, 1, 1),
                       Color::White);
        sprites_->End();
        check("Direct2D Texture2D SetData preserves an outstanding Immediate draw", 1, 1,
              Color(211, 17, 39, 255));
        check("Direct2D Texture2D SetData updates the next Immediate draw", 5, 1,
              updatedDuringDraw[0]);

        // D2D-82: independent geometric oracle for a positively-offset atlas crop combined with
        // a nonzero source origin, nonuniform destination scale, 90-degree rotation, batch
        // translation, and optional horizontal flip. Each source texel occupies a 3x2 interior
        // block after the transform, so the probes avoid rasterization edges entirely.
        const auto transformAtlas = Texture2D::CreateFromPixels(
            device, 4, 3, std::vector<std::uint8_t>{
                0, 0, 0, 255,  255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 0, 255,
                0, 0, 0, 255,  0, 0, 255, 255,  255, 255, 0, 255,  0, 0, 0, 255,
                0, 0, 0, 255,  0, 0, 0, 255,  0, 0, 0, 255,  0, 0, 0, 255,
            });
        const Rectangle transformCrop(1, 0, 2, 2);
        const Rectangle transformDestination(10, 10, 4, 6);
        const Vector2 transformOrigin(1.0f, 1.0f);
        const float quarterTurn = static_cast<float>(std::acos(0.0));
        const Matrix batchTranslation = Matrix::CreateTranslation(2.0f, 1.0f, 0.0f);
        const auto drawTransformOracle = [&](SpriteEffects effects, const char* label,
                                             const Color& top, const Color& right,
                                             const Color& left, const Color& bottom) {
            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr,
                            nullptr, batchTranslation);
            sprites_->Draw(transformAtlas, transformDestination, transformCrop, Color::White,
                           quarterTurn, transformOrigin, effects, 0.0f);
            sprites_->End();
            check((std::string(label) + " top").c_str(), 13, 10, top);
            check((std::string(label) + " right").c_str(), 13, 12, right);
            check((std::string(label) + " left").c_str(), 10, 10, left);
            check((std::string(label) + " bottom").c_str(), 10, 12, bottom);
        };
        drawTransformOracle(SpriteEffects::None, "D2D-82 crop/origin/rotation/scale",
                            Color::Red, Color(0, 255, 0, 255), Color::Blue, Color::Yellow);
        drawTransformOracle(SpriteEffects::FlipHorizontally,
                            "D2D-82 crop/origin/rotation/scale/flip",
                            Color(0, 255, 0, 255), Color::Red, Color::Yellow, Color::Blue);

        // 4. Render into an 8x8 target, unbind, then sample it as a Texture2D. This exercises
        // the sibling ITextureRenderer path instead of assuming every SpriteBatch source is a
        // plain Direct2DTextureRenderer.
        RenderTarget2D target(device, 8, 8);
        device.SetRenderTarget(&target);
        device.Clear(Color(0, 0, 255, 255));
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(*white_, Rectangle(0, 0, 4, 3), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
        sprites_->End();

        const Rectangle targetRegion(1, 1, 1, 1);
        // While a 2D target is active, the public readback must observe that target rather
        // than the stale swap chain. This is a second route through the same Direct2D CPU
        // bitmap path and specifically covers D2D-3's active-target semantics.
        Color activeTargetPixel(0, 0, 0, 0);
        device.GetBackBufferData(&targetRegion, &activeTargetPixel, 0, 1);
        const bool activeReadbackMatches = Matches(activeTargetPixel, Color(255, 0, 0, 255));
        std::printf("[%s] active RenderTarget2D GetBackBufferData: got=(%d,%d,%d,%d), expected red\n",
                    activeReadbackMatches ? "PASS" : "FAIL",
                    activeTargetPixel.getRProperty(), activeTargetPixel.getGProperty(),
                    activeTargetPixel.getBProperty(), activeTargetPixel.getAProperty());
        passed = passed && activeReadbackMatches;

        device.SetRenderTarget(nullptr);

        // RenderTarget2D has no CPU shadow. This must exercise Direct2DRenderTargetRenderer's
        // CopyFromRenderTarget/CopyFromBitmap -> CPU_READ bitmap -> Map path rather than the
        // ordinary Texture2D shadow-data path.
        Color targetPixel(0, 0, 0, 0);
        target.GetData(0, &targetRegion, &targetPixel, 0, 1);
        const bool targetReadbackMatches = Matches(targetPixel, Color(255, 0, 0, 255));
        std::printf("[%s] RenderTarget2D direct GetData: got=(%d,%d,%d,%d), expected red\n",
                    targetReadbackMatches ? "PASS" : "FAIL",
                    targetPixel.getRProperty(), targetPixel.getGProperty(),
                    targetPixel.getBProperty(), targetPixel.getAProperty());
        passed = passed && targetReadbackMatches;
        std::vector<Color> fullTargetPixels(8 * 8, Color::Transparent);
        target.GetData(fullTargetPixels.data(), 0, static_cast<int>(fullTargetPixels.size()));
        const bool fullTargetReadbackMatches =
            Matches(fullTargetPixels[1 + 1 * 8], Color(255, 0, 0, 255)) &&
            Matches(fullTargetPixels[7 + 7 * 8], Color(0, 0, 255, 255));
        std::printf("[%s] RenderTarget2D full GetData retains red marker and blue background\n",
                    fullTargetReadbackMatches ? "PASS" : "FAIL");
        passed = passed && fullTargetReadbackMatches;

        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(target, Rectangle(0, 12, 8, 8), Rectangle(0, 0, 8, 8), Color::White);
        sprites_->End();
        check("RenderTarget2D sampled marker", 1, 13, Color(255, 0, 0, 255));
        check("RenderTarget2D sampled background", 6, 18, Color(0, 0, 255, 255));

        if (verifyRenderTargetDecoration)
        {
            // 5. Direct2D's image-brush/effect route decorates a rendered target without a CPU
            // shadow or a render-target readback. Use a small red/green source so tint, flip,
            // Wrap, and Mirror all have pixel-exact, independently observable results.
            RenderTarget2D patternTarget(device, 2, 1);
            device.SetRenderTarget(&patternTarget);
            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(*twoTexels_, Rectangle(0, 0, 2, 1), Rectangle(0, 0, 2, 1), Color::White);
            sprites_->End();
            device.SetRenderTarget(nullptr);

            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(patternTarget, Rectangle(0, 0, 16, 4), Rectangle(0, 0, 2, 1),
                           Color(128, 255, 255, 255));
            sprites_->Draw(patternTarget, Rectangle(16, 0, 16, 4), Rectangle(0, 0, 2, 1), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
            sprites_->End();
            check("RenderTarget2D GPU tint", 3, 1, Color(128, 0, 0, 255));
            check("RenderTarget2D GPU tint second texel", 12, 1, Color(0, 255, 0, 255));
            check("RenderTarget2D GPU flip left", 19, 1, Color(0, 255, 0, 255));
            check("RenderTarget2D GPU flip right", 28, 1, Color(255, 0, 0, 255));

            SamplerState pointWrap;
            pointWrap.setFilterProperty(TextureFilter::Point);
            pointWrap.setAddressUProperty(TextureAddressMode::Wrap);
            pointWrap.setAddressVProperty(TextureAddressMode::Clamp);

            SamplerState pointMirror;
            pointMirror.setFilterProperty(TextureFilter::Point);
            pointMirror.setAddressUProperty(TextureAddressMode::Mirror);
            pointMirror.setAddressVProperty(TextureAddressMode::Clamp);

            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr);
            sprites_->Draw(patternTarget, Rectangle(0, 8, 16, 4), Rectangle(-1, 0, 4, 1), Color::White);
            sprites_->End();
            check("RenderTarget2D GPU Wrap first", 1, 9, Color(0, 255, 0, 255));
            check("RenderTarget2D GPU Wrap second", 5, 9, Color(255, 0, 0, 255));
            check("RenderTarget2D GPU Wrap third", 9, 9, Color(0, 255, 0, 255));
            check("RenderTarget2D GPU Wrap fourth", 13, 9, Color(255, 0, 0, 255));

            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointMirror, nullptr, nullptr);
            sprites_->Draw(patternTarget, Rectangle(0, 14, 16, 4), Rectangle(-1, 0, 4, 1), Color::White);
            sprites_->End();
            check("RenderTarget2D GPU Mirror first", 1, 15, Color(255, 0, 0, 255));
            check("RenderTarget2D GPU Mirror second", 5, 15, Color(255, 0, 0, 255));
            check("RenderTarget2D GPU Mirror third", 9, 15, Color(0, 255, 0, 255));
            check("RenderTarget2D GPU Mirror fourth", 13, 15, Color(0, 255, 0, 255));
        }
        else
        {
            std::printf("[SKIP] RenderTarget2D GPU decoration (Wine Direct2D lacks ColorMatrix)\n");
        }

        // 6. EasyGL/FNA keeps UVs outside [0,1] and delegates the result to SamplerState. The
        // Direct2D ordinary-texture CPU route and render-target image-brush route must therefore
        // agree for Clamp-to-edge, Wrap, and Mirror; this also makes a source rectangle extending
        // past an image a supported 2D operation instead of a Direct2D-only error.
        RenderTarget2D samplerTarget(device, 2, 1);
        device.SetRenderTarget(&samplerTarget);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(*twoTexels_, Rectangle(0, 0, 2, 1), Rectangle(0, 0, 2, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);

        SamplerState pointWrap;
        pointWrap.setFilterProperty(TextureFilter::Point);
        pointWrap.setAddressUProperty(TextureAddressMode::Wrap);
        pointWrap.setAddressVProperty(TextureAddressMode::Clamp);
        SamplerState pointMirror;
        pointMirror.setFilterProperty(TextureFilter::Point);
        pointMirror.setAddressUProperty(TextureAddressMode::Mirror);
        pointMirror.setAddressVProperty(TextureAddressMode::Clamp);
        SamplerState pointWrapRows;
        pointWrapRows.setFilterProperty(TextureFilter::Point);
        pointWrapRows.setAddressUProperty(TextureAddressMode::Clamp);
        pointWrapRows.setAddressVProperty(TextureAddressMode::Wrap);
        SamplerState pointMirrorRows;
        pointMirrorRows.setFilterProperty(TextureFilter::Point);
        pointMirrorRows.setAddressUProperty(TextureAddressMode::Clamp);
        pointMirrorRows.setAddressVProperty(TextureAddressMode::Mirror);
        SamplerState linearClamp;
        linearClamp.setFilterProperty(TextureFilter::Linear);
        linearClamp.setAddressUProperty(TextureAddressMode::Clamp);
        linearClamp.setAddressVProperty(TextureAddressMode::Clamp);
        SamplerState linearWrap;
        linearWrap.setFilterProperty(TextureFilter::Linear);
        linearWrap.setAddressUProperty(TextureAddressMode::Wrap);
        linearWrap.setAddressVProperty(TextureAddressMode::Clamp);
        SamplerState linearMirror;
        linearMirror.setFilterProperty(TextureFilter::Linear);
        linearMirror.setAddressUProperty(TextureAddressMode::Mirror);
        linearMirror.setAddressVProperty(TextureAddressMode::Clamp);
        SamplerState linearWrapRows;
        linearWrapRows.setFilterProperty(TextureFilter::Linear);
        linearWrapRows.setAddressUProperty(TextureAddressMode::Clamp);
        linearWrapRows.setAddressVProperty(TextureAddressMode::Wrap);
        SamplerState linearMirrorRows;
        linearMirrorRows.setFilterProperty(TextureFilter::Linear);
        linearMirrorRows.setAddressUProperty(TextureAddressMode::Clamp);
        linearMirrorRows.setAddressVProperty(TextureAddressMode::Mirror);

        RenderTarget2D samplerRowsTarget(device, 1, 2);
        device.SetRenderTarget(&samplerRowsTarget);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sprites_->Draw(*twoRows_, Rectangle(0, 0, 1, 2), Rectangle(0, 0, 1, 2), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);

        const auto drawOutsideSource = [&](SamplerState& sampler, int y,
                                          SpriteEffects effects = SpriteEffects::None) {
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
            sprites_->Draw(*twoTexels_, Rectangle(0, y, 16, 2), Rectangle(-1, 0, 4, 1), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->Draw(samplerTarget, Rectangle(16, y, 16, 2), Rectangle(-1, 0, 4, 1), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->End();
        };
        const auto drawOutsideRows = [&](SamplerState& sampler, int y,
                                         SpriteEffects effects = SpriteEffects::None) {
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
            sprites_->Draw(*twoRows_, Rectangle(32, y, 4, 8), Rectangle(0, -1, 1, 4), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->Draw(samplerRowsTarget, Rectangle(40, y, 4, 8), Rectangle(0, -1, 1, 4), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->End();
        };
        const auto drawOutsideLinear = [&](SamplerState& sampler, int y,
                                           SpriteEffects effects = SpriteEffects::None) {
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
            sprites_->Draw(*twoTexels_, Rectangle(0, y, 16, 1), Rectangle(-1, 0, 4, 1), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->Draw(samplerTarget, Rectangle(16, y, 16, 1), Rectangle(-1, 0, 4, 1), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->End();
        };
        const auto drawOutsideLinearRows = [&](SamplerState& sampler, int y,
                                               SpriteEffects effects = SpriteEffects::None) {
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
            sprites_->Draw(*twoRows_, Rectangle(32, y, 1, 8), Rectangle(0, -1, 1, 4), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->Draw(samplerRowsTarget, Rectangle(40, y, 1, 8), Rectangle(0, -1, 1, 4), Color::White,
                           0.0f, Vector2::Zero, effects, 0.0f);
            sprites_->End();
        };
        device.Clear(Color::Black);
        drawOutsideSource(point, 16);
        drawOutsideSource(pointWrap, 19);
        drawOutsideSource(pointMirror, 22);
        drawOutsideRows(point, 0);
        drawOutsideRows(pointWrapRows, 8);
        drawOutsideRows(pointMirrorRows, 16);
        drawOutsideSource(point, 26, SpriteEffects::FlipHorizontally);
        drawOutsideRows(point, 24, SpriteEffects::FlipVertically);
        drawOutsideLinear(linearClamp, 29);
        drawOutsideLinear(linearWrap, 30);
        drawOutsideLinear(linearMirror, 31);
        const auto checkAddressingRow = [&](const char* mode, int y, const std::array<Color, 4>& expected) {
            for (int index = 0; index < 4; ++index)
            {
                const int x = 1 + index * 4;
                std::string ordinaryLabel = std::string("Texture2D ") + mode + " outside source " +
                                            std::to_string(index);
                std::string targetLabel = std::string("RenderTarget2D ") + mode + " outside source " +
                                          std::to_string(index);
                check(ordinaryLabel.c_str(), x, y, expected[static_cast<std::size_t>(index)]);
                check(targetLabel.c_str(), x + 16, y, expected[static_cast<std::size_t>(index)]);
            }
        };
        const auto checkAddressingColumn = [&](const char* mode, int y, const std::array<Color, 4>& expected) {
            for (int index = 0; index < 4; ++index)
            {
                const int sampleY = y + 1 + index * 2;
                std::string ordinaryLabel = std::string("Texture2D vertical ") + mode + " outside source " +
                                            std::to_string(index);
                std::string targetLabel = std::string("RenderTarget2D vertical ") + mode + " outside source " +
                                          std::to_string(index);
                check(ordinaryLabel.c_str(), 33, sampleY, expected[static_cast<std::size_t>(index)]);
                check(targetLabel.c_str(), 41, sampleY, expected[static_cast<std::size_t>(index)]);
            }
        };
        checkAddressingRow("Clamp", 16, {Color(255, 0, 0, 255), Color(255, 0, 0, 255),
                                         Color(0, 255, 0, 255), Color(0, 255, 0, 255)});
        checkAddressingRow("Wrap", 19, {Color(0, 255, 0, 255), Color(255, 0, 0, 255),
                                        Color(0, 255, 0, 255), Color(255, 0, 0, 255)});
        checkAddressingRow("Mirror", 22, {Color(255, 0, 0, 255), Color(255, 0, 0, 255),
                                          Color(0, 255, 0, 255), Color(0, 255, 0, 255)});
        checkAddressingColumn("Clamp", 0, {Color(255, 0, 0, 255), Color(255, 0, 0, 255),
                                            Color(0, 255, 0, 255), Color(0, 255, 0, 255)});
        checkAddressingColumn("Wrap", 8, {Color(0, 255, 0, 255), Color(255, 0, 0, 255),
                                           Color(0, 255, 0, 255), Color(255, 0, 0, 255)});
        checkAddressingColumn("Mirror", 16, {Color(255, 0, 0, 255), Color(255, 0, 0, 255),
                                              Color(0, 255, 0, 255), Color(0, 255, 0, 255)});
        checkAddressingRow("Clamp FlipH", 26, {Color(0, 255, 0, 255), Color(0, 255, 0, 255),
                                                Color(255, 0, 0, 255), Color(255, 0, 0, 255)});
        checkAddressingColumn("Clamp FlipV", 24, {Color(0, 255, 0, 255), Color(0, 255, 0, 255),
                                                   Color(255, 0, 0, 255), Color(255, 0, 0, 255)});
        const auto checkLinearParity = [&](const char* mode, int y, int x) {
            Color ordinary(0, 0, 0, 0);
            Color target(0, 0, 0, 0);
            const Rectangle ordinaryRegion(x, y, 1, 1);
            const Rectangle targetRegion(x + 16, y, 1, 1);
            device.GetBackBufferData(&ordinaryRegion, &ordinary, 0, 1);
            device.GetBackBufferData(&targetRegion, &target, 0, 1);
            const bool same = Matches(ordinary, target);
            std::printf("[%s] Linear %s Texture2D/RenderTarget2D parity x=%d: ordinary=(%d,%d,%d,%d), target=(%d,%d,%d,%d)\n",
                        same ? "PASS" : "FAIL", mode, x,
                        ordinary.getRProperty(), ordinary.getGProperty(), ordinary.getBProperty(), ordinary.getAProperty(),
                        target.getRProperty(), target.getGProperty(), target.getBProperty(), target.getAProperty());
            passed = passed && same;
        };
        for (int x : {1, 3, 5, 7, 9, 11, 13, 15})
        {
            checkLinearParity("Clamp", 29, x);
            checkLinearParity("Wrap", 30, x);
            checkLinearParity("Mirror", 31, x);
        }
        Color linearProbe(0, 0, 0, 0);
        const Rectangle linearProbeRegion(3, 30, 1, 1);
        device.GetBackBufferData(&linearProbeRegion, &linearProbe, 0, 1);
        const bool interpolationObserved = linearProbe.getRProperty() > 4 && linearProbe.getRProperty() < 251 &&
                                           linearProbe.getGProperty() > 4 && linearProbe.getGProperty() < 251;
        std::printf("[%s] Linear sampler produces an interpolated Wrap sample\n",
                    interpolationObserved ? "PASS" : "FAIL");
        passed = passed && interpolationObserved;
        // The same parity matrix through the V coordinate. Draw it after the point-source probes
        // above have been read, because it intentionally reuses their narrow right-side columns.
        drawOutsideLinearRows(linearClamp, 0);
        drawOutsideLinearRows(linearWrapRows, 8);
        drawOutsideLinearRows(linearMirrorRows, 16);
        const auto checkLinearVerticalParity = [&](const char* mode, int y, int sampleY) {
            Color ordinary(0, 0, 0, 0);
            Color target(0, 0, 0, 0);
            const Rectangle ordinaryRegion(32, y + sampleY, 1, 1);
            const Rectangle targetRegion(40, y + sampleY, 1, 1);
            device.GetBackBufferData(&ordinaryRegion, &ordinary, 0, 1);
            device.GetBackBufferData(&targetRegion, &target, 0, 1);
            const bool same = Matches(ordinary, target);
            std::printf("[%s] Linear vertical %s Texture2D/RenderTarget2D parity y=%d: ordinary=(%d,%d,%d,%d), target=(%d,%d,%d,%d)\n",
                        same ? "PASS" : "FAIL", mode, y + sampleY,
                        ordinary.getRProperty(), ordinary.getGProperty(), ordinary.getBProperty(), ordinary.getAProperty(),
                        target.getRProperty(), target.getGProperty(), target.getBProperty(), target.getAProperty());
            passed = passed && same;
        };
        for (int sampleY : {1, 3, 5, 7})
        {
            checkLinearVerticalParity("Clamp", 0, sampleY);
            checkLinearVerticalParity("Wrap", 8, sampleY);
            checkLinearVerticalParity("Mirror", 16, sampleY);
        }
        // Source-origin correction must happen before the brush reflection. Exercise that rule
        // with the interpolation path too, after all non-flipped samples have been observed.
        drawOutsideLinear(linearClamp, 0, SpriteEffects::FlipHorizontally);
        drawOutsideLinear(linearWrap, 1, SpriteEffects::FlipHorizontally);
        drawOutsideLinear(linearMirror, 2, SpriteEffects::FlipHorizontally);
        for (int x : {1, 3, 5, 7, 9, 11, 13, 15})
        {
            checkLinearParity("Clamp FlipH", 0, x);
            checkLinearParity("Wrap FlipH", 1, x);
            checkLinearParity("Mirror FlipH", 2, x);
        }
        drawOutsideLinearRows(linearClamp, 0, SpriteEffects::FlipVertically);
        drawOutsideLinearRows(linearWrapRows, 8, SpriteEffects::FlipVertically);
        drawOutsideLinearRows(linearMirrorRows, 16, SpriteEffects::FlipVertically);
        for (int sampleY : {1, 3, 5, 7})
        {
            checkLinearVerticalParity("Clamp FlipV", 0, sampleY);
            checkLinearVerticalParity("Wrap FlipV", 8, sampleY);
            checkLinearVerticalParity("Mirror FlipV", 16, sampleY);
        }

        // D2D-80: ID2D1ImageBrush's sourceRectangle bounds/clips which part of the underlying
        // image is visible, but does NOT by itself align that rectangle's top-left corner with
        // the brush's local origin -- brush-local (0,0) still maps to the FULL image's (0,0)
        // unless SetTransform compensates. DrawSprite's existing compensation
        // (`clippedLeft`/`clippedTop` in the ImageBrush branch) only ever shifts for a NEGATIVE
        // source origin (`std::max(0.0f, -X)` is exactly zero for any X >= 0), so a positively
        // offset, in-bounds crop from a multi-region atlas is the untested case this task names.
        // Forcing the ImageBrush code path under Wine (which lacks the GPU ColorMatrix/Premultiply
        // effects that would otherwise divert a tinted draw to the CPU fallback instead) requires
        // an untinted Color::White draw combined with a non-default SpriteEffects/address mode --
        // FlipHorizontally is used here purely as that trigger, not as the property under test (a
        // 1x1 crop has no visible flip of its own). The oracle is the atlas's own known per-pixel
        // content, not any Direct2D render-target branch of the same code being verified.
        {
            Texture2D atlasTexture = Texture2D::CreateFromPixels(
                device, 4, 1,
                std::vector<uint8_t>{255, 0, 0, 255,   0, 255, 0, 255,
                                     0, 0, 255, 255,   255, 255, 255, 255});
            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(atlasTexture, Rectangle(0, 0, 1, 1), Rectangle(2, 0, 1, 1), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
            sprites_->End();
            check("Direct2D ImageBrush positive-offset atlas crop (X=2)", 0, 0, Color(0, 0, 255, 255));
        }

        // D2D-81: an independent numeric oracle for a source rectangle that is PARTIALLY inside
        // and partially outside the texture, under Linear filtering with Clamp/Wrap/Mirror -- the
        // existing outside-source coverage above (checkLinearParity) only compares the ordinary
        // Texture2D path against the RenderTarget2D path, which proves the two AGREE but not that
        // either is CORRECT. Sample points are chosen so the expected value never depends on
        // Direct2D's exact undocumented half-texel sampling convention: deep-interior points
        // (safely inside a 4-texel same-colored block) are pure-color regardless of exactly which
        // neighboring texels bilinear blends; the one interior blend point sits at u=4.0 exactly,
        // the precise midpoint between texel 3's and texel 4's centers (3.5 and 4.5) under the
        // universal "texel center = index + 0.5" convention, so it must be an exact 50/50 blend
        // under ANY reasonable implementation. The two outside-the-declared-crop points (u<0 and
        // u>=8) land solidly inside a single-color 4-texel block after applying each addressing
        // mode's own standard, well-documented coordinate transform (clamp-to-edge / wrap-modulo /
        // mirror-reflect), so only which color block they land in matters, not sub-texel precision.
        {
            std::vector<uint8_t> stripePixels(8 * 4, 0);
            for (int i = 0; i < 4; ++i)
            {
                stripePixels[static_cast<std::size_t>(i) * 4 + 0] = 255; // texels 0-3: red
                stripePixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
            }
            for (int i = 4; i < 8; ++i)
            {
                stripePixels[static_cast<std::size_t>(i) * 4 + 1] = 255; // texels 4-7: green
                stripePixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
            }
            Texture2D stripeTexture = Texture2D::CreateFromPixels(device, 8, 1, stripePixels);
            RenderTarget2D stripeTarget(device, 8, 1);
            device.SetRenderTarget(&stripeTarget);
            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(*white_, Rectangle(0, 0, 4, 1), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
            sprites_->Draw(*white_, Rectangle(4, 0, 4, 1), Rectangle(0, 0, 1, 1), Color(0, 255, 0, 255));
            sprites_->End();
            device.SetRenderTarget(nullptr);

            const Color red(255, 0, 0, 255);
            const Color green(0, 255, 0, 255);
            const Color halfBlend(128, 128, 0, 255);
            // Wrap's outside-crop points land close to, but not exactly on, texel 7's/texel 0's
            // center -- and WRAP topology puts a DIFFERENT-colored texel directly adjacent across
            // the wrap seam (texel 7 green next to wrapped texel 0 red), unlike the interior
            // red/green points above where all four neighboring texels share one color. The
            // texel-center convention (index + 0.5) gives an exact weight of 1/18 for the minority
            // color at both points: 255/18 = 14.1(6) -> 14, 255*17/18 = 240.8(3) -> 241.
            const Color wrapOutsideLeft(14, 241, 0, 255);
            const Color wrapOutsideRight(241, 14, 0, 255);
            struct StripeSample { int destX; Color clampExpected; Color wrapExpected; Color mirrorExpected; const char* label; };
            const StripeSample stripeSamples[] = {
                {1, red, red, red, "u~0.67 interior red"},
                {2, red, red, red, "u~1.78 interior red"},
                {4, halfBlend, halfBlend, halfBlend, "u=4.0 exact red/green midpoint"},
                {6, green, green, green, "u~6.22 interior green"},
                {0, red, wrapOutsideLeft, red, "u~-0.44 outside left"},
                {8, green, wrapOutsideRight, green, "u~8.44 outside right"},
            };

            for (SamplerState* sampler : {&linearClamp, &linearWrap, &linearMirror})
            {
                const char* modeName = sampler == &linearClamp ? "Clamp" : sampler == &linearWrap ? "Wrap" : "Mirror";
                for (const StripeSample& sample : stripeSamples)
                {
                    const Color& expected = sampler == &linearClamp   ? sample.clampExpected
                                            : sampler == &linearWrap  ? sample.wrapExpected
                                                                      : sample.mirrorExpected;
                    device.Clear(Color::Black);
                    sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr);
                    sprites_->Draw(stripeTexture, Rectangle(0, 0, 9, 1), Rectangle(-1, 0, 10, 1), Color::White);
                    sprites_->Draw(stripeTarget, Rectangle(0, 1, 9, 1), Rectangle(-1, 0, 10, 1), Color::White);
                    sprites_->End();
                    Color ordinaryActual(0, 0, 0, 0);
                    Color targetActual(0, 0, 0, 0);
                    const Rectangle ordinaryTexel(sample.destX, 0, 1, 1);
                    const Rectangle targetTexel(sample.destX, 1, 1, 1);
                    device.GetBackBufferData(&ordinaryTexel, &ordinaryActual, 0, 1);
                    device.GetBackBufferData(&targetTexel, &targetActual, 0, 1);
                    const bool ordinaryMatches = Matches(ordinaryActual, expected, 8);
                    const bool targetMatches = Matches(targetActual, expected, 8);
                    std::printf("[%s] Texture2D independent oracle Linear %s %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                                ordinaryMatches ? "PASS" : "FAIL", modeName, sample.label,
                                ordinaryActual.getRProperty(), ordinaryActual.getGProperty(),
                                ordinaryActual.getBProperty(), ordinaryActual.getAProperty(),
                                expected.getRProperty(), expected.getGProperty(),
                                expected.getBProperty(), expected.getAProperty());
                    std::printf("[%s] RenderTarget2D independent oracle Linear %s %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                                targetMatches ? "PASS" : "FAIL", modeName, sample.label,
                                targetActual.getRProperty(), targetActual.getGProperty(),
                                targetActual.getBProperty(), targetActual.getAProperty(),
                                expected.getRProperty(), expected.getGProperty(),
                                expected.getBProperty(), expected.getAProperty());
                    passed = passed && ordinaryMatches && targetMatches;
                }
            }

            // D2D-83: the SAME oracle applied to the CPU FALLBACK path instead of ImageBrush --
            // requiresCpuBitmap forces MakeSpritePixels whenever a draw is tinted (non-white
            // Color) and GPU ColorMatrix is unavailable, which is unconditionally true under this
            // Wine/WineD3D build. MakeSpritePixels itself does PURE NEAREST-NEIGHBOR sampling (see
            // its sampleCoordinate lambda -- always returns an integer index, no fractional
            // blending); the flattened, per-texel-tinted 1:1 RGBA buffer it produces is uploaded as
            // a transient bitmap and THEN linearly interpolated by the GPU when scaled to the
            // destination rect. Tint is a per-channel multiply (D2D-34), which commutes with
            // linear interpolation, so the correct expected value is simply each ImageBrush-path
            // expected color scaled by the tint factor -- reusing D2D-81's already-validated
            // blend weights rather than re-deriving them.
            const Color tint(200, 200, 200, 255);
            const auto tinted = [&](const Color& c) {
                return Color(static_cast<int>(c.getRProperty()) * 200 / 255,
                             static_cast<int>(c.getGProperty()) * 200 / 255,
                             static_cast<int>(c.getBProperty()) * 200 / 255, 255);
            };
            const StripeSample tintedSamples[] = {
                {1, tinted(red), tinted(red), tinted(red), "u~0.67 interior red"},
                {2, tinted(red), tinted(red), tinted(red), "u~1.78 interior red"},
                {4, tinted(halfBlend), tinted(halfBlend), tinted(halfBlend), "u=4.0 exact red/green midpoint"},
                {6, tinted(green), tinted(green), tinted(green), "u~6.22 interior green"},
                {0, tinted(red), tinted(wrapOutsideLeft), tinted(red), "u~-0.44 outside left"},
                {8, tinted(green), tinted(wrapOutsideRight), tinted(green), "u~8.44 outside right"},
            };
            for (SamplerState* sampler : {&linearClamp, &linearWrap, &linearMirror})
            {
                const char* modeName = sampler == &linearClamp ? "Clamp" : sampler == &linearWrap ? "Wrap" : "Mirror";
                for (const StripeSample& sample : tintedSamples)
                {
                    const Color& expected = sampler == &linearClamp   ? sample.clampExpected
                                            : sampler == &linearWrap  ? sample.wrapExpected
                                                                      : sample.mirrorExpected;
                    device.Clear(Color::Black);
                    sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr);
                    sprites_->Draw(stripeTexture, Rectangle(0, 0, 9, 1), Rectangle(-1, 0, 10, 1), tint);
                    sprites_->End();
                    Color actual(0, 0, 0, 0);
                    const Rectangle texel(sample.destX, 0, 1, 1);
                    device.GetBackBufferData(&texel, &actual, 0, 1);
                    const bool matches = Matches(actual, expected, 8);
                    std::printf("[%s] Texture2D CPU-fallback oracle Linear %s %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                                matches ? "PASS" : "FAIL", modeName, sample.label,
                                actual.getRProperty(), actual.getGProperty(),
                                actual.getBProperty(), actual.getAProperty(),
                                expected.getRProperty(), expected.getGProperty(),
                                expected.getBProperty(), expected.getAProperty());
                    passed = passed && matches;
                }
            }
        }

        // D2D-81 (vertical half): the same independent numeric oracle transposed onto the Y axis,
        // which the horizontal block above deliberately left uncovered.
        //
        // The expected values are NOT re-derived: they are the horizontal ones, and that is the
        // whole point. Direct2D's bilinear filter is separable and its extend modes are per-axis --
        // D2D1_IMAGE_BRUSH_PROPERTIES carries extendModeX and extendModeY as two independent
        // parameters -- so a vertical sample through the same texel layout must produce the same
        // numbers as the horizontal one, including Wrap's 1/18 seam weights (255/18 = 14.1(6) ->
        // 14, 255*17/18 = 240.8(3) -> 241) that the horizontal run confirmed empirically. If a
        // runtime disagrees here while agreeing there, that asymmetry is itself the finding: it
        // would mean the Y axis is not being addressed or filtered the way the X axis is.
        {
            std::vector<uint8_t> columnPixels(8 * 4, 0);
            for (int i = 0; i < 4; ++i)
            {
                columnPixels[static_cast<std::size_t>(i) * 4 + 0] = 255; // rows 0-3: red
                columnPixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
            }
            for (int i = 4; i < 8; ++i)
            {
                columnPixels[static_cast<std::size_t>(i) * 4 + 1] = 255; // rows 4-7: green
                columnPixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
            }
            Texture2D columnTexture = Texture2D::CreateFromPixels(device, 1, 8, columnPixels);
            RenderTarget2D columnTarget(device, 1, 8);
            device.SetRenderTarget(&columnTarget);
            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(*white_, Rectangle(0, 0, 1, 4), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
            sprites_->Draw(*white_, Rectangle(0, 4, 1, 4), Rectangle(0, 0, 1, 1), Color(0, 255, 0, 255));
            sprites_->End();
            device.SetRenderTarget(nullptr);

            const Color red(255, 0, 0, 255);
            const Color green(0, 255, 0, 255);
            const Color halfBlend(128, 128, 0, 255);
            const Color wrapOutsideTop(14, 241, 0, 255);
            const Color wrapOutsideBottom(241, 14, 0, 255);
            struct ColumnSample { int destY; Color clampExpected; Color wrapExpected; Color mirrorExpected; const char* label; };
            // Destination height 9 is odd for the same reason the horizontal width was: it puts one
            // sample exactly at v=4.0, the midpoint between row 3's and row 4's centers.
            const ColumnSample columnSamples[] = {
                {1, red, red, red, "v~0.67 interior red"},
                {2, red, red, red, "v~1.78 interior red"},
                {4, halfBlend, halfBlend, halfBlend, "v=4.0 exact red/green midpoint"},
                {6, green, green, green, "v~6.22 interior green"},
                {0, red, wrapOutsideTop, red, "v~-0.44 outside top"},
                {8, green, wrapOutsideBottom, green, "v~8.44 outside bottom"},
            };

            for (SamplerState* sampler : {&linearClamp, &linearWrap, &linearMirror})
            {
                const char* modeName = sampler == &linearClamp ? "Clamp"
                    : sampler == &linearWrap ? "Wrap" : "Mirror";
                for (const ColumnSample& sample : columnSamples)
                {
                    const Color& expected = sampler == &linearClamp   ? sample.clampExpected
                                            : sampler == &linearWrap  ? sample.wrapExpected
                                                                      : sample.mirrorExpected;
                    device.Clear(Color::Black);
                    sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, sampler, nullptr, nullptr);
                    sprites_->Draw(columnTexture, Rectangle(0, 0, 1, 9), Rectangle(0, -1, 1, 10), Color::White);
                    sprites_->Draw(columnTarget, Rectangle(1, 0, 1, 9), Rectangle(0, -1, 1, 10), Color::White);
                    sprites_->End();
                    Color ordinaryActual(0, 0, 0, 0);
                    Color targetActual(0, 0, 0, 0);
                    const Rectangle ordinaryTexel(0, sample.destY, 1, 1);
                    const Rectangle targetTexel(1, sample.destY, 1, 1);
                    device.GetBackBufferData(&ordinaryTexel, &ordinaryActual, 0, 1);
                    device.GetBackBufferData(&targetTexel, &targetActual, 0, 1);
                    const bool ordinaryMatches = Matches(ordinaryActual, expected, 8);
                    const bool targetMatches = Matches(targetActual, expected, 8);
                    std::printf("[%s] Texture2D vertical oracle Linear %s %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                                ordinaryMatches ? "PASS" : "FAIL", modeName, sample.label,
                                ordinaryActual.getRProperty(), ordinaryActual.getGProperty(),
                                ordinaryActual.getBProperty(), ordinaryActual.getAProperty(),
                                expected.getRProperty(), expected.getGProperty(),
                                expected.getBProperty(), expected.getAProperty());
                    std::printf("[%s] RenderTarget2D vertical oracle Linear %s %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                                targetMatches ? "PASS" : "FAIL", modeName, sample.label,
                                targetActual.getRProperty(), targetActual.getGProperty(),
                                targetActual.getBProperty(), targetActual.getAProperty(),
                                expected.getRProperty(), expected.getGProperty(),
                                expected.getBProperty(), expected.getAProperty());
                    passed = passed && ordinaryMatches && targetMatches;
                }
            }
        }

        // 7. A recorded scissor rectangle must only affect SpriteBatch once RasterizerState
        // explicitly enables its test. Clear deliberately ignores it, so both branches begin
        // from the same black backbuffer.
        RasterizerState scissorDisabled;
        scissorDisabled.setScissorTestEnableProperty(false);
        RasterizerState scissorEnabled = scissorDisabled;
        scissorEnabled.setScissorTestEnableProperty(true);
        device.setScissorRectangleProperty(Rectangle(0, 22, 4, 4));
        device.setRasterizerStateProperty(scissorDisabled);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 22, 8, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Scissor disabled leaves outside pixels", 6, 23, Color::White);

        device.setRasterizerStateProperty(scissorEnabled);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorEnabled);
        sprites_->Draw(*white_, Rectangle(0, 22, 8, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Scissor enabled keeps inside pixels", 1, 23, Color::White);
        check("Scissor enabled clips outside pixels", 6, 23, Color::Black);
        device.setRasterizerStateProperty(scissorDisabled);
        device.setScissorRectangleProperty(Rectangle(0, 0, 48, 32));

        // Binding a smaller target resets the public scissor rectangle. Set it again after the
        // bind and verify its top-left Direct2D coordinates survive the target switch and sample
        // back without an inverted Y axis.
        RenderTarget2D scissorTarget(device, 8, 4);
        device.SetRenderTarget(&scissorTarget);
        device.setScissorRectangleProperty(Rectangle(0, 0, 4, 4));
        device.setRasterizerStateProperty(scissorEnabled);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorEnabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 8, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(scissorTarget, Rectangle(16, 22, 8, 4), Rectangle(0, 0, 8, 4), Color::White);
        sprites_->End();
        check("RenderTarget2D scissor top-left", 17, 23, Color::White);
        check("RenderTarget2D scissor right side", 22, 23, Color::Black);
        device.setRasterizerStateProperty(scissorDisabled);
        device.setScissorRectangleProperty(Rectangle(0, 0, 48, 32));

        // 8. SpriteBatch coordinates are viewport-local, and the viewport is also an output clip.
        // This establishes the Direct2D 2D rule without involving a 3D projection/depth range.
        device.setViewportProperty(Viewport(10, 4, 8, 4));
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 12, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Viewport local origin", 11, 5, Color::White);
        check("Viewport clips local overflow", 19, 5, Color::Black);
        check("Viewport leaves old origin untouched", 1, 1, Color::Black);

        RenderTarget2D viewportTarget(device, 12, 6);
        device.SetRenderTarget(&viewportTarget);
        device.setViewportProperty(Viewport(2, 1, 6, 3));
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 8, 3), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(viewportTarget, Rectangle(0, 8, 12, 6), Rectangle(0, 0, 12, 6), Color::White);
        sprites_->End();
        check("RenderTarget2D viewport offset", 3, 10, Color::White);
        check("RenderTarget2D viewport left clip", 1, 10, Color::Black);
        check("RenderTarget2D viewport right clip", 8, 10, Color::Black);
        device.setViewportProperty(Viewport(0, 0, 48, 32));

        // 9. Direct2D's native blends cover the three supported standard SpriteBatch presets.
        // AlphaBlend consumes premultiplied texture data, while NonPremultiplied must
        // premultiply a straight-alpha source before Direct2D source-over. Opaque exercises the
        // native copy composite; the RT case proves the same behavior while an off-screen bitmap
        // is the destination. XNA Additive (SourceAlpha/One) is rejected later because Direct2D's
        // PLUS primitive is One/One for a GPU-only render-target source.
        auto premultipliedHalfRed = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{128, 0, 0, 128});
        auto straightHalfRed = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{255, 0, 0, 128});
        const Color blendBackground(20, 40, 80, 255);
        const Color halfRedOverBackground(138, 20, 40, 255);
        const Color halfAlphaTint(255, 255, 255, 128);
        // D2D-34: EasyGL's SpriteBatch shader is FragColor = texture * Color, a purely
        // component-wise multiply. Color.A must scale only the resulting alpha; it must not also
        // attenuate the (already premultiplied) source RGB a second time. The tinted premultiplied
        // source therefore stays (128,0,0,64) here -- full red, halved alpha -- not (64,0,0,64).
        const Color halfRedColorAlphaOverBackground(143, 30, 60, 255);

        device.Clear(blendBackground);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr, &scissorDisabled);
        sprites_->Draw(straightHalfRed, Rectangle(8, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        // D2D-34: Color.A attenuates only the resulting alpha, for every preset including
        // Opaque -- it must never additionally attenuate the source RGB (which stays scaled by
        // Color.R/G/B alone, independent of Color.A).
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 4, 4, 4),
                       Rectangle(0, 0, 1, 1), halfAlphaTint);
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr, &scissorDisabled);
        sprites_->Draw(straightHalfRed, Rectangle(8, 4, 4, 4),
                       Rectangle(0, 0, 1, 1), halfAlphaTint);
        sprites_->End();
        if (verifyAdvancedBlend)
        {
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(premultipliedHalfRed, Rectangle(24, 0, 4, 4),
                           Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(premultipliedHalfRed, Rectangle(24, 4, 4, 4),
                           Rectangle(0, 0, 1, 1), halfAlphaTint);
            sprites_->End();
        }
        check("AlphaBlend premultiplied texture", 1, 1, halfRedOverBackground);
        check("NonPremultiplied straight texture", 9, 1, halfRedOverBackground);
        check("AlphaBlend texture Color.A", 1, 5, halfRedColorAlphaOverBackground);
        check("NonPremultiplied texture Color.A", 9, 5, halfRedColorAlphaOverBackground);
        if (verifyAdvancedBlend)
        {
            check("Opaque copies premultiplied source", 25, 1, Color(128, 0, 0, 128));
            check("Opaque texture Color.A", 25, 5, Color(128, 0, 0, 64));
        }
        else
        {
            std::printf("[SKIP] Opaque BOUNDED_SOURCE_COPY composite mode (Wine compatibility limit)\n");
        }

        RenderTarget2D blendTarget(device, 4, 4);
        device.SetRenderTarget(&blendTarget);
        device.Clear(blendBackground);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(blendTarget, Rectangle(32, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("AlphaBlend RenderTarget2D destination", 33, 1, halfRedOverBackground);

        // A render target has no CPU shadow, so this exercises the image-brush path as a blend
        // source. Write the same premultiplied (128,0,0,128) source as the texture above through
        // AlphaBlend over transparency; Clear's ColorF input is straight-alpha instead.
        RenderTarget2D blendSourceTarget(device, 1, 1);
        device.SetRenderTarget(&blendSourceTarget);
        device.Clear(Color::Transparent);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);
        device.Clear(blendBackground);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("AlphaBlend RenderTarget2D source", 1, 5, halfRedOverBackground);
        if (verifyAdvancedBlend)
        {
            device.Clear(blendBackground);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Opaque RenderTarget2D source", 1, 5, Color(128, 0, 0, 128));

            // D2D-33: DestinationOver is the symmetric Add tuple
            // (InverseDestinationAlpha, One). Test both a direct bitmap DrawImage and the
            // Flip-triggered ImageBrush -> command-list materialization path. Each retains an
            // existing blue destination and fills only the transparent half with red.
            auto opaqueRed = Texture2D::CreateFromPixels(
                device, 1, 1, std::vector<uint8_t>{255, 0, 0, 255});
            auto opaqueBlue = Texture2D::CreateFromPixels(
                device, 1, 1, std::vector<uint8_t>{0, 0, 255, 255});
            BlendState destinationOver = BlendState::Opaque;
            destinationOver.setColorSourceBlendProperty(Blend::InverseDestinationAlpha);
            destinationOver.setAlphaSourceBlendProperty(Blend::InverseDestinationAlpha);
            destinationOver.setColorDestinationBlendProperty(Blend::One);
            destinationOver.setAlphaDestinationBlendProperty(Blend::One);

            device.Clear(Color::Transparent);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr,
                            &scissorDisabled);
            sprites_->Draw(opaqueBlue, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->Draw(opaqueBlue, Rectangle(8, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            sprites_->Begin(SpriteSortMode::Deferred, destinationOver, &point, nullptr,
                            &scissorDisabled);
            sprites_->Draw(opaqueRed, Rectangle(0, 0, 8, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->Draw(opaqueRed, Rectangle(8, 0, 8, 4), Rectangle(0, 0, 1, 1), Color::White,
                           0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
            sprites_->End();
            check("DestinationOver direct preserves destination", 1, 1, Color(0, 0, 255, 255));
            check("DestinationOver direct fills transparent", 6, 1, Color(255, 0, 0, 255));
            check("DestinationOver brush preserves destination", 9, 1, Color(0, 0, 255, 255));
            check("DestinationOver brush fills transparent", 14, 1, Color(255, 0, 0, 255));

            // D2D-40: every exact composite mode against an INDEPENDENT algebraic oracle. The
            // checks above compare one Direct2D path with another Direct2D path, which cannot
            // detect a mode that is uniformly mapped to the wrong operator.
            // CompositePorterDuffReference evaluates the operator's own Fs/Fd definition on the
            // CPU without consulting CNA's mapping at all, and both operands are deliberately
            // channel-asymmetric and partially transparent so that no two operators agree by
            // accident.
            constexpr std::array<std::uint8_t, 4> compositeSource{{100, 50, 25, 200}};
            constexpr std::array<std::uint8_t, 4> compositeDestination{{40, 80, 120, 150}};
            struct CompositeModeCase
            {
                const char* name;
                Blend colorSource;
                Blend colorDestination;
                Direct2DBlendMode mode;
            };
            const std::array<CompositeModeCase, 10> compositeCases{{
                {"Copy", Blend::One, Blend::Zero, Direct2DBlendMode::Copy},
                {"SourceOver", Blend::One, Blend::InverseSourceAlpha, Direct2DBlendMode::SourceOver},
                {"DestinationOver", Blend::InverseDestinationAlpha, Blend::One,
                 Direct2DBlendMode::DestinationOver},
                {"SourceIn", Blend::DestinationAlpha, Blend::Zero, Direct2DBlendMode::SourceIn},
                {"DestinationIn", Blend::Zero, Blend::SourceAlpha, Direct2DBlendMode::DestinationIn},
                {"SourceOut", Blend::InverseDestinationAlpha, Blend::Zero, Direct2DBlendMode::SourceOut},
                {"DestinationOut", Blend::Zero, Blend::InverseSourceAlpha,
                 Direct2DBlendMode::DestinationOut},
                {"SourceAtop", Blend::DestinationAlpha, Blend::InverseSourceAlpha,
                 Direct2DBlendMode::SourceAtop},
                {"DestinationAtop", Blend::InverseDestinationAlpha, Blend::SourceAlpha,
                 Direct2DBlendMode::DestinationAtop},
                {"Xor", Blend::InverseDestinationAlpha, Blend::InverseSourceAlpha,
                 Direct2DBlendMode::Xor},
            }};

            auto compositeSourceTexture = Texture2D::CreateFromPixels(
                device, 1, 1,
                std::vector<uint8_t>{compositeSource[0], compositeSource[1], compositeSource[2],
                                     compositeSource[3]});
            auto compositeDestinationTexture = Texture2D::CreateFromPixels(
                device, 1, 1,
                std::vector<uint8_t>{compositeDestination[0], compositeDestination[1],
                                     compositeDestination[2], compositeDestination[3]});
            // SetData writes the render target's level-zero bytes exactly (D2D-87), so the
            // GPU-resident source variant carries the same premultiplied operand as the texture
            // instead of an approximation produced by a preceding blend.
            RenderTarget2D compositeSourceTarget(device, 1, 1);
            const std::array<Color, 1> compositeSourcePixel{
                Color(compositeSource[0], compositeSource[1], compositeSource[2], compositeSource[3])};
            compositeSourceTarget.SetData(compositeSourcePixel.data(),
                                          static_cast<int>(compositeSourcePixel.size()));

            device.Clear(Color::Transparent);
            // One destination pass for the whole matrix: Copy writes the operand bytes verbatim.
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr,
                            &scissorDisabled);
            for (std::size_t index = 0; index < compositeCases.size(); ++index)
            {
                const int tileX = static_cast<int>(index) * 4;
                sprites_->Draw(compositeDestinationTexture, Rectangle(tileX, 0, 4, 4),
                               Rectangle(0, 0, 1, 1), Color::White);
                sprites_->Draw(compositeDestinationTexture, Rectangle(tileX, 4, 4, 4),
                               Rectangle(0, 0, 1, 1), Color::White);
            }
            sprites_->End();

            for (std::size_t index = 0; index < compositeCases.size(); ++index)
            {
                const CompositeModeCase& testCase = compositeCases[index];
                BlendState compositeBlend = BlendState::Opaque;
                compositeBlend.setColorSourceBlendProperty(testCase.colorSource);
                compositeBlend.setAlphaSourceBlendProperty(testCase.colorSource);
                compositeBlend.setColorDestinationBlendProperty(testCase.colorDestination);
                compositeBlend.setAlphaDestinationBlendProperty(testCase.colorDestination);
                const int tileX = static_cast<int>(index) * 4;
                sprites_->Begin(SpriteSortMode::Deferred, compositeBlend, &point, nullptr,
                                &scissorDisabled);
                sprites_->Draw(compositeSourceTexture, Rectangle(tileX, 0, 4, 4),
                               Rectangle(0, 0, 1, 1), Color::White);
                sprites_->Draw(compositeSourceTarget, Rectangle(tileX, 4, 4, 4),
                               Rectangle(0, 0, 1, 1), Color::White);
                sprites_->End();
            }

            for (std::size_t index = 0; index < compositeCases.size(); ++index)
            {
                const CompositeModeCase& testCase = compositeCases[index];
                const std::array<std::uint8_t, 4> expected = CompositePorterDuffReference(
                    testCase.mode, compositeSource, compositeDestination);
                const Color expectedColor(expected[0], expected[1], expected[2], expected[3]);
                const int tileX = static_cast<int>(index) * 4 + 1;
                const std::string textureLabel =
                    std::string("Porter-Duff oracle ") + testCase.name + " (Texture2D source)";
                const std::string targetLabel =
                    std::string("Porter-Duff oracle ") + testCase.name + " (RenderTarget2D source)";
                check(textureLabel.c_str(), tileX, 1, expectedColor);
                check(targetLabel.c_str(), tileX, 5, expectedColor);
            }
        }
        // D2D-45: golden matrix over blend preset x source type x tint x alpha representation,
        // generated from the data below rather than written out case by case, and compared against
        // the same independent CPU reference D2D-40 uses. The destination is opaque so it can be
        // established with Clear on every runtime -- an exact premultiplied destination would need
        // the bounded-copy composite, which Wine ignores, and that would have made the whole matrix
        // native-only for no gain.
        //
        // Only the two source-over presets appear here. Opaque's independent RGB/alpha tint is
        // still an open native question (D2D-41), and asserting an answer for it from this matrix
        // would be inventing evidence rather than collecting it.
        {
            struct AlphaRepresentation
            {
                const char* name;
                bool straight;
                std::array<std::uint8_t, 4> stored;
            };
            struct TintCase
            {
                const char* name;
                Color tint;
            };
            const std::array<AlphaRepresentation, 2> representations{{
                // Premultiplied: every channel is already scaled by alpha, so it must not be
                // scaled again anywhere in the pipeline.
                {"premultiplied", false, {{120, 60, 30, 200}}},
                // Straight: the same visual colour stored unmultiplied, converted exactly once.
                {"straight", true, {{153, 77, 38, 200}}},
            }};
            const std::array<TintCase, 3> tints{{
                {"white", Color::White},
                {"rgb", Color(200, 150, 100, 255)},
                {"alpha", Color(255, 255, 255, 128)},
            }};
            const std::array<std::uint8_t, 4> matrixDestination{{40, 80, 120, 255}};

            const auto expectedFor = [&](const AlphaRepresentation& representation,
                                         const TintCase& tintCase) {
                // The renderer's documented contract (D2D-34): a straight source is premultiplied
                // exactly once, then Color.RGB scales the premultiplied RGB and Color.A scales only
                // the alpha. Everything after that is plain source-over.
                const int storedAlpha = representation.stored[3];
                std::array<std::uint8_t, 4> effective{};
                for (int channel = 0; channel < 3; ++channel)
                {
                    const int premultiplied = representation.straight
                        ? representation.stored[static_cast<std::size_t>(channel)] * storedAlpha / 255
                        : representation.stored[static_cast<std::size_t>(channel)];
                    const int tintChannel = channel == 0 ? tintCase.tint.getRProperty()
                        : (channel == 1 ? tintCase.tint.getGProperty() : tintCase.tint.getBProperty());
                    effective[static_cast<std::size_t>(channel)] =
                        static_cast<std::uint8_t>(premultiplied * tintChannel / 255);
                }
                effective[3] = static_cast<std::uint8_t>(storedAlpha * tintCase.tint.getAProperty() / 255);
                const std::array<std::uint8_t, 4> composited = CompositePorterDuffReference(
                    Direct2DBlendMode::SourceOver, effective, matrixDestination);
                return Color(composited[0], composited[1], composited[2], composited[3]);
            };

            SamplerState matrixSampler = SamplerState::PointClamp;
            for (std::size_t representationIndex = 0; representationIndex < representations.size();
                 ++representationIndex)
            {
                const AlphaRepresentation& representation = representations[representationIndex];
                auto matrixTexture = Texture2D::CreateFromPixels(
                    device, 1, 1,
                    std::vector<uint8_t>{representation.stored[0], representation.stored[1],
                                         representation.stored[2], representation.stored[3]});
                const BlendState& blend = representation.straight
                    ? BlendState::NonPremultiplied : BlendState::AlphaBlend;

                device.Clear(Color(matrixDestination[0], matrixDestination[1], matrixDestination[2],
                                   matrixDestination[3]));
                for (std::size_t tintIndex = 0; tintIndex < tints.size(); ++tintIndex)
                {
                    sprites_->Begin(SpriteSortMode::Deferred, blend, &matrixSampler, nullptr,
                                    &scissorDisabled);
                    sprites_->Draw(matrixTexture,
                                   Rectangle(static_cast<int>(tintIndex) * 4, 0, 4, 4),
                                   Rectangle(0, 0, 1, 1), tints[tintIndex].tint);
                    sprites_->End();
                }
                for (std::size_t tintIndex = 0; tintIndex < tints.size(); ++tintIndex)
                {
                    const std::string label = std::string("golden matrix ") + representation.name +
                        " x " + tints[tintIndex].name + " (Texture2D)";
                    check(label.c_str(), static_cast<int>(tintIndex) * 4 + 1, 1,
                          expectedFor(representation, tints[tintIndex]));
                }

                // The same matrix with a GPU-resident source. Its decoration needs the built-in
                // effects, so this half -- and only this half -- is the documented native branch.
                if (!verifyRenderTargetDecoration) continue;
                RenderTarget2D matrixTarget(device, 1, 1, false, SurfaceFormat::Color,
                                            DepthFormat::None);
                const std::array<Color, 1> matrixTargetPixel{
                    Color(representation.stored[0], representation.stored[1],
                          representation.stored[2], representation.stored[3])};
                matrixTarget.SetData(matrixTargetPixel.data(),
                                     static_cast<int>(matrixTargetPixel.size()));
                device.Clear(Color(matrixDestination[0], matrixDestination[1], matrixDestination[2],
                                   matrixDestination[3]));
                for (std::size_t tintIndex = 0; tintIndex < tints.size(); ++tintIndex)
                {
                    sprites_->Begin(SpriteSortMode::Deferred, blend, &matrixSampler, nullptr,
                                    &scissorDisabled);
                    sprites_->Draw(matrixTarget,
                                   Rectangle(static_cast<int>(tintIndex) * 4, 0, 4, 4),
                                   Rectangle(0, 0, 1, 1), tints[tintIndex].tint);
                    sprites_->End();
                }
                for (std::size_t tintIndex = 0; tintIndex < tints.size(); ++tintIndex)
                {
                    const std::string label = std::string("golden matrix ") + representation.name +
                        " x " + tints[tintIndex].name + " (RenderTarget2D)";
                    check(label.c_str(), static_cast<int>(tintIndex) * 4 + 1, 1,
                          expectedFor(representation, tints[tintIndex]));
                }
            }
        }

        if (verifyRenderTargetDecoration)
        {
            device.Clear(blendBackground);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), halfAlphaTint);
            sprites_->End();
            // D2D-34: the GPU ColorMatrix path uses its default (non-STRAIGHT) alpha mode here,
            // applying the tint matrix directly to the already-premultiplied source -- the same
            // independent-component result as the CPU fallback above, so it shares that oracle.
            check("AlphaBlend RenderTarget2D source Color.A", 1, 5, halfRedColorAlphaOverBackground);

            // NonPremultiplied declares its RT input to be straight-alpha data. Its stored
            // premultiplied red is therefore premultiplied once more before source-over.
            device.Clear(blendBackground);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr,
                            &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), halfAlphaTint);
            sprites_->End();
            check("NonPremultiplied RenderTarget2D source Color.A", 1, 5, Color(47, 30, 60, 255));

            if (verifyAdvancedBlend)
            {
                device.Clear(blendBackground);
                sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr,
                                &scissorDisabled);
                sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1),
                               halfAlphaTint);
                sprites_->End();
                check("Opaque RenderTarget2D source Color.A", 1, 5, Color(128, 0, 0, 64));
            }
        }
        else
        {
            // D2D-85: when ColorMatrix isn't available (this Wine build), tinting a RenderTarget2D
            // SpriteBatch source must throw a NAMED, documented capability exception instead of a
            // generic std::runtime_error wrapping a raw HRESULT like "hr=0x88990028". Unlike
            // Texture2D, a RenderTarget2D source has no CPU fallback to fall back to instead.
            // SpriteSortMode::Immediate is used (not Deferred) so Draw() calls the native renderer
            // synchronously instead of queueing -- with Deferred, the actual native draw (and this
            // exception) would fire inside End()'s flushBatch(), not Draw(), and a caught exception
            // there would leave the queued sprite for a retry loop rather than a clean recovery.
            bool namedExceptionThrown = false;
            sprites_->Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
            try
            {
                sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), halfAlphaTint);
            }
            catch (const System::NotSupportedException&)
            {
                namedExceptionThrown = true;
            }
            sprites_->End();
            std::printf("[%s] RenderTarget2D source tint without ColorMatrix throws a named "
                        "NotSupportedException (not a raw HRESULT)\n",
                        namedExceptionThrown ? "PASS" : "FAIL");
            passed = passed && namedExceptionThrown;

            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Direct2D usable after rejected RenderTarget2D source tint", 0, 0, Color::White);
        }

        // D2D-86: sampling a RenderTarget2D as a SpriteBatch source while it is STILL the actively
        // bound render target is a read/write bitmap alias -- must be rejected explicitly, and the
        // rejection must not disturb the active target's own binding or content.
        {
            RenderTarget2D selfSampleTarget(device, 2, 2);
            device.SetRenderTarget(&selfSampleTarget);
            device.Clear(Color(10, 20, 30, 255));
            bool selfSampleRejected = false;
            try
            {
                sprites_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, nullptr, nullptr);
                sprites_->Draw(selfSampleTarget, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
            }
            catch (const System::NotSupportedException&)
            {
                selfSampleRejected = true;
            }
            sprites_->End();
            std::printf("[%s] Direct2D rejects sampling the actively bound RenderTarget2D as its own source\n",
                        selfSampleRejected ? "PASS" : "FAIL");
            passed = passed && selfSampleRejected;

            const bool stillBound = device.GetRenderTargets().size() == 1;
            std::printf("[%s] Rejected self-sample left the active render target bound\n",
                        stillBound ? "PASS" : "FAIL");
            passed = passed && stillBound;

            Color preserved(0, 0, 0, 0);
            const Rectangle preservedTexel(0, 0, 1, 1);
            selfSampleTarget.GetData(0, &preservedTexel, &preserved, 0, 1);
            const bool contentPreserved = Matches(preserved, Color(10, 20, 30, 255));
            std::printf("[%s] Rejected self-sample left the target's own content untouched\n",
                        contentPreserved ? "PASS" : "FAIL");
            passed = passed && contentPreserved;

            bool presentWhileTargetActiveRejected = false;
            try
            {
                device.GetRenderer().Present();
            }
            catch (const System::NotSupportedException&)
            {
                presentWhileTargetActiveRejected = true;
            }
            const bool presentRejectionPreservedBinding =
                presentWhileTargetActiveRejected && device.GetRenderTargets().size() == 1;
            std::printf("[%s] Direct2D rejects Present while RenderTarget2D is active without unbinding it\n",
                        presentRejectionPreservedBinding ? "PASS" : "FAIL");
            passed = passed && presentRejectionPreservedBinding;

            device.SetRenderTarget(nullptr);
            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Direct2D usable after rejected self-sample", 0, 0, Color::White);
        }

        // D2D-87: RenderTarget2D::SetData while the target is the ACTIVE render target with an
        // open BeginDraw recording session (a pending Clear queued, not yet flushed) must not race
        // ID2D1Bitmap::CopyFromMemory against that pending GPU work. Verifies the update lands
        // consistently: a subsequent readback and a subsequent draw both see the SetData'd content,
        // not the earlier queued Clear color.
        {
            RenderTarget2D setDataWhileBoundTarget(device, 2, 2);
            device.SetRenderTarget(&setDataWhileBoundTarget);
            device.Clear(Color(1, 2, 3, 255)); // queues a pending draw the flush must complete
            const std::vector<Color> newPixels(4, Color(40, 50, 60, 255));
            setDataWhileBoundTarget.SetData(newPixels.data(), static_cast<int>(newPixels.size()));
            device.SetRenderTarget(nullptr);

            Color readback(0, 0, 0, 0);
            const Rectangle readbackTexel(0, 0, 1, 1);
            setDataWhileBoundTarget.GetData(0, &readbackTexel, &readback, 0, 1);
            const bool readbackMatches = Matches(readback, Color(40, 50, 60, 255));
            std::printf("[%s] RenderTarget2D SetData while actively bound is readback-consistent: "
                        "got=(%d,%d,%d,%d)\n",
                        readbackMatches ? "PASS" : "FAIL",
                        readback.getRProperty(), readback.getGProperty(),
                        readback.getBProperty(), readback.getAProperty());
            passed = passed && readbackMatches;

            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sprites_->Draw(setDataWhileBoundTarget, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("RenderTarget2D SetData while actively bound is draw-consistent", 0, 0,
                  Color(40, 50, 60, 255));
        }

        // 10. A mipmapped Texture2D owns one Direct2D bitmap per initialized level. SpriteBatch
        // chooses the nearest populated level during minification, rather than sampling an
        // uninitialized lower level or silently collapsing every draw to level zero.
        Texture2D mipTexture(device, 4, 4, true, SurfaceFormat::Color);
        std::vector<Color> mipLevel0(16, Color(255, 0, 0, 255));
        std::vector<Color> mipLevel1(4, Color(0, 255, 0, 255));
        std::vector<Color> mipLevel2(1, Color(0, 0, 255, 255));
        mipTexture.SetData(0, nullptr, mipLevel0.data(), 0, static_cast<int>(mipLevel0.size()));
        mipTexture.SetData(1, nullptr, mipLevel1.data(), 0, static_cast<int>(mipLevel1.size()));
        mipTexture.SetData(2, nullptr, mipLevel2.data(), 0, static_cast<int>(mipLevel2.size()));
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(mipTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->Draw(mipTexture, Rectangle(8, 0, 2, 2), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->Draw(mipTexture, Rectangle(12, 0, 1, 1), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("Texture2D mip level 0", 1, 1, Color(255, 0, 0, 255));
        check("Texture2D mip level 1", 8, 0, Color(0, 255, 0, 255));
        check("Texture2D mip level 2", 12, 0, Color(0, 0, 255, 255));

        // Decoration must not silently reset an ordinary Texture2D to level zero. Native
        // Direct2D uses the image-brush/effect graph here; Wine/Proton fall back to a CPU bitmap
        // only when the built-in effects are unavailable. Both paths must retain the selected mip.
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(mipTexture, Rectangle(16, 0, 2, 2), Rectangle(0, 0, 4, 4), Color(255, 128, 255, 255));
        sprites_->Draw(mipTexture, Rectangle(20, 0, 1, 1), Rectangle(0, 0, 4, 4), Color(255, 255, 128, 255));
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr, &scissorDisabled);
        sprites_->Draw(mipTexture, Rectangle(24, 0, 1, 1), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("Texture2D tinted mip level 1", 16, 0, Color(0, 128, 0, 255));
        check("Texture2D tinted mip level 2", 20, 0, Color(0, 0, 128, 255));
        check("Texture2D NonPremultiplied mip level 2", 24, 0, Color(0, 0, 255, 255));

        // D2D-30: the destination rectangle alone is 1:1, but the complete SpriteBatch matrix
        // minifies it to one quarter. Level two (blue) must therefore be selected before drawing;
        // the old rectangle-only LOD path incorrectly kept level zero (red).
        device.Clear(Color::Black);
        const Matrix quarterScale = Matrix::CreateScale(0.25f);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr,
                        &scissorDisabled, nullptr, quarterScale);
        sprites_->Draw(mipTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("Texture2D mip selection includes SpriteBatch transform", 0, 0,
              Color(0, 0, 255, 255));

        // D2D-74: TextureFilter::Linear's MipFilter component is Linear, so it must interpolate
        // between the two mip levels bracketing a fractional LOD rather than snapping to the
        // nearest one. A batch scale of 2^-1.5 puts the LOD exactly halfway between level 1
        // (green) and level 2 (blue): expect an even blend of both, not either solid color.
        SamplerState linearSampler = SamplerState::LinearClamp;
        device.Clear(Color::Black);
        const Matrix mipBlendScale = Matrix::CreateScale(0.35355338f);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &linearSampler, nullptr,
                        &scissorDisabled, nullptr, mipBlendScale);
        sprites_->Draw(mipTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("Texture2D MipLinear blends adjacent levels", 0, 0, Color(0, 128, 128, 255));

        // D2D-79: mip LEVEL selection (as opposed to the pure PreferredMipLevelForTransform math,
        // already covered independently by D2D-75's unit tests) is exercised here as an
        // INTEGRATION matrix: destination scale (minification at three levels, plus a fractional
        // scale that must still floor to level 0, not round up early) crossed with every
        // SpriteEffects flip combination. A geometric symmetric solid-color quad makes flip
        // invisible to the OUTPUT color, so any mismatch here can only mean flip corrupted mip
        // selection itself (e.g. a sign error feeding into ComputeMinificationSigma), not a
        // sampling artifact.
        {
            struct MipScaleCase { int destSize; Color expected; const char* label; };
            const MipScaleCase mipScaleCases[] = {
                {8, Color(255, 0, 0, 255), "scale 2.0 magnification (level 0)"},
                {4, Color(255, 0, 0, 255), "scale 1.0 (level 0)"},
                {3, Color(255, 0, 0, 255), "scale 0.75 fractional bound (still level 0)"},
                {2, Color(0, 255, 0, 255), "scale 0.5 (level 1)"},
                {1, Color(0, 0, 255, 255), "scale 0.25 (level 2)"},
            };
            const SpriteEffects flipCases[] = {
                SpriteEffects::None, SpriteEffects::FlipHorizontally, SpriteEffects::FlipVertically,
                static_cast<SpriteEffects>(static_cast<int>(SpriteEffects::FlipHorizontally) |
                                           static_cast<int>(SpriteEffects::FlipVertically))};
            const char* flipLabels[] = {"None", "FlipH", "FlipV", "FlipH|FlipV"};

            bool mipSelectionMatrixPassed = true;
            for (const MipScaleCase& scaleCase : mipScaleCases)
            {
                for (int flipIndex = 0; flipIndex < 4; ++flipIndex)
                {
                    device.Clear(Color::Black);
                    sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
                    sprites_->Draw(mipTexture, Rectangle(0, 0, scaleCase.destSize, scaleCase.destSize),
                                   Rectangle(0, 0, 4, 4), Color::White, 0.0f, Vector2(0.0f, 0.0f),
                                   flipCases[flipIndex], 0.0f);
                    sprites_->End();
                    Color actual(0, 0, 0, 0);
                    const Rectangle centerTexel(0, 0, 1, 1);
                    device.GetBackBufferData(&centerTexel, &actual, 0, 1);
                    const bool caseMatches = Matches(actual, scaleCase.expected);
                    if (!caseMatches)
                    {
                        std::printf("[FAIL] Texture2D mip selection matrix: %s, flip=%s: got=(%d,%d,%d,%d), "
                                    "expected=(%d,%d,%d,%d)\n",
                                    scaleCase.label, flipLabels[flipIndex],
                                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                                    actual.getAProperty(), scaleCase.expected.getRProperty(),
                                    scaleCase.expected.getGProperty(), scaleCase.expected.getBProperty(),
                                    scaleCase.expected.getAProperty());
                    }
                    mipSelectionMatrixPassed = mipSelectionMatrixPassed && caseMatches;
                }
            }
            std::printf("[%s] Texture2D mip selection matrix (scale x flip, %zu cases)\n",
                        mipSelectionMatrixPassed ? "PASS" : "FAIL",
                        std::size(mipScaleCases) * std::size(flipCases));
            passed = passed && mipSelectionMatrixPassed;
        }

        // D2D-78: the historical generated-mip algorithm for NPOT render targets omitted source
        // regions. Mipmapped RenderTarget2D is therefore outside Direct2D's supported 2D contract
        // and must fail at construction, before allocating or binding any native target.
        bool mipmappedRenderTargetRejected = false;
        try
        {
            RenderTarget2D unsupportedMipTarget(
                device, 7, 5, true, SurfaceFormat::Color, DepthFormat::None);
            (void)unsupportedMipTarget;
        }
        catch (const System::NotSupportedException&)
        {
            mipmappedRenderTargetRejected = true;
        }
        std::printf("[%s] Direct2D rejects NPOT mipmapped RenderTarget2D at construction\n",
                    mipmappedRenderTargetRejected ? "PASS" : "FAIL");
        passed = passed && mipmappedRenderTargetRejected;

        // D2D-32: exercise anisotropic interpolation through both DrawImage and ImageBrush. The
        // solid level-zero color makes the assertion independent of vendor-specific filter taps;
        // successful drawing proves both Direct2D entry points accept the native mode.
        SamplerState anisotropic = SamplerState::AnisotropicClamp;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &anisotropic, nullptr,
                        &scissorDisabled);
        sprites_->Draw(mipTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->Draw(mipTexture, Rectangle(8, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White,
                       0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
        sprites_->End();
        check("Direct2D anisotropic DrawImage", 1, 1, Color(255, 0, 0, 255));
        check("Direct2D anisotropic ImageBrush", 9, 1, Color(255, 0, 0, 255));

        // 12. Desktop context-loss recovery is an atomic Direct3D/DXGI/Direct2D recreation.  A
        // recoverable Texture2D must acquire a new bitmap from its CPU shadow; a recoverable RT
        // must be safely reallocated with transparent contents instead of exposing its old COM
        // bitmap. This uses the public debug hook, exactly like EasyGL's desktop recovery probe.
        auto& direct2dRenderer = device.GetRenderer();

        // Renderer-entry-point hardening: these calls are normally normalized by GraphicsDevice,
        // but the concrete renderer must still reject invalid input without corrupting the next
        // frame. This is deliberately separate from the public BlendState rejection checks below.
        const auto expectRendererRejection = [&](const char* label, const auto& action) {
            bool rejected = false;
            try
            {
                action();
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            std::printf("[%s] %s\n", rejected ? "PASS" : "FAIL", label);
            passed = passed && rejected;
        };
        expectRendererRejection("Direct2D rejects null MRT array with count one", [&] {
            direct2dRenderer.SetRenderTargets(nullptr, 1);
        });
        expectRendererRejection("Direct2D rejects negative MRT count", [&] {
            direct2dRenderer.SetRenderTargets(nullptr, -1);
        });
        expectRendererRejection("Direct2D rejects negative scissor dimensions", [&] {
            direct2dRenderer.SetScissorRect(0, 0, -1, 1);
        });
        expectRendererRejection("Direct2D rejects negative viewport dimensions", [&] {
            direct2dRenderer.SetViewport(0, 0, 1, -1, 0.0f, 1.0f);
        });
        expectRendererRejection("Direct2D rejects unknown presentation mode", [&] {
            direct2dRenderer.SetPresentationMode(99);
        });
        expectRendererRejection("Direct2D rejects unsupported swap interval", [&] {
            direct2dRenderer.SetSwapInterval(3);
        });
        expectRendererRejection("Direct2D rejects half-zero virtual resolution", [&] {
            direct2dRenderer.SetVirtualResolution(0, 8);
        });
        expectRendererRejection("Direct2D rejects non-Color backbuffer format", [&] {
            direct2dRenderer.UpdatePresentationFormatEXT(
                static_cast<int>(SurfaceFormat::Bgr565), static_cast<int>(DepthFormat::None), false);
        });
        expectRendererRejection("Direct2D rejects backbuffer depth format", [&] {
            direct2dRenderer.UpdatePresentationFormatEXT(
                static_cast<int>(SurfaceFormat::Color), static_cast<int>(DepthFormat::Depth16), false);
        });
        expectRendererRejection("Direct2D rejects non-Color RenderTarget2D format", [&] {
            (void)direct2dRenderer.CreateRenderTarget2DEXT(2, 2, 0, false, false, 0,
                                                          static_cast<int>(SurfaceFormat::Bgr565));
        });
        expectRendererRejection("Direct2D rejects RenderTarget2D depth format", [&] {
            (void)direct2dRenderer.CreateRenderTarget2DEXT(
                2, 2, static_cast<int>(DepthFormat::Depth16), false, false, 0,
                static_cast<int>(SurfaceFormat::Color));
        });
        expectRendererRejection("Direct2D rejects mipmapped RenderTarget2D", [&] {
            (void)direct2dRenderer.CreateRenderTarget2DEXT(7, 5, 0, false, true, 0, 0);
        });
        expectRendererRejection("Direct2D rejects multisampled RenderTarget2D", [&] {
            (void)direct2dRenderer.CreateRenderTarget2DEXT(2, 2, 0, false, false, 4, 0);
        });
        expectRendererRejection("Direct2D rejects extreme RenderTarget2D dimensions before allocation", [&] {
            (void)direct2dRenderer.CreateRenderTarget2DEXT(
                std::numeric_limits<int>::max(), 1, 0, false, false, 0, 0);
        });
        expectRendererRejection("Direct2D rejects non-finite viewport depth", [&] {
            direct2dRenderer.SetViewport(
                0, 0, 1, 1, std::numeric_limits<float>::quiet_NaN(), 1.0f);
        });
        expectRendererRejection("Direct2D rejects non-finite blend factors", [&] {
            direct2dRenderer.SetBlendFactor(
                std::numeric_limits<float>::quiet_NaN(), 1.0f, 1.0f, 1.0f);
        });
        expectRendererRejection("Direct2D rejects unknown SpriteEffects bits", [&] {
            std::exception_ptr rejection;
            sprites_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, nullptr,
                            &scissorDisabled);
            try
            {
                sprites_->Draw(mipTexture, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                               Color::White, 0.0f, Vector2::Zero,
                               static_cast<SpriteEffects>(4), 0.0f);
            }
            catch (...)
            {
                rejection = std::current_exception();
            }
            sprites_->End();
            if (rejection)
                std::rethrow_exception(rejection);
        });

        bool default3DPolicyRejected = false;
        try
        {
            direct2dRenderer.Ensure3DSupported("Direct2D parity policy probe");
        }
        catch (const std::exception&)
        {
            default3DPolicyRejected = true;
        }
        direct2dRenderer.SetUnsupported3DGraphicsCallBehavior(
            CNA::Unsupported3DGraphicsCallBehavior::WarnAndStub);
        bool warnAndStubReturnedResource = false;
        try
        {
            direct2dRenderer.Ensure3DSupported("Direct2D parity policy probe");
            warnAndStubReturnedResource =
                static_cast<bool>(direct2dRenderer.CreateVertexBuffer(3));
        }
        catch (const std::exception&)
        {
            warnAndStubReturnedResource = false;
        }
        direct2dRenderer.SetUnsupported3DGraphicsCallBehavior(
            CNA::Unsupported3DGraphicsCallBehavior::Throw);
        std::printf("[%s] Direct2D unsupported-3D policy throws by default and returns safe stubs only in WarnAndStub\n",
                    default3DPolicyRejected && warnAndStubReturnedResource ? "PASS" : "FAIL");
        passed = passed && default3DPolicyRejected && warnAndStubReturnedResource;

        // D2D-26: deterministic rectangle fuzz covers every sampler addressing mode, both flips,
        // and source regions far outside the image. The fixed seed keeps failures reproducible;
        // the final marker proves that an accepted edge case did not poison the next Begin/EndDraw.
        std::uint32_t fuzzState = 0xD2D2601u;
        const auto nextFuzz = [&] {
            fuzzState = fuzzState * 1664525u + 1013904223u;
            return fuzzState;
        };
        constexpr std::array addressModes{
            TextureAddressMode::Clamp, TextureAddressMode::Wrap, TextureAddressMode::Mirror};
        device.Clear(Color::Black);
        for (const TextureAddressMode addressMode : addressModes)
        {
            SamplerState fuzzSampler;
            fuzzSampler.setFilterProperty(TextureFilter::Point);
            fuzzSampler.setAddressUProperty(addressMode);
            fuzzSampler.setAddressVProperty(addressMode);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &fuzzSampler,
                            nullptr, &scissorDisabled);
            for (int iteration = 0; iteration < 64; ++iteration)
            {
                const int sourceX = static_cast<int>(nextFuzz() % 257u) - 128;
                const int sourceY = static_cast<int>(nextFuzz() % 257u) - 128;
                const int sourceWidth = static_cast<int>(nextFuzz() % 16u) + 1;
                const int sourceHeight = static_cast<int>(nextFuzz() % 16u) + 1;
                const int destinationX = static_cast<int>(nextFuzz() % 44u) - 4;
                const int destinationY = static_cast<int>(nextFuzz() % 28u) - 4;
                const SpriteEffects fuzzEffects = static_cast<SpriteEffects>(nextFuzz() & 0x3u);
                sprites_->Draw(*twoTexels_, Rectangle(destinationX, destinationY, 4, 4),
                               Rectangle(sourceX, sourceY, sourceWidth, sourceHeight), Color::White,
                               0.0f, Vector2::Zero, fuzzEffects, 0.0f);
            }
            // These must remain defined no-ops without evaluating INT_MIN negation or endpoint sums.
            sprites_->Draw(*twoTexels_, Rectangle(0, 0, 1, 1),
                           Rectangle(std::numeric_limits<int>::min(),
                                     std::numeric_limits<int>::max(), 0, 1), Color::White);
            sprites_->Draw(*twoTexels_, Rectangle(0, 0, 1, 1),
                           Rectangle(std::numeric_limits<int>::max(),
                                     std::numeric_limits<int>::min(), 1, -1), Color::White);
            sprites_->End();
        }
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point,
                        nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D deterministic source-rectangle fuzz leaves drawing usable",
              0, 0, Color::White);

        // Disposed and foreign targets are rejected before native state changes. Exercise both
        // public binding overloads and the renderer entry point, then continue rendering on the
        // original device to prove each failure was transactional.
        RenderTarget2D disposedTarget(device, 2, 2);
        disposedTarget.Dispose();
        expectRendererRejection("SetRenderTarget rejects a disposed target", [&] {
            device.SetRenderTarget(&disposedTarget);
        });
        expectRendererRejection("SetRenderTargets rejects a disposed target", [&] {
            device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&disposedTarget))});
        });
        {
            PresentationParameters foreignPresentation;
            foreignPresentation.setBackBufferWidthProperty(8);
            foreignPresentation.setBackBufferHeightProperty(8);
            GraphicsDevice foreignDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                                          GraphicsProfile::Reach, foreignPresentation);
            RenderTarget2D foreignTarget(foreignDevice, 2, 2);
            expectRendererRejection("SetRenderTarget rejects a foreign GraphicsDevice target", [&] {
                device.SetRenderTarget(&foreignTarget);
            });
            expectRendererRejection("SetRenderTargets rejects a foreign GraphicsDevice target", [&] {
                device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&foreignTarget))});
            });
            expectRendererRejection("Direct2D renderer rejects a foreign device target", [&] {
                direct2dRenderer.SetRenderTarget2D(foreignTarget.GetRenderTargetRenderer());
            });
        }
        const bool targetFailuresKeptBackbuffer = device.GetRenderTargets().empty();
        std::printf("[%s] rejected target bindings leave the Direct2D backbuffer active\n",
                    targetFailuresKeptBackbuffer ? "PASS" : "FAIL");
        passed = passed && targetFailuresKeptBackbuffer;

        // D2D-64: a Texture2D/RenderTarget2D that outlives the C++ scope of its GraphicsDevice
        // must not dereference a dangling renderer/device pointer in its own later destructor.
        // GraphicsDevice::Dispose() proactively disposes every GraphicsResource it still tracks
        // (which resets each one's own renderer_ while the owning Direct2DRenderer is still
        // alive) before tearing down its own renderer, so this is safe by construction -- verified
        // empirically here rather than trusted from reading the code alone. Reaching the PASS
        // print below (instead of crashing) is the test.
        {
            std::unique_ptr<Texture2D> outlivingTexture;
            std::unique_ptr<RenderTarget2D> outlivingRenderTarget;
            {
                PresentationParameters shortLivedPresentation;
                shortLivedPresentation.setBackBufferWidthProperty(4);
                shortLivedPresentation.setBackBufferHeightProperty(4);
                GraphicsDevice shortLivedDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                                                 GraphicsProfile::Reach, shortLivedPresentation);
                outlivingTexture = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                    shortLivedDevice, 1, 1, std::vector<uint8_t>{1, 2, 3, 4}));
                outlivingRenderTarget = std::make_unique<RenderTarget2D>(shortLivedDevice, 2, 2);
                // shortLivedDevice destructs here, while outlivingTexture/outlivingRenderTarget
                // (declared in the enclosing scope) are still alive.
            }
            outlivingTexture.reset();
            outlivingRenderTarget.reset();
        }
        std::printf("[PASS] Texture2D/RenderTarget2D destructors survive outliving their GraphicsDevice\n");

        // GraphicsDevice owns the bind-time clear contract, while Direct2D must preserve the
        // actual bitmap for PreserveContents and PlatformContents. Check all three public usages
        // after a real unbind/rebind cycle.
        const auto verifyRenderTargetUsage = [&](RenderTargetUsage usage, const char* label,
                                                  const Color& expected) {
            RenderTarget2D usageTarget(device, 2, 2, false, SurfaceFormat::Color,
                                       DepthFormat::None, 0, usage);
            device.SetRenderTarget(&usageTarget);
            device.Clear(Color(173, 31, 97, 255));
            device.SetRenderTarget(nullptr);
            device.SetRenderTarget(&usageTarget);
            Color actual(0, 0, 0, 0);
            const Rectangle texel(0, 0, 1, 1);
            usageTarget.GetData(0, &texel, &actual, 0, 1);
            const bool matches = Matches(actual, expected);
            std::printf("[%s] Direct2D RenderTargetUsage %s\n", matches ? "PASS" : "FAIL", label);
            passed = passed && matches;
            device.SetRenderTarget(nullptr);
        };
        verifyRenderTargetUsage(RenderTargetUsage::DiscardContents, "DiscardContents", Color::Black);
        verifyRenderTargetUsage(RenderTargetUsage::PreserveContents, "PreserveContents", Color(173, 31, 97, 255));
        verifyRenderTargetUsage(RenderTargetUsage::PlatformContents, "PlatformContents", Color(173, 31, 97, 255));

        // D2D-60: RenderTarget2D::Dispose() rejects destroying a target GraphicsDevice still
        // considers bound, but a target bound directly through the renderer (bypassing
        // GraphicsDevice::SetRenderTarget, as CNA's device-agnostic ITextureRenderer contract
        // permits) is invisible to that guard. Destroying it while still the renderer's active
        // target must not let ~Direct2DRenderTargetRenderer's unbind path escape the destructor;
        // this exercises that edge case and proves the device stays usable afterward.
        {
            RenderTarget2D directBoundTarget(device, 2, 2);
            directBoundTarget.GetRenderTargetRenderer()->BindAsRenderTarget();
        }
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D survives destroying a directly-bound active render target", 0, 0, Color::White);

        auto recoveryTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{13, 99, 201, 255});
        RenderTarget2D recoveryTarget(device, 2, 2);
        device.SetRenderTarget(&recoveryTarget);
        device.Clear(Color(255, 0, 0, 255));
        device.SetRenderTarget(nullptr);

        direct2dRenderer.DebugSimulateContextLoss();
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(recoveryTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->Draw(mipTexture, Rectangle(16, 0, 1, 1), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(recoveryTarget, Rectangle(8, 0, 4, 4), Rectangle(0, 0, 2, 2), Color::White);
        sprites_->End();
        check("Context recovery restores Texture2D", 1, 1, Color(13, 99, 201, 255));
        check("Context recovery restores Texture2D mip", 16, 0, Color(0, 0, 255, 255));
        check("Context recovery clears RenderTarget2D", 9, 1, Color::Black);

        // Resources created while recovery is disabled deliberately do not enter the registry.
        // After the following reset they must reject the old Direct2D bitmap rather than drawing
        // through a stale COM resource; newly created sources in the new generation still work.
        device.SetContextRecoveryEnabled(false);
        auto unrecoverableTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{241, 77, 19, 255});
        RenderTarget2D unrecoverableTarget(device, 2, 2);
        direct2dRenderer.DebugRestoreContext();
        bool staleResourceRejected = false;
        try
        {
            // Immediate mode reaches the renderer synchronously. This makes the probe entirely
            // public-API based, while its finally-like End below returns SpriteBatch to a usable
            // state if the stale resource correctly rejects the draw.
            sprites_->Begin(SpriteSortMode::Immediate, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(unrecoverableTexture, Rectangle(0, 0, 1, 1),
                           Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
        }
        catch (const std::exception&)
        {
            staleResourceRejected = true;
            try
            {
                sprites_->End();
            }
            catch (const std::exception&)
            {
                // The original stale-resource rejection is the contract under test.
            }
        }
        std::printf("[%s] Context recovery disabled rejects stale Texture2D\n",
                    staleResourceRejected ? "PASS" : "FAIL");
        passed = passed && staleResourceRejected;

        // D2D-61: a render target from a lost device generation (recovery disabled, so it was
        // never re-registered) must be rejected before activeRenderTarget_ or the native Direct2D
        // target are touched at all -- not partway through, which would leave the logical and
        // native target pointing at different resources.
        bool staleTargetRejected = false;
        try
        {
            device.SetRenderTarget(&unrecoverableTarget);
        }
        catch (const std::exception&)
        {
            staleTargetRejected = true;
        }
        std::printf("[%s] Context recovery disabled rejects stale RenderTarget2D bind\n",
                    staleTargetRejected ? "PASS" : "FAIL");
        passed = passed && staleTargetRejected;
        const bool staleTargetLeftBackbufferActive = device.GetRenderTargets().empty();
        std::printf("[%s] Rejected stale RenderTarget2D bind left the backbuffer active\n",
                    staleTargetLeftBackbufferActive ? "PASS" : "FAIL");
        passed = passed && staleTargetLeftBackbufferActive;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D usable after rejected stale RenderTarget2D bind", 0, 0, Color::White);

        auto postRecoveryTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{40, 180, 90, 255});
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(postRecoveryTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Context recovery new Texture2D", 1, 1, Color(40, 180, 90, 255));
        device.SetContextRecoveryEnabled(true);

        // D2D-71: a device loss while a SpriteBatch is open (Begin called, a tinted Draw already
        // issued -- so transient COM resources, e.g. a CPU-fallback bitmap, are held -- but End
        // not yet called) must not leave dangling transients or draw from a stale generation.
        // ReleaseDeviceResourcesNoThrow already flushes the open EndDraw and clears every
        // transient collection as part of recovery; this verifies that empirically rather than
        // trusting it from reading the code.
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color(10, 20, 30, 128));
        direct2dRenderer.DebugSimulateContextLoss();
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D SpriteBatch survives mid-batch device loss", 0, 0, Color::White);

        // D2D-72: registry invariants under churn. Repeated cycles of create/dispose/recovery
        // must not duplicate-register a resource, leave a dangling pointer in
        // recoverableTextures_/recoverableRenderTargets_, or corrupt a surviving resource's
        // generation/content when other resources are created and disposed around it.
        {
            struct StressEntry { std::unique_ptr<Texture2D> texture; uint8_t expectedRed; };
            std::vector<StressEntry> stressTextures;
            std::vector<std::unique_ptr<RenderTarget2D>> stressTargets;
            for (int cycle = 0; cycle < 4; ++cycle)
            {
                for (int i = 0; i < 3; ++i)
                {
                    const uint8_t red = static_cast<uint8_t>(cycle * 20 + i * 5 + 1);
                    stressTextures.push_back(StressEntry{
                        std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                            device, 1, 1, std::vector<uint8_t>{red, 0, 0, 255})),
                        red});
                    stressTargets.push_back(std::make_unique<RenderTarget2D>(device, 2, 2));
                }
                // Churn: dispose every other texture/target accumulated so far before the next
                // recovery, so surviving and freshly-created resources interleave across losses.
                for (std::size_t i = 0; i < stressTextures.size(); i += 2)
                    stressTextures[i].texture.reset();
                for (std::size_t i = 0; i < stressTargets.size(); i += 2)
                    stressTargets[i].reset();
                direct2dRenderer.DebugSimulateContextLoss();
            }

            bool stressPassed = true;
            for (const StressEntry& entry : stressTextures)
            {
                if (!entry.texture) continue;
                device.Clear(Color::Black);
                sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
                sprites_->Draw(*entry.texture, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                sprites_->End();
                Color actual(0, 0, 0, 0);
                const Rectangle texel(0, 0, 1, 1);
                device.GetBackBufferData(&texel, &actual, 0, 1);
                if (!Matches(actual, Color(static_cast<int>(entry.expectedRed), 0, 0, 255))) { stressPassed = false; break; }
            }
            std::printf("[%s] Direct2D registry survives create/dispose/recovery churn\n",
                        stressPassed ? "PASS" : "FAIL");
            passed = passed && stressPassed;
        }

        // D2D-70: device-lost/reset notification must reach the public XNA GraphicsDevice
        // lifecycle (DeviceLost/DeviceResetting/DeviceReset events, GraphicsDeviceStatus), not
        // just recover internally. Direct2D's recovery is atomic (no separately observable
        // "lost, awaiting reset" state -- see DebugRestoreContext's own comment), so a single
        // DebugSimulateContextLoss() call is expected to fire all three events exactly once each,
        // in order.
        {
            // D2D-134: callbacks remain subscribed until GraphicsDevice destruction. Their state
            // is therefore a test-object member declared before manager_, so manager_/device and
            // its callbacks are destroyed first; a later D2D-63 recovery cannot capture a dead
            // block-local vector by reference.
            deviceEventOrder_.clear();
            device.DeviceLost += [this](System::Object*, const System::EventArgs&) {
                deviceEventOrder_.emplace_back("Lost");
            };
            device.DeviceResetting += [this](System::Object*, const System::EventArgs&) {
                deviceEventOrder_.emplace_back("Resetting");
            };
            device.DeviceReset += [this](System::Object*, const System::EventArgs&) {
                deviceEventOrder_.emplace_back("Reset");
            };

            const bool statusStartedNormal =
                device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal;
            std::printf("[%s] Direct2D GraphicsDeviceStatus starts Normal\n", statusStartedNormal ? "PASS" : "FAIL");
            passed = passed && statusStartedNormal;

            direct2dRenderer.DebugSimulateContextLoss();

            const bool orderCorrect = deviceEventOrder_.size() == 3 &&
                deviceEventOrder_[0] == "Lost" && deviceEventOrder_[1] == "Resetting" &&
                deviceEventOrder_[2] == "Reset";
            std::printf("[%s] Direct2D fires DeviceLost/DeviceResetting/DeviceReset exactly once each, in order\n",
                        orderCorrect ? "PASS" : "FAIL");
            passed = passed && orderCorrect;

            const bool statusRecoveredToNormal =
                device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal;
            std::printf("[%s] Direct2D GraphicsDeviceStatus returns to Normal after recovery\n",
                        statusRecoveredToNormal ? "PASS" : "FAIL");
            passed = passed && statusRecoveredToNormal;

            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Direct2D usable after DeviceLost/DeviceReset notification cycle", 0, 0, Color::White);
        }

        // D2D-77: GetMaxTextureDimension() must reflect the active device's real
        // ID2D1DeviceContext::GetMaximumBitmapSize(), not the base class's platform-wide 16384
        // constant. Exercised with a 1-row texture (dimension x 1) so the boundary is tested
        // exactly without allocating an O(dimension^2) buffer.
        {
            const int maxDim = device.GetMaxTextureDimension();
            const bool maxDimSane = maxDim > 0 && maxDim <= (1 << 20);
            std::printf("[%s] Direct2D GetMaxTextureDimension reports a device-derived value (%d)\n",
                        maxDimSane ? "PASS" : "FAIL", maxDim);
            passed = passed && maxDimSane;

            bool atLimitAccepted = true;
            try
            {
                Texture2D atLimit(device, maxDim, 1);
                (void)atLimit;
            }
            catch (const std::exception&)
            {
                atLimitAccepted = false;
            }
            std::printf("[%s] Direct2D accepts a Texture2D exactly at the reported max dimension\n",
                        atLimitAccepted ? "PASS" : "FAIL");
            passed = passed && atLimitAccepted;

            bool overLimitRejected = false;
            try
            {
                Texture2D overLimit(device, maxDim + 1, 1);
                (void)overLimit;
            }
            catch (const System::NotSupportedException&)
            {
                overLimitRejected = true;
            }
            std::printf("[%s] Direct2D rejects a Texture2D one past the reported max dimension\n",
                        overLimitRejected ? "PASS" : "FAIL");
            passed = passed && overLimitRejected;
        }

        // D2D-63: device loss while an unrecoverable render target (created with recovery
        // disabled) is the ACTIVE target must not silently leave GraphicsDevice's public binding
        // out of sync with the renderer's actual current target -- it now surfaces loudly instead
        // of quietly redirecting later Clear/Draw calls to the backbuffer.
        device.SetContextRecoveryEnabled(false);
        RenderTarget2D unrecoverableActiveTarget(device, 2, 2);
        device.SetRenderTarget(&unrecoverableActiveTarget);
        device.Clear(Color(11, 22, 33, 255));
        bool deviceLossWithUnrecoverableActiveRejected = false;
        try
        {
            direct2dRenderer.DebugSimulateContextLoss();
        }
        catch (const std::exception&)
        {
            deviceLossWithUnrecoverableActiveRejected = true;
        }
        std::printf("[%s] Direct2D surfaces device loss with an unrecoverable active render target\n",
                    deviceLossWithUnrecoverableActiveRejected ? "PASS" : "FAIL");
        passed = passed && deviceLossWithUnrecoverableActiveRejected;
        device.SetContextRecoveryEnabled(true);
        device.SetRenderTarget(nullptr);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D usable after device loss with unrecoverable active target", 0, 0, Color::White);

        // D2D-62: rejecting a stale RT bind while a DIFFERENT, live RT is the active target must
        // leave that original RT active and still draw-able -- not silently fall back to the
        // backbuffer or leave the renderer in a broken state. Render target content is documented
        // to reset to transparent across a device loss regardless (RecreateBitmap), so this checks
        // a fresh control draw after the rejection, not content predating the loss.
        RenderTarget2D liveActiveTarget(device, 2, 2);
        device.SetRenderTarget(&liveActiveTarget);
        device.SetRenderTarget(nullptr); // unbind before the loss so D2D-63's own check does not trip here
        device.SetContextRecoveryEnabled(false);
        RenderTarget2D staleWhileOtherActiveTarget(device, 2, 2);
        direct2dRenderer.DebugRestoreContext();
        device.SetContextRecoveryEnabled(true);
        device.SetRenderTarget(&liveActiveTarget);
        bool staleBindWhileOtherActiveRejected = false;
        try
        {
            device.SetRenderTarget(&staleWhileOtherActiveTarget);
        }
        catch (const std::exception&)
        {
            staleBindWhileOtherActiveRejected = true;
        }
        std::printf("[%s] Direct2D rejects a stale RT bind while a different RT is active\n",
                    staleBindWhileOtherActiveRejected ? "PASS" : "FAIL");
        passed = passed && staleBindWhileOtherActiveRejected;
        const bool originalTargetStillBound = device.GetRenderTargets().size() == 1 &&
            device.GetRenderTargets()[0].getRenderTargetProperty() == &liveActiveTarget;
        std::printf("[%s] Rejected stale RT bind left the original render target bound\n",
                    originalTargetStillBound ? "PASS" : "FAIL");
        passed = passed && originalTargetStillBound;
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        Color controlDrawPixel(0, 0, 0, 0);
        const Rectangle liveTargetTexel(0, 0, 1, 1);
        liveActiveTarget.GetData(0, &liveTargetTexel, &controlDrawPixel, 0, 1);
        const bool controlDrawAccepted = Matches(controlDrawPixel, Color::White);
        std::printf("[%s] Original render target accepts a control draw after rejected stale bind: got=(%d,%d,%d,%d)\n",
                    controlDrawAccepted ? "PASS" : "FAIL",
                    controlDrawPixel.getRProperty(), controlDrawPixel.getGProperty(),
                    controlDrawPixel.getBProperty(), controlDrawPixel.getAProperty());
        passed = passed && controlDrawAccepted;
        device.SetRenderTarget(nullptr);

        // 13. Resize through the public GraphicsDeviceManager path. Direct2D has to recreate
        // its DXGI target lazily for the newly sized SDL client area, then keep the logical
        // NativeBackBuffer viewport/readback contract coherent for subsequent SpriteBatch work.
        manager_->setPreferredBackBufferWidthProperty(40);
        manager_->setPreferredBackBufferHeightProperty(24);
        manager_->ApplyChanges();
        const bool resizedPresentationParameters =
            device.getPresentationParametersProperty().getBackBufferWidthProperty() == 40 &&
            device.getPresentationParametersProperty().getBackBufferHeightProperty() == 24;
        const bool resizedViewport = device.getViewportProperty().getWidthProperty() == 40 &&
            device.getViewportProperty().getHeightProperty() == 24;
        std::printf("[%s] Direct2D resize updates presentation parameters to 40x24\n",
                    resizedPresentationParameters ? "PASS" : "FAIL");
        std::printf("[%s] Direct2D resize updates NativeBackBuffer viewport to 40x24\n",
                    resizedViewport ? "PASS" : "FAIL");
        passed = passed && resizedPresentationParameters && resizedViewport;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D resize draws on recreated swap chain", 1, 1, Color::White);
        check("Direct2D resize preserves resized bounds", 4, 1, Color::Black);

        // 14. Direct2D primitive antialiasing is not an XNA requested-sample-count pipeline.
        // Reject the request rather than silently clamping it to a different resource contract.
        expectRendererRejection("Direct2D RenderTarget2D rejects requested 4x MSAA", [&] {
            RenderTarget2D requestedMsaaTarget(device, 4, 4, false, SurfaceFormat::Color,
                                               DepthFormat::None, 4);
            (void)requestedMsaaTarget;
        });

        // AnisotropicFiltering is now a real Direct2D SpriteBatch capability. Every remaining
        // entry belongs to the intentionally absent 3D/depth/query/custom-effect surface.
        const bool anisotropicCapability =
            device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering);
        std::printf("[%s] Direct2D capability AnisotropicFiltering is supported\n",
                    anisotropicCapability ? "PASS" : "FAIL");
        passed = passed && anisotropicCapability;
        constexpr std::array unsupportedCapabilities{
            std::pair{"ThreeD", CNA::GraphicsCapability::ThreeD},
            std::pair{"DepthStencilBuffer", CNA::GraphicsCapability::DepthStencilBuffer},
            std::pair{"MultiSampleAntiAliasing", CNA::GraphicsCapability::MultiSampleAntiAliasing},
            std::pair{"MultipleRenderTargets", CNA::GraphicsCapability::MultipleRenderTargets},
            std::pair{"WireFrame", CNA::GraphicsCapability::WireFrame},
            std::pair{"OcclusionQuery", CNA::GraphicsCapability::OcclusionQuery},
            std::pair{"CustomEffects", CNA::GraphicsCapability::CustomEffects},
            std::pair{"Texture3D", CNA::GraphicsCapability::Texture3D},
            std::pair{"MultiStreamVertexInput", CNA::GraphicsCapability::MultiStreamVertexInput},
            std::pair{"Instancing", CNA::GraphicsCapability::Instancing},
            std::pair{"StencilBuffer", CNA::GraphicsCapability::StencilBuffer},
            std::pair{"AdditiveBlending", CNA::GraphicsCapability::AdditiveBlending},
        };
        for (const auto& [name, capability] : unsupportedCapabilities)
        {
            const bool correctlyRejected = !device.SupportsCapability(capability);
            std::printf("[%s] Direct2D capability %s is unsupported\n",
                        correctlyRejected ? "PASS" : "FAIL", name);
            passed = passed && correctlyRejected;
        }

        // 15. Unsupported output-mask and blend-factor state must fail explicitly instead of
        // silently rendering an approximation. Standard SpriteBatch presets above still use the
        // all-channel/all-sample defaults and therefore remain valid.
        const auto expectNamedBlendRejection = [&](const char* label, const auto& action) {
            bool rejected = false;
            try
            {
                action();
            }
            catch (const std::exception&)
            {
                rejected = true;
            }
            std::printf("[%s] %s\n", rejected ? "PASS" : "FAIL", label);
            passed = passed && rejected;
        };
        BlendState maskedChannels = BlendState::Opaque;
        maskedChannels.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        expectNamedBlendRejection("Direct2D rejects ColorWriteChannels mask", [&] {
            device.setBlendStateProperty(maskedChannels);
        });
        BlendState maskedSamples = BlendState::Opaque;
        maskedSamples.setMultiSampleMaskProperty(0x1);
        expectNamedBlendRejection("Direct2D rejects MultiSampleMask", [&] {
            device.setBlendStateProperty(maskedSamples);
        });
        BlendState arbitraryFactors = BlendState::Opaque;
        arbitraryFactors.setColorSourceBlendProperty(Blend::DestinationColor);
        expectNamedBlendRejection("Direct2D rejects general blend factors", [&] {
            device.setBlendStateProperty(arbitraryFactors);
        });
        expectNamedBlendRejection("Direct2D rejects XNA BlendState::Additive (SourceAlpha/One)", [&] {
            device.setBlendStateProperty(BlendState::Additive);
        });
        // D2D-135: ApplyBlendState and the embedded BlendFactor validation are one transaction.
        // Keep a supported state active, reject a different candidate, then bypass SpriteBatch's
        // front-end state setup for one draw so the pixel proves the native renderer did not
        // partially publish the rejected candidate.
        device.Clear(blendBackground);
        device.setBlendStateProperty(BlendState::NonPremultiplied);
        BlendState embeddedBlendFactor = BlendState::AlphaBlend;
        embeddedBlendFactor.setBlendFactorProperty(Color(64, 255, 255, 255));
        expectNamedBlendRejection("Direct2D rejects BlendState.BlendFactor", [&] {
            device.setBlendStateProperty(embeddedBlendFactor);
        });
        const bool blendCachePreserved =
            device.getBlendStateProperty().getColorSourceBlendProperty() == Blend::SourceAlpha &&
            device.getBlendStateProperty().getColorDestinationBlendProperty() ==
                Blend::InverseSourceAlpha;
        std::printf("[%s] rejected BlendState preserves the public state cache\n",
                    blendCachePreserved ? "PASS" : "FAIL");
        passed = passed && blendCachePreserved;
        auto transactionalBatch = device.GetRenderer().CreateSpriteBatch();
        transactionalBatch->SetSamplerFilter(1);
        transactionalBatch->Begin();
        transactionalBatch->Draw(straightHalfRed.GetRenderer(), Rectangle(0, 0, 4, 4),
                                  Rectangle(0, 0, 1, 1), Color::White);
        transactionalBatch->End();
        check("rejected BlendState preserves Direct2D blend pixels", 1, 1,
              halfRedOverBackground);
        device.setBlendStateProperty(BlendState::Opaque);
        expectNamedBlendRejection("Direct2D rejects GraphicsDevice.BlendFactor", [&] {
            device.setBlendFactorProperty(Color(64, 255, 255, 255));
        });
        device.setBlendFactorProperty(Color::White);

        // D2D-99: GraphicsDevice's constructor applies DepthStencilState.Default once before game
        // code can run. The renderer permits only that bootstrap write; subsequent depth or stencil
        // requests must reject because the advertised 2D surface has no such buffer. None remains
        // the supported state.
        DepthStencilState depthEnabledState = DepthStencilState::Default;
        bool depthDefaultRejected = false;
        try
        {
            device.setDepthStencilStateProperty(depthEnabledState);
        }
        catch (const std::exception&)
        {
            depthDefaultRejected = true;
        }
        std::printf("[%s] Direct2D rejects depth-enabled state after device bootstrap\n",
                    depthDefaultRejected ? "PASS" : "FAIL");
        passed = passed && depthDefaultRejected;
        bool depthNoneAccepted = true;
        try
        {
            device.setDepthStencilStateProperty(DepthStencilState::None);
        }
        catch (const std::exception&)
        {
            depthNoneAccepted = false;
        }
        std::printf("[%s] Direct2D accepts DepthStencilState.None\n",
                    depthNoneAccepted ? "PASS" : "FAIL");
        passed = passed && depthNoneAccepted;
        DepthStencilState stencilEnabledState = DepthStencilState::None;
        stencilEnabledState.setStencilEnableProperty(true);
        expectNamedBlendRejection("Direct2D rejects DepthStencilState.StencilEnable", [&] {
            device.setDepthStencilStateProperty(stencilEnabledState);
        });
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D usable after rejected StencilEnable", 0, 0, Color::White);

        // D2D-100: Direct2D has no wireframe rasterization pipeline and no depth buffer to bias
        // against. CullMode is left unchecked (see ApplyRasterizerState's own comment) since
        // GraphicsDevice's constructor unconditionally applies RasterizerState.CullCounterClockwise
        // before any game code runs; FillMode/DepthBias/SlopeScaleDepthBias all stay at their
        // inert defaults (Solid, 0, 0) in every named preset, so rejecting real deviations here
        // can never trip on that constructor-forced default either.
        RasterizerState wireFrameState = RasterizerState::CullNone;
        wireFrameState.setFillModeProperty(FillMode::WireFrame);
        expectNamedBlendRejection("Direct2D rejects RasterizerState.FillMode.WireFrame", [&] {
            device.setRasterizerStateProperty(wireFrameState);
        });
        RasterizerState depthBiasState = RasterizerState::CullNone;
        depthBiasState.setDepthBiasProperty(0.01f);
        expectNamedBlendRejection("Direct2D rejects RasterizerState.DepthBias", [&] {
            device.setRasterizerStateProperty(depthBiasState);
        });
        RasterizerState slopeBiasState = RasterizerState::CullNone;
        slopeBiasState.setSlopeScaleDepthBiasProperty(0.5f);
        expectNamedBlendRejection("Direct2D rejects RasterizerState.SlopeScaleDepthBias", [&] {
            device.setRasterizerStateProperty(slopeBiasState);
        });
        device.setRasterizerStateProperty(scissorDisabled);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D usable after rejected RasterizerState deviations", 0, 0, Color::White);

        // 16. Presentation transforms use the physical HWND client size, rather than assuming
        // GraphicsDeviceManager's virtual dimensions. The default scene now lives in a logical
        // Direct2D target, so these non-1:1 modes retain exact logical GetBackBufferData as well.
        auto& renderer = device.GetRenderer();
        SDL_Window* const window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
        bool emptyFrameResizeAccepted = true;
        try
        {
            renderer.Present(); // close the prior frame before the resize-under-no-draw probe
            emptyFrameResizeAccepted = SDL_SetWindowSize(window, 80, 24) && SDL_SyncWindow(window);
            if (emptyFrameResizeAccepted)
                renderer.Present(); // no Clear/Draw: Present itself must realize the new client size
        }
        catch (const std::exception&)
        {
            emptyFrameResizeAccepted = false;
        }
        int physicalWidth = 0;
        int physicalHeight = 0;
        emptyFrameResizeAccepted = emptyFrameResizeAccepted &&
            SDL_GetWindowSize(window, &physicalWidth, &physicalHeight) &&
            physicalWidth == 80 && physicalHeight == 24;
        std::printf("[%s] Direct2D empty-frame Present realizes an 80x24 SDL client resize\n",
                    emptyFrameResizeAccepted ? "PASS" : "FAIL");
        passed = passed && emptyFrameResizeAccepted;
        renderer.SetVirtualResolution(48, 32);
        // Not named `near`: <windows.h> (reached through the Direct2D renderer header above)
        // still defines the legacy empty `near`/`far` macros.
        const auto nearlyEqual = [](float actual, float expected) {
            return std::abs(actual - expected) <= 0.05f;
        };
        const auto verifyPresentationTransform = [&](PresentationMode mode, const char* label,
                                                     float scaleX, float scaleY,
                                                     float offsetX, float offsetY) {
            renderer.SetPresentationMode(static_cast<int>(mode));
            device.Clear(Color::Black); // realizes the resized DXGI backbuffer before querying transforms
            float windowX = 0.0f;
            float windowY = 0.0f;
            float logicalX = 0.0f;
            float logicalY = 0.0f;
            const bool transformedOut = renderer.TransformLogicalToWindow(
                12.0f, 8.0f, windowX, windowY);
            const bool transformedBack = transformedOut && renderer.TransformWindowToLogical(
                windowX, windowY, logicalX, logicalY);
            const bool matches = transformedBack && nearlyEqual(windowX, 12.0f * scaleX + offsetX) &&
                nearlyEqual(windowY, 8.0f * scaleY + offsetY) &&
                nearlyEqual(logicalX, 12.0f) && nearlyEqual(logicalY, 8.0f);
            std::printf("[%s] Direct2D presentation %s uses physical client transform\n",
                        matches ? "PASS" : "FAIL", label);
            passed = passed && matches;
        };
        const float physicalWidthF = static_cast<float>(physicalWidth);
        const float physicalHeightF = static_cast<float>(physicalHeight);
        const float virtualWidthF = 48.0f;
        const float virtualHeightF = 32.0f;
        const float fitScale = std::min(physicalWidthF / virtualWidthF, physicalHeightF / virtualHeightF);
        const float fillScale = std::max(physicalWidthF / virtualWidthF, physicalHeightF / virtualHeightF);
        verifyPresentationTransform(PresentationMode::Letterbox, "Letterbox", fitScale, fitScale,
                                    (physicalWidthF - virtualWidthF * fitScale) * 0.5f,
                                    (physicalHeightF - virtualHeightF * fitScale) * 0.5f);
        verifyPresentationTransform(PresentationMode::Overscan, "Overscan", fillScale, fillScale,
                                    (physicalWidthF - virtualWidthF * fillScale) * 0.5f,
                                    (physicalHeightF - virtualHeightF * fillScale) * 0.5f);
        verifyPresentationTransform(PresentationMode::Stretch, "Stretch",
                                    physicalWidthF / virtualWidthF, physicalHeightF / virtualHeightF,
                                    0.0f, 0.0f);
        verifyPresentationTransform(PresentationMode::NativeBackBuffer, "NativeBackBuffer",
                                    1.0f, 1.0f, 0.0f, 0.0f);
        verifyPresentationTransform(PresentationMode::FixedHeightDynamicWidth, "FixedHeightDynamicWidth",
                                    physicalHeightF / virtualHeightF, physicalHeightF / virtualHeightF,
                                    0.0f, 0.0f);

        // D2D-46: NativeBackBuffer applies no virtual-resolution scaling at all, so it must draw
        // across the entire physical backbuffer (80x24 here) rather than only a virtual-size
        // (48x32) corner of it left uncovered by a stale identity transform. This probes the
        // renderer directly (GetViewportSize/ReadBackbuffer): GraphicsDevice::GetBackBufferData
        // validates against PresentationParameters, which this section intentionally never
        // resizes through GraphicsDeviceManager (it drives the renderer's SDL window/virtual
        // resolution directly to isolate the renderer's own presentation-transform contract).
        renderer.SetPresentationMode(static_cast<int>(PresentationMode::NativeBackBuffer));
        int nativeViewportWidth = 0;
        int nativeViewportHeight = 0;
        renderer.GetViewportSize(nativeViewportWidth, nativeViewportHeight);
        const bool nativeViewportMatchesPhysical =
            nativeViewportWidth == physicalWidth && nativeViewportHeight == physicalHeight;
        std::printf("[%s] Direct2D NativeBackBuffer viewport matches physical size: got=(%d,%d), expected=(%d,%d)\n",
                    nativeViewportMatchesPhysical ? "PASS" : "FAIL",
                    nativeViewportWidth, nativeViewportHeight, physicalWidth, physicalHeight);
        passed = passed && nativeViewportMatchesPhysical;

        // Earlier sections left the renderer's own SpriteBatch-local viewport clip set to a small
        // sub-region; reset it to the full physical target so it cannot hide this probe's marker.
        renderer.SetViewport(0, 0, physicalWidth, physicalHeight, 0.0f, 1.0f);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(physicalWidth - 4, physicalHeight - 4, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        std::array<uint8_t, 4> farCornerPixel{};
        renderer.ReadBackbuffer(physicalWidth - 1, physicalHeight - 1, 1, 1, farCornerPixel.data());
        const bool farCornerMatches = farCornerPixel[0] == 255 && farCornerPixel[1] == 255 &&
            farCornerPixel[2] == 255 && farCornerPixel[3] == 255;
        std::printf("[%s] Direct2D NativeBackBuffer covers full physical backbuffer far corner: got=(%d,%d,%d,%d)\n",
                    farCornerMatches ? "PASS" : "FAIL", farCornerPixel[0], farCornerPixel[1],
                    farCornerPixel[2], farCornerPixel[3]);
        passed = passed && farCornerMatches;
        std::array<uint8_t, 4> nearCornerPixel{};
        renderer.ReadBackbuffer(1, 1, 1, 1, nearCornerPixel.data());
        const bool nearCornerMatches = nearCornerPixel[0] == 0 && nearCornerPixel[1] == 0 &&
            nearCornerPixel[2] == 0 && nearCornerPixel[3] == 255;
        std::printf("[%s] Direct2D NativeBackBuffer keeps other physical pixels background: got=(%d,%d,%d,%d)\n",
                    nearCornerMatches ? "PASS" : "FAIL", nearCornerPixel[0], nearCornerPixel[1],
                    nearCornerPixel[2], nearCornerPixel[3]);
        passed = passed && nearCornerMatches;

        renderer.SetPresentationMode(static_cast<int>(PresentationMode::Letterbox));
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(2, 2, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D non-1:1 presentation logical readback", 3, 3, Color::White);
        check("Direct2D non-1:1 presentation logical background", 8, 3, Color::Black);

        // D2D-53: an extreme physical/virtual aspect ratio under FixedHeightDynamicWidth must
        // never collapse the logical framebuffer to zero width.
        // lround(physicalWidth * virtualHeight_ / physicalHeight) rounds down to 0 well before
        // either input actually reaches zero (e.g. lround(8.0 * 8 / 200) == 0).
        const bool extremeResizeAccepted = SDL_SetWindowSize(window, 8, 200) && SDL_SyncWindow(window);
        std::printf("[%s] SDL accepts Direct2D extreme-aspect resize request\n",
                    extremeResizeAccepted ? "PASS" : "FAIL");
        passed = passed && extremeResizeAccepted;
        renderer.SetVirtualResolution(8, 8);
        renderer.SetPresentationMode(static_cast<int>(PresentationMode::FixedHeightDynamicWidth));
        int extremeViewportWidth = 0;
        int extremeViewportHeight = 0;
        renderer.GetViewportSize(extremeViewportWidth, extremeViewportHeight);
        const bool extremeViewportPositive = extremeViewportWidth >= 1 && extremeViewportHeight >= 1;
        std::printf("[%s] Direct2D FixedHeightDynamicWidth keeps a >=1x1 logical framebuffer for "
                    "an extreme aspect ratio: got=(%d,%d)\n",
                    extremeViewportPositive ? "PASS" : "FAIL",
                    extremeViewportWidth, extremeViewportHeight);
        passed = passed && extremeViewportPositive;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        std::array<uint8_t, 4> extremePixel{};
        renderer.ReadBackbuffer(0, 0, 1, 1, extremePixel.data());
        const bool extremePixelValid = extremePixel[3] == 255;
        std::printf("[%s] Direct2D usable with an extreme FixedHeightDynamicWidth aspect ratio: "
                    "got=(%d,%d,%d,%d)\n",
                    extremePixelValid ? "PASS" : "FAIL",
                    extremePixel[0], extremePixel[1], extremePixel[2], extremePixel[3]);
        passed = passed && extremePixelValid;

        // D2D-50/D2D-51/D2D-52: an INT_MAX virtual resolution asks for a logical bitmap far
        // beyond any device's maximum bitmap size, so CreateBitmap must fail -- but that failure
        // must not corrupt virtualWidth_/virtualHeight_/presentationMode_, nor leave a null
        // logical target that would crash the very next frame. CreateLogicalTargetBitmap creates
        // the replacement before releasing the current one, so the prior logical target survives
        // a failed resize untouched.
        int viewportBeforeFault[2] = {0, 0};
        renderer.GetViewportSize(viewportBeforeFault[0], viewportBeforeFault[1]);
        bool virtualResolutionFaultRejected = false;
        try
        {
            renderer.SetVirtualResolution(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
        }
        catch (const std::exception&)
        {
            virtualResolutionFaultRejected = true;
        }
        std::printf("[%s] Direct2D rejects an INT_MAX SetVirtualResolution\n",
                    virtualResolutionFaultRejected ? "PASS" : "FAIL");
        passed = passed && virtualResolutionFaultRejected;
        int viewportAfterFault[2] = {0, 0};
        renderer.GetViewportSize(viewportAfterFault[0], viewportAfterFault[1]);
        const bool viewportRestoredAfterFault = viewportAfterFault[0] == viewportBeforeFault[0] &&
            viewportAfterFault[1] == viewportBeforeFault[1];
        std::printf("[%s] Rejected SetVirtualResolution restored the previous viewport: got=(%d,%d), expected=(%d,%d)\n",
                    viewportRestoredAfterFault ? "PASS" : "FAIL",
                    viewportAfterFault[0], viewportAfterFault[1],
                    viewportBeforeFault[0], viewportBeforeFault[1]);
        passed = passed && viewportRestoredAfterFault;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Direct2D usable after rejected extreme SetVirtualResolution", 0, 0, Color::White);

        std::printf("[%s] Direct2D 2D parity baseline\n", passed ? "PASS" : "FAIL");
        result_ = passed ? 0 : 1;
        Exit();
    }

private:
    std::vector<std::string> deviceEventOrder_;
    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<Texture2D> twoTexels_;
    std::unique_ptr<Texture2D> twoRows_;
    bool done_ = false;
    int result_ = 1;
};

int main()
{
    Direct2D2DParityTest test;
    test.Run();
    return test.Result();
}
