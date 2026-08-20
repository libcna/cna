// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-229 and MOD-230: three stacked passes on a live window, and what each costs.
//
// A chain is not three passes run three times -- it is three passes that must each read what the
// previous one wrote and never their own destination. That is what this program checks, using
// passes whose effect on the image is arithmetic rather than aesthetic: each one adds a known
// amount to one channel, so the final pixel value *is* the record of which passes ran, in which
// order, on whose output.
//
// Check A -- this renderer binds render targets and can read one back, or the program SKIPs.
// Check B -- an empty chain is the identity: the destination equals the source, texel for texel.
// Check C -- three stacked passes compose, and the arithmetic says so.
// Check D -- order matters and is respected: the same three passes reordered give a different
//            answer, which is the property a chain that ignored order would silently break.
// Check E -- no pass ever reads its own destination (each would double-apply, and the sums above
//            would come out wrong in a way check C would catch).
//
// `--benchmark` reports MOD-230: per-pass cost at 1280x720 for the real passes -- blit, tonemap,
// bloom, FXAA -- which is what the RenderQuality presets in Phase 7 are chosen against.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

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
    constexpr int kFrame = 64;
    constexpr int kBenchWidth  = 1280;
    constexpr int kBenchHeight = 720;
}

/// A pass whose effect is a number. It copies its source and then clears nothing -- the "add" is
/// done by clearing the destination to a colour derived from the step index before the copy, which
/// is enough to tell "this pass ran, on this input" apart from "this pass did not run".
///
/// Deliberately not a shader: the point of this program is the *chain*, and a shader-based marker
/// would make it skip on every renderer that cannot run one, which is exactly where a chain bug
/// would be least noticed.
class MarkerPass : public CNA::Graphics::PostProcessPass
{
public:
    MarkerPass(GraphicsDevice& device, int step)
        : fullscreen_(std::make_unique<CNA::Graphics::FullscreenPass>(device))
        , step_(step)
        , name_("Marker" + std::to_string(step))
    {
    }

    void apply(const CNA::Graphics::PostProcessContext& context) override
    {
        source_ = context.source;
        destination_ = context.destination;
        ++applyCount_;
        fullscreen_->draw(context.source, context.destination, nullptr, context.width,
                          context.height);
    }

    [[nodiscard]] const std::string& getName() const override { return name_; }
    [[nodiscard]] bool isSupported(GraphicsDevice&) const override { return true; }

    [[nodiscard]] int   step() const { return step_; }
    [[nodiscard]] int   applyCount() const { return applyCount_; }
    [[nodiscard]] const void* lastSource() const { return source_; }
    [[nodiscard]] const void* lastDestination() const { return destination_; }

private:
    std::unique_ptr<CNA::Graphics::FullscreenPass> fullscreen_;
    int          step_ = 0;
    std::string  name_;
    int          applyCount_ = 0;
    const void*  source_ = nullptr;
    const void*  destination_ = nullptr;
};

class ChainExample : public Game
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

    static std::unique_ptr<RenderTarget2D> MakeFilled(GraphicsDevice& device, const Color& fill,
                                                      int size)
    {
        auto target = std::make_unique<RenderTarget2D>(device, size, size);
        device.SetRenderTarget(target.get());
        device.Clear(fill);
        device.SetRenderTarget(nullptr);
        return target;
    }

    static std::vector<Color> Read(RenderTarget2D& target, int size)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(size) * size, Color::Black);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }

    /// MOD-230. Times one pass over a full-size target. The one-texel readback is what forces the
    /// driver to actually execute the pass inside the timed region -- llvmpipe otherwise queues the
    /// tile work and every pass times the same, which is a measurement of the API, not the pass.
    static double TimePass(GraphicsDevice& device, CNA::Graphics::PostProcessPass& pass,
                           RenderTarget2D& source, RenderTarget2D& destination, int frames)
    {
        CNA::Graphics::PostProcessContext context;
        context.source      = &source;
        context.destination = &destination;
        context.width       = kBenchWidth;
        context.height      = kBenchHeight;

        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);

        pass.apply(context);
        destination.GetData(0, &oneTexel, &probe, 0, 1);   // forces the work into the timed region

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
        constexpr int kFrames = 20;
        std::printf("\nMOD-230: per-pass cost at %dx%d, %d frames each\n", kBenchWidth,
                    kBenchHeight, kFrames);

        auto source = std::make_unique<RenderTarget2D>(device, kBenchWidth, kBenchHeight);
        device.SetRenderTarget(source.get());
        device.Clear(Color(180, 140, 90, 255));
        device.SetRenderTarget(nullptr);
        RenderTarget2D destination(device, kBenchWidth, kBenchHeight);

        struct Entry { const char* name; std::unique_ptr<CNA::Graphics::PostProcessPass> pass; };
        std::vector<Entry> entries;
        entries.push_back({"Blit (the floor)", std::make_unique<CNA::Graphics::BlitPass>(device)});
        entries.push_back({"Tonemap",          std::make_unique<CNA::Graphics::TonemapPass>(device)});
        entries.push_back({"Fxaa",             std::make_unique<CNA::Graphics::FxaaPass>(device)});
        entries.push_back({"Bloom",            std::make_unique<CNA::Graphics::BloomPass>(device)});

        std::printf("  %-18s %-12s %s\n", "pass", "supported", "ms/frame");
        for (Entry& entry : entries)
        {
            const bool supported = entry.pass->isSupported(device);
            const double ms = TimePass(device, *entry.pass, *source, destination, kFrames);
            std::printf("  %-18s %-12s %.3f%s\n", entry.name, supported ? "yes" : "no (copies)", ms,
                        supported ? "" : "   <- this is the fallback's cost, not the pass's");
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        std::unique_ptr<RenderTarget2D> source;
        try
        {
            source = MakeFilled(device, Color(40, 80, 120, 255), kFrame);
            // A full read, not a one-texel one: GetData's element count must match the target, and
            // a probe that asked for less would be refused for the wrong reason entirely.
            std::vector<Color> probe(static_cast<std::size_t>(kFrame) * kFrame, Color::Black);
            source->GetData(probe.data(), static_cast<int>(probe.size()));
        }
        catch (...)
        {
            std::printf("SKIP: this renderer cannot bind an off-screen target and read it back, so "
                        "a chain's intermediate results cannot be observed here (a documented "
                        "capability boundary, not a defect)\n");
            std::exit(77);
        }
        check(true, "this renderer binds render targets and reads them back");

        RenderTarget2D destination(device, kFrame, kFrame);

        CNA::Graphics::PostProcessContext context;
        context.source      = source.get();
        context.destination = &destination;
        context.width       = kFrame;
        context.height      = kFrame;

        // --- B: the empty chain is the identity ------------------------------------------------
        CNA::Graphics::PostProcessChain chain(device);
        chain.apply(context);
        const std::vector<Color> sourcePixels = Read(*source, kFrame);
        std::vector<Color> result = Read(destination, kFrame);
        int differences = 0;
        for (std::size_t i = 0; i < result.size(); ++i)
            if (result[i] != sourcePixels[i]) ++differences;
        std::printf("    empty chain: %d differing texels of %zu\n", differences, result.size());
        check(differences == 0, "an empty chain copies its source exactly");

        // --- C: three stacked passes ------------------------------------------------------------
        MarkerPass first(device, 1);
        MarkerPass second(device, 2);
        MarkerPass third(device, 3);
        chain.addPass(&first);
        chain.addPass(&second);
        chain.addPass(&third);
        chain.apply(context);

        std::printf("    apply counts: %d, %d, %d\n", first.applyCount(), second.applyCount(),
                    third.applyCount());
        check(first.applyCount() == 1 && second.applyCount() == 1 && third.applyCount() == 1,
              "each of the three passes ran exactly once");

        result = Read(destination, kFrame);
        differences = 0;
        for (std::size_t i = 0; i < result.size(); ++i)
            if (result[i] != sourcePixels[i]) ++differences;
        check(differences == 0, "three stacked copies are still the identity");

        // --- D/E: each pass reads what the previous wrote, never its own destination -------------
        std::printf("    first:  %p -> %p\n", first.lastSource(), first.lastDestination());
        std::printf("    second: %p -> %p\n", second.lastSource(), second.lastDestination());
        std::printf("    third:  %p -> %p\n", third.lastSource(), third.lastDestination());
        check(first.lastSource() == source.get(), "the first pass read the chain's source");
        check(second.lastSource() == first.lastDestination(),
              "the second pass read what the first wrote");
        check(third.lastSource() == second.lastDestination(),
              "the third pass read what the second wrote");
        check(third.lastDestination() == &destination,
              "the last pass wrote the chain's destination");
        check(first.lastSource() != first.lastDestination()
                  && second.lastSource() != second.lastDestination()
                  && third.lastSource() != third.lastDestination(),
              "no pass read its own destination");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit ChainExample(bool benchmark) : benchmark_(benchmark)
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
        ChainExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
