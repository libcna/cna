// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-604, MOD-607, MOD-608: FXAA on and off, and what it costs on each kind of image.
//
// FXAA's whole job is to remove a specific artefact -- the stair-stepping along a high-contrast
// diagonal -- so this measures exactly that: the total contrast between vertically adjacent texels
// along a drawn diagonal. Aliasing *is* that contrast, so a filter that works reduces it, and one
// that has smeared the whole frame reduces something else too. Both are checked.
//
// Check A -- FXAA disabled leaves the frame bit-identical to no pipeline at all.
// Check B -- this renderer runs the FXAA shader, or the program SKIPs.
// Check C -- FXAA reduces the step contrast along a diagonal edge.
// Check D -- it leaves a flat field alone: the early exit means an image with no edges comes back
//            unchanged, which is what stops FXAA from softening a whole frame.
// Check E -- MOD-604: a higher edge threshold filters less. A preset that changed nothing would be
//            a setting in name only.
//
// `--benchmark` reports MOD-608 at 720p and 1080p, on **two** images: one flat, one full of detail.
// FXAA's cost is decided by how often its early exit is taken, so a single number would be a
// number about one picture rather than about the pass.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::RenderQuality;

namespace
{
    constexpr int kFrame = 64;
}

class FxaaExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> white_;
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

    /// A hard diagonal edge, drawn as a staircase of one-pixel-tall rows. This is aliasing on
    /// purpose: each row steps one pixel across, so every row boundary is a maximum-contrast step.
    void DrawDiagonal()
    {
        spriteBatch_->Begin();
        for (int y = 0; y < kFrame; ++y)
            spriteBatch_->Draw(*white_, Rectangle(0, y, y + 1, 1), Rectangle(0, 0, 1, 1),
                               Color::White);
        spriteBatch_->End();
    }

    /// The same staircase in a grey whose luminance sits *between* the quality thresholds. The
    /// white one above is useless for MOD-604: its steps are maximum contrast, so every preset
    /// filters it identically and a check on it passes without measuring anything. Grey 25 has a
    /// luma of about 0.098 -- above `High`'s 0.0625 and below `Medium`'s 0.125 -- so the presets
    /// genuinely disagree about whether it is an edge at all.
    void DrawFaintDiagonal()
    {
        spriteBatch_->Begin();
        for (int y = 0; y < kFrame; ++y)
            spriteBatch_->Draw(*white_, Rectangle(0, y, y + 1, 1), Rectangle(0, 0, 1, 1),
                               Color(25, 25, 25, 255));
        spriteBatch_->End();
    }

    void DrawFlat()
    {
        spriteBatch_->Begin();
        spriteBatch_->Draw(*white_, Rectangle(0, 0, kFrame, kFrame), Rectangle(0, 0, 1, 1),
                           Color(90, 90, 90, 255));
        spriteBatch_->End();
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

    /// Total contrast between vertically adjacent texels. On the staircase this is the aliasing
    /// itself; on a flat field it is zero, and a filter that softened everything would still leave
    /// it zero -- which is why check D compares the whole image instead.
    static long StepContrast(const std::vector<Color>& pixels)
    {
        long total = 0;
        for (int y = 1; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
            {
                const int here  = pixels[static_cast<std::size_t>(y) * kFrame + x].getRProperty();
                const int above = pixels[static_cast<std::size_t>(y - 1) * kFrame + x].getRProperty();
                total += std::abs(here - above);
            }
        return total;
    }

    /// MOD-608. Times FXAA over a full-size target of one kind of content.
    static double TimeFxaa(GraphicsDevice& device, int width, int height, bool detailed,
                           RenderQuality quality, int frames)
    {
        CNA::Graphics::FxaaPass pass(device);
        CNA::Graphics::RenderPipelineSettings settings;
        settings.setRenderQuality(quality);
        settings.applyRenderQualityPresetEXT();

        auto source = std::make_unique<Texture2D>(device, width, height);
        std::vector<Color> pixels(static_cast<std::size_t>(width) * height, Color(90, 90, 90, 255));
        if (detailed)
            // A one-pixel checkerboard: maximum local contrast everywhere, so the early exit is
            // never taken and the shader runs its full path on every texel.
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                    pixels[static_cast<std::size_t>(y) * width + x] =
                        ((x + y) % 2 == 0) ? Color::White : Color::Black;
        source->SetData(pixels.data(), static_cast<int>(pixels.size()));

        RenderTarget2D destination(device, width, height);
        CNA::Graphics::PostProcessContext context;
        context.source      = source.get();
        context.destination = &destination;
        context.width       = width;
        context.height      = height;
        context.settings    = &settings;

        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);

        pass.apply(context);
        destination.GetData(0, &oneTexel, &probe, 0, 1);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i)
        {
            pass.apply(context);
            destination.GetData(0, &oneTexel, &probe, 0, 1);
        }
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start).count() / frames;
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        constexpr int kFrames = 20;
        std::printf("\nMOD-608: FXAA cost, %d frames each\n", kFrames);
        std::printf("  %-8s %-10s %-14s %s\n", "preset", "threshold", "flat 720p",
                    "detailed 720p");
        for (const auto& [quality, name] :
             {std::pair{RenderQuality::Low, "Low"}, std::pair{RenderQuality::Medium, "Medium"},
              std::pair{RenderQuality::High, "High"}, std::pair{RenderQuality::Ultra, "Ultra"}})
        {
            std::printf("  %-8s %-10.4f %-14.3f %.3f\n", name,
                        CNA::Graphics::FxaaPass::edgeThresholdForQuality(quality),
                        TimeFxaa(device, 1280, 720, false, quality, kFrames),
                        TimeFxaa(device, 1280, 720, true, quality, kFrames));
        }
        std::printf("  %-8s %-10s %-14.3f %.3f\n", "Medium", "at 1080p",
                    TimeFxaa(device, 1920, 1080, false, RenderQuality::Medium, kFrames),
                    TimeFxaa(device, 1920, 1080, true, RenderQuality::Medium, kFrames));
        std::printf("  (ms/frame. 'flat' takes the early exit on every texel; 'detailed' is a\n"
                    "   one-pixel checkerboard and takes it nowhere. Real frames sit between.)\n");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color opaque = Color::White;
        white_->SetData(&opaque, 1);

        // --- A: FXAA off is the identity --------------------------------------------------------
        device.Clear(Color::Black);
        DrawDiagonal();
        const std::vector<Color> withoutPipeline = ReadFrame(device);

        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setTonemappingMode(CNA::Graphics::TonemappingMode::None);

        pipeline.begin(Color::Black);
        DrawDiagonal();
        pipeline.end();
        const std::vector<Color> inert = ReadFrame(device);

        int differences = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (inert[i] != withoutPipeline[i]) ++differences;
        std::printf("    FXAA disabled vs no pipeline: %d differing texels of %zu\n", differences,
                    inert.size());
        check(differences == 0, "FXAA disabled is bit-identical to no pipeline at all");

        const long aliasedContrast = StepContrast(withoutPipeline);
        std::printf("    step contrast on the raw staircase: %ld\n", aliasedContrast);
        check(aliasedContrast > 0, "the staircase really is aliased, so there is something to fix");

        // --- B ------------------------------------------------------------------------------------
        CNA::Graphics::FxaaPass probe(device);
        if (!probe.isSupported(device))
        {
            std::printf("SKIP: this renderer cannot run the FXAA shader (a documented capability "
                        "boundary, not a defect). The identity check above still ran and passed.\n");
            std::exit(77);
        }
        check(true, "this renderer runs the FXAA shader");

        // --- C: it reduces the aliasing ----------------------------------------------------------
        settings.setFXAAEnabled(true);
        settings.setRenderQuality(RenderQuality::Ultra);
        settings.applyRenderQualityPresetEXT();
        pipeline.begin(Color::Black);
        DrawDiagonal();
        pipeline.end();
        const long filteredContrast = StepContrast(ReadFrame(device));
        std::printf("    step contrast after FXAA: %ld\n", filteredContrast);
        check(filteredContrast < aliasedContrast, "FXAA reduced the staircase's step contrast");

        // --- D: it leaves a flat field alone ------------------------------------------------------
        settings.setFXAAEnabled(false);
        pipeline.begin(Color::Black);
        DrawFlat();
        pipeline.end();
        const std::vector<Color> flatPlain = ReadFrame(device);

        settings.setFXAAEnabled(true);
        pipeline.begin(Color::Black);
        DrawFlat();
        pipeline.end();
        const std::vector<Color> flatFiltered = ReadFrame(device);

        int flatDifferences = 0;
        for (std::size_t i = 0; i < flatPlain.size(); ++i)
            if (flatPlain[i] != flatFiltered[i]) ++flatDifferences;
        std::printf("    flat field, FXAA on vs off: %d differing texels\n", flatDifferences);
        check(flatDifferences == 0,
              "the early exit leaves an image with no edges completely untouched");

        // --- E: the presets are real (MOD-604) ----------------------------------------------------
        // On a *faint* edge, not the white staircase: the white one's steps are maximum contrast,
        // so every preset filters them identically and a check on it would pass while measuring
        // nothing. That is exactly what the first version of this check did.
        settings.setFXAAEnabled(false);
        pipeline.begin(Color::Black);
        DrawFaintDiagonal();
        pipeline.end();
        const long faintUnfiltered = StepContrast(ReadFrame(device));
        std::printf("    faint staircase, unfiltered: %ld\n", faintUnfiltered);

        settings.setFXAAEnabled(true);
        long previous = -1;
        bool higherThresholdFiltersLess = true;
        bool anyPresetDiffers = false;
        for (const RenderQuality quality :
             {RenderQuality::Ultra, RenderQuality::High, RenderQuality::Medium, RenderQuality::Low})
        {
            settings.setRenderQuality(quality);
            settings.applyRenderQualityPresetEXT();
            pipeline.begin(Color::Black);
            DrawFaintDiagonal();
            pipeline.end();
            const long contrast = StepContrast(ReadFrame(device));
            std::printf("    threshold %.4f: step contrast %ld\n",
                        CNA::Graphics::FxaaPass::edgeThresholdForQuality(quality), contrast);
            if (previous >= 0)
            {
                if (contrast < previous) higherThresholdFiltersLess = false;
                if (contrast != previous) anyPresetDiffers = true;
            }
            previous = contrast;
        }
        check(higherThresholdFiltersLess,
              "MOD-604: a higher edge threshold leaves more aliasing, never less");
        check(anyPresetDiffers,
              "the presets are observable on an edge that straddles their thresholds");
        check(previous >= faintUnfiltered * 9 / 10,
              "the loosest preset leaves the faint edge essentially unfiltered");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit FxaaExample(bool benchmark) : benchmark_(benchmark)
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
        FxaaExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
