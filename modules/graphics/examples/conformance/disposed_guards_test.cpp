// SPDX-License-Identifier: MS-PL
// Task 717: disposed-resource guards throw ObjectDisposedException consistently (Texture2D,
// RenderTarget2D, BlendState, SamplerState, SpriteBatch).
//
// Microsoft XNA is authoritative for state/resource lifetime: disposed graphics states are
// rejected when they are applied, and a disposed SpriteBatch rejects further public use. FNA's
// missing state guards are a documented divergence rather than this test's expected contract.
//
// Real bug found and fixed while writing this test: SpriteBatch::Draw(Texture2D&, ...) had no
// guard at all, and a disposed Texture2D's renderer is null, so the draw dereferenced address 0.
// pushSprite, the single funnel of every Draw/DrawString overload, now throws
// ObjectDisposedException instead.
//
// Task 718 adds the second Dispose(): it is a no-op for RenderTarget2D, BlendState, SamplerState
// and SpriteBatch, and every guard above still refuses the resource afterwards.
//
// Runs with PresentationMode::NativeBackBuffer so logical and physical pixels map 1:1 for the
// readback. First written for the retired SDL_RENDERER renderer (Tasks 717 and 718).
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdio>
#include <memory>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DisposedGuardsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             whiteTex_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

    template <typename Fn>
    bool ThrowsObjectDisposed(Fn&& fn)
    {
        try
        {
            fn();
            return false;
        }
        catch (const System::ObjectDisposedException&)
        {
            return true;
        }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        whiteTex_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);

        // --- RenderTarget2D: guarded at SetRenderTarget/SetRenderTargets. ---
        {
            RenderTarget2D rt(dev, 4, 4);
            rt.Dispose();
            check(ThrowsObjectDisposed([&]{ dev.SetRenderTarget(&rt); }),
                  "SetRenderTarget throws ObjectDisposedException for a disposed RenderTarget2D");
        }
        {
            RenderTarget2D rt(dev, 4, 4);
            rt.Dispose();
            std::vector<RenderTargetBinding> bindings{RenderTargetBinding(&rt)};
            check(ThrowsObjectDisposed([&]{ dev.SetRenderTargets(bindings); }),
                  "SetRenderTargets throws ObjectDisposedException for a disposed RenderTarget2D");
        }

        // --- Texture2D: NOW guarded at SpriteBatch::Draw (Task 717's own fix). ---
        {
            Texture2D tex = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 255});
            tex.Dispose();
            sb_->Begin();
            check(ThrowsObjectDisposed([&]{
                sb_->Draw(tex, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
            }), "SpriteBatch::Draw throws ObjectDisposedException for a disposed Texture2D (Task 717 fix)");
            // The throw happens before anything is queued, so End() here just flushes an empty
            // batch -- closes out Begin()/End() state cleanly for the next check.
            sb_->End();
        }

        // --- BlendState: rejected at the GraphicsDevice consumption boundary. ---
        {
            BlendState bs;
            bs.Dispose();
            check(ThrowsObjectDisposed([&]{ dev.setBlendStateProperty(bs); }),
                  "setBlendStateProperty rejects a disposed BlendState");
        }

        // --- SamplerState: Deferred retains it at Begin and rejects it when End applies state. ---
        {
            SamplerState ss;
            ss.Dispose();
            check(ThrowsObjectDisposed([&]{
                sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &ss, nullptr, nullptr);
                sb_->Draw(*whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                sb_->End();
            }), "SpriteBatch::End rejects a disposed deferred SamplerState");
        }

        // --- SpriteBatch: public use after disposal is rejected. ---
        {
            SpriteBatch localSb(dev);
            localSb.Dispose();
            check(ThrowsObjectDisposed([&]{
                localSb.Begin();
                localSb.Draw(*whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                localSb.End();
            }), "A disposed SpriteBatch rejects Begin");
        }

        // --- Task 718: a SECOND Dispose() is a no-op, and the guards above still hold after it. ---
        {
            RenderTarget2D rt(dev, 4, 4);
            rt.Dispose();
            rt.Dispose();
            check(rt.getIsDisposedProperty(), "RenderTarget2D reports IsDisposed after a double Dispose");
            check(ThrowsObjectDisposed([&]{ dev.SetRenderTarget(&rt); }),
                  "a double-disposed RenderTarget2D is still refused by SetRenderTarget");
        }
        {
            BlendState bs;
            bs.Dispose();
            bs.Dispose();
            check(bs.getIsDisposedProperty(), "BlendState reports IsDisposed after a double Dispose");
            check(ThrowsObjectDisposed([&]{ dev.setBlendStateProperty(bs); }),
                  "a double-disposed BlendState is still refused by setBlendStateProperty");
        }
        {
            SamplerState ss;
            ss.Dispose();
            ss.Dispose();
            check(ss.getIsDisposedProperty(), "SamplerState reports IsDisposed after a double Dispose");
            // A fresh batch: XNA's End applies the render state BEFORE it leaves the Begin/End pair,
            // so a batch whose End threw is still inside it and would refuse the next Begin.
            SpriteBatch batch(dev);
            check(ThrowsObjectDisposed([&]{
                batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &ss, nullptr, nullptr);
                batch.Draw(*whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                batch.End();
            }), "a double-disposed SamplerState is still refused when SpriteBatch::End applies it");
        }
        {
            SpriteBatch localSb(dev);
            localSb.Dispose();
            localSb.Dispose();
            check(ThrowsObjectDisposed([&]{
                localSb.Begin();
                localSb.Draw(*whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                localSb.End();
            }), "a double-disposed SpriteBatch still rejects Begin");
        }

        // --- The device must remain fully usable after all of the above. ---
        dev.Clear(Color(255, 0, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getRProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional after every disposed-guard check above");

        Exit();
    }

public:
    DisposedGuardsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // pre-SDL_GPU PSG-0008: GetBackBufferData is HiDef-only
        // (SOFTWARE-213), so under the default Reach profile this aborted before its first
        // check (GTI-0003).
        gdm_->setGraphicsProfileProperty(
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("disposed_guards"))
        return CNA::Examples::Conformance::kSkipExitCode;
    // Unbuffered so a crash mid-test (this file exercises exactly the scenarios that used to
    // crash before Task 717's fixes) doesn't lose whichever PASS/FAIL lines already printed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    DisposedGuardsTest game;
    game.Run();
    return game.getResult();
}
