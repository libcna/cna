// SPDX-License-Identifier: MS-PL
// Task 215 / REMED-GFX-004: Backend GPU handles are released exactly once.
//
// For each resource type (VertexBuffer, IndexBuffer, Texture2D, RenderTarget2D):
//   1. HasBackend() == true  before Dispose()
//   2. HasBackend() == false after  Dispose()       → handle freed on first Dispose()
//   3. HasBackend() == false after  second Dispose() → no double-free attempt
//
// RenderTargetCube additionally covers its public cached-backend accessor, bound-target
// rejection, destructor-only cleanup, and both resource/device destruction orders.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/IDisposable.hpp"
#include "System/InvalidOperationException.hpp"

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

    static bool throwsInvalidOperation(System::IDisposable& r)
    {
        try
        {
            r.Dispose();
            return false;
        }
        catch (const System::InvalidOperationException&)
        {
            return true;
        }
    }

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

        // ── RenderTargetCube lifecycle parity with RenderTarget2D ─────────
        {
            RenderTargetCube bound(
                dev, 4, false, SurfaceFormat::Color, DepthFormat::None);
            dev.SetRenderTarget(&bound, CubeMapFace::PositiveX);
            check(throwsInvalidOperation(bound),
                  "RenderTargetCube: disposing while bound throws InvalidOperationException");
            check(!bound.getIsDisposedProperty() &&
                      bound.GetRenderTargetCubeBackend() != nullptr,
                  "RenderTargetCube: rejected disposal leaves the resource live");
            dev.SetRenderTarget(nullptr);
            disposeVia(bound);
        }

        int cubeDestroyCount = 0;
        dev.ResourceDestroyed +=
            [&](System::Object*, const ResourceDestroyedEventArgs& args)
            {
                if (args.getNameProperty() == "REMED-GFX-004")
                    ++cubeDestroyCount;
            };

        {
            RenderTargetCube rt(
                dev, 4, false, SurfaceFormat::Color, DepthFormat::None);
            rt.setNameProperty("REMED-GFX-004");
            check(rt.GetRenderTargetCubeBackend() != nullptr,
                  "RenderTargetCube: backend is live before explicit Dispose");

            disposeVia(rt);

            check(rt.getIsDisposedProperty(),
                  "RenderTargetCube: explicit Dispose publishes IsDisposed");
            check(rt.GetRenderTargetCubeBackend() == nullptr,
                  "RenderTargetCube: explicit Dispose clears the cached backend pointer");

            disposeVia(rt);

            check(rt.GetRenderTargetCubeBackend() == nullptr,
                  "RenderTargetCube: cached backend stays null after repeated Dispose");
            check(cubeDestroyCount == 1,
                  "RenderTargetCube: repeated Dispose reports exactly one destruction");
        }
        check(cubeDestroyCount == 1,
              "RenderTargetCube: destructor after explicit Dispose does not clean up twice");

        {
            RenderTargetCube rt(
                dev, 4, false, SurfaceFormat::Color, DepthFormat::None);
            rt.setNameProperty("REMED-GFX-004");
            check(rt.GetRenderTargetCubeBackend() != nullptr,
                  "RenderTargetCube: backend is live before destructor-only cleanup");
        }
        check(cubeDestroyCount == 2,
              "RenderTargetCube: destructor-only cleanup reports one destruction");

        auto* deviceOwned = new RenderTargetCube(
            dev, 4, false, SurfaceFormat::Color, DepthFormat::None);
        deviceOwned->setNameProperty("REMED-GFX-004");
        check(deviceOwned->GetRenderTargetCubeBackend() != nullptr,
              "RenderTargetCube: backend is live before device-first disposal");

        dev.Dispose();

        check(deviceOwned->getIsDisposedProperty(),
              "RenderTargetCube: device-first disposal publishes IsDisposed");
        check(deviceOwned->GetRenderTargetCubeBackend() == nullptr,
              "RenderTargetCube: device-first disposal clears the cached backend pointer");
        check(cubeDestroyCount == 3,
              "RenderTargetCube: device-first disposal reports one destruction");

        disposeVia(*deviceOwned);
        delete deviceOwned;
        check(cubeDestroyCount == 3,
              "RenderTargetCube: post-device Dispose/destructor do not clean up twice");
        dev.ResourceDestroyed.Clear();

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
