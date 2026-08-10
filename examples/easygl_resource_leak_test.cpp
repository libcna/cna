// SPDX-License-Identifier: MS-PL
// Task 219: Leak-check — create and dispose many resources, verify no handles remain.
//
// For each of N=20 iterations, creates one Texture2D, VertexBuffer, IndexBuffer,
// and RenderTarget2D (80 resources total), disposes them all explicitly, then checks:
//   - ResourceCreated event fired exactly 80 times (all tracked).
//   - ResourceDestroyed event fired exactly 80 times (all untracked).
//   - Every HasRenderer() is false (no GPU handle leaked).
//   - GraphicsDevice tracking list is empty (GetTrackedResourceCount() == 0).
//   - No crash (no double-free or use-after-free from stray GL calls).

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
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int N = 20;   // instances of each type
static constexpr int TYPES = 4; // Texture2D, VertexBuffer, IndexBuffer, RenderTarget2D
static constexpr int TOTAL = N * TYPES;

class ResourceLeakTest : public Game
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

        // ── Track events ──────────────────────────────────────────────────
        int createCount  = 0;
        int destroyCount = 0;
        dev.ResourceCreated   += [&](System::Object*, const ResourceCreatedEventArgs&)   { ++createCount;  };
        dev.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyCount; };

        // ── Create N of each resource type ────────────────────────────────
        std::vector<std::unique_ptr<Texture2D>>    textures;
        std::vector<std::unique_ptr<VertexBuffer>> vbufs;
        std::vector<std::unique_ptr<IndexBuffer>>  ibufs;
        std::vector<std::unique_ptr<RenderTarget2D>> rts;

        textures.reserve(N);
        vbufs.reserve(N);
        ibufs.reserve(N);
        rts.reserve(N);

        const std::size_t trackBefore = dev.GetTrackedResourceCount();

        for (int i = 0; i < N; ++i)
        {
            textures.push_back(std::make_unique<Texture2D>(dev, 4, 4));
            vbufs.push_back(std::make_unique<VertexBuffer>(dev, 4));
            ibufs.push_back(std::make_unique<IndexBuffer>(dev, 4));
            rts.push_back(std::make_unique<RenderTarget2D>(dev, 8, 8));
        }

        const std::size_t trackAfter = dev.GetTrackedResourceCount();

        check(createCount == TOTAL,
              "ResourceCreated fired exactly TOTAL times after creation");
        check(static_cast<int>(trackAfter - trackBefore) == TOTAL,
              "Tracking list grew by exactly TOTAL entries");

        // ── Dispose all ───────────────────────────────────────────────────
        for (auto& r : textures) disposeVia(*r);
        for (auto& r : vbufs)    disposeVia(*r);
        for (auto& r : ibufs)    disposeVia(*r);
        for (auto& r : rts)      disposeVia(*r);

        check(destroyCount == TOTAL,
              "ResourceDestroyed fired exactly TOTAL times after disposal");
        check(dev.GetTrackedResourceCount() == trackBefore,
              "Tracking list returned to pre-creation count after disposal");

        // ── Verify no renderers remain alive ───────────────────────────────
        int leaked = 0;
        for (auto& r : textures) if (r->HasRenderer()) ++leaked;
        for (auto& r : vbufs)    if (r->HasRenderer()) ++leaked;
        for (auto& r : ibufs)    if (r->HasRenderer()) ++leaked;
        for (auto& r : rts)      if (r->HasRenderer()) ++leaked;

        check(leaked == 0, "No GPU handles remain after disposing all resources");

        // ── Verify unique_ptrs can go out of scope without crashing ───────
        // (destructors run here; renderers are null so no GL calls made)
        textures.clear();
        vbufs.clear();
        ibufs.clear();
        rts.clear();

        check(true, "Clearing unique_ptrs after dispose does not crash");

        // ── Quick repeat to catch one-time initialization artifacts ───────
        createCount = destroyCount = 0;
        {
            VertexBuffer vb(dev, 8);
            Texture2D    tx(dev, 2, 2);
            disposeVia(vb);
            disposeVia(tx);
        }
        check(createCount == 2 && destroyCount == 2,
              "Second batch: 2 resources created and destroyed cleanly");

        dev.ResourceCreated.Clear();
        dev.ResourceDestroyed.Clear();

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    ResourceLeakTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    ResourceLeakTest game;
    game.Run();
    return game.getResult();
}
