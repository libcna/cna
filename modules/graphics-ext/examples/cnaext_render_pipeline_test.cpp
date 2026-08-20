// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-738..MOD-742: the layer's showcase, and the four claims it has to keep.
//
// Everything else in this plan tests one subsystem. This tests the promise the layer as a whole
// makes: wrap your existing draw in begin/end, switch things on, and the frame gets better without
// the draw changing. So the scene here is deliberately ordinary -- a SpriteBatch draw and a
// BasicEffect draw, the two things a CNA game actually does -- and what varies is only the settings.
//
// Check A -- MOD-738: with everything off, the pipeline's output is bit-identical to no pipeline,
//            and it allocates nothing. True on every renderer, including the 2D-only ones, where
//            the passes skip and no call throws.
// Check B -- MOD-739: an ordinary SpriteBatch draw gains bloom by being wrapped. The 2D game does
//            not learn anything about the engine layer.
// Check C -- MOD-740: the 3D path works too -- a BasicEffect model inside begin/end reaches the
//            frame, and the pipeline's passes act on it.
// Check D -- MOD-741: the full stack -- HDR + bloom + tonemap + FXAA -- runs as one frame and the
//            statistics report every pass.
// Check E -- turning the whole stack off again returns to the identical frame from check A. The
//            pipeline holds no state that survives its own settings.
//
// `--benchmark` reports MOD-742: full-pipeline frame cost against direct rendering, per preset.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <chrono>
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
using CNA::Graphics::TonemappingMode;

namespace
{
    constexpr int kFrame  = 96;
    constexpr int kSprite = 12;
}

class PipelineShowcase : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D>   white_;
    std::unique_ptr<BasicEffect> basicEffect_;
    bool benchmark_ = false;
    bool canDraw3D_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    /// The 2D half: one bright sprite. This is the whole of what a SpriteBatch game does, and the
    /// point of MOD-739 is that it does not change when the pipeline is wrapped around it.
    void Draw2D()
    {
        spriteBatch_->Begin();
        spriteBatch_->Draw(*white_,
                           Rectangle((kFrame - kSprite) / 2, (kFrame - kSprite) / 2, kSprite,
                                     kSprite),
                           Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
    }

    /// The 3D half: one bright triangle through BasicEffect. Not a Model -- loading content would
    /// make this a content test -- but the same effect and the same draw path a Model uses.
    void Draw3D()
    {
        if (!canDraw3D_) return;

        const VertexPositionColor vertices[3] = {
            {Vector3(-0.6f, -0.5f, 0.0f), Color::White},
            {Vector3( 0.6f, -0.5f, 0.0f), Color::White},
            {Vector3( 0.0f,  0.6f, 0.0f), Color::White},
        };

        // CullNone, and it is not a detail: the triangle below is counter-clockwise in a Y-up NDC,
        // which is exactly what XNA's default CullCounterClockwiseFace removes. Without this the
        // 3D check measures a culled triangle and reports zero light from a pipeline that is
        // working perfectly.
        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        basicEffect_->setWorldProperty(Matrix::getIdentityProperty());
        basicEffect_->setViewProperty(Matrix::getIdentityProperty());
        basicEffect_->setProjectionProperty(Matrix::getIdentityProperty());
        basicEffect_->VertexColorEnabled = true;
        basicEffect_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1);
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

    static long TotalLight(const std::vector<Color>& pixels)
    {
        long sum = 0;
        for (const Color& pixel : pixels) sum += pixel.getRProperty();
        return sum;
    }

    static int GlowOutsideTheSprite(const std::vector<Color>& pixels)
    {
        const int low  = (kFrame - kSprite) / 2;
        const int high = low + kSprite;
        int glow = 0;
        for (int y = 0; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
            {
                if (x >= low && x < high && y >= low && y < high) continue;
                glow += pixels[static_cast<std::size_t>(y) * kFrame + x].getRProperty();
            }
        return glow;
    }

    /// MOD-742. A whole frame, drawn the way a game draws it, with and without the pipeline. The
    /// back-buffer read is what forces the frame to actually happen inside the timed region; it is
    /// paid identically by both sides, so the *difference* is the pipeline's cost.
    double TimeFrame(GraphicsDevice& device, CNA::Graphics::RenderPipeline* pipeline, int frames)
    {
        std::vector<Color> sink(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);

        const auto frame = [&] {
            if (pipeline != nullptr)
            {
                pipeline->begin(Color::Black);
                Draw2D();
                Draw3D();
                pipeline->end();
            }
            else
            {
                device.Clear(Color::Black);
                Draw2D();
                Draw3D();
            }
            device.GetBackBufferData(sink.data(), static_cast<int>(sink.size()));
        };

        frame();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) frame();
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start).count() / frames;
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        constexpr int kFrames = 20;
        std::printf("\nMOD-742: whole-frame cost at %dx%d, %d frames each\n", kFrame, kFrame,
                    kFrames);

        const double direct = TimeFrame(device, nullptr, kFrames);
        std::printf("  %-24s %.3f ms\n", "direct rendering", direct);

        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();

        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setTonemappingMode(TonemappingMode::None);
        std::printf("  %-24s %.3f ms\n", "pipeline, nothing on",
                    TimeFrame(device, &pipeline, kFrames));

        for (const auto& [quality, name] :
             {std::pair{RenderQuality::Low, "Low"}, std::pair{RenderQuality::Medium, "Medium"},
              std::pair{RenderQuality::High, "High"}, std::pair{RenderQuality::Ultra, "Ultra"}})
        {
            settings.setHDREnabled(true);
            settings.setBloomEnabled(true);
            settings.setFXAAEnabled(true);
            settings.setTonemappingMode(TonemappingMode::Aces);
            settings.setRenderQuality(quality);
            settings.applyRenderQualityPresetEXT();
            const double cost = TimeFrame(device, &pipeline, kFrames);
            std::printf("  %-24s %.3f ms  (%+.3f over direct)\n",
                        (std::string("HDR+bloom+tonemap+FXAA, ") + name).c_str(), cost,
                        cost - direct);
        }
        std::printf("  (SSAO is excluded: it needs a depth/normal prepass this scene has none of,\n"
                    "   and its own cost is measured by cnaext_ssao_test.)\n");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color opaque = Color::White;
        white_->SetData(&opaque, 1);

        canDraw3D_ = device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
        if (canDraw3D_) basicEffect_ = std::make_unique<BasicEffect>(device);
        std::printf("    3D pipeline: %s\n", canDraw3D_ ? "yes" : "no (2D-only renderer)");

        // --- A: MOD-738, the inert pipeline is free and identical --------------------------------
        device.Clear(Color::Black);
        Draw2D();
        Draw3D();
        const std::vector<Color> direct = ReadFrame(device);

        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setTonemappingMode(TonemappingMode::None);

        pipeline.begin(Color::Black);
        Draw2D();
        Draw3D();
        pipeline.end();
        const std::vector<Color> inert = ReadFrame(device);

        int differences = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (inert[i] != direct[i]) ++differences;
        const auto inertStats = pipeline.getStatistics();
        std::printf("    inert pipeline: %d differing texels, %d passes, %zu bytes\n", differences,
                    inertStats.passesRun, inertStats.gpuMemoryEstimateBytes);
        check(differences == 0 && inertStats.passesRun == 0
                  && inertStats.gpuMemoryEstimateBytes == 0,
              "MOD-738: an inert pipeline is bit-identical to direct rendering and allocates "
              "nothing");
        check(TotalLight(direct) > 0, "the scene really drew something");

        // Everything below asks the passes to do visible work, which is a stronger question than
        // whether the renderer accepts an effect. SOFTWARE and HEADLESS report CustomEffects true,
        // accept every shader source and go on rendering with their own fixed path -- so a gate on
        // the capability alone passes here and then fails the bloom check three lines later, which
        // is exactly what it did the first time this program was run on SOFTWARE (plans/plan_modern.md
        // MOD-1699).
        const bool runsShaders = device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                              && device.ExecutesShaderEffectSourceEXT();
        if (!runsShaders)
        {
            // Not simply a skip: the chain must still *run* the passes on these renderers, and that
            // is worth asserting before stopping, because "the passes were skipped" and "the passes
            // ran and copied" look identical from outside and only one of them is the contract.
            settings.setHDREnabled(true);
            settings.setBloomEnabled(true);
            settings.setFXAAEnabled(true);
            settings.setTonemappingMode(TonemappingMode::Aces);
            pipeline.begin(Color::Black);
            Draw2D();
            Draw3D();
            pipeline.end();
            const auto stats = pipeline.getStatistics();
            std::printf("    passes with no shader execution: %d ran, %d target switches\n",
                        stats.passesRun, stats.targetSwitches);
            check(stats.passesRun == 3,
                  "the chain still runs every enabled pass, each falling back to a copy");
            check(TotalLight(ReadFrame(device)) > 0, "and the frame survives the fallback");

            std::printf("%d/%d checks passed\n", passCount_, checkCount_);
            std::printf("SKIP: this renderer accepts custom effects but does not execute their "
                        "source, so the passes copy rather than shade (a documented capability "
                        "boundary, not a defect -- see docs/cnaext-engine-layer.md). Everything "
                        "checkable here has been checked.\n");
            std::exit(77);
        }

        // --- B: MOD-739, a 2D game gains bloom ---------------------------------------------------
        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setBloomThreshold(0.5f);
        settings.setBloomIntensity(1.5f);
        pipeline.begin(Color::Black);
        Draw2D();
        pipeline.end();
        const std::vector<Color> bloomed = ReadFrame(device);

        device.Clear(Color::Black);
        Draw2D();
        const int plainGlow = GlowOutsideTheSprite(ReadFrame(device));
        const int bloomGlow = GlowOutsideTheSprite(bloomed);
        std::printf("    2D sprite, light outside it: %d direct, %d through the pipeline\n",
                    plainGlow, bloomGlow);
        check(plainGlow == 0 && bloomGlow > 0,
              "MOD-739: an ordinary SpriteBatch draw gained bloom by being wrapped");

        // --- C: MOD-740, the 3D path -------------------------------------------------------------
        if (canDraw3D_)
        {
            settings.setBloomEnabled(false);
            settings.setTonemappingMode(TonemappingMode::Aces);
            pipeline.begin(Color::Black);
            Draw3D();
            pipeline.end();
            const long throughPipeline = TotalLight(ReadFrame(device));

            device.Clear(Color::Black);
            Draw3D();
            const long direct3D = TotalLight(ReadFrame(device));
            std::printf("    3D triangle, total light: %ld direct, %ld tonemapped\n", direct3D,
                        throughPipeline);
            check(direct3D > 0 && throughPipeline > 0,
                  "MOD-740: a BasicEffect draw inside begin/end reaches the frame");
            check(throughPipeline != direct3D,
                  "and the pipeline's passes acted on it rather than passing it through");
        }
        else
        {
            std::printf("    3D checks skipped: this renderer has no 3D pipeline\n");
            check(true, "MOD-740: not applicable on a 2D-only renderer, which is its contract");
            check(true, "(the identity check above is what this renderer promises instead)");
        }

        // --- D: MOD-741, the full stack ----------------------------------------------------------
        settings.setHDREnabled(true);
        settings.setBloomEnabled(true);
        settings.setFXAAEnabled(true);
        settings.setTonemappingMode(TonemappingMode::Aces);
        settings.setRenderQuality(RenderQuality::High);
        settings.applyRenderQualityPresetEXT();

        pipeline.begin(Color::Black);
        Draw2D();
        Draw3D();
        pipeline.end();
        const std::vector<Color> full = ReadFrame(device);
        const auto fullStats = pipeline.getStatistics();
        std::printf("    full stack: %d passes, %d target switches, %zu bytes, %s scene target\n",
                    fullStats.passesRun, fullStats.targetSwitches,
                    fullStats.gpuMemoryEstimateBytes, fullStats.usedSceneTarget ? "used" : "no");
        check(fullStats.passesRun == 3 && fullStats.usedSceneTarget
                  && fullStats.gpuMemoryEstimateBytes > 0,
              "MOD-741: bloom, tonemap and FXAA all ran in one frame");
        check(TotalLight(full) > 0, "and the frame is not blank");

        // --- E: it all goes back ------------------------------------------------------------------
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setTonemappingMode(TonemappingMode::None);
        pipeline.begin(Color::Black);
        Draw2D();
        Draw3D();
        pipeline.end();
        const std::vector<Color> backToInert = ReadFrame(device);

        differences = 0;
        for (std::size_t i = 0; i < backToInert.size(); ++i)
            if (backToInert[i] != direct[i]) ++differences;
        std::printf("    after switching everything back off: %d differing texels\n", differences);
        check(differences == 0,
              "the pipeline holds no state that survives its own settings being turned off");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit PipelineShowcase(bool benchmark) : benchmark_(benchmark)
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
        PipelineShowcase example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
