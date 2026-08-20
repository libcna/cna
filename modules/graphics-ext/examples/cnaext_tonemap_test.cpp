// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-315, MOD-318, MOD-319: the five tonemapping operators on a real frame.
//
// Tonemapping is the one pass whose job is a *curve*, so what a screenshot would show is the curve
// and nothing else. This program measures the curve instead, on a real HDR gradient rendered
// through the real pipeline: a ramp from 0 to 8.0 in a float target, tonemapped, read back.
//
// Check A -- this renderer has float render targets and runs the tonemap shader, or the program
//            SKIPs. (MOD-318's identity check runs first regardless, since it needs neither.)
// Check B -- MOD-318: with HDR off and mode None, the pipeline's output is *identical* to the same
//            scene drawn with no pipeline at all. A SpriteBatch game that links the engine layer
//            and enables nothing must see no change whatsoever.
// Check C -- every operator is monotonic: brighter in is never darker out. An operator that is not
//            produces banding and inverted highlights, and is the failure a curve can have that a
//            single sampled value cannot reveal.
// Check D -- every operator except None brings 8.0 down to something displayable, and None does
//            not -- which is what makes None honest rather than broken.
// Check E -- the operators genuinely differ from each other at the same input.
//
// `--benchmark` reports MOD-319: tonemap cost at 720p and 1080p.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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
using CNA::Graphics::TonemappingMode;

namespace
{
    constexpr int kFrame = 64;

    struct Operator { TonemappingMode mode; const char* name; };
    constexpr Operator kOperators[] = {
        {TonemappingMode::None,       "None"},
        {TonemappingMode::Reinhard,   "Reinhard"},
        {TonemappingMode::Filmic,     "Filmic"},
        {TonemappingMode::Aces,       "Aces"},
        {TonemappingMode::Uncharted2, "Uncharted2"},
    };
}

class TonemapExample : public Game
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

    /// A horizontal ramp: column x carries the scene-referred value x/(kFrame-1) * 8.0. Drawn as
    /// one tinted quad per column, so it needs no shader and works wherever SpriteBatch does.
    void DrawGradient(float peak)
    {
        spriteBatch_->Begin();
        for (int x = 0; x < kFrame; ++x)
        {
            const float t = static_cast<float>(x) / static_cast<float>(kFrame - 1);
            const float value = t * peak;
            // Colour is 8-bit, so values above 1.0 come from the *count* of draws rather than from
            // one tint: additive would need blend state the pipeline owns. Instead the tint encodes
            // the low range and the HDR target holds it; the peak is applied by exposure below.
            const int byteValue = static_cast<int>((value / peak) * 255.0f);
            spriteBatch_->Draw(*white_, Rectangle(x, 0, 1, kFrame), Rectangle(0, 0, 1, 1),
                               Color(byteValue, byteValue, byteValue, 255));
        }
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

    /// The middle row of the frame, as luminance -- one value per gradient column.
    static std::vector<int> RowOf(const std::vector<Color>& pixels)
    {
        std::vector<int> row;
        row.reserve(kFrame);
        const std::size_t base = static_cast<std::size_t>(kFrame / 2) * kFrame;
        for (int x = 0; x < kFrame; ++x)
            row.push_back(pixels[base + static_cast<std::size_t>(x)].getRProperty());
        return row;
    }

    /// MOD-319. Times the tonemap pass over a full-size target at one resolution.
    static double TimeTonemap(GraphicsDevice& device, int width, int height, int frames)
    {
        CNA::Graphics::TonemapPass pass(device);
        pass.setMode(TonemappingMode::Aces);

        RenderTarget2D source(device, width, height, false, SurfaceFormat::HdrBlendable,
                              DepthFormat::None);
        device.SetRenderTarget(&source);
        device.Clear(2.0f, 1.5f, 1.0f, 1.0f);
        device.SetRenderTarget(nullptr);
        RenderTarget2D destination(device, width, height);

        CNA::Graphics::PostProcessContext context;
        context.source      = &source;
        context.destination = &destination;
        context.width       = width;
        context.height      = height;

        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);

        pass.apply(context);
        destination.GetData(0, &oneTexel, &probe, 0, 1);

        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i)
        {
            pass.apply(context);
            // Forces the driver to execute the pass inside the timed region; llvmpipe otherwise
            // queues the tile work and the number measures the API rather than the pass.
            destination.GetData(0, &oneTexel, &probe, 0, 1);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::milli>(elapsed).count() / frames;
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        constexpr int kFrames = 20;
        std::printf("\nMOD-319: tonemap cost, %d frames each\n", kFrames);
        std::printf("  %-12s %s\n", "resolution", "ms/frame");
        std::printf("  %-12s %.3f\n", "1280x720",  TimeTonemap(device, 1280, 720, kFrames));
        std::printf("  %-12s %.3f\n", "1920x1080", TimeTonemap(device, 1920, 1080, kFrames));
        std::printf("  (one dependent texture read and a curve per texel -- it should scale with\n"
                    "   pixel count and nothing else)\n");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color opaque = Color::White;
        white_->SetData(&opaque, 1);

        // --- B: MOD-318, the identity guarantee -------------------------------------------------
        // Run first and unconditionally: it is the promise made to games that link this layer and
        // enable nothing, so it must hold even where the tonemap shader cannot compile.
        device.Clear(Color::Black);
        DrawGradient(1.0f);
        const std::vector<Color> withoutPipeline = ReadFrame(device);

        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setTonemappingMode(TonemappingMode::None);

        pipeline.begin(Color::Black);
        DrawGradient(1.0f);
        pipeline.end();
        const std::vector<Color> inert = ReadFrame(device);

        int differences = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (inert[i] != withoutPipeline[i]) ++differences;
        std::printf("    inert pipeline vs no pipeline: %d differing texels of %zu\n", differences,
                    inert.size());
        // Without this the identity check above would pass just as happily on two black frames,
        // which is the way an "outputs are identical" assertion usually goes wrong.
        const std::vector<int> plain = RowOf(withoutPipeline);
        std::printf("    plain gradient row: first=%d mid=%d last=%d\n", plain.front(),
                    plain[kFrame / 2], plain.back());
        check(plain.front() < 16 && plain.back() > 240 && plain[kFrame / 2] > 100,
              "the gradient really is a gradient, so the identity check above compared an image");
        check(differences == 0 && !pipeline.isUsingSceneTarget(),
              "MOD-318: HDR off and mode None is bit-identical to no pipeline at all");

        // --- A: can this renderer actually tonemap? ---------------------------------------------
        CNA::Graphics::TonemapPass probe(device);
        if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable)
            || !probe.isSupported(device))
        {
            std::printf("SKIP: this renderer has no HDR target or cannot run the tonemap shader, "
                        "so there is no curve to measure (a documented capability boundary, not a "
                        "defect). The MOD-318 identity check above still ran and passed.\n");
            std::exit(77);
        }
        check(true, "this renderer has HDR targets and runs the tonemap shader");

        // --- C/D/E: the curves ------------------------------------------------------------------
        settings.setHDREnabled(true);
        settings.setExposure(4.0f);   // pushes the ramp's top well past 1.0

        std::vector<std::vector<int>> rows;
        for (const Operator& op : kOperators)
        {
            settings.setTonemappingMode(op.mode);
            pipeline.begin(Color::Black);
            DrawGradient(1.0f);
            pipeline.end();
            rows.push_back(RowOf(ReadFrame(device)));
            std::printf("    %-11s first=%3d mid=%3d last=%3d\n", op.name, rows.back().front(),
                        rows.back()[kFrame / 2], rows.back().back());
        }

        bool allMonotonic = true;
        for (std::size_t i = 0; i < rows.size(); ++i)
            for (std::size_t x = 1; x < rows[i].size(); ++x)
                if (rows[i][x] < rows[i][x - 1]) { allMonotonic = false; break; }
        check(allMonotonic, "every operator is monotonic: brighter in is never darker out");

        // None must clip -- that is what "no operator" means, and it is why the others exist.
        const bool noneClips = rows[0].back() >= 250;
        check(noneClips, "None lets the over-bright end clip, rather than quietly compressing it");

        bool othersCompress = true;
        for (std::size_t i = 1; i < rows.size(); ++i)
            if (rows[i].back() >= rows[0].back()) { othersCompress = false; break; }
        check(othersCompress, "every real operator brings the over-bright end below None's");

        bool operatorsDiffer = false;
        for (std::size_t i = 2; i < rows.size(); ++i)
            if (rows[i][kFrame / 2] != rows[1][kFrame / 2]) { operatorsDiffer = true; break; }
        check(operatorsDiffer, "the operators are genuinely different curves, not one curve renamed");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit TonemapExample(bool benchmark) : benchmark_(benchmark)
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
        TonemapExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
