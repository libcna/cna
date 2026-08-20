// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-209: a disabled pass is skipped entirely, not run with its effect turned off.
//
// The distinction matters and is invisible from a screenshot. A pass "disabled" inside its own
// shader still costs a fullscreen draw, a target bind and a sampler read every frame -- so a game
// that switches everything off still pays for a pipeline it is not using, and a quality preset that
// disables three passes saves nothing. What the settings do instead is decide which passes are
// added to the chain at all, and the counting fake below is how that is checked: a pass that never
// ran cannot have incremented its own counter.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <string>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr int kWidth  = 32;
constexpr int kHeight = 32;

/// Counts how many times it was actually asked to run. It does no drawing, so it is safe on every
/// renderer, and its count is the only thing under test.
class CountingPass : public CNA::Graphics::PostProcessPass
{
public:
    void apply(const PostProcessContext&) override { ++applyCount_; }

    [[nodiscard]] const std::string& getName() const override
    {
        static const std::string name = "Counting";
        return name;
    }

    [[nodiscard]] bool isSupported(GraphicsDevice&) const override { return true; }

    [[nodiscard]] int applyCount() const { return applyCount_; }

private:
    int applyCount_ = 0;
};

/// One frame through the pipeline, with no scene drawn -- the passes are what is being counted.
void RunFrame(RenderPipeline& pipeline)
{
    pipeline.begin(Color::Black);
    pipeline.end();
}

TEST(PassEnableFlagTest, EverythingOffRunsNoPassesAtAll)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setBloomEnabled(false);
    settings.setSSAOEnabled(false);
    settings.setFXAAEnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);

    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 0)
        << "a pipeline with nothing enabled still built a chain";
    EXPECT_FALSE(pipeline.isUsingSceneTarget())
        << "with no pass to run, there is nothing to render off-screen for";
}

TEST(PassEnableFlagTest, EachFlagAddsExactlyOnePass)
{
    // Counted one flag at a time, so a pass that is added twice -- or a flag that quietly enables a
    // second pass -- shows up as an arithmetic mismatch rather than as a slower frame.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);

    settings.setBloomEnabled(true);
    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1) << "bloom alone";

    settings.setFXAAEnabled(true);
    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 2) << "bloom and FXAA";

    settings.setBloomEnabled(false);
    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1) << "turning bloom back off removes its pass";

    settings.setFXAAEnabled(false);
    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 0);
}

TEST(PassEnableFlagTest, HdrAddsTheTonemapperEvenWithoutAnOperator)
{
    // Not an off-by-one: an HDR scene target holds values above 1.0 and something has to bring them
    // down before the back buffer, so enabling HDR enables the tonemap pass whatever the operator
    // is set to. Without this the frame would simply clamp, which is the failure Phase 1 measured.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setBloomEnabled(false);
    settings.setSSAOEnabled(false);
    settings.setFXAAEnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);
    settings.setHDREnabled(true);

    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1);
}

TEST(PassEnableFlagTest, ADisabledPassIsNeverAskedToRun)
{
    // The counting fake the row names. A user pass is added to the pipeline once and the pipeline
    // is run twice; what changes between the runs is only whether the built-in passes are on.
    // The user pass must run both times, and its count is the proof that "skipped" means skipped
    // rather than "ran with its effect disabled".
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);
    settings.setBloomEnabled(false);
    settings.setSSAOEnabled(false);
    settings.setFXAAEnabled(false);

    CountingPass counting;
    pipeline.addUserPass(&counting);

    RunFrame(pipeline);
    EXPECT_EQ(counting.applyCount(), 1);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1) << "only the user pass";

    settings.setBloomEnabled(true);
    RunFrame(pipeline);
    EXPECT_EQ(counting.applyCount(), 2) << "the user pass runs regardless of the built-in flags";
    EXPECT_EQ(pipeline.getLastFramePassCount(), 2) << "bloom joined it";

    settings.setBloomEnabled(false);
    RunFrame(pipeline);
    EXPECT_EQ(counting.applyCount(), 3);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1)
        << "bloom was removed from the chain, not run with its effect off";
}

TEST(PassEnableFlagTest, TheFlagsTakeEffectOnTheNextFrameWithoutRebuildingAnything)
{
    // A quality preset flips several flags between frames. If that needed the pipeline rebuilt, a
    // settings menu would stutter; the chain is rebuilt from the flags each frame instead, which is
    // cheap because the passes themselves are long-lived.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);

    for (int frame = 0; frame < 8; ++frame)
    {
        const bool on = (frame % 2) == 0;
        settings.setBloomEnabled(on);
        settings.setFXAAEnabled(on);
        RunFrame(pipeline);
        EXPECT_EQ(pipeline.getLastFramePassCount(), on ? 2 : 0) << "frame " << frame;
    }
}

} // namespace

#endif // CNA_CNAEXT
