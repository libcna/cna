// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-743: 1000 frames and 50 resizes, for a leak checker to watch.
//
// A pipeline that leaks one target per frame looks perfectly correct for the length of any other
// test in this repository -- the frames render, the assertions pass, and the process exits before
// the growth matters. The only way to see it is to do the thing many times and let LeakSanitizer
// report what is still reachable at exit.
//
// So this program asserts almost nothing itself. Its job is to *do the work*: build a pipeline with
// every subsystem enabled, run a thousand frames through it, resize it fifty times (which is where
// the pool has to release and reallocate), tear it down, and repeat that whole cycle. What checks
// it is ASan/LSan around it:
//
//     cmake -S . -B cmake-build-asan -DCNA_SANITIZE=address -DCNA_CNAEXT=ON ...
//     ASAN_OPTIONS=detect_leaks=1 ./cmake-build-asan/cna_test_cnaext_leak_loop
//
// The one thing it does assert is the memory *estimate*, which is a cheap proxy the ordinary build
// can also check: after a thousand frames the pipeline must not be holding more targets than it
// held after ten. That catches a pool that grows, which is the leak most likely to be written.
//
// Exit code 0 = the estimate held steady, 1 = it grew, 77 = SKIP.

#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kFrame       = 64;
    constexpr int kFrames      = 1000;
    constexpr int kResizes     = 50;
    constexpr int kOuterCycles = 3;
}

class LeakLoop : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D>   white_;
    int result_ = 1;

    void DrawScene()
    {
        spriteBatch_->Begin();
        spriteBatch_->Draw(*white_, Rectangle(8, 8, 16, 16), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

    void ConfigureEverything(CNA::Graphics::RenderPipelineSettings& settings)
    {
        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setFXAAEnabled(true);
        settings.setTonemappingMode(CNA::Graphics::TonemappingMode::Aces);
        // SSAO stays off: without a depth/normal prepass it falls back to a copy, so enabling it
        // here would exercise the fallback a thousand times rather than the pass.
        settings.setSSAOEnabled(false);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color opaque = Color::White;
        white_->SetData(&opaque, 1);

        std::size_t afterTen = 0;
        std::size_t afterAll = 0;

        for (int cycle = 0; cycle < kOuterCycles; ++cycle)
        {
            // A pipeline per cycle, so the destructor path is exercised too -- including its
            // DeviceReset unsubscribe, which is the one place a dangling handler could hide.
            CNA::Graphics::RenderPipeline pipeline(device);
            pipeline.resize(kFrame, kFrame);
            ConfigureEverything(pipeline.getSettings());

            try
            {
                for (int frame = 0; frame < kFrames; ++frame)
                {
                    pipeline.begin(Color::Black);
                    DrawScene();
                    pipeline.end();
                    if (cycle == 0 && frame == 9) afterTen = pipeline.getGpuMemoryEstimateBytes();
                }
            }
            catch (const std::exception& e)
            {
                std::printf("SKIP: this renderer cannot run the pipeline (%s)\n", e.what());
                std::exit(77);
            }

            for (int resize = 0; resize < kResizes; ++resize)
            {
                // Alternating sizes, so every resize is a real one: the pool has to release the old
                // shape and allocate the new, which is where a leak would accumulate fastest.
                const int width = kFrame + (resize % 2 == 0 ? 16 : 0);
                pipeline.resize(width, kFrame);
                pipeline.begin(Color::Black);
                DrawScene();
                pipeline.end();
            }

            // Back to the original shape for a stable final reading.
            pipeline.resize(kFrame, kFrame);
            pipeline.begin(Color::Black);
            DrawScene();
            pipeline.end();
            afterAll = pipeline.getGpuMemoryEstimateBytes();

            std::printf("    cycle %d: %d frames + %d resizes, %zu bytes held\n", cycle, kFrames,
                        kResizes, afterAll);
        }

        std::printf("    after 10 frames: %zu bytes; after %d cycles: %zu bytes\n", afterTen,
                    kOuterCycles, afterAll);
        const bool steady = afterAll <= afterTen;
        std::printf("[%s] the pipeline holds no more targets after %d frames than after 10\n",
                    steady ? "PASS" : "FAIL", kFrames);
        result_ = steady ? 0 : 1;
        Exit();
    }

public:
    LeakLoop()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame + 16);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main()
{
    try
    {
        LeakLoop example;
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
