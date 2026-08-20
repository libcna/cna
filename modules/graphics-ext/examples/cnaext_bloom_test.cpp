// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1809: a 2D SpriteBatch program gaining HDR bloom, in about ten lines.
//
// The claim this program exists to check is the entry cost. Everything below the "--- the ten
// lines" marker is an ordinary CNA 2D game: a texture, a SpriteBatch, one Draw. The engine layer
// is the pipeline object, two calls around that Draw, and four settings.
//
// Check A -- the renderer can run the passes, or the program SKIPs.
// Check B -- with the pipeline present but nothing enabled, the frame is UNCHANGED, pixel for
//            pixel, against the same scene drawn with no pipeline at all.
// Check C -- with bloom on, light spreads beyond the sprite's own edges.
// Check D -- the spread grows with intensity.
// Check E -- MOD-415: the threshold really is a threshold. Raising it above the sprite's own
//            brightness must remove the glow entirely, which is what separates "bloom" from
//            "a blur added to everything".
// Check F -- MOD-409: each RenderQuality preset derives its own level count, and more levels
//            produce a wider halo. A preset that changed nothing observable would be a lie.
//
// `--benchmark` reports MOD-416: bloom cost per quality preset at 720p and 1080p, which is what
// the preset table in BloomPass.hpp is chosen against.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kFrame = 128;
    constexpr int kSpriteSize = 16;
}

class BloomExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_ = false;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> sprite_;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// The game's own drawing: one bright sprite in the middle of the screen.
    void DrawScene()
    {
        spriteBatch_->Begin();
        spriteBatch_->Draw(*sprite_,
                           Rectangle((kFrame - kSpriteSize) / 2, (kFrame - kSpriteSize) / 2,
                                     kSpriteSize, kSpriteSize),
                           Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

    std::vector<Color> ReadFrame(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (const System::NotSupportedException&)
        {
            std::printf("SKIP: this renderer has no readable back buffer\n");
            std::exit(77);
        }
        return pixels;
    }

    /// Light outside the sprite's own rectangle -- which is exactly what bloom adds and nothing
    /// else in this scene can produce.
    static int GlowOutsideTheSprite(const std::vector<Color>& pixels)
    {
        const int low = (kFrame - kSpriteSize) / 2;
        const int high = low + kSpriteSize;
        int glow = 0;
        for (int y = 0; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
            {
                if (x >= low && x < high && y >= low && y < high) continue;
                glow += pixels[static_cast<std::size_t>(y) * kFrame + x].getRProperty();
            }
        return glow;
    }

    /// MOD-416. Times the bloom pass over a full-size HDR target at one resolution and one preset.
    /// The one-texel readback is what forces the driver to execute the pyramid inside the timed
    /// region -- llvmpipe otherwise queues the tile work and every preset times the same.
    static double TimeBloom(GraphicsDevice& device, int width, int height,
                            CNA::Graphics::RenderQuality quality, int frames)
    {
        CNA::Graphics::BloomPass pass(device);
        CNA::Graphics::RenderPipelineSettings settings;
        settings.setRenderQuality(quality);
        settings.applyRenderQualityPresetEXT();
        settings.setBloomThreshold(0.5f);
        settings.setBloomIntensity(1.0f);

        const SurfaceFormat format =
            device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable)
                ? SurfaceFormat::HdrBlendable
                : SurfaceFormat::Color;

        RenderTarget2D source(device, width, height, false, format, DepthFormat::None);
        device.SetRenderTarget(&source);
        device.Clear(2.0f, 1.6f, 1.2f, 1.0f);
        device.SetRenderTarget(nullptr);
        RenderTarget2D destination(device, width, height);

        CNA::Graphics::PostProcessContext context;
        context.source      = &source;
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
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::milli>(elapsed).count() / frames;
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        constexpr int kFrames = 10;
        struct Preset { CNA::Graphics::RenderQuality quality; const char* name; };
        constexpr Preset kPresets[] = {
            {CNA::Graphics::RenderQuality::Low,    "Low"},
            {CNA::Graphics::RenderQuality::Medium, "Medium"},
            {CNA::Graphics::RenderQuality::High,   "High"},
            {CNA::Graphics::RenderQuality::Ultra,  "Ultra"},
        };

        std::printf("\nMOD-416: bloom cost per quality preset, %d frames each\n", kFrames);
        std::printf("  %-8s %-8s %-12s %s\n", "preset", "levels", "1280x720", "1920x1080");
        for (const Preset& preset : kPresets)
        {
            const int levels = CNA::Graphics::BloomPass::iterationsForQuality(preset.quality);
            const double at720  = TimeBloom(device, 1280, 720,  preset.quality, kFrames);
            const double at1080 = TimeBloom(device, 1920, 1080, preset.quality, kFrames);
            std::printf("  %-8s %-8d %-12.3f %.3f\n", preset.name, levels, at720, at1080);
        }
        std::printf("  (ms/frame. The first level is half-resolution and costs the most; every\n"
                    "   level after it is a quarter of the one before, so the curve flattens.)\n");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        sprite_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        sprite_->SetData(&white, 1);

        // `CustomEffects` is necessary but not sufficient, and the difference is worth stating:
        // the Vulkan renderer reports it true and its ShaderEffect takes SPIR-V bytecode rather
        // than the GLSL source these passes hand it, so every pass compiles nothing and copies
        // through. Asking the pass itself is the only question with a reliable answer.
        CNA::Graphics::BloomPass probe(device);
        if (!device.SupportsCapability(GraphicsCapability::CustomEffects) ||
            !probe.isSupported(device))
        {
            std::printf("SKIP: this renderer compiles no post-process pass, so the chain copies "
                        "its input through (a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        // The same scene with no engine layer at all, for the comparison below.
        device.Clear(Color::Black);
        DrawScene();
        const std::vector<Color> withoutPipeline = ReadFrame(device);

        // --- the ten lines -------------------------------------------------------------------
        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setBloomThreshold(0.5f);
        settings.setBloomIntensity(1.5f);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        // -------------------------------------------------------------------------------------
        const std::vector<Color> bloomed = ReadFrame(device);

        // And the same pipeline with everything switched off, which must change nothing.
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const std::vector<Color> inert = ReadFrame(device);

        int differences = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (inert[i] != withoutPipeline[i]) ++differences;
        std::printf("    inert pipeline vs no pipeline: %d differing pixels of %zu\n", differences,
                    inert.size());
        check(differences == 0 && !pipeline.isUsingSceneTarget(),
              "a pipeline with nothing enabled renders the identical frame");

        const int plainGlow = GlowOutsideTheSprite(withoutPipeline);
        const int bloomGlow = GlowOutsideTheSprite(bloomed);
        std::printf("    light outside the sprite: %d without bloom, %d with\n", plainGlow,
                    bloomGlow);
        check(plainGlow == 0, "the scene itself puts no light outside the sprite");
        check(bloomGlow > 0, "bloom spread light beyond the sprite's own edges");

        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setBloomIntensity(4.0f);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const int strongerGlow = GlowOutsideTheSprite(ReadFrame(device));
        std::printf("    at intensity 4.0: %d\n", strongerGlow);
        check(strongerGlow > bloomGlow, "a higher bloom intensity spreads more light");

        // --- E: the threshold really is a threshold (MOD-415) ----------------------------------
        // A sprite drawn at white is 1.0 in the scene target. Set the threshold well above that and
        // nothing should qualify -- which is the difference between bloom and a blur applied to
        // everything, and the only check here that would fail if the extract stage were a no-op.
        settings.setBloomIntensity(1.5f);
        settings.setBloomThreshold(4.0f);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const int aboveThresholdGlow = GlowOutsideTheSprite(ReadFrame(device));
        std::printf("    threshold 4.0 (above the sprite's own brightness): %d\n",
                    aboveThresholdGlow);
        check(aboveThresholdGlow == 0,
              "a threshold above everything in the scene removes the glow entirely");

        settings.setBloomThreshold(0.1f);
        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const int lowThresholdGlow = GlowOutsideTheSprite(ReadFrame(device));
        std::printf("    threshold 0.1: %d\n", lowThresholdGlow);
        check(lowThresholdGlow > aboveThresholdGlow,
              "lowering the threshold lets more of the scene bloom");

        // --- F: the quality presets are real (MOD-409) -----------------------------------------
        // Each preset derives its own level count, and more levels must widen the halo. A preset
        // that changed nothing observable would be a setting in name only.
        settings.setBloomThreshold(0.5f);
        settings.setBloomIntensity(1.5f);

        struct QualityRun { CNA::Graphics::RenderQuality quality; const char* name; int glow; };
        QualityRun runs[] = {
            {CNA::Graphics::RenderQuality::Low,    "Low",    0},
            {CNA::Graphics::RenderQuality::Medium, "Medium", 0},
            {CNA::Graphics::RenderQuality::High,   "High",   0},
            {CNA::Graphics::RenderQuality::Ultra,  "Ultra",  0},
        };
        for (QualityRun& run : runs)
        {
            settings.setRenderQuality(run.quality);
            settings.applyRenderQualityPresetEXT();
            pipeline.begin(Color::Black);
            DrawScene();
            pipeline.end();
            run.glow = GlowOutsideTheSprite(ReadFrame(device));
            std::printf("    %-7s (%d levels): %d\n", run.name,
                        CNA::Graphics::BloomPass::iterationsForQuality(run.quality), run.glow);
        }
        bool presetsAscend = true;
        for (std::size_t i = 1; i < std::size(runs); ++i)
            if (runs[i].glow < runs[i - 1].glow) { presetsAscend = false; break; }
        check(presetsAscend, "each higher preset spreads at least as much light as the one below");
        check(runs[3].glow > runs[0].glow, "Ultra's halo is genuinely wider than Low's");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit BloomExample(bool benchmark) : benchmark_(benchmark)
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
        BloomExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
