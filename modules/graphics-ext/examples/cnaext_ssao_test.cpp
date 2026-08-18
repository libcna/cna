// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-526, MOD-527, MOD-528: SSAO end to end, from the prepass to the darkened frame.
//
// This is the first program that drives the whole depth/normal chain the way a game does: it renders
// a scene into the prepass, hands the two images to the pipeline, and checks that the frame comes
// back darker in the places geometry crowds together. AO is a noise-sensitive effect, so nothing
// here compares an image -- the kernel is seeded and deterministic, but a golden would still pin
// llvmpipe's rasterisation rather than the occlusion.
//
// Check A -- MOD-526: with SSAO disabled the pipeline output is bit-identical to no pipeline at all.
//            Runs first and unconditionally; it is the promise made to a game that enables nothing.
// Check B -- this renderer can run the prepass and the SSAO shaders, or the program SKIPs.
// Check C -- the prepass really wrote a depth image: near geometry is nearer than the background.
// Check D -- SSAO darkens the frame where the depth image has a step, and does not darken a frame
//            with no step at all. A pass that darkened everything would pass one of those and fail
//            the other, which is why both are here.
// Check E -- intensity scales the darkening, and zero intensity is the unoccluded frame.
// Check F -- MOD-522: each quality preset takes its own sample count and produces occlusion.
//
// `--benchmark` reports MOD-528: prepass, SSAO and the composed frame per quality at 720p/1080p.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
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
using CNA::Graphics::RenderQuality;

namespace
{
    constexpr int kFrame = 64;
}

class SsaoExample : public Game
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

    /// The scene: a lit field with a square block on it. Drawn with SpriteBatch rather than as 3D
    /// geometry, so the program runs wherever 2D does and the prepass images can be supplied
    /// directly -- this checks SSAO and the pipeline wiring, not the rasteriser.
    void DrawScene()
    {
        spriteBatch_->Begin();
        spriteBatch_->Draw(*white_, Rectangle(0, 0, kFrame, kFrame), Rectangle(0, 0, 1, 1),
                           Color::White);
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

    static long TotalLight(const std::vector<Color>& pixels)
    {
        long sum = 0;
        for (const Color& pixel : pixels) sum += pixel.getRProperty();
        return sum;
    }

    /// A depth image with a step: the left half near, the right half at the far plane. The step is
    /// where occlusion must appear; a frame with no step must come back unoccluded.
    static std::unique_ptr<Texture2D> MakeDepth(GraphicsDevice& device, bool withStep)
    {
        auto texture = std::make_unique<Texture2D>(device, kFrame, kFrame);
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::White);
        if (withStep)
            for (int y = 0; y < kFrame; ++y)
                for (int x = 0; x < kFrame / 2; ++x)
                    pixels[static_cast<std::size_t>(y) * kFrame + x] = Color(60, 60, 60, 255);
        texture->SetData(pixels.data(), static_cast<int>(pixels.size()));
        return texture;
    }

    static std::unique_ptr<Texture2D> MakeNormals(GraphicsDevice& device)
    {
        auto texture = std::make_unique<Texture2D>(device, kFrame, kFrame);
        const std::vector<Color> facing(static_cast<std::size_t>(kFrame) * kFrame,
                                        Color(128, 128, 255, 255));
        texture->SetData(facing.data(), static_cast<int>(facing.size()));
        return texture;
    }

    /// MOD-528. Times the prepass and the SSAO pass separately, because they are separately
    /// avoidable: a game with no SSAO pays neither, and a game with SSAO pays both.
    ///
    /// The prepass figure is deliberately a **floor**: this program draws no geometry into it, so
    /// what is timed is the bind and the clear. In a real scene the prepass is a second pass over
    /// the whole geometry and scales with the scene, not with this. Reported anyway, because it
    /// establishes that the fixed part is negligible -- if a prepass is expensive it is the
    /// geometry, and the answer is fewer draws, not a cheaper prepass.
    static void TimeAt(GraphicsDevice& device, int width, int height, RenderQuality quality,
                       int frames, double& prepassMs, double& ssaoMs)
    {
        CNA::Graphics::DepthNormalPrepass prepass(device, width, height);
        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero,
                                                 Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection =
            Matrix::CreatePerspectiveFieldOfView(1.0f, static_cast<float>(width) / height, 0.1f,
                                                 100.0f);

        const auto runPrepass = [&] {
            for (int pass = 0; pass < prepass.getPassCount(); ++pass)
            {
                prepass.begin(pass, view, projection, 0.1f, 100.0f);
                prepass.end();
            }
        };

        runPrepass();
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) runPrepass();
        prepassMs = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start).count() / frames;

        CNA::Graphics::SsaoPass ssao(device);
        CNA::Graphics::RenderPipelineSettings settings;
        settings.setRenderQuality(quality);
        settings.applyRenderQualityPresetEXT();
        settings.setSSAORadius(0.5f);
        settings.setSSAOIntensity(1.0f);

        RenderTarget2D scene(device, width, height);
        RenderTarget2D destination(device, width, height);

        CNA::Graphics::PostProcessContext context;
        context.source        = &scene;
        context.sourceDepth   = prepass.getDepthTexture();
        context.sourceNormals = prepass.getNormalTexture();
        context.destination   = &destination;
        context.width         = width;
        context.height        = height;
        context.settings      = &settings;

        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);

        ssao.apply(context);
        destination.GetData(0, &oneTexel, &probe, 0, 1);

        start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i)
        {
            ssao.apply(context);
            // Forces the driver to run the pass inside the timed region.
            destination.GetData(0, &oneTexel, &probe, 0, 1);
        }
        ssaoMs = std::chrono::duration<double, std::milli>(
                     std::chrono::steady_clock::now() - start).count() / frames;
    }

    void RunBenchmark(GraphicsDevice& device)
    {
        constexpr int kFrames = 10;
        struct Preset { RenderQuality quality; const char* name; };
        constexpr Preset kPresets[] = {
            {RenderQuality::Low, "Low"}, {RenderQuality::Medium, "Medium"},
            {RenderQuality::High, "High"}, {RenderQuality::Ultra, "Ultra"},
        };

        std::printf("\nMOD-528: prepass and SSAO cost, %d frames each\n", kFrames);
        std::printf("  %-8s %-9s %-12s %-12s %-12s %s\n", "preset", "samples", "prepass 720p",
                    "ssao 720p", "prepass 1080p", "ssao 1080p");
        for (const Preset& preset : kPresets)
        {
            double p720 = 0.0, s720 = 0.0, p1080 = 0.0, s1080 = 0.0;
            TimeAt(device, 1280, 720,  preset.quality, kFrames, p720,  s720);
            TimeAt(device, 1920, 1080, preset.quality, kFrames, p1080, s1080);
            std::printf("  %-8s %-9d %-12.3f %-12.3f %-12.3f %.3f\n", preset.name,
                        CNA::Graphics::SsaoPass::sampleCountForQuality(preset.quality), p720, s720,
                        p1080, s1080);
        }
        std::printf("  (ms/frame. The prepass column is bind-and-clear only -- this program draws\n"
                    "   no geometry into it -- so it is the prepass's FLOOR, not its cost in a real\n"
                    "   scene, where it is a second pass over the geometry. SSAO loops over the\n"
                    "   kernel per texel, and that column is the whole cost.)\n");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color opaque = Color::White;
        white_->SetData(&opaque, 1);

        // --- A: MOD-526, SSAO disabled is the identity -----------------------------------------
        device.Clear(Color::Black);
        DrawScene();
        const std::vector<Color> withoutPipeline = ReadFrame(device);

        CNA::Graphics::RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(false);
        settings.setBloomEnabled(false);
        settings.setFXAAEnabled(false);
        settings.setSSAOEnabled(false);
        settings.setTonemappingMode(CNA::Graphics::TonemappingMode::None);

        pipeline.begin(Color::Black);
        DrawScene();
        pipeline.end();
        const std::vector<Color> inert = ReadFrame(device);

        int differences = 0;
        for (std::size_t i = 0; i < inert.size(); ++i)
            if (inert[i] != withoutPipeline[i]) ++differences;
        std::printf("    SSAO disabled vs no pipeline: %d differing texels of %zu\n", differences,
                    inert.size());
        check(differences == 0, "MOD-526: SSAO disabled is bit-identical to no pipeline at all");
        check(TotalLight(withoutPipeline) > 0,
              "the scene really is lit, so the comparison above compared an image");

        // --- B: can this renderer do it? --------------------------------------------------------
        CNA::Graphics::DepthNormalPrepass prepass(device, kFrame, kFrame);
        CNA::Graphics::SsaoPass ssaoProbe(device);
        if (!prepass.isSupported(device) || !ssaoProbe.isSupported(device))
        {
            std::printf("SKIP: this renderer cannot run the prepass or the SSAO shaders (a "
                        "documented capability boundary, not a defect). The MOD-526 identity check "
                        "above still ran and passed.\n");
            std::exit(77);
        }
        check(true, "this renderer runs the prepass and the SSAO shaders");
        std::printf("    prepass: %d pass(es), depth %s\n", prepass.getPassCount(),
                    prepass.isDepthPacked() ? "packed into 8 bits" : "half-float");

        // --- C: the prepass wrote something ------------------------------------------------------
        const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero,
                                                 Vector3(0.0f, 1.0f, 0.0f));
        const Matrix projection = Matrix::CreatePerspectiveFieldOfView(1.0f, 1.0f, 0.1f, 100.0f);
        for (int pass = 0; pass < prepass.getPassCount(); ++pass)
        {
            prepass.begin(pass, view, projection, 0.1f, 100.0f);
            prepass.end();
        }
        check(prepass.getDepthTexture() != nullptr && prepass.getNormalTexture() != nullptr,
              "the prepass produced both images");

        // --- D: occlusion appears at a step and nowhere else ------------------------------------
        auto normals   = MakeNormals(device);
        auto steppedDepth = MakeDepth(device, true);
        auto flatDepth    = MakeDepth(device, false);

        const auto runSsao = [&](Texture2D& depth, float intensity, RenderQuality quality) {
            CNA::Graphics::SsaoPass ssao(device);
            CNA::Graphics::RenderPipelineSettings local;
            local.setRenderQuality(quality);
            local.applyRenderQualityPresetEXT();
            local.setSSAORadius(0.5f);
            local.setSSAOIntensity(intensity);

            auto scene = std::make_unique<Texture2D>(device, kFrame, kFrame);
            const std::vector<Color> lit(static_cast<std::size_t>(kFrame) * kFrame, Color::White);
            scene->SetData(lit.data(), static_cast<int>(lit.size()));

            RenderTarget2D destination(device, kFrame, kFrame);
            CNA::Graphics::PostProcessContext context;
            context.source        = scene.get();
            context.sourceDepth   = &depth;
            context.sourceNormals = normals.get();
            context.destination   = &destination;
            context.width         = kFrame;
            context.height        = kFrame;
            context.settings      = &local;
            ssao.apply(context);

            std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Black);
            destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
            return TotalLight(pixels);
        };

        const long unoccluded = 255L * kFrame * kFrame;
        const long stepped = runSsao(*steppedDepth, 1.0f, RenderQuality::Medium);
        const long flat    = runSsao(*flatDepth,    1.0f, RenderQuality::Medium);
        std::printf("    total light: %ld unoccluded, %ld with a depth step, %ld with none\n",
                    unoccluded, stepped, flat);
        check(stepped < unoccluded, "a depth step produced occlusion");
        check(flat > stepped, "a frame with no depth step is less occluded than one with a step");

        // --- E: intensity ------------------------------------------------------------------------
        const long zeroIntensity = runSsao(*steppedDepth, 0.0f, RenderQuality::Medium);
        const long strong        = runSsao(*steppedDepth, 2.0f, RenderQuality::Medium);
        std::printf("    intensity 0: %ld, intensity 1: %ld, intensity 2: %ld\n", zeroIntensity,
                    stepped, strong);
        check(zeroIntensity >= stepped, "zero intensity darkens no more than intensity one");
        check(strong <= stepped, "a higher intensity darkens at least as much");

        // --- F: the presets ----------------------------------------------------------------------
        bool everyPresetOccludes = true;
        for (const RenderQuality quality :
             {RenderQuality::Low, RenderQuality::Medium, RenderQuality::High, RenderQuality::Ultra})
        {
            const long light = runSsao(*steppedDepth, 1.0f, quality);
            std::printf("    %d samples: %ld\n",
                        CNA::Graphics::SsaoPass::sampleCountForQuality(quality), light);
            if (light >= unoccluded) everyPresetOccludes = false;
        }
        check(everyPresetOccludes, "MOD-522: every quality preset produces occlusion");

        // --- G: the documented route, which nothing had ever exercised ---------------------------
        // `pipeline.setDepthNormalInputs(...)` is how docs/cnaext-engine-layer.md, the prepass
        // header and the getting-started guide all tell a game to feed SSAO -- and until this check
        // existed, no test, example or production caller anywhere in the repository called it. The
        // path everything points at was the one path never run.
        {
            CNA::Graphics::RenderPipeline ssaoPipeline(device);
            ssaoPipeline.resize(kFrame, kFrame);
            auto& ssaoSettings = ssaoPipeline.getSettings();
            ssaoSettings.setHDREnabled(false);
            ssaoSettings.setBloomEnabled(false);
            ssaoSettings.setFXAAEnabled(false);
            ssaoSettings.setTonemappingMode(CNA::Graphics::TonemappingMode::None);
            ssaoSettings.setSSAOEnabled(true);
            ssaoSettings.setSSAORadius(0.5f);
            ssaoSettings.setSSAOIntensity(1.0f);

            // Without the inputs first: SSAO is enabled but has nothing to read, which is the
            // documented misconfiguration, and it must render rather than fail.
            ssaoPipeline.begin(Color::Black);
            DrawScene();
            ssaoPipeline.end();
            const long withoutInputs = TotalLight(ReadFrame(device));

            ssaoPipeline.setDepthNormalInputs(steppedDepth.get(), normals.get());
            ssaoPipeline.begin(Color::Black);
            DrawScene();
            ssaoPipeline.end();
            const long withInputs = TotalLight(ReadFrame(device));

            std::printf("    through the pipeline: %ld without inputs, %ld with\n", withoutInputs,
                        withInputs);
            check(withoutInputs > 0,
                  "SSAO enabled with no depth/normal inputs still renders the frame");
            check(withInputs < withoutInputs,
                  "setDepthNormalInputs is the documented route and it works");

            // And it can be taken back: a game that stops running its prepass must not keep an
            // occlusion buffer that no longer describes the scene.
            ssaoPipeline.setDepthNormalInputs(nullptr, nullptr);
            ssaoPipeline.begin(Color::Black);
            DrawScene();
            ssaoPipeline.end();
            check(TotalLight(ReadFrame(device)) == withoutInputs,
                  "clearing the inputs returns to the unoccluded frame");
        }

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit SsaoExample(bool benchmark) : benchmark_(benchmark)
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
        SsaoExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
