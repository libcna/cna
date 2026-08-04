// SPDX-License-Identifier: MS-PL
// D2D-1: public-API pixel baseline for the Direct2D 2D-only backend.
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

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

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

namespace
{
    [[nodiscard]] bool Matches(const Color& actual, const Color& expected, int tolerance = 4)
    {
        return std::abs(static_cast<int>(actual.getRProperty()) - expected.getRProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getGProperty()) - expected.getGProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getBProperty()) - expected.getBProperty()) <= tolerance &&
               std::abs(static_cast<int>(actual.getAProperty()) - expected.getAProperty()) <= tolerance;
    }
}

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
        // WineD3D currently ignores Direct2D's non-source-over DrawImage composite modes
        // (`PLUS` and `BOUNDED_SOURCE_COPY`). Native Windows retains these exact blend probes.
        const bool verifyAdvancedBlend =
            std::getenv("CNA_DIRECT2D_SKIP_ADVANCED_BLEND") == nullptr;

        const auto check = [&](const char* label, int x, int y, const Color& expected) {
            Color actual(0, 0, 0, 0);
            const Rectangle region(x, y, 1, 1);
            device.GetBackBufferData(&region, &actual, 0, 1);
            const bool matches = Matches(actual, expected);
            std::printf("[%s] %s: got=(%d,%d,%d,%d), expected=(%d,%d,%d,%d)\n",
                        matches ? "PASS" : "FAIL", label,
                        actual.getRProperty(), actual.getGProperty(), actual.getBProperty(), actual.getAProperty(),
                        expected.getRProperty(), expected.getGProperty(), expected.getBProperty(), expected.getAProperty());
            passed = passed && matches;
        };

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

        // 4. Render into an 8x8 target, unbind, then sample it as a Texture2D. This exercises
        // the sibling ITextureBackend path instead of assuming every SpriteBatch source is a
        // plain Direct2DTextureBackend.
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

        // RenderTarget2D has no CPU shadow. This must exercise Direct2DRenderTargetBackend's
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

        // 9. Direct2D's three primitive blends cover CNA's four standard SpriteBatch presets.
        // AlphaBlend consumes premultiplied texture data, while NonPremultiplied must
        // premultiply a straight-alpha source before Direct2D source-over. Additive and Opaque
        // exercise their own native primitive blend modes; the RT case proves the same behavior
        // while an off-screen bitmap is the destination.
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
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, &scissorDisabled);
            sprites_->Draw(premultipliedHalfRed, Rectangle(16, 0, 4, 4),
                           Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(premultipliedHalfRed, Rectangle(24, 0, 4, 4),
                           Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, &scissorDisabled);
            sprites_->Draw(premultipliedHalfRed, Rectangle(16, 4, 4, 4),
                           Rectangle(0, 0, 1, 1), halfAlphaTint);
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
            // D2D-35: BlendState.Additive is SourceAlpha/One, not Direct2D's unconditional
            // One/One PLUS. Pre-multiplying the CPU-generated source by its own resulting alpha
            // (see MakeSpritePixels) makes PLUS reproduce that factor: for the untinted
            // (128,0,0,128) source, fragA=128 folds RGB to (64,0,0) and alpha to 64 before adding
            // the (20,40,80,255) background -- (84,40,80,255), not the old (148,40,80,255) that
            // used the raw unfolded source directly.
            check("Additive premultiplied texture", 17, 1, Color(84, 40, 80, 255));
            check("Opaque copies premultiplied source", 25, 1, Color(128, 0, 0, 128));
            // With halfAlphaTint (Color.A=128), fragA folds to 64 first (128*128/255), then the
            // Additive fold multiplies RGB/A by that smaller fragA again: (32,0,0,16) + background
            // == (52,40,80,255), not the old (84,40,80,255).
            check("Additive texture Color.A", 17, 5, Color(52, 40, 80, 255));
            check("Opaque texture Color.A", 25, 5, Color(128, 0, 0, 64));
        }
        else
        {
            std::printf("[SKIP] Additive/Opaque composite modes (Wine Direct2D ignores PLUS/BOUNDED_SOURCE_COPY)\n");
        }

        // D2D-35: unlike the two checks above, this does not depend on Direct2D's PLUS composite
        // mode actually reaching the GPU as PLUS. MakeSpritePixels' Additive fold runs on the CPU
        // regardless of platform, and any sane compositing formula (Direct2D's real PLUS, or
        // whatever WineD3D substitutes for a composite mode it ignores) reduces to "= source" over
        // a fully transparent destination, since the destination-side term is multiplied by zero
        // either way. This isolates and verifies the CPU-side SourceAlpha fold itself on Wine.
        RenderTarget2D additiveFoldTarget(device, 1, 1);
        device.SetRenderTarget(&additiveFoldTarget);
        device.Clear(Color::Transparent);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        device.SetRenderTarget(nullptr);
        Color additiveFoldPixel(0, 0, 0, 0);
        const Rectangle additiveFoldTexel(0, 0, 1, 1);
        additiveFoldTarget.GetData(0, &additiveFoldTexel, &additiveFoldPixel, 0, 1);
        const bool additiveFoldMatches = Matches(additiveFoldPixel, Color(64, 0, 0, 64));
        std::printf("[%s] Additive SourceAlpha fold over transparent destination: got=(%d,%d,%d,%d), expected=(64,0,0,64)\n",
                    additiveFoldMatches ? "PASS" : "FAIL",
                    additiveFoldPixel.getRProperty(), additiveFoldPixel.getGProperty(),
                    additiveFoldPixel.getBProperty(), additiveFoldPixel.getAProperty());
        passed = passed && additiveFoldMatches;

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
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Additive RenderTarget2D source", 1, 5, Color(148, 40, 80, 255));

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
            std::printf("[SKIP] RenderTarget2D source Color.A/NonPremultiplied (Wine Direct2D lacks ColorMatrix)\n");
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

        // Generating a mip while the target remains bound ends the Direct2D recording interval.
        // A subsequent draw in the same bind interval has to invalidate the chain again; otherwise
        // level one incorrectly keeps the red value read before the green draw below.
        RenderTarget2D dirtyMipTarget(device, 4, 4, true, SurfaceFormat::Color, DepthFormat::None);
        device.SetRenderTarget(&dirtyMipTarget);
        device.Clear(Color(255, 0, 0, 255));
        Color dirtyMipBefore(0, 0, 0, 0);
        const Rectangle dirtyMipTexel(0, 0, 1, 1);
        dirtyMipTarget.GetData(1, &dirtyMipTexel, &dirtyMipBefore, 0, 1);
        const bool dirtyMipInitialReadMatches = Matches(dirtyMipBefore, Color(255, 0, 0, 255));
        std::printf("[%s] RenderTarget2D active mip read before a later draw\n",
                    dirtyMipInitialReadMatches ? "PASS" : "FAIL");
        passed = passed && dirtyMipInitialReadMatches;
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color(0, 255, 0, 255));
        sprites_->End();
        device.SetRenderTarget(nullptr);
        Color dirtyMipAfter(0, 0, 0, 0);
        dirtyMipTarget.GetData(1, &dirtyMipTexel, &dirtyMipAfter, 0, 1);
        const bool dirtyMipRegenerated = Matches(dirtyMipAfter, Color(0, 255, 0, 255));
        std::printf("[%s] RenderTarget2D regenerates mip after later active-target draw\n",
                    dirtyMipRegenerated ? "PASS" : "FAIL");
        passed = passed && dirtyMipRegenerated;

        // 11. RenderTarget2D(mipMap=true) owns the same complete Direct2D bitmap chain, generated
        // from level zero when the target is unbound. The solid source makes the required 2x2/1x1
        // output independent of Direct2D's exact linear downsampling kernel, while the three
        // destination sizes exercise SpriteBatch's selected level 0/1/2 path.
        const Color renderTargetMipColor(17, 153, 221, 255);
        RenderTarget2D mipRenderTarget(device, 4, 4, true, SurfaceFormat::Color, DepthFormat::None);
        device.SetRenderTarget(&mipRenderTarget);
        device.Clear(renderTargetMipColor);
        device.SetRenderTarget(nullptr);
        Color mipLevelOne(0, 0, 0, 0);
        Color mipLevelTwo(0, 0, 0, 0);
        const Rectangle oneMipTexel(0, 0, 1, 1);
        mipRenderTarget.GetData(1, &oneMipTexel, &mipLevelOne, 0, 1);
        mipRenderTarget.GetData(2, &oneMipTexel, &mipLevelTwo, 0, 1);
        const bool mipOneMatches = Matches(mipLevelOne, renderTargetMipColor);
        const bool mipTwoMatches = Matches(mipLevelTwo, renderTargetMipColor);
        std::printf("[%s] RenderTarget2D mip GetData level 1\n", mipOneMatches ? "PASS" : "FAIL");
        std::printf("[%s] RenderTarget2D mip GetData level 2\n", mipTwoMatches ? "PASS" : "FAIL");
        passed = passed && mipOneMatches && mipTwoMatches;
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(mipRenderTarget, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->Draw(mipRenderTarget, Rectangle(8, 0, 2, 2), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->Draw(mipRenderTarget, Rectangle(12, 0, 1, 1), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("RenderTarget2D mip level 0", 1, 1, renderTargetMipColor);
        check("RenderTarget2D mip level 1", 8, 0, renderTargetMipColor);
        check("RenderTarget2D mip level 2", 12, 0, renderTargetMipColor);

        // D2D-29: RenderTarget2D inherits Texture2D::SetData. Lower levels must update the real
        // per-level Direct2D bitmap, and a partial update must preserve GPU pixels outside its
        // rectangle without leaving a CPU shadow that can become stale after later rendering.
        RenderTarget2D authoredMipTarget(device, 4, 4, true, SurfaceFormat::Color, DepthFormat::None);
        std::vector<Color> authoredLevelOne(4, Color(0, 255, 0, 255));
        authoredMipTarget.SetData(1, nullptr, authoredLevelOne.data(), 0,
                                  static_cast<int>(authoredLevelOne.size()));
        const Rectangle firstAuthoredTexel(0, 0, 1, 1);
        const Color authoredBlue(0, 0, 255, 255);
        authoredMipTarget.SetData(1, &firstAuthoredTexel, &authoredBlue, 0, 1);
        std::vector<Color> authoredReadback(4, Color::Transparent);
        authoredMipTarget.GetData(1, nullptr, authoredReadback.data(), 0,
                                  static_cast<int>(authoredReadback.size()));
        const bool authoredMipUploadMatches = Matches(authoredReadback[0], authoredBlue) &&
            Matches(authoredReadback[1], Color(0, 255, 0, 255)) &&
            Matches(authoredReadback[2], Color(0, 255, 0, 255)) &&
            Matches(authoredReadback[3], Color(0, 255, 0, 255));
        std::printf("[%s] RenderTarget2D SetData(level 1) updates GPU and preserves partial neighbors\n",
                    authoredMipUploadMatches ? "PASS" : "FAIL");
        passed = passed && authoredMipUploadMatches;

        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(authoredMipTarget, Rectangle(0, 0, 2, 2), Rectangle(0, 0, 4, 4), Color::White);
        sprites_->End();
        check("RenderTarget2D authored mip SpriteBatch sampling", 0, 0, authoredBlue);

        device.SetRenderTarget(&authoredMipTarget);
        device.Clear(Color(255, 0, 0, 255));
        device.SetRenderTarget(nullptr);
        Color regeneratedAuthoredMip(0, 0, 0, 0);
        authoredMipTarget.GetData(1, &firstAuthoredTexel, &regeneratedAuthoredMip, 0, 1);
        const bool authoredMipInvalidated = Matches(regeneratedAuthoredMip, Color(255, 0, 0, 255));
        std::printf("[%s] RenderTarget2D level-zero draw invalidates authored lower mip\n",
                    authoredMipInvalidated ? "PASS" : "FAIL");
        passed = passed && authoredMipInvalidated;

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
        auto& direct2dBackend = device.GetBackend();

        // Backend-entry-point hardening: these calls are normally normalized by GraphicsDevice,
        // but the concrete backend must still reject invalid input without corrupting the next
        // frame. This is deliberately separate from the public BlendState rejection checks below.
        const auto expectBackendRejection = [&](const char* label, const auto& action) {
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
        expectBackendRejection("Direct2D rejects null MRT array with count one", [&] {
            direct2dBackend.SetRenderTargets(nullptr, 1);
        });
        expectBackendRejection("Direct2D rejects negative MRT count", [&] {
            direct2dBackend.SetRenderTargets(nullptr, -1);
        });
        expectBackendRejection("Direct2D rejects negative scissor dimensions", [&] {
            direct2dBackend.SetScissorRect(0, 0, -1, 1);
        });
        expectBackendRejection("Direct2D rejects negative viewport dimensions", [&] {
            direct2dBackend.SetViewport(0, 0, 1, -1, 0.0f, 1.0f);
        });
        expectBackendRejection("Direct2D rejects unknown presentation mode", [&] {
            direct2dBackend.SetPresentationMode(99);
        });

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

        // The public readback validation must widen endpoint arithmetic before addition. These
        // rectangles used to reach signed-overflow expressions before the backend's own checks.
        Color rejectedReadback(0, 0, 0, 0);
        const Rectangle overflowingReadbackX(std::numeric_limits<int>::max() - 2, 0, 8, 1);
        expectBackendRejection("GetBackBufferData rejects overflowing X endpoint", [&] {
            device.GetBackBufferData(&overflowingReadbackX, &rejectedReadback, 0, 1);
        });
        const Rectangle overflowingReadbackY(0, std::numeric_limits<int>::max() - 2, 1, 8);
        expectBackendRejection("GetBackBufferData rejects overflowing Y endpoint", [&] {
            device.GetBackBufferData(&overflowingReadbackY, &rejectedReadback, 0, 1);
        });
        const Rectangle oneReadbackPixel(0, 0, 1, 1);
        expectBackendRejection("GetBackBufferData rejects negative startIndex", [&] {
            device.GetBackBufferData(&oneReadbackPixel, &rejectedReadback, -1, 1);
        });

        // Disposed and foreign targets are rejected before native state changes. Exercise both
        // public binding overloads and the backend entry point, then continue rendering on the
        // original device to prove each failure was transactional.
        RenderTarget2D disposedTarget(device, 2, 2);
        disposedTarget.Dispose();
        expectBackendRejection("SetRenderTarget rejects a disposed target", [&] {
            device.SetRenderTarget(&disposedTarget);
        });
        expectBackendRejection("SetRenderTargets rejects a disposed target", [&] {
            device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&disposedTarget))});
        });
        {
            PresentationParameters foreignPresentation;
            foreignPresentation.setBackBufferWidthProperty(8);
            foreignPresentation.setBackBufferHeightProperty(8);
            GraphicsDevice foreignDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                                          GraphicsProfile::Reach, foreignPresentation);
            RenderTarget2D foreignTarget(foreignDevice, 2, 2);
            expectBackendRejection("SetRenderTarget rejects a foreign GraphicsDevice target", [&] {
                device.SetRenderTarget(&foreignTarget);
            });
            expectBackendRejection("SetRenderTargets rejects a foreign GraphicsDevice target", [&] {
                device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&foreignTarget))});
            });
            expectBackendRejection("Direct2D backend rejects a foreign device target", [&] {
                direct2dBackend.SetRenderTarget2D(foreignTarget.GetRenderTargetBackend());
            });
        }
        const bool targetFailuresKeptBackbuffer = device.GetRenderTargets().empty();
        std::printf("[%s] rejected target bindings leave the Direct2D backbuffer active\n",
                    targetFailuresKeptBackbuffer ? "PASS" : "FAIL");
        passed = passed && targetFailuresKeptBackbuffer;

        // D2D-64: a Texture2D/RenderTarget2D that outlives the C++ scope of its GraphicsDevice
        // must not dereference a dangling backend/device pointer in its own later destructor.
        // GraphicsDevice::Dispose() proactively disposes every GraphicsResource it still tracks
        // (which resets each one's own backend_ while the owning Direct2DGraphicsBackend is still
        // alive) before tearing down its own backend, so this is safe by construction -- verified
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
        // considers bound, but a target bound directly through the backend (bypassing
        // GraphicsDevice::SetRenderTarget, as CNA's device-agnostic ITextureBackend contract
        // permits) is invisible to that guard. Destroying it while still the backend's active
        // target must not let ~Direct2DRenderTargetBackend's unbind path escape the destructor;
        // this exercises that edge case and proves the device stays usable afterward.
        {
            RenderTarget2D directBoundTarget(device, 2, 2);
            directBoundTarget.GetRenderTargetBackend()->BindAsRenderTarget();
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

        direct2dBackend.DebugSimulateContextLoss();
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
        direct2dBackend.DebugRestoreContext();
        bool staleResourceRejected = false;
        try
        {
            // Immediate mode reaches the backend synchronously. This makes the probe entirely
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
        direct2dBackend.DebugSimulateContextLoss();
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
                direct2dBackend.DebugSimulateContextLoss();
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
            std::vector<std::string> eventOrder;
            device.DeviceLost += [&](System::Object*, const System::EventArgs&) { eventOrder.emplace_back("Lost"); };
            device.DeviceResetting += [&](System::Object*, const System::EventArgs&) { eventOrder.emplace_back("Resetting"); };
            device.DeviceReset += [&](System::Object*, const System::EventArgs&) { eventOrder.emplace_back("Reset"); };

            const bool statusStartedNormal =
                device.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal;
            std::printf("[%s] Direct2D GraphicsDeviceStatus starts Normal\n", statusStartedNormal ? "PASS" : "FAIL");
            passed = passed && statusStartedNormal;

            direct2dBackend.DebugSimulateContextLoss();

            const bool orderCorrect = eventOrder.size() == 3 &&
                eventOrder[0] == "Lost" && eventOrder[1] == "Resetting" && eventOrder[2] == "Reset";
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

        // D2D-63: device loss while an unrecoverable render target (created with recovery
        // disabled) is the ACTIVE target must not silently leave GraphicsDevice's public binding
        // out of sync with the backend's actual current target -- it now surfaces loudly instead
        // of quietly redirecting later Clear/Draw calls to the backbuffer.
        device.SetContextRecoveryEnabled(false);
        RenderTarget2D unrecoverableActiveTarget(device, 2, 2);
        device.SetRenderTarget(&unrecoverableActiveTarget);
        device.Clear(Color(11, 22, 33, 255));
        bool deviceLossWithUnrecoverableActiveRejected = false;
        try
        {
            direct2dBackend.DebugSimulateContextLoss();
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
        // backbuffer or leave the backend in a broken state. Render target content is documented
        // to reset to transparent across a device loss regardless (RecreateBitmap), so this checks
        // a fresh control draw after the rejection, not content predating the loss.
        RenderTarget2D liveActiveTarget(device, 2, 2);
        device.SetRenderTarget(&liveActiveTarget);
        device.SetRenderTarget(nullptr); // unbind before the loss so D2D-63's own check does not trip here
        device.SetContextRecoveryEnabled(false);
        RenderTarget2D staleWhileOtherActiveTarget(device, 2, 2);
        direct2dBackend.DebugRestoreContext();
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

        // 14. Direct2D primitive antialiasing is not a requested sample-count pipeline. The
        // backend deliberately clamps both the swap chain and every 2D render target to zero
        // samples instead of quietly introducing a D3D11 multisample resolve pass.
        RenderTarget2D requestedMsaaTarget(device, 4, 4, false, SurfaceFormat::Color,
                                           DepthFormat::None, 4);
        const bool msaaTargetCorrectlyRejected = requestedMsaaTarget.getMultiSampleCountProperty() == 0;
        std::printf("[%s] Direct2D RenderTarget2D clamps requested 4x MSAA to 0\n",
                    msaaTargetCorrectlyRejected ? "PASS" : "FAIL");
        passed = passed && msaaTargetCorrectlyRejected;

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
        expectNamedBlendRejection("Direct2D rejects GraphicsDevice.BlendFactor", [&] {
            device.setBlendFactorProperty(Color(64, 255, 255, 255));
        });
        device.setBlendFactorProperty(Color::White);

        // D2D-99: Direct2D has no depth or stencil buffer at all. GraphicsDevice's own
        // constructor unconditionally applies DepthStencilState.Default (DepthBufferEnable=true,
        // StencilEnable=false) before any game code runs -- already proven harmless simply by
        // this test having reached this point -- so DepthBufferEnable/WriteEnable/Function must
        // stay silently accepted (they can never have an observable effect here) while
        // StencilEnable=true, a real feature gap, fails with a named exception instead of
        // silently doing nothing.
        DepthStencilState depthEnabledState = DepthStencilState::Default;
        bool depthDefaultAccepted = true;
        try
        {
            device.setDepthStencilStateProperty(depthEnabledState);
        }
        catch (const std::exception&)
        {
            depthDefaultAccepted = false;
        }
        std::printf("[%s] Direct2D accepts DepthStencilState.Default (DepthBufferEnable=true)\n",
                    depthDefaultAccepted ? "PASS" : "FAIL");
        passed = passed && depthDefaultAccepted;
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
        auto& backend = device.GetBackend();
        SDL_Window* const window = backend.GetWindowInternal();
        SDL_SetWindowSize(window, 80, 24);
        SDL_SyncWindow(window);
        int physicalWidth = 0;
        int physicalHeight = 0;
        SDL_GetWindowSize(window, &physicalWidth, &physicalHeight);
        backend.SetVirtualResolution(48, 32);
        const auto near = [](float actual, float expected) {
            return std::abs(actual - expected) <= 0.05f;
        };
        const auto verifyPresentationTransform = [&](PresentationMode mode, const char* label,
                                                     float scaleX, float scaleY,
                                                     float offsetX, float offsetY) {
            backend.SetPresentationMode(static_cast<int>(mode));
            device.Clear(Color::Black); // realizes the resized DXGI backbuffer before querying transforms
            float windowX = 0.0f;
            float windowY = 0.0f;
            float logicalX = 0.0f;
            float logicalY = 0.0f;
            const bool transformedOut = backend.TransformLogicalToWindow(
                12.0f, 8.0f, windowX, windowY);
            const bool transformedBack = transformedOut && backend.TransformWindowToLogical(
                windowX, windowY, logicalX, logicalY);
            const bool matches = transformedBack && near(windowX, 12.0f * scaleX + offsetX) &&
                near(windowY, 8.0f * scaleY + offsetY) && near(logicalX, 12.0f) && near(logicalY, 8.0f);
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
        // backend directly (GetViewportSize/ReadBackbuffer): GraphicsDevice::GetBackBufferData
        // validates against PresentationParameters, which this section intentionally never
        // resizes through GraphicsDeviceManager (it drives the backend's SDL window/virtual
        // resolution directly to isolate the backend's own presentation-transform contract).
        backend.SetPresentationMode(static_cast<int>(PresentationMode::NativeBackBuffer));
        int nativeViewportWidth = 0;
        int nativeViewportHeight = 0;
        backend.GetViewportSize(nativeViewportWidth, nativeViewportHeight);
        const bool nativeViewportMatchesPhysical =
            nativeViewportWidth == physicalWidth && nativeViewportHeight == physicalHeight;
        std::printf("[%s] Direct2D NativeBackBuffer viewport matches physical size: got=(%d,%d), expected=(%d,%d)\n",
                    nativeViewportMatchesPhysical ? "PASS" : "FAIL",
                    nativeViewportWidth, nativeViewportHeight, physicalWidth, physicalHeight);
        passed = passed && nativeViewportMatchesPhysical;

        // Earlier sections left the backend's own SpriteBatch-local viewport clip set to a small
        // sub-region; reset it to the full physical target so it cannot hide this probe's marker.
        backend.SetViewport(0, 0, physicalWidth, physicalHeight, 0.0f, 1.0f);
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(*white_, Rectangle(physicalWidth - 4, physicalHeight - 4, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        std::array<uint8_t, 4> farCornerPixel{};
        backend.ReadBackbuffer(physicalWidth - 1, physicalHeight - 1, 1, 1, farCornerPixel.data());
        const bool farCornerMatches = farCornerPixel[0] == 255 && farCornerPixel[1] == 255 &&
            farCornerPixel[2] == 255 && farCornerPixel[3] == 255;
        std::printf("[%s] Direct2D NativeBackBuffer covers full physical backbuffer far corner: got=(%d,%d,%d,%d)\n",
                    farCornerMatches ? "PASS" : "FAIL", farCornerPixel[0], farCornerPixel[1],
                    farCornerPixel[2], farCornerPixel[3]);
        passed = passed && farCornerMatches;
        std::array<uint8_t, 4> nearCornerPixel{};
        backend.ReadBackbuffer(1, 1, 1, 1, nearCornerPixel.data());
        const bool nearCornerMatches = nearCornerPixel[0] == 0 && nearCornerPixel[1] == 0 &&
            nearCornerPixel[2] == 0 && nearCornerPixel[3] == 255;
        std::printf("[%s] Direct2D NativeBackBuffer keeps other physical pixels background: got=(%d,%d,%d,%d)\n",
                    nearCornerMatches ? "PASS" : "FAIL", nearCornerPixel[0], nearCornerPixel[1],
                    nearCornerPixel[2], nearCornerPixel[3]);
        passed = passed && nearCornerMatches;

        backend.SetPresentationMode(static_cast<int>(PresentationMode::Letterbox));
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
        SDL_SetWindowSize(window, 8, 200);
        SDL_SyncWindow(window);
        backend.SetVirtualResolution(8, 8);
        backend.SetPresentationMode(static_cast<int>(PresentationMode::FixedHeightDynamicWidth));
        int extremeViewportWidth = 0;
        int extremeViewportHeight = 0;
        backend.GetViewportSize(extremeViewportWidth, extremeViewportHeight);
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
        backend.ReadBackbuffer(0, 0, 1, 1, extremePixel.data());
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
        backend.GetViewportSize(viewportBeforeFault[0], viewportBeforeFault[1]);
        bool virtualResolutionFaultRejected = false;
        try
        {
            backend.SetVirtualResolution(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
        }
        catch (const std::exception&)
        {
            virtualResolutionFaultRejected = true;
        }
        std::printf("[%s] Direct2D rejects an INT_MAX SetVirtualResolution\n",
                    virtualResolutionFaultRejected ? "PASS" : "FAIL");
        passed = passed && virtualResolutionFaultRejected;
        int viewportAfterFault[2] = {0, 0};
        backend.GetViewportSize(viewportAfterFault[0], viewportAfterFault[1]);
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
