// SPDX-License-Identifier: MS-PL
// Task 718: Verify double-Dispose is safe for every SDL_Renderer-backed resource type.
//
// Texture2D::Dispose()'s idempotency (and its shared-ownership/copy semantics) was already
// thoroughly covered by Task 689. This task sweeps the remaining resource types touched by
// Task 717's own disposed-guard audit -- RenderTarget2D, BlendState, SamplerState, SpriteBatch --
// confirming calling Dispose() a SECOND time on each is always safe (no crash, no double-free,
// no corrupted device state), and that each resource's already-established "no bug found"
// post-disposal behavior from Task 717 still holds after the SECOND Dispose() call too.
//
// Particular attention to RenderTarget2D: Task 717 fixed RenderTarget2D::Dispose() to clear its
// cached rtRenderer_ raw pointer to nullptr (previously left dangling) -- this test confirms that
// fix is itself idempotent (setting an already-null pointer to null again is trivially safe, but
// worth confirming the whole Dispose() call chain -- including Texture2D::Dispose()'s shared_ptr
// reset() and the base GraphicsResource's own isDisposed_ guard -- behaves correctly end-to-end
// on a second call).
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
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlDoubleDisposeTest : public Game
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

        // --- RenderTarget2D: double-Dispose is safe, and GetRenderTargetRenderer() stays null. ---
        {
            RenderTarget2D rt(dev, 4, 4);
            rt.Dispose();
            rt.Dispose();
            check(rt.getIsDisposedProperty(), "RenderTarget2D reports disposed after a double-Dispose");
            check(rt.GetRenderTargetRenderer() == nullptr,
                  "RenderTarget2D::GetRenderTargetRenderer() is still safely null after a double-Dispose");
        }

        // --- BlendState: double-Dispose is safe; still usable afterward (matches FNA's own lack of guard). ---
        {
            BlendState bs;
            bs.Dispose();
            bs.Dispose();
            bool threw = false;
            try { dev.setBlendStateProperty(bs); }
            catch (const std::exception&) { threw = true; }
            check(!threw, "A double-disposed BlendState can still be applied via setBlendStateProperty without throwing");
        }

        // --- SamplerState: double-Dispose is safe; still usable afterward. ---
        {
            SamplerState ss;
            ss.Dispose();
            ss.Dispose();
            bool threw = false;
            try
            {
                sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &ss, nullptr, nullptr);
                sb_->Draw(*whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                sb_->End();
            }
            catch (const std::exception&) { threw = true; }
            check(!threw, "A double-disposed SamplerState can still be used via SpriteBatch::Begin without throwing");
        }

        // --- SpriteBatch: double-Dispose is safe; Begin/Draw/End still work afterward. ---
        {
            SpriteBatch localSb(dev);
            localSb.Dispose();
            localSb.Dispose();
            bool threw = false;
            try
            {
                localSb.Begin();
                localSb.Draw(*whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1), Color::White);
                localSb.End();
            }
            catch (const std::exception&) { threw = true; }
            check(!threw, "A double-disposed SpriteBatch's Begin/Draw/End still work normally");
        }

        // --- The device must remain fully usable after every double-Dispose above. ---
        dev.Clear(Color(0, 255, 255, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240 && pixel.getBProperty() >= 240,
              "GraphicsDevice remains fully functional after every double-Dispose check above");

        Exit();
    }

public:
    SdlDoubleDisposeTest()
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
    SdlDoubleDisposeTest game;
    game.Run();
    return game.getResult();
}
