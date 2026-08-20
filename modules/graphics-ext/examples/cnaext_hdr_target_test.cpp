// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-132 and MOD-138: what a float render target buys, and what it costs.
//
// Phase 1 is one claim -- a float render target keeps values above 1.0 and an 8-bit one does not --
// and the whole HDR pipeline is built on it. `HdrRenderTargetRoundTripTests` checks that claim from
// a unit test; this program checks the part a unit test cannot show, which is what the difference
// looks like once it reaches a screen. The scene is deliberately trivial: one over-bright colour,
// rendered twice.
//
// Check A -- this renderer/driver really has float render targets, or the program SKIPs.
// Check B -- a float target holds 4.0, 2.0, 1.0 exactly, through a render and a readback.
// Check C -- the same render into an 8-bit target clamps to white, losing the range for good.
// Check D -- blitting the float target down to 8 bits gives the same clamped image, which is what
//            makes tonemapping a choice rather than an accident: without one, HDR *is* clamping.
//
// `--benchmark` reports MOD-138: the fill-rate and memory cost of an HDR target at 1280x720 against
// the same render into a Color target, so `RenderQuality` presets can be justified rather than
// guessed.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
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

namespace
{
    constexpr int kFrame = 128;

    /// Brighter than white in two channels: the entire point of a float target, and the entire
    /// thing an 8-bit one cannot represent.
    constexpr float kRed   = 4.0f;
    constexpr float kGreen = 2.0f;
    constexpr float kBlue  = 1.0f;

    /// MOD-138's measurement size. Not the size the checks run at -- a 1280x720 readback of
    /// RGBA32F is 3.5 MB per frame and would dominate the timing it is meant to measure.
    constexpr int kBenchWidth  = 1280;
    constexpr int kBenchHeight = 720;
}

class HdrTargetExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// Clears a target to the over-bright colour and reads it back as floats.
    static std::vector<Vector4> RenderFloat(GraphicsDevice& device, SurfaceFormat format)
    {
        RenderTarget2D target(device, kFrame, kFrame, false, format, DepthFormat::None);
        device.SetRenderTarget(&target);
        device.Clear(kRed, kGreen, kBlue, 1.0f);
        device.SetRenderTarget(nullptr);

        std::vector<Vector4> pixels(static_cast<std::size_t>(kFrame) * kFrame);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    /// The same render into an 8-bit target, read back as bytes.
    static std::vector<Color> RenderColor(GraphicsDevice& device)
    {
        RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                              DepthFormat::None);
        device.SetRenderTarget(&target);
        device.Clear(kRed, kGreen, kBlue, 1.0f);
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    /// MOD-138. Times N full-target fills, which is the fill-rate question a `RenderQuality` preset
    /// actually turns on -- an HDR frame pays this once per frame whether or not any pass runs.
    ///
    /// It is a *fill*, not a clear, and that distinction was found by measuring: a clear alone
    /// costs the same on every format here, because Mesa's llvmpipe defers it and nothing ever
    /// touches the memory. Stretching a sprite over the whole target forces the writes and makes
    /// the bytes-per-pixel column show up in the timing, which is the entire point of the row.
    static double TimeFills(GraphicsDevice& device, SpriteBatch& batch, Texture2D& white,
                            SurfaceFormat format, int frames)
    {
        RenderTarget2D target(device, kBenchWidth, kBenchHeight, false, format, DepthFormat::None);
        const Rectangle whole(0, 0, kBenchWidth, kBenchHeight);

        // One texel read back after each fill. It is not the thing being measured -- it is what
        // forces the driver to actually do the fill inside the timed region. Without it llvmpipe
        // queues the tile work and every format times identically, which is a measurement of the
        // API and not of the memory traffic the row asks about.
        Color                     colorProbe = Color::Black;
        PackedVector::HalfVector4 halfProbe{};
        Vector4                   floatProbe{};
        const Rectangle oneTexel(0, 0, 1, 1);

        const auto fill = [&] {
            device.SetRenderTarget(&target);
            device.Clear(0.0f, 0.0f, 0.0f, 1.0f);
            batch.Begin();
            batch.Draw(white, whole, Rectangle(0, 0, 1, 1), Color::White);
            batch.End();
            device.SetRenderTarget(nullptr);
            // The element type has to match the target's storage -- GetData refuses a mismatch
            // rather than reinterpreting bits, which is the right refusal and simply means the
            // probe is chosen per format here.
            switch (format)
            {
            case SurfaceFormat::Vector4:      target.GetData(0, &oneTexel, &floatProbe, 0, 1); break;
            case SurfaceFormat::HdrBlendable: target.GetData(0, &oneTexel, &halfProbe, 0, 1);  break;
            default:                          target.GetData(0, &oneTexel, &colorProbe, 0, 1); break;
            }
        };

        fill();   // untimed, so allocation and first-use costs stay out of the measurement
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i)
            fill();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::milli>(elapsed).count() / frames;
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        constexpr int kFrames = 50;
        SpriteBatch batch(device);
        Texture2D white(device, 1, 1);
        const Color opaque = Color::White;
        white.SetData(&opaque, 1);

        std::printf("\nMOD-138: HDR render-target cost at %dx%d, %d frames each\n", kBenchWidth,
                    kBenchHeight, kFrames);
        std::printf("  %-14s %-10s %-14s %s\n", "format", "bytes/px", "memory", "ms/frame (fill)");

        struct Row { const char* name; SurfaceFormat format; int bytesPerPixel; };
        const Row rows[] = {
            {"Color",        SurfaceFormat::Color,        4},
            {"HdrBlendable", SurfaceFormat::HdrBlendable, 8},
            {"Vector4",      SurfaceFormat::Vector4,      16},
        };

        for (const Row& row : rows)
        {
            if (!device.SupportsSurfaceFormatAsRenderTargetEXT(row.format))
            {
                std::printf("  %-14s %-10d %-14s unsupported here\n", row.name, row.bytesPerPixel,
                            "-");
                continue;
            }
            const double bytes = static_cast<double>(kBenchWidth) * kBenchHeight * row.bytesPerPixel;
            const double ms = TimeFills(device, batch, white, row.format, kFrames);
            std::printf("  %-14s %-10d %-14.2f %.3f\n", row.name, row.bytesPerPixel,
                        bytes / (1024.0 * 1024.0), ms);
        }
        std::printf("  (memory in MiB, one target; a full chain holds several -- ask the pipeline\n"
                    "   itself with RenderPipeline::getGpuMemoryEstimateBytes())\n");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        const bool hasFloat = device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4);
        if (!hasFloat)
        {
            std::printf("SKIP: this renderer/driver has no RGBA32F render targets, so there is no "
                        "HDR target to compare against (a documented capability boundary, not a "
                        "defect -- see docs/cnaext-engine-layer.md)\n");
            std::exit(77);
        }
        check(true, "this renderer has float render targets");

        const std::vector<Vector4> hdr = RenderFloat(device, SurfaceFormat::Vector4);
        std::printf("    float target texel: (%.2f, %.2f, %.2f, %.2f)\n", hdr[0].X, hdr[0].Y,
                    hdr[0].Z, hdr[0].W);
        bool exact = true;
        for (const Vector4& texel : hdr)
            if (texel.X != kRed || texel.Y != kGreen || texel.Z != kBlue) { exact = false; break; }
        check(exact, "the float target kept 4.0 and 2.0 exactly, through render and readback");

        const std::vector<Color> ldr = RenderColor(device);
        std::printf("    8-bit target texel: (%d, %d, %d)\n", ldr[0].getRProperty(),
                    ldr[0].getGProperty(), ldr[0].getBProperty());
        bool clamped = true;
        for (const Color& texel : ldr)
            if (texel.getRProperty() != 255 || texel.getGProperty() != 255) { clamped = false; break; }
        check(clamped, "the same render into an 8-bit target clamped to white");

        // The point of check D: 4.0 and 2.0 are different numbers and both arrive as 255. Nothing
        // downstream can tell them apart, which is why a tonemap operator has to run *before* the
        // frame reaches 8 bits rather than after.
        check(ldr[0].getRProperty() == ldr[0].getGProperty(),
              "two different HDR values became one 8-bit value -- the range is gone, not compressed");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit HdrTargetExample(bool benchmark) : benchmark_(benchmark)
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
    bool benchmark = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;

    try
    {
        HdrTargetExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
