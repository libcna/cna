// SPDX-License-Identifier: MS-PL
//
// NOXNA. A backend-agnostic sprite benchmark, deliberately written against nothing but the public
// XNA API (no CNA::Internal::Backends::* include, no backend-specific hook) so the identical
// source can be compiled against CANVAS, EASYGL or HTML_DOM and produce a genuine like-for-like
// comparison -- built once per backend via three separate `emcmake` configures
// (cmake-build-canvas/cmake-build-easygl/cmake-build-htmldom), each producing its own
// cna_bench_graphics_backend.html.
//
// Same methodology as examples/htmldom_stress_test.cpp's own HTMLDOM-89 benchmark: N animated
// sprites/frame (position AND tint changing every frame -- the real steady-state case, not an
// idle one), timed with performance.now(), averaged over many frames after a warm-up frame that
// pays one-time setup cost. Reports ms/frame to console; the driver script/JS harness reads it
// back via window.__cnaBenchAvgMs for a single-number cross-backend comparison table.
//
// Not registered as a CTest -- like every other Emscripten-only browser page in this project, it
// needs a real browser (headless Chromium via Playwright), not `node`.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

// The only "which backend am I" this file needs -- a compile-time constant already computed from
// the CNA_BACKEND_* define BackendSelection.cmake sets, the same value every backend's own
// getCurrentGraphicsBackendName() call already reports elsewhere in this project. Using it instead
// of inventing a fresh per-benchmark compile definition keeps this file genuinely backend-agnostic
// source -- nothing about it changes per backend except which CNA_GRAPHICS_BACKEND the CMake
// configure step that builds it selected.
#include "CNA/GraphicsBackendType.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSpriteCount = 500;
    constexpr int kBenchmarkFrames = 60;

#if defined(__EMSCRIPTEN__)
    EM_JS(double, JsNow, (), { return performance.now(); });

    EM_JS(void, JsPublishResult, (double avgMs, const char* backendName), {
        window.__cnaBenchAvgMs = avgMs;
        window.__cnaBenchBackend = UTF8ToString(backendName);
        window.__cnaSmokeDone = true;
        window.__cnaSmokeResult = 0;
    });
#else
    double JsNow() { return 0.0; }
    void JsPublishResult(double, const char*) {}
#endif
}

class GraphicsBackendBenchmark : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    int frame_ = 0;
    double totalMs_ = 0.0;
    int framesTimed_ = 0;

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 2, 2, std::vector<std::uint8_t>{
                255, 255, 255, 255,  255, 255, 255, 255,
                255, 255, 255, 255,  255, 255, 255, 255,
            }));
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(Color(10, 10, 20, 255));

        const bool timed = frame_ > 1 && frame_ <= 1 + kBenchmarkFrames;
        const double t0 = timed ? JsNow() : 0.0;

        spriteBatch_->Begin();
        for (int i = 0; i < kSpriteCount; ++i)
        {
            const float x = static_cast<float>((i * 37) % 400);
            const float y = static_cast<float>((i * 53) % 200);
            const std::uint8_t r = static_cast<std::uint8_t>(
                std::fmod(static_cast<float>(frame_) * 3.7f + i, 256.0f));
            spriteBatch_->Draw(*texture_, Vector2(x, y),
                               Color(r, static_cast<std::uint8_t>(200), static_cast<std::uint8_t>(150),
                                     static_cast<std::uint8_t>(255)));
        }
        spriteBatch_->End();

        if (timed)
        {
            const double t1 = JsNow();
            totalMs_ += (t1 - t0);
            ++framesTimed_;
        }

        if (frame_ == 2 + kBenchmarkFrames)
        {
            const double avgMs = framesTimed_ > 0 ? totalMs_ / framesTimed_ : -1.0;
            const auto backendName = CNA::getCurrentGraphicsBackendName();
            std::printf("=== [%.*s] %d sprites/frame, %d frames: %.4f ms/frame (%.1f fps-equivalent) ===\n",
                        static_cast<int>(backendName.size()), backendName.data(),
                        kSpriteCount, framesTimed_, avgMs, avgMs > 0 ? 1000.0 / avgMs : 0.0);
            std::fflush(stdout);
            const std::string backendNameStr(backendName);
            JsPublishResult(avgMs, backendNameStr.c_str());
            Exit();
        }
    }

public:
    GraphicsBackendBenchmark()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(480);
        gdm_->setPreferredBackBufferHeightProperty(270);
    }
};

int main()
{
    GraphicsBackendBenchmark game;
    game.Run();
    return 0;
}
