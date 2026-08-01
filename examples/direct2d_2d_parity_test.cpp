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
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <array>
#include <cstdlib>
#include <cstdio>
#include <exception>
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
        // (`PLUS` and `SOURCE_COPY`). Native Windows retains these exact blend probes.
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
        const Color quarterRedOverBackground(79, 30, 60, 255);

        device.Clear(blendBackground);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr, &scissorDisabled);
        sprites_->Draw(straightHalfRed, Rectangle(8, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        // Color.A has to attenuate both the premultiplied RGB value and alpha for source-over
        // and Plus. It must still attenuate the copied alpha for Opaque while leaving that
        // preset's RGB source factor equal to one.
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
        check("AlphaBlend texture Color.A", 1, 5, quarterRedOverBackground);
        check("NonPremultiplied texture Color.A", 9, 5, quarterRedOverBackground);
        if (verifyAdvancedBlend)
        {
            check("Additive premultiplied texture", 17, 1, Color(148, 40, 80, 255));
            check("Opaque copies premultiplied source", 25, 1, Color(128, 0, 0, 128));
            check("Additive texture Color.A", 17, 5, Color(84, 40, 80, 255));
            check("Opaque texture Color.A", 25, 5, Color(128, 0, 0, 64));
        }
        else
        {
            std::printf("[SKIP] Additive/Opaque composite modes (Wine Direct2D ignores PLUS/SOURCE_COPY)\n");
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
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Additive, &point, nullptr, &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Additive RenderTarget2D source", 1, 5, Color(148, 40, 80, 255));

            device.Clear(blendBackground);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            check("Opaque RenderTarget2D source", 1, 5, Color(128, 0, 0, 128));
        }
        if (verifyRenderTargetDecoration)
        {
            device.Clear(blendBackground);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), halfAlphaTint);
            sprites_->End();
            check("AlphaBlend RenderTarget2D source Color.A", 1, 5, quarterRedOverBackground);

            // NonPremultiplied declares its RT input to be straight-alpha data. Its stored
            // premultiplied red is therefore premultiplied once more before source-over.
            device.Clear(blendBackground);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr,
                            &scissorDisabled);
            sprites_->Draw(blendSourceTarget, Rectangle(0, 4, 4, 4), Rectangle(0, 0, 1, 1), halfAlphaTint);
            sprites_->End();
            check("NonPremultiplied RenderTarget2D source Color.A", 1, 5, Color(47, 30, 60, 255));
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

        // 12. Desktop context-loss recovery is an atomic Direct3D/DXGI/Direct2D recreation.  A
        // recoverable Texture2D must acquire a new bitmap from its CPU shadow; a recoverable RT
        // must be safely reallocated with transparent contents instead of exposing its old COM
        // bitmap. This uses the public debug hook, exactly like EasyGL's desktop recovery probe.
        auto& direct2dBackend = device.GetBackend();
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

        auto postRecoveryTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<uint8_t>{40, 180, 90, 255});
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &scissorDisabled);
        sprites_->Draw(postRecoveryTexture, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        check("Context recovery new Texture2D", 1, 1, Color(40, 180, 90, 255));
        device.SetContextRecoveryEnabled(true);

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

        // GraphicsCapability currently contains only optional 3D-pipeline facilities. The
        // Direct2D backend must report none of them: owning D3D11/DXGI presentation resources
        // does not turn this deliberately 2D-only backend into a second D3D11 renderer.
        constexpr std::array unsupportedCapabilities{
            std::pair{"ThreeD", CNA::GraphicsCapability::ThreeD},
            std::pair{"DepthStencilBuffer", CNA::GraphicsCapability::DepthStencilBuffer},
            std::pair{"MultiSampleAntiAliasing", CNA::GraphicsCapability::MultiSampleAntiAliasing},
            std::pair{"MultipleRenderTargets", CNA::GraphicsCapability::MultipleRenderTargets},
            std::pair{"AnisotropicFiltering", CNA::GraphicsCapability::AnisotropicFiltering},
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
