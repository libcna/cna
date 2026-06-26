// SPDX-License-Identifier: MS-PL
// Task 215: Backend GPU handles are released exactly once on Dispose().
//
// For each resource type (VertexBuffer, IndexBuffer, Texture2D, RenderTarget2D):
//   1. HasBackend() == true  before Dispose()
//   2. HasBackend() == false after  Dispose()       → handle freed on first Dispose()
//   3. HasBackend() == false after  second Dispose() → no double-free attempt

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/IDisposable.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class HandleReleaseTest : public Game
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

    static void disposeVia(System::IDisposable& r) { r.Dispose(); }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        // ── VertexBuffer ──────────────────────────────────────────────────
        {
            VertexBuffer vb(dev, 8);
            check(vb.HasBackend(),
                  "VertexBuffer: HasBackend true before Dispose");

            disposeVia(vb);

            check(!vb.HasBackend(),
                  "VertexBuffer: HasBackend false after Dispose (handle freed)");
            check(vb.getIsDisposedProperty(),
                  "VertexBuffer: IsDisposed true");

            disposeVia(vb);  // second call — must not crash

            check(!vb.HasBackend(),
                  "VertexBuffer: HasBackend still false after second Dispose");
        }

        // ── IndexBuffer ───────────────────────────────────────────────────
        {
            IndexBuffer ib(dev, 12);
            check(ib.HasBackend(),
                  "IndexBuffer: HasBackend true before Dispose");

            disposeVia(ib);

            check(!ib.HasBackend(),
                  "IndexBuffer: HasBackend false after Dispose (handle freed)");
            check(ib.getIsDisposedProperty(),
                  "IndexBuffer: IsDisposed true");

            disposeVia(ib);

            check(!ib.HasBackend(),
                  "IndexBuffer: HasBackend still false after second Dispose");
        }

        // ── Texture2D ─────────────────────────────────────────────────────
        {
            Texture2D tex(dev, 4, 4);
            check(tex.HasBackend(),
                  "Texture2D: HasBackend true before Dispose");

            disposeVia(tex);

            check(!tex.HasBackend(),
                  "Texture2D: HasBackend false after Dispose (handle freed)");
            check(tex.getIsDisposedProperty(),
                  "Texture2D: IsDisposed true");

            disposeVia(tex);

            check(!tex.HasBackend(),
                  "Texture2D: HasBackend still false after second Dispose");
        }

        // ── RenderTarget2D (via Texture2D::HasBackend) ────────────────────
        {
            RenderTarget2D rt(dev, 8, 8);
            check(rt.HasBackend(),
                  "RenderTarget2D: HasBackend true before Dispose");

            disposeVia(rt);

            check(!rt.HasBackend(),
                  "RenderTarget2D: HasBackend false after Dispose (handle freed)");
            check(rt.getIsDisposedProperty(),
                  "RenderTarget2D: IsDisposed true");

            disposeVia(rt);

            check(!rt.HasBackend(),
                  "RenderTarget2D: HasBackend still false after second Dispose");
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    HandleReleaseTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    HandleReleaseTest game;
    game.Run();
    return game.getResult();
}
