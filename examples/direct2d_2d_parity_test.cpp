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
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        bool passed = true;
        // Wine's Direct2D implementation renders this test correctly but leaves
        // ID2D1Bitmap::CopyFromRenderTarget as E_NOTIMPL.  Native Windows keeps both probes
        // enabled; the cross-Wine CTest deliberately retains the rest of this public 2D matrix.
        const bool verifyCpuRenderTargetReadback =
            std::getenv("CNA_DIRECT2D_SKIP_CPU_RENDER_TARGET_READBACK") == nullptr;
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
        if (verifyCpuRenderTargetReadback)
        {
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
        }
        else
        {
            std::printf("[SKIP] active RenderTarget2D CPU readback (Wine Direct2D lacks CopyFromRenderTarget)\n");
        }

        device.SetRenderTarget(nullptr);

        // RenderTarget2D has no CPU shadow. This must exercise Direct2DRenderTargetBackend's
        // CopyFromRenderTarget -> CPU_READ bitmap -> Map path rather than the ordinary Texture2D
        // shadow-data path.
        if (verifyCpuRenderTargetReadback)
        {
            Color targetPixel(0, 0, 0, 0);
            target.GetData(0, &targetRegion, &targetPixel, 0, 1);
            const bool targetReadbackMatches = Matches(targetPixel, Color(255, 0, 0, 255));
            std::printf("[%s] RenderTarget2D direct GetData: got=(%d,%d,%d,%d), expected red\n",
                        targetReadbackMatches ? "PASS" : "FAIL",
                        targetPixel.getRProperty(), targetPixel.getGProperty(),
                        targetPixel.getBProperty(), targetPixel.getAProperty());
            passed = passed && targetReadbackMatches;
        }
        else
        {
            std::printf("[SKIP] RenderTarget2D direct GetData (Wine Direct2D lacks CopyFromRenderTarget)\n");
        }

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

        // 6. A recorded scissor rectangle must only affect SpriteBatch once RasterizerState
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

        // 7. SpriteBatch coordinates are viewport-local, and the viewport is also an output clip.
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

        // 8. Direct2D's three primitive blends cover CNA's four standard SpriteBatch presets.
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

        device.Clear(blendBackground);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, &scissorDisabled);
        sprites_->Draw(premultipliedHalfRed, Rectangle(0, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
        sprites_->End();
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::NonPremultiplied, &point, nullptr, &scissorDisabled);
        sprites_->Draw(straightHalfRed, Rectangle(8, 0, 4, 4),
                       Rectangle(0, 0, 1, 1), Color::White);
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
        }
        check("AlphaBlend premultiplied texture", 1, 1, halfRedOverBackground);
        check("NonPremultiplied straight texture", 9, 1, halfRedOverBackground);
        if (verifyAdvancedBlend)
        {
            check("Additive premultiplied texture", 17, 1, Color(148, 40, 80, 255));
            check("Opaque copies premultiplied source", 25, 1, Color(128, 0, 0, 128));
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

        // 9. A mipmapped Texture2D owns one Direct2D bitmap per initialized level. SpriteBatch
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

        // 10. Desktop context-loss recovery is an atomic Direct3D/DXGI/Direct2D recreation.  A
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

        // 11. GraphicsCapability currently contains only optional 3D-pipeline facilities. The
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

        // 12. Unsupported output-mask and blend-factor state must fail explicitly instead of
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
    bool done_ = false;
    int result_ = 1;
};

int main()
{
    Direct2D2DParityTest test;
    test.Run();
    return test.Result();
}
