// SPDX-License-Identifier: MS-PL
// Task 719: Leak-check — create and dispose 80 SDL_Renderer textures/render-targets,
// verify GraphicsDevice's tracking list (GetTrackedResourceCount) returns to baseline.
// Mirrors Task 219's EasyGL leak-check, scoped to the two resource types named in this
// task: Texture2D and RenderTarget2D (VertexBuffer/IndexBuffer are already covered by
// Task 219's own test, which exercises shared, renderer-agnostic GraphicsDevice code).
//
// For each of N=40 iterations, creates one Texture2D and one RenderTarget2D (80 resources
// total), disposes them all explicitly, then checks:
//   - ResourceCreated fired exactly 80 times (all tracked).
//   - ResourceDestroyed fired exactly 80 times (all untracked).
//   - Every HasRenderer() is false (no SDL_Texture handle leaked).
//   - GraphicsDevice tracking list is back to its pre-creation count.
//   - No crash (no double-free or use-after-free from stray SDL_Renderer calls).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/IDisposable.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int N = 40;    // instances of each type
static constexpr int TYPES = 2; // Texture2D, RenderTarget2D
static constexpr int TOTAL = N * TYPES;

class SdlResourceLeakTest : public Game
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

        int createCount  = 0;
        int destroyCount = 0;
        dev.ResourceCreated   += [&](System::Object*, const ResourceCreatedEventArgs&)   { ++createCount;  };
        dev.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyCount; };

        std::vector<std::unique_ptr<Texture2D>>      textures;
        std::vector<std::unique_ptr<RenderTarget2D>> rts;
        textures.reserve(N);
        rts.reserve(N);

        const std::size_t trackBefore = dev.GetTrackedResourceCount();

        for (int i = 0; i < N; ++i)
        {
            textures.push_back(std::make_unique<Texture2D>(dev, 4, 4));
            rts.push_back(std::make_unique<RenderTarget2D>(dev, 8, 8));
        }

        const std::size_t trackAfter = dev.GetTrackedResourceCount();

        check(createCount == TOTAL,
              "ResourceCreated fired exactly TOTAL times after creating 40 Texture2D + 40 RenderTarget2D");
        check(static_cast<int>(trackAfter - trackBefore) == TOTAL,
              "Tracking list grew by exactly TOTAL entries");

        for (auto& r : textures) disposeVia(*r);
        for (auto& r : rts)      disposeVia(*r);

        check(destroyCount == TOTAL,
              "ResourceDestroyed fired exactly TOTAL times after disposal");
        check(dev.GetTrackedResourceCount() == trackBefore,
              "Tracking list returned to pre-creation count after disposal");

        int leaked = 0;
        for (auto& r : textures) if (r->HasRenderer()) ++leaked;
        for (auto& r : rts)      if (r->HasRenderer()) ++leaked;
        check(leaked == 0, "No SDL_Texture handles remain after disposing all resources");

        textures.clear();
        rts.clear();
        check(true, "Clearing unique_ptrs after dispose does not crash");

        // Quick repeat to catch one-time initialization artifacts.
        createCount = destroyCount = 0;
        {
            Texture2D      tx(dev, 2, 2);
            RenderTarget2D rt(dev, 2, 2);
            disposeVia(tx);
            disposeVia(rt);
        }
        check(createCount == 2 && destroyCount == 2,
              "Second batch: 2 resources created and destroyed cleanly");

        dev.ResourceCreated.Clear();
        dev.ResourceDestroyed.Clear();

        // Device must remain fully functional after all the churn above.
        dev.Clear(Color(0, 255, 0, 255));
        Color pixel(0, 0, 0, 0);
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        check(pixel.getGProperty() >= 240,
              "GraphicsDevice remains fully functional after the leak-check churn above");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    SdlResourceLeakTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    SdlResourceLeakTest game;
    game.Run();
    return game.getResult();
}
