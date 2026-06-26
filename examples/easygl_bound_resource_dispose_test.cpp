// SPDX-License-Identifier: MS-PL
// Task 214: Disposing resources while bound to GraphicsDevice.
//
// FNA behaviour verified here:
//   1. Texture2D disposed while bound in Textures[0] → slot becomes nullptr.
//   2. VertexBuffer disposed while bound → no crash (FNA keeps stale ref; CNA matches).
//   3. IndexBuffer disposed while bound → no crash (same).
//   4. RenderTarget2D disposed while still set as render target →
//      throws InvalidOperationException ("Disposing target that is still bound").
//   5. RenderTarget2D disposed after being unbound → no exception.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/IDisposable.hpp"
#include "System/InvalidOperationException.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class BoundResourceDisposeTest : public Game
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

    static bool throwsInvalidOp(System::IDisposable& res)
    {
        try { res.Dispose(); return false; }
        catch (const System::InvalidOperationException&) { return true; }
        catch (...) { return false; }
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── 1. Texture2D disposed while bound → slot cleared ──────────────
        {
            Texture2D tex(dev, 2, 2);
            dev.getTexturesProperty()(0, &tex);
            check(dev.getTexturesProperty()[0] == &tex,
                  "Texture2D: slot[0] holds texture before dispose");

            static_cast<System::IDisposable&>(tex).Dispose();

            check(dev.getTexturesProperty()[0] == nullptr,
                  "Texture2D: slot[0] cleared to nullptr after dispose while bound");
            check(tex.getIsDisposedProperty(),
                  "Texture2D: IsDisposed true after dispose");
        }

        // ── 2. VertexBuffer disposed while bound → no crash ───────────────
        {
            VertexBuffer vb(dev, 4);
            dev.SetVertexBuffer(&vb);
            // FNA does not clear the device binding on VB dispose; CNA matches.
            bool ok = false;
            try { static_cast<System::IDisposable&>(vb).Dispose(); ok = true; }
            catch (...) {}
            check(ok, "VertexBuffer: disposing while bound does not throw");
            check(vb.getIsDisposedProperty(),
                  "VertexBuffer: IsDisposed true after dispose while bound");
            dev.SetVertexBuffer(nullptr);   // clear stale binding
        }

        // ── 3. IndexBuffer disposed while bound → no crash ────────────────
        {
            IndexBuffer ib(dev, 4);
            dev.SetIndexBuffer(&ib);
            bool ok = false;
            try { static_cast<System::IDisposable&>(ib).Dispose(); ok = true; }
            catch (...) {}
            check(ok, "IndexBuffer: disposing while bound does not throw");
            check(ib.getIsDisposedProperty(),
                  "IndexBuffer: IsDisposed true after dispose while bound");
            dev.SetIndexBuffer(nullptr);    // clear stale binding
        }

        // ── 4. RenderTarget2D disposed while still set → throws ───────────
        {
            RenderTarget2D rt(dev, 16, 16);
            dev.SetRenderTarget(&rt);
            check(throwsInvalidOp(rt),
                  "RenderTarget2D: dispose while bound throws InvalidOperationException");
            dev.SetRenderTarget(nullptr);   // unbind before leaving scope
        }

        // ── 5. RenderTarget2D disposed after unbinding → no throw ─────────
        {
            RenderTarget2D rt(dev, 16, 16);
            dev.SetRenderTarget(&rt);
            dev.SetRenderTarget(nullptr);   // unbind first
            bool ok = false;
            try { static_cast<System::IDisposable&>(rt).Dispose(); ok = true; }
            catch (...) {}
            check(ok, "RenderTarget2D: dispose after unbind does not throw");
            check(rt.getIsDisposedProperty(),
                  "RenderTarget2D: IsDisposed true after dispose");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BoundResourceDisposeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    BoundResourceDisposeTest game;
    game.Run();
    return game.getResult();
}
