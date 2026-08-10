// SPDX-License-Identifier: MS-PL
// D2D-108/D2D-116/D2D-117: throughput per draw branch, and a long resize/recovery/target-switch
// soak with a working-set oracle.
//
// Two deliberate design decisions:
//
//   * The benchmark REPORTS by default and only gates when CNA_DIRECT2D_BENCH_BASELINE_MS names a
//     baseline. A timing threshold on shared CI hardware, on WARP, or under Wine either has to be
//     so loose it proves nothing or it fails at random; a recorded baseline from the same class of
//     machine is the only honest way to detect a regression.
//   * The soak measures the process working set rather than counting objects. A leaked
//     ID2D1Bitmap1, a transient collection that stops being cleared, or a cache that never evicts
//     all show up there, and the debug-layer live-object gate covers the COM side separately.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

// NOGDI keeps <windows.h> from declaring the global ::Rectangle GDI function, which would make
// every unqualified Rectangle below ambiguous against Microsoft::Xna::Framework::Rectangle.
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#include <psapi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    [[nodiscard]] int EnvironmentInt(const char* name, int fallback)
    {
        const char* const value = std::getenv(name);
        if (value == nullptr || *value == '\0') return fallback;
        try
        {
            const int parsed = std::stoi(value);
            return parsed > 0 ? parsed : fallback;
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    [[nodiscard]] double EnvironmentDouble(const char* name, double fallback)
    {
        const char* const value = std::getenv(name);
        if (value == nullptr || *value == '\0') return fallback;
        try
        {
            return std::stod(value);
        }
        catch (const std::exception&)
        {
            return fallback;
        }
    }

    /// Current working set in bytes, or zero when the query is unavailable.
    [[nodiscard]] std::size_t WorkingSetBytes()
    {
        PROCESS_MEMORY_COUNTERS counters{};
        counters.cb = sizeof(counters);
        if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) return 0;
        return static_cast<std::size_t>(counters.WorkingSetSize);
    }
}

class Direct2DSoakTest final : public Game
{
public:
    Direct2DSoakTest()
    {
        manager_ = std::make_unique<GraphicsDeviceManager>(this);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(48);
        manager_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const { return result_; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        sprites_ = std::make_unique<SpriteBatch>(device);
        atlas_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 2, 2,
            std::vector<uint8_t>{255, 0, 0, 255, 0, 255, 0, 255,
                                 0, 0, 255, 255, 255, 255, 0, 255}));
        source_ = std::make_unique<RenderTarget2D>(device, 4, 4, false, SurfaceFormat::Color,
                                                   DepthFormat::None);
        const std::vector<Color> sourcePixels(16, Color(40, 80, 120, 255));
        source_->SetData(sourcePixels.data(), static_cast<int>(sourcePixels.size()));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        try
        {
            RunBenchmark(device);
            RunSoak(device);
        }
        catch (const std::exception& error)
        {
            std::printf("[FAIL] Direct2D soak raised: %s\n", error.what());
            result_ = 1;
        }
        Exit();
    }

private:
    struct BranchResult
    {
        const char* name;
        double millisecondsPerThousand;
    };

    /// One measured pass of `count` sprites through a chosen branch.
    [[nodiscard]] double MeasureBranch(GraphicsDevice& device, int count, int branch)
    {
        SamplerState point = SamplerState::PointClamp;
        const auto started = std::chrono::steady_clock::now();
        device.Clear(Color::Black);
        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, nullptr);
        for (int index = 0; index < count; ++index)
        {
            const int x = (index * 7) % 60;
            const int y = (index * 11) % 44;
            const Rectangle destination(x, y, 4, 4);
            switch (branch)
            {
                case 0: // plain bitmap: no tint, no flip, in-bounds crop
                    sprites_->Draw(*atlas_, destination, Rectangle(0, 0, 2, 2), Color::White);
                    break;
                case 1: // ImageBrush: a flip forces the brush path without needing an effect
                    sprites_->Draw(*atlas_, destination, Rectangle(0, 0, 2, 2), Color::White,
                                   0.0f, Vector2::Zero, SpriteEffects::FlipHorizontally, 0.0f);
                    break;
                case 2: // decorated: a tint takes the effect path natively and the CPU fallback
                        // elsewhere; both are the branch this row is about
                    sprites_->Draw(*atlas_, destination, Rectangle(0, 0, 2, 2),
                                   Color(200, 150, 100, 255));
                    break;
                default: // render-target source
                    sprites_->Draw(*source_, destination, Rectangle(0, 0, 4, 4), Color::White);
                    break;
            }
        }
        sprites_->End();
        // Force the batch to actually execute before stopping the clock; without a readback the
        // measurement would only cover command recording.
        Color pixel(0, 0, 0, 0);
        const Rectangle probe(0, 0, 1, 1);
        device.GetBackBufferData(&probe, &pixel, 0, 1);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        const double milliseconds = std::chrono::duration<double, std::milli>(elapsed).count();
        return milliseconds * 1000.0 / static_cast<double>(count);
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        const int sprites = EnvironmentInt("CNA_DIRECT2D_BENCH_SPRITES", 512);
        const std::array<const char*, 4> names{
            "plain-bitmap", "image-brush", "decorated", "render-target-source"};
        std::array<BranchResult, 4> results{};
        for (int branch = 0; branch < 4; ++branch)
        {
            // Warm-up pass: first-touch allocations, effect capability probes and the caches must
            // not be attributed to the measured pass.
            (void)MeasureBranch(device, sprites, branch);
            results[static_cast<std::size_t>(branch)] =
                BranchResult{names[static_cast<std::size_t>(branch)],
                             MeasureBranch(device, sprites, branch)};
        }

        const bool forcedWarp = std::getenv("CNA_DIRECT2D_FORCE_WARP") != nullptr;
        for (const BranchResult& result : results)
        {
            std::printf("[BENCH] branch=%s driver=%s sprites=%d ms-per-1000=%.4f\n",
                        result.name, forcedWarp ? "WARP-forced" : "default", sprites,
                        result.millisecondsPerThousand);
        }

        // D2D-116: gate only against an explicitly supplied baseline from comparable hardware.
        const double baseline = EnvironmentDouble("CNA_DIRECT2D_BENCH_BASELINE_MS", 0.0);
        if (baseline <= 0.0)
        {
            std::printf("[BENCH] no CNA_DIRECT2D_BENCH_BASELINE_MS supplied; reporting only.\n");
            return;
        }
        const double tolerance = EnvironmentDouble("CNA_DIRECT2D_BENCH_TOLERANCE", 2.0);
        for (const BranchResult& result : results)
        {
            if (result.millisecondsPerThousand <= baseline * tolerance) continue;
            std::printf("[FAIL] branch=%s ms-per-1000=%.4f exceeds baseline %.4f x tolerance %.2f\n",
                        result.name, result.millisecondsPerThousand, baseline, tolerance);
            result_ = 1;
        }
    }

    void RunSoak(GraphicsDevice& device)
    {
        const int cycles = EnvironmentInt("CNA_DIRECT2D_SOAK_CYCLES", 2000);
        SamplerState point = SamplerState::PointClamp;
        std::size_t warmWorkingSet = 0;
        const int warmUpCycles = 32;

        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            // Resource churn: a target and a texture are created and destroyed every cycle, so a
            // registry or cache that never releases anything grows without bound.
            RenderTarget2D target(device, 8, 8, false, SurfaceFormat::Color, DepthFormat::None);
            auto scratch = Texture2D::CreateFromPixels(
                device, 1, 1,
                std::vector<uint8_t>{static_cast<uint8_t>(cycle & 0xFF), 16, 32, 255});

            device.SetRenderTarget(&target);
            device.Clear(Color(0, 0, 0, 255));
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr,
                            nullptr);
            sprites_->Draw(scratch, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color::White);
            sprites_->End();
            device.SetRenderTarget(nullptr);

            device.Clear(Color::Black);
            sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr,
                            nullptr);
            sprites_->Draw(target, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 8, 8), Color::White);
            sprites_->End();

            Color pixel(0, 0, 0, 0);
            const Rectangle probe(1, 1, 1, 1);
            device.GetBackBufferData(&probe, &pixel, 0, 1);
            const Color expected(static_cast<int>(cycle & 0xFF), 16, 32, 255);
            if (pixel != expected)
            {
                std::printf("[FAIL] soak cycle %d read (%d,%d,%d,%d), expected (%d,%d,%d,%d)\n",
                            cycle, pixel.getRProperty(), pixel.getGProperty(),
                            pixel.getBProperty(), pixel.getAProperty(), expected.getRProperty(),
                            expected.getGProperty(), expected.getBProperty(),
                            expected.getAProperty());
                result_ = 1;
                return;
            }

            // Device recovery and a real resize are the two state transitions most likely to leak
            // or to leave the device drifting; both run repeatedly rather than once.
            if (cycle % 250 == 249) device.GetRenderer().DebugSimulateContextLoss();
            if (cycle % 500 == 499)
            {
                manager_->setPreferredBackBufferWidthProperty((cycle % 1000 == 999) ? 64 : 72);
                manager_->ApplyChanges();
            }
            if (cycle == warmUpCycles) warmWorkingSet = WorkingSetBytes();
        }

        const std::size_t finalWorkingSet = WorkingSetBytes();
        if (warmWorkingSet == 0 || finalWorkingSet == 0)
        {
            std::printf("[WARN] working-set query unavailable; soak completed %d cycles without a "
                        "memory oracle.\n", cycles);
            return;
        }
        // Generous on purpose: the oracle is for unbounded growth, not for allocator noise. A real
        // per-cycle leak of even one small bitmap crosses this within a few hundred cycles.
        const std::size_t allowedGrowth = 64u * 1024u * 1024u;
        const bool stable = finalWorkingSet <= warmWorkingSet + allowedGrowth;
        std::printf("[%s] soak cycles=%d working-set warm=%zu final=%zu growth=%lld bytes\n",
                    stable ? "PASS" : "FAIL", cycles, warmWorkingSet, finalWorkingSet,
                    static_cast<long long>(finalWorkingSet) - static_cast<long long>(warmWorkingSet));
        if (!stable) result_ = 1;
    }

    std::unique_ptr<GraphicsDeviceManager> manager_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<Texture2D> atlas_;
    std::unique_ptr<RenderTarget2D> source_;
    bool done_ = false;
    int result_ = 0;
};

int main()
{
    Direct2DSoakTest test;
    test.Run();
    return test.Result();
}
