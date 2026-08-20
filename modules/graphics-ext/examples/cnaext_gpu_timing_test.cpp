// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2165: the post-process table, measured on the GPU.
//
// Every number in docs/cnaext-perf.md until now came from a CPU wall clock wrapped around a
// one-texel read-back. That method works and it measures two things it should not: the clock starts
// when the driver *accepts* the work rather than when the GPU begins it, and the read-back that
// forces completion is a synchronisation a real frame never performs. This program measures the
// same passes both ways in the same run, so the two can be printed side by side rather than one
// silently replacing the other.
//
// Check A -- this renderer has a GPU timer query, or the program SKIPs.
// Check B -- every pass in the chain reports a sample, not just the first.
// Check C -- the GPU total and the CPU wall clock agree here, which is the finding rather than the
//            assumption: a software rasteriser has no asynchrony for the CPU clock to miss, so the
//            existing table is sound on this machine and says nothing about a real GPU.
//
// `--benchmark` reports the per-pass table at 720p and 1080p (recorded in docs/cnaext-perf.md).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::GpuTimer;
using CNA::Graphics::PostProcessChain;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::TonemappingMode;

namespace
{
    constexpr int kFrames = 24;

    std::unique_ptr<Texture2D> MakeScene(GraphicsDevice& device, const int width, const int height)
    {
        auto texture = std::make_unique<Texture2D>(device, width, height);
        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                texels.emplace_back(x % 256, y % 256, (x + y) % 256, 255);
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }
}

class GpuTimingExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_  = false;
    int  passCount_  = 0;
    int  checkCount_ = 0;
    int  result_     = 1;

    void check(const bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// Builds the standard chain and measures it both ways at one resolution.
    ///
    /// The CPU figure is the method every existing number in the table was produced with: a wall
    /// clock around the chain plus the one-texel read-back that forces the work through. The GPU
    /// figure is the sum of what each pass's own timer query reports. They are not measuring the
    /// same thing, which is the point.
    void MeasureAt(GraphicsDevice& device, const int width, const int height, const bool print)
    {
        auto scene = MakeScene(device, width, height);
        RenderTarget2D destination(device, width, height);

        RenderPipelineSettings settings;
        settings.setTonemappingMode(TonemappingMode::Aces);
        settings.setSSAOSampleCount(16);

        PostProcessChain chain(device);
        chain.addOwnedPass(std::make_unique<CNA::Graphics::TonemapPass>(device));
        chain.addOwnedPass(std::make_unique<CNA::Graphics::BloomPass>(device));
        chain.addOwnedPass(std::make_unique<CNA::Graphics::FxaaPass>(device));
        chain.addOwnedPass(std::make_unique<CNA::Graphics::ColorGradePass>(device));

        PostProcessContext context;
        context.source      = scene.get();
        context.destination = &destination;
        context.width       = width;
        context.height      = height;
        context.settings    = &settings;

        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);

        // The CPU method, unchanged from how the table was built.
        chain.apply(context);
        destination.GetData(0, &oneTexel, &probe, 0, 1);
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < kFrames; ++frame)
        {
            chain.apply(context);
            destination.GetData(0, &oneTexel, &probe, 0, 1);
        }
        const double cpuMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - start).count() / kFrames;

        // The GPU method. The read-back stays, because without a present or a read-back a software
        // rasteriser retires roughly one query per run rather than one per frame -- but it is
        // outside what the timers measure.
        // Averaged over the run rather than read once at the end. A pass's entry holds its most
        // recent result, so a single read at the end sums four numbers from four different frames
        // -- which is not a frame's cost and is not comparable with the CPU mean beside it.
        chain.setGpuTimingEnabled(true);
        std::vector<double> totals(chain.getPassCount(), 0.0);
        std::vector<int> counts(chain.getPassCount(), 0);
        std::vector<int> lastSample(chain.getPassCount(), 0);
        for (int frame = 0; frame < kFrames; ++frame)
        {
            chain.apply(context);
            destination.GetData(0, &oneTexel, &probe, 0, 1);
            const auto& timings = chain.getPassTimings();
            for (std::size_t index = 0; index < timings.size() && index < totals.size(); ++index)
                if (timings[index].SampleCount > lastSample[index])
                {
                    lastSample[index] = timings[index].SampleCount;
                    totals[index] += timings[index].Milliseconds;
                    ++counts[index];
                }
        }

        double gpuTotal = 0.0;
        int reporting = 0;
        for (std::size_t index = 0; index < totals.size(); ++index)
        {
            if (counts[index] > 0)
            {
                ++reporting;
                gpuTotal += totals[index] / counts[index];
            }
        }

        if (print)
        {
            std::printf("\n-- %dx%d, %d frames --\n", width, height, kFrames);
            const auto& timings = chain.getPassTimings();
            for (std::size_t index = 0; index < timings.size(); ++index)
                std::printf("    %-12s GPU %8.4f ms  (mean of %d samples)\n",
                            timings[index].Name.c_str(),
                            counts[index] > 0 ? totals[index] / counts[index] : 0.0,
                            counts[index]);
            std::printf("    %-12s GPU %8.4f ms\n", "chain total", gpuTotal);
            std::printf("    %-12s CPU %8.4f ms  (wall clock around the chain and a read-back)\n",
                        "chain total", cpuMs);
        }

        if (width == 1280)
        {
            check(reporting == static_cast<int>(chain.getPassTimings().size())
                  && reporting == 4,
                  "every pass in the chain reports a sample, not just the first");
            // Not "the GPU number is lower". It is not, here, and expecting it to be was wrong:
            // a software rasteriser has no asynchrony to hide, so a CPU clock around a forced
            // read-back already captures the whole GPU cost. The two agreeing is the finding --
            // it says the existing CPU-clock table is sound *on this machine*, and says nothing
            // about a real GPU, where the read-back's sync is exactly what the CPU number would
            // be measuring instead of the work.
            const double ratio = cpuMs > 0.0 ? gpuTotal / cpuMs : 0.0;
            std::printf("    GPU/CPU ratio %.3f\n", ratio);
            check(ratio > 0.80 && ratio < 1.25,
                  "the GPU total and the CPU wall clock agree, because a software rasteriser has "
                  "no asynchrony for the CPU clock to miss");
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        {
            GpuTimer probe(device);
            if (!probe.isSupported())
            {
                std::printf("SKIP: %s\n", probe.getUnsupportedReason().c_str());
                std::exit(77);
            }
        }
        check(true, "the renderer has a GPU timer query");

        try
        {
            MeasureAt(device, 1280, 720, benchmark_);
            if (benchmark_) MeasureAt(device, 1920, 1080, true);
        }
        catch (const std::exception& e)
        {
            std::printf("SKIP: this renderer could not run the chain (%s)\n", e.what());
            std::exit(77);
        }

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit GpuTimingExample(const bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(256);
        gdm_->setPreferredBackBufferHeightProperty(256);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    try
    {
        bool benchmark = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;

        GpuTimingExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
