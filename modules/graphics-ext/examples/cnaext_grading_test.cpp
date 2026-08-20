// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2133: grading and output end to end, and what each part costs.
//
// The unit tests take a table apart. This drives the whole delivery path the way a game does: write
// a .cube file, load it from disk, grade a frame through it, and dither the result on the way to an
// eight-bit target. The costs matter here in a way they do not for a shadow pass, because these are
// the passes a game is most tempted to leave on unconditionally.
//
// Check A -- this renderer runs the grade, or the program SKIPs.
// Check B -- a .cube file read from disk grades the frame it names.
// Check C -- MOD-2131: tetrahedral keeps a neutral neutral where trilinear tints it.
// Check D -- MOD-2132: the dither turns a banded ramp into a gradient again.
//
// `--benchmark` reports the cost of each interpolation, each table layout, and the dither
// (recorded in `docs/cnaext-perf.md`).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/CubeLut.hpp"
#include "CNA/Graphics/LutInterpolation.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::ColorGradePass;
using CNA::Graphics::CubeLut;
using CNA::Graphics::LutInterpolation;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::TonemapPass;
using CNA::Graphics::TonemappingMode;

namespace
{
    constexpr int kFrame     = 256;
    constexpr int kTableSize = 8;
    constexpr const char* kProbeFile = "cnaext_grading_probe.cube";

    /// A saturation boost that gets stronger with brightness: non-separable, nonlinear, and it maps
    /// every neutral to a neutral -- the three properties check C needs to say anything.
    Vector3 Grade(const Vector3& colour)
    {
        const float luminance = 0.2126f * colour.X + 0.7152f * colour.Y + 0.0722f * colour.Z;
        const float saturation = 1.0f + 3.0f * luminance;
        const auto channel = [&](const float value) {
            return std::clamp(luminance + (value - luminance) * saturation, 0.0f, 1.0f);
        };
        return Vector3(channel(colour.X), channel(colour.Y), channel(colour.Z));
    }

    bool WriteProbeCube(const int size)
    {
        std::ofstream file(kProbeFile);
        if (!file) return false;
        file << "TITLE \"luminance saturation\"\nLUT_3D_SIZE " << size << "\n";
        const float last = static_cast<float>(size - 1);
        for (int blue = 0; blue < size; ++blue)
            for (int green = 0; green < size; ++green)
                for (int red = 0; red < size; ++red)
                {
                    const Vector3 value = Grade(Vector3(static_cast<float>(red) / last,
                                                        static_cast<float>(green) / last,
                                                        static_cast<float>(blue) / last));
                    file << value.X << " " << value.Y << " " << value.Z << "\n";
                }
        return static_cast<bool>(file);
    }

    double MillisecondsOf(const int repeats, const std::function<void()>& work)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i) work();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() /
               static_cast<double>(repeats);
    }

    std::unique_ptr<Texture2D> MakeGreyRamp(GraphicsDevice& device, const int width,
                                            const int height)
    {
        auto texture = std::make_unique<Texture2D>(device, width, height);
        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const int level = x * 255 / std::max(1, width - 1);
                texels.emplace_back(level, level, level, 255);
            }
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }
}

class GradingExample : public Game
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

    std::vector<Color> ReadFrame(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (...)
        {
            std::printf("SKIP: this renderer has no readable back buffer\n");
            std::exit(77);
        }
        return pixels;
    }

    static int WorstNeutralSpread(const std::vector<Color>& pixels)
    {
        int worst = 0;
        for (const Color& pixel : pixels)
        {
            const int high = std::max({pixel.getRProperty(), pixel.getGProperty(),
                                       pixel.getBProperty()});
            const int low  = std::min({pixel.getRProperty(), pixel.getGProperty(),
                                       pixel.getBProperty()});
            worst = std::max(worst, high - low);
        }
        return worst;
    }

    static int DistinctColumnMeans(const std::vector<Color>& pixels)
    {
        std::set<long> seen;
        for (int x = 0; x < kFrame; ++x)
        {
            double total = 0.0;
            for (int y = 0; y < kFrame; ++y)
                total += pixels[static_cast<std::size_t>(y) * kFrame + x].getRProperty();
            seen.insert(std::lround(total / kFrame * 100.0));
        }
        return static_cast<int>(seen.size());
    }

    /// MOD-2133. Both dials, at two resolutions, each forced to completion inside the timed region.
    void RunBenchmark(GraphicsDevice& device, const CubeLut& lut)
    {
        std::printf("\n-- grading and output --\n");
        for (const int size : {720, 1080})
        {
            auto source = MakeGreyRamp(device, size, size);
            auto strip  = lut.createStripTexture(device);
            RenderTarget2D destination(device, size, size);

            PostProcessContext context;
            context.source      = source.get();
            context.destination = &destination;
            context.width       = size;
            context.height      = size;

            Color probe = Color::Black;
            const Rectangle oneTexel(0, 0, 1, 1);

            ColorGradePass grade(device);
            grade.setLut(strip.get());

            const auto timed = [&](const char* label, const std::function<void()>& work) {
                work();
                destination.GetData(0, &oneTexel, &probe, 0, 1);
                const double ms = MillisecondsOf(20, [&] {
                    work();
                    // Without this the driver may return before running the pass, and the
                    // measurement reports submission rather than work.
                    destination.GetData(0, &oneTexel, &probe, 0, 1);
                });
                std::printf("    %4dx%-4d  %-42s %7.3f ms\n", size, size, label, ms);
            };

            grade.setInterpolation(LutInterpolation::Trilinear);
            timed("grade, strip, trilinear (hardware filtered)", [&] { grade.apply(context); });
            grade.setInterpolation(LutInterpolation::Tetrahedral);
            timed("grade, strip, tetrahedral", [&] { grade.apply(context); });

            if (device.SupportsCapability(CNA::GraphicsCapability::Texture3D))
            {
                auto volume = lut.createVolumeTexture(device);
                grade.setVolumeLut(volume.get());
                grade.setInterpolation(LutInterpolation::Trilinear);
                timed("grade, volume, trilinear", [&] { grade.apply(context); });
                grade.setInterpolation(LutInterpolation::Tetrahedral);
                timed("grade, volume, tetrahedral", [&] { grade.apply(context); });
                grade.setVolumeLut(nullptr);
            }

            TonemapPass tonemap(device);
            tonemap.setMode(TonemappingMode::Aces);
            tonemap.setDebandEnabled(false);
            timed("tonemap (ACES), no dither", [&] { tonemap.apply(context); });
            tonemap.setDebandEnabled(true);
            timed("tonemap (ACES), debanding dither", [&] { tonemap.apply(context); });
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        ColorGradePass grade(device);
        if (!grade.isSupported(device))
        {
            std::printf("SKIP: this renderer cannot run the colour grade\n");
            std::exit(77);
        }
        check(true, "the renderer runs the colour grade");

        if (!WriteProbeCube(kTableSize))
        {
            std::printf("SKIP: cannot write a probe .cube in the working directory\n");
            std::exit(77);
        }
        CubeLut lut = CubeLut::parse("LUT_3D_SIZE 2\n0 0 0\n0 0 0\n0 0 0\n0 0 0\n"
                                     "0 0 0\n0 0 0\n0 0 0\n0 0 0\n");
        try
        {
            lut = CubeLut::loadFromFile(kProbeFile);
        }
        catch (const std::exception& e)
        {
            std::printf("FAIL: the probe .cube did not load: %s\n", e.what());
            std::remove(kProbeFile);
            result_ = 1;
            Exit();
            return;
        }
        std::remove(kProbeFile);

        auto strip = lut.createStripTexture(device);
        auto ramp  = MakeGreyRamp(device, kFrame, kFrame);

        PostProcessContext context;
        context.source      = ramp.get();
        context.destination = nullptr;   // straight to the back buffer
        context.width       = kFrame;
        context.height      = kFrame;

        grade.setLut(strip.get());
        grade.setInterpolation(LutInterpolation::Trilinear);
        grade.apply(context);
        std::vector<Color> frame = ReadFrame(device);
        const int trilinear = WorstNeutralSpread(frame);

        // The file really did carry a grade: a saturation table leaves a mid grey where it was and
        // an extreme one clamped, so the check is that the frame is not the ramp it started as.
        int changed = 0;
        for (int x = 0; x < kFrame; ++x)
        {
            const int expected = x * 255 / (kFrame - 1);
            if (std::abs(frame[static_cast<std::size_t>(kFrame / 2) * kFrame + x].getRProperty()
                         - expected) > 4) ++changed;
        }
        std::printf("    %d of %d ramp columns were moved by the loaded table\n", changed, kFrame);
        check(changed > kFrame / 8, "a .cube file read from disk grades the frame it names");

        grade.setInterpolation(LutInterpolation::Tetrahedral);
        grade.apply(context);
        const int tetrahedral = WorstNeutralSpread(ReadFrame(device));
        std::printf("    worst neutral spread: trilinear %d, tetrahedral %d (of 255)\n",
                    trilinear, tetrahedral);
        check(trilinear > 4 && tetrahedral <= 1,
              "tetrahedral keeps a neutral neutral where trilinear tints it");

        // MOD-2132. An exposure of 0.02 puts the 256-value ramp onto six output values, so the
        // undithered frame is six flat bands.
        TonemapPass tonemap(device);
        tonemap.setMode(TonemappingMode::None);
        tonemap.setExposure(0.02f);
        tonemap.setGamma(1.0f);
        tonemap.setDebandEnabled(false);
        tonemap.apply(context);
        const int banded = DistinctColumnMeans(ReadFrame(device));
        tonemap.setDebandEnabled(true);
        tonemap.apply(context);
        const int dithered = DistinctColumnMeans(ReadFrame(device));
        std::printf("    distinct column means over the ramp: banded %d, dithered %d\n",
                    banded, dithered);
        check(banded < 12 && dithered > banded * 8,
              "the dither turns a banded ramp back into a gradient");

        if (benchmark_) RunBenchmark(device, lut);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit GradingExample(const bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
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

        GradingExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
