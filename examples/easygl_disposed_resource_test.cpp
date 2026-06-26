// SPDX-License-Identifier: MS-PL
// Task 210: GraphicsDevice rejects disposed resources.
//
// Checks that the graphics layer throws System::ObjectDisposedException when:
//   1. A disposed Texture2D is assigned to Textures[0]
//   2. A disposed BasicEffect has Apply() called on it
//   3. A disposed RenderTarget2D is passed to SetRenderTarget()
//
// VB and IB are not yet derived from GraphicsResource in CNA (Task 212),
// so those disposal guards will be added in that task.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "System/IDisposable.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class DisposedResourceTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;
    bool done_ = false;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

    template<class Fn>
    bool throwsODE(Fn&& fn)
    {
        try { fn(); return false; }
        catch (const System::ObjectDisposedException&) { return true; }
        catch (...) { return false; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── 1. Disposed Texture2D → Textures[0] throws ODE ──────────────
        {
            Texture2D tex(dev, 1, 1);
            tex.Dispose();
            check(throwsODE([&]{ dev.getTexturesProperty()(0, &tex); }),
                  "Disposed Texture2D assigned to Textures[0] throws ObjectDisposedException");
        }

        // ── 2. Disposed BasicEffect → Apply() throws ODE ─────────────────
        {
            BasicEffect fx(dev);
            static_cast<System::IDisposable&>(fx).Dispose();
            check(throwsODE([&]{ fx.Apply(); }),
                  "Disposed BasicEffect Apply() throws ObjectDisposedException");
        }

        // ── 3. Disposed RenderTarget2D → SetRenderTarget() throws ODE ────
        {
            RenderTarget2D rt(dev, 64, 64, false,
                              SurfaceFormat::Color,
                              DepthFormat::None,
                              0,
                              RenderTargetUsage::DiscardContents);
            rt.Dispose();
            check(throwsODE([&]{ dev.SetRenderTarget(&rt); }),
                  "Disposed RenderTarget2D passed to SetRenderTarget() throws ObjectDisposedException");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    DisposedResourceTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    DisposedResourceTest game;
    game.Run();
    return game.getResult();
}
