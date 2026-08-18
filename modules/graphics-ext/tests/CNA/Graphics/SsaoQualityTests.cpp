// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-522, MOD-523: SSAO's quality mapping and its half-resolution option.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::SsaoPass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 32;

TEST(SsaoQualityTest, EachPresetMapsToItsDocumentedSampleCount)
{
    EXPECT_EQ(SsaoPass::sampleCountForQuality(RenderQuality::Low), 8);
    EXPECT_EQ(SsaoPass::sampleCountForQuality(RenderQuality::Medium), 16);
    EXPECT_EQ(SsaoPass::sampleCountForQuality(RenderQuality::High), 32);
    EXPECT_EQ(SsaoPass::sampleCountForQuality(RenderQuality::Ultra), 64);
}

TEST(SsaoQualityTest, ThePresetsAreOrderedAndAcceptedByThePass)
{
    GraphicsDevice gd;
    SsaoPass pass(gd);

    int previous = 0;
    for (const RenderQuality quality :
         {RenderQuality::Low, RenderQuality::Medium, RenderQuality::High, RenderQuality::Ultra})
    {
        const int samples = SsaoPass::sampleCountForQuality(quality);
        EXPECT_GT(samples, previous) << "the presets are not ordered";
        previous = samples;

        // A preset the pass would clamp is a preset that silently becomes a different preset.
        pass.setSampleCount(samples);
        EXPECT_EQ(pass.getSampleCount(), samples);
    }
}

TEST(SsaoQualityTest, AValueOutsideTheEnumGetsTheDefault)
{
    EXPECT_EQ(SsaoPass::sampleCountForQuality(static_cast<RenderQuality>(99)),
              SsaoPass::sampleCountForQuality(RenderQuality::Medium));
}

TEST(SsaoQualityTest, ApplyingThePresetSetsBothSubsystems)
{
    // One call, both dials -- and neither is touched until it is made, which is the property
    // MOD-409 established and MOD-522 has to keep.
    RenderPipelineSettings settings;
    settings.setSSAOSampleCount(23);
    settings.setRenderQuality(RenderQuality::Ultra);
    EXPECT_EQ(settings.getSSAOSampleCount(), 23) << "setRenderQuality overwrote a tuned value";

    settings.applyRenderQualityPresetEXT();
    EXPECT_EQ(settings.getSSAOSampleCount(), SsaoPass::sampleCountForQuality(RenderQuality::Ultra));
    EXPECT_EQ(settings.getBloomIterations(),
              CNA::Graphics::BloomPass::iterationsForQuality(RenderQuality::Ultra));
}

TEST(SsaoHalfResolutionTest, ItIsOffByDefaultAndRoundTrips)
{
    GraphicsDevice gd;
    SsaoPass pass(gd);
    EXPECT_FALSE(pass.isHalfResolution())
        << "half resolution must be opt-in: it costs thin contact shadows their definition";
    pass.setHalfResolution(true);
    EXPECT_TRUE(pass.isHalfResolution());
    pass.setHalfResolution(false);
    EXPECT_FALSE(pass.isHalfResolution());
}

TEST(SsaoHalfResolutionTest, BothPathsProduceOcclusionRatherThanOnlyTheFullOne)
{
    // The failure this catches is a half-resolution path that silently produces nothing -- a
    // mis-sized target, a noise scale that lands outside the buffer -- which would look like AO
    // simply being weak rather than like the option being broken.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    SsaoPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the SSAO shaders";

    // A depth image with a step in it: half the frame near, half far. A step is the one feature
    // that must produce occlusion, at the edge between the two.
    auto depth   = std::make_unique<Texture2D>(gd, kSize, kSize);
    auto normals = std::make_unique<Texture2D>(gd, kSize, kSize);
    auto scene   = std::make_unique<Texture2D>(gd, kSize, kSize);

    std::vector<Color> depthPixels(static_cast<std::size_t>(kSize) * kSize, Color::White);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize / 2; ++x)
            depthPixels[static_cast<std::size_t>(y) * kSize + x] = Color(60, 60, 60, 255);
    depth->SetData(depthPixels.data(), static_cast<int>(depthPixels.size()));

    const std::vector<Color> facing(static_cast<std::size_t>(kSize) * kSize,
                                    Color(128, 128, 255, 255));
    normals->SetData(facing.data(), static_cast<int>(facing.size()));

    const std::vector<Color> white(static_cast<std::size_t>(kSize) * kSize, Color::White);
    scene->SetData(white.data(), static_cast<int>(white.size()));

    const auto totalLight = [&](bool halfRes) {
        pass.setHalfResolution(halfRes);
        pass.setRadius(0.5f);
        pass.setIntensity(1.0f);
        pass.setSampleCount(16);

        RenderTarget2D destination(gd, kSize, kSize);
        PostProcessContext context;
        context.source        = scene.get();
        context.sourceDepth   = depth.get();
        context.sourceNormals = normals.get();
        context.destination   = &destination;
        context.width         = kSize;
        context.height        = kSize;
        pass.apply(context);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
        destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
        long sum = 0;
        for (const Color& pixel : pixels) sum += pixel.getRProperty();
        return sum;
    };

    const long full = totalLight(false);
    const long half = totalLight(true);
    const long unoccluded = 255L * kSize * kSize;

    EXPECT_LT(full, unoccluded) << "full-resolution AO darkened nothing at all";
    EXPECT_LT(half, unoccluded) << "half-resolution AO darkened nothing -- the option is broken, "
                                   "not merely softer";
    // Not asserted equal: the whole point of the option is that it differs. What must hold is that
    // both are in the same neighbourhood, or "half resolution" is really "no AO".
    const double ratio = static_cast<double>(unoccluded - half)
                       / static_cast<double>(std::max(1L, unoccluded - full));
    EXPECT_GT(ratio, 0.25) << "half resolution lost more than three quarters of the occlusion";
    EXPECT_LT(ratio, 4.0)  << "half resolution produced far more occlusion than full -- the noise "
                              "scale is probably tiling against the wrong size";
}

} // namespace

#endif // CNA_CNAEXT
