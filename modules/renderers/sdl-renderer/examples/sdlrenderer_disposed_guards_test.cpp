// SPDX-License-Identifier: MS-PL
// Task 717: Verify disposed-resource guards throw ObjectDisposedException consistently on
// SDL_Renderer (Texture2D, RenderTarget2D, BlendState, SamplerState, SpriteBatch).
//
// Microsoft XNA is authoritative for state/resource lifetime: disposed graphics states are
// rejected when they are applied, and a disposed SpriteBatch rejects further public use. FNA's
// missing state guards are a documented divergence rather than this test's expected contract.
//
// Real bug found and fixed while writing this test: SpriteBatch::Draw(Texture2D&, ...) had NO
// guard at all -- worse than FNA's own leniency, since a disposed Texture2D's renderer_ (a
// shared_ptr<ITextureRenderer>) becomes null on Dispose(), and SpriteBatch::pushSprite ultimately
// dereferences it via Texture2D::GetRenderer() (`return *renderer_;`) with no null check --
// a GUARANTEED crash (virtual call through a reference to address 0), not a graceful failure or
// even FNA's own safer managed-exception failure mode. Fixed by adding an ObjectDisposedException
// guard in pushSprite (the single funnel point for every Draw/DrawString overload).
//
// Requires PresentationMode::NativeBackBuffer (Task 915 finding): SDL_RenderReadPixels operates
// in physical output coordinates, while this renderer's default presentation mode
// (FixedHeightDynamicWidth) does not map logical pixels 1:1 to physical ones.
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlDisposedGuardsTest : public Game
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
    SdlDisposedGuardsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    // Unbuffered so a crash mid-test (this file exercises exactly the scenarios that used to
    // crash before Task 717's fixes) doesn't lose whichever PASS/FAIL lines already printed.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SdlDisposedGuardsTest game;
    game.Run();
    return game.getResult();
}
