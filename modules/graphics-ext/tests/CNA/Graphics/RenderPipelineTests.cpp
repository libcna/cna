// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-700..MOD-745: the orchestrator.
//
// Two properties matter more than the rest and are tested first: a pipeline with nothing enabled
// must produce the frame the game would have produced without it (D8), and it must not pay for an
// off-screen target it has no use for (MOD-708). Everything else in this layer is optional; those
// two are what make it safe to wrap an existing game in.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BlitPass.hpp"
#include "EngineTestSupport.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::PostProcessPass;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture;
using Microsoft::Xna::Framework::Matrix;

constexpr int kWidth  = 32;
constexpr int kHeight = 16;

/// Counts invocations without touching the GPU.
class CountingPass final : public PostProcessPass
{
public:
    void apply(const PostProcessContext&) override { ++applyCount; }

    [[nodiscard]] const std::string& getName() const override
    {
        static const std::string name = "Counting";
        return name;
    }

    [[nodiscard]] bool isSupported(GraphicsDevice&) const override { return true; }

    int applyCount = 0;
};

TEST(RenderPipelineTest, AnInertPipelineNeverAllocatesASceneTarget)
{
    // MOD-708. With nothing enabled there is nothing an off-screen target would enable, so the
    // frame goes straight to the back buffer -- and the memory estimate proves no target was made.
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    pipeline.begin(Color::CornflowerBlue);
    EXPECT_FALSE(pipeline.isUsingSceneTarget());
    EXPECT_EQ(pipeline.getSceneTarget(), nullptr);
    pipeline.end();

    EXPECT_EQ(pipeline.getLastFramePassCount(), 0);
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u);
}

TEST(RenderPipelineTest, EnablingAnythingSwitchesToTheSceneTarget)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setTonemappingMode(TonemappingMode::Aces);

    pipeline.begin(Color::Black);
    EXPECT_TRUE(pipeline.isUsingSceneTarget());
    EXPECT_NE(pipeline.getSceneTarget(), nullptr);
    pipeline.end();

    EXPECT_EQ(pipeline.getLastFramePassCount(), 1);
    EXPECT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u);
}

TEST(RenderPipelineTest, HdrPicksTheBestSceneFormatTheRendererActuallyHas)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setHDREnabled(true);

    pipeline.begin(Color::Black);
    const SurfaceFormat format = pipeline.getSceneTargetFormat();
    pipeline.end();

    if (gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        EXPECT_EQ(format, SurfaceFormat::HdrBlendable);
    else if (gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        EXPECT_EQ(format, SurfaceFormat::Vector4);
    else
        EXPECT_EQ(format, SurfaceFormat::Color);   // reported honestly rather than refused
}

TEST(RenderPipelineTest, TheSceneTargetIsNotVisibleOutsideAFrame)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setHDREnabled(true);

    EXPECT_EQ(pipeline.getSceneTarget(), nullptr);
    pipeline.begin(Color::Black);
    EXPECT_NE(pipeline.getSceneTarget(), nullptr);
    pipeline.end();
    EXPECT_EQ(pipeline.getSceneTarget(), nullptr);
}

TEST(RenderPipelineTest, UserPassesRunAfterTheBuiltInOnes)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setTonemappingMode(TonemappingMode::Reinhard);

    CountingPass user;
    pipeline.addUserPass(&user);
    pipeline.addUserPass(nullptr);   // ignored rather than crashing

    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_EQ(user.applyCount, 1);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 2);   // tonemap + user
}

TEST(RenderPipelineTest, AUserPassAloneIsEnoughToRunAFrameThroughTheChain)
{
    // A game that wants only its own effect should not have to enable HDR to get one.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    CountingPass user;
    pipeline.addUserPass(&user);

    pipeline.begin(Color::Black);
    EXPECT_TRUE(pipeline.isUsingSceneTarget());
    pipeline.end();

    EXPECT_EQ(user.applyCount, 1);
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1);
}

TEST(RenderPipelineTest, ClearingUserPassesReturnsThePipelineToInert)
{
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    CountingPass user;
    pipeline.addUserPass(&user);
    pipeline.clearUserPasses();

    pipeline.begin(Color::Black);
    EXPECT_FALSE(pipeline.isUsingSceneTarget());
    pipeline.end();

    EXPECT_EQ(user.applyCount, 0);
}

TEST(RenderPipelineTest, SettingsChangesTakeEffectOnTheNextFrame)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_EQ(pipeline.getLastFramePassCount(), 0);

    pipeline.getSettings().setTonemappingMode(TonemappingMode::Filmic);

    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1);
}

TEST(RenderPipelineTest, BothMisusesAreRejected)
{
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);

    // begin() before any size is known.
    EXPECT_THROW(pipeline.begin(Color::Black), std::logic_error);

    pipeline.resize(kWidth, kHeight);
    EXPECT_THROW(pipeline.end(), std::logic_error);          // end without begin

    pipeline.begin(Color::Black);
    EXPECT_THROW(pipeline.begin(Color::Black), std::logic_error);   // double begin
    pipeline.end();
}

TEST(RenderPipelineTest, ANonPositiveSizeIsRejected)
{
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);

    EXPECT_THROW(pipeline.resize(0, kHeight), std::invalid_argument);
    EXPECT_THROW(pipeline.resize(kWidth, -4), std::invalid_argument);
}

TEST(RenderPipelineTest, RepeatedResizesStayBounded)
{
    // A resized game must not keep paying for every size it has ever been.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.getSettings().setHDREnabled(true);

    for (int i = 0; i < 20; ++i)
    {
        pipeline.resize(kWidth + (i % 2) * 8, kHeight + (i % 2) * 8);
        pipeline.begin(Color::Black);
        pipeline.end();
    }

    const std::size_t oneTarget = static_cast<std::size_t>(kWidth + 8) * (kHeight + 8) * 16u;
    EXPECT_LE(pipeline.getGpuMemoryEstimateBytes(), oneTarget * 3u);
}

TEST(RenderPipelineTest, ManyFramesDoNotAccumulateTargets)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setHDREnabled(true);
    pipeline.getSettings().setTonemappingMode(TonemappingMode::Aces);

    pipeline.begin(Color::Black);
    pipeline.end();
    const std::size_t afterFirstFrame = pipeline.getGpuMemoryEstimateBytes();

    for (int frame = 0; frame < 50; ++frame)
    {
        pipeline.begin(Color::Black);
        pipeline.end();
    }

    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), afterFirstFrame);
}

TEST(RenderPipelineTest, AnInertPipelineProducesTheSameFrameAsNoPipelineAtAll)
{
    // D8, and the reason the short circuit exists: wrapping a game in an unconfigured pipeline
    // must not change one pixel of its output.
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    pipeline.begin(Color(70, 130, 180, 255));
    pipeline.end();

    // The frame went to the back buffer, which is what a game without a pipeline clears; there is
    // no scene target standing between the game and the screen.
    EXPECT_FALSE(pipeline.isUsingSceneTarget());
    EXPECT_EQ(pipeline.getLastFramePassCount(), 0);
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u);
}

TEST(RenderPipelineTest, SsaoRunsOnlyWhenItsInputsAreSupplied)
{
    // The pipeline cannot produce depth and normals itself -- that means drawing the game's
    // geometry a second time with a different effect, which only the game can do. Enabling SSAO
    // without them is a misconfiguration that must still render a frame.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setSSAOEnabled(true);

    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_TRUE(pipeline.isUsingSceneTarget());
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1);   // SSAO ran, and passed the frame through
}

TEST(RenderPipelineTest, TheFixedPassOrderIsSsaoThenBloomThenTonemapThenFxaa)
{
    // Each position is a decision with a reason: SSAO shades the scene before anything measures
    // its brightness, bloom's threshold reads scene-referred values, tonemapping is the boundary
    // to display-referred colour, and FXAA finds edges in displayed pixels.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    auto& settings = pipeline.getSettings();
    settings.setSSAOEnabled(true);
    settings.setBloomEnabled(true);
    settings.setTonemappingMode(TonemappingMode::Aces);
    settings.setFXAAEnabled(true);

    CountingPass user;
    pipeline.addUserPass(&user);

    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_EQ(pipeline.getLastFramePassCount(), 5);   // ssao, bloom, tonemap, fxaa, user
    EXPECT_EQ(user.applyCount, 1);
}

TEST(RenderPipelineTest, SsrSitsBetweenSsaoAndBloom)
{
    // plan_modern.md MOD-2005. Two decisions, each with a reason. SSR runs **after SSAO**, because
    // what a mirror shows should be the shaded scene rather than the unshaded one; and **before the
    // tonemapper**, for the reason bloom is -- a reflection carries scene-referred colour, and
    // mixing it in after the range has been compressed makes a reflected highlight
    // indistinguishable from a reflected white wall.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    auto& settings = pipeline.getSettings();
    settings.setSSAOEnabled(true);
    settings.setSSREnabled(true);
    settings.setBloomEnabled(true);
    settings.setTonemappingMode(TonemappingMode::Aces);
    settings.setFXAAEnabled(true);

    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_EQ(pipeline.getLastFramePassCount(), 5);   // ssao, ssr, bloom, tonemap, fxaa
}

TEST(RenderPipelineTest, TheCameraHistoryAdvancesOncePerFrameAndNotPerSetCamera)
{
    // plan_modern.md MOD-2031. Motion blur compares this frame's camera against the one the
    // *previous frame was rendered from*, so the history has to advance in end() and nowhere else.
    // Advancing it in setCamera would make a game that sets the camera twice in a frame -- or once
    // every other frame -- compare against a camera nothing was ever drawn with, and the blur would
    // be wrong in a way no single frame could reveal.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    const auto view = [](const float x) {
        return Matrix::CreateLookAt(Microsoft::Xna::Framework::Vector3(x, 0.0f, 0.0f),
                                    Microsoft::Xna::Framework::Vector3(x, 0.0f, -1.0f),
                                    Microsoft::Xna::Framework::Vector3::Up);
    };
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 1.0f, 100.0f);

    pipeline.getSettings().setMotionBlurStrength(0.5f);

    // Two setCamera calls before a single frame: only the last is what the frame is drawn with, and
    // the first must not become "the previous frame".
    pipeline.setCamera(view(0.0f), projection, 1.0f, 100.0f);
    pipeline.setCamera(view(5.0f), projection, 1.0f, 100.0f);
    EXPECT_NO_THROW(pipeline.begin(Color::Black));
    EXPECT_NO_THROW(pipeline.end());

    // A second frame from the same camera. Nothing moved between the two frames that were actually
    // rendered, whatever happened between the setCamera calls.
    EXPECT_NO_THROW(pipeline.begin(Color::Black));
    EXPECT_NO_THROW(pipeline.end());
    EXPECT_EQ(pipeline.getLastFramePassCount(), 1) << "motion blur was not in the chain";
}

TEST(RenderPipelineTest, DepthOfFieldSitsBeforeBloomBecauseItBelongsToTheLens)
{
    // plan_modern.md MOD-2011. An out-of-focus highlight should bloom as the spread circle it
    // became, not as the point it was. Blooming first and blurring the glow afterwards is wrong in
    // the same way tonemapping before bloom would be, so the lens runs first.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    auto& settings = pipeline.getSettings();
    settings.setDOFEnabled(true);
    settings.setBloomEnabled(true);
    settings.setTonemappingMode(TonemappingMode::Aces);

    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_EQ(pipeline.getLastFramePassCount(), 3);   // dof, bloom, tonemap
}

TEST(RenderPipelineTest, DepthOfFieldAloneIsEnoughToNeedASceneTarget)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u);

    pipeline.getSettings().setDOFEnabled(true);
    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u)
        << "enabling depth of field did not take the pipeline off the back buffer";
}

TEST(RenderPipelineTest, SsrAloneIsEnoughToNeedASceneTarget)
{
    // A pass that reads the frame cannot run against the back buffer, so enabling it must take the
    // pipeline out of its inert short circuit -- the same claim every other pass has.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u) << "an inert pipeline allocated a target";

    pipeline.getSettings().setSSREnabled(true);
    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u)
        << "enabling SSR did not take the pipeline off the back buffer";
}

TEST(RenderPipelineTest, TheCameraIsValidatedAndReachesTheScreenSpacePasses)
{
    // The pipeline has never carried a camera before: the sky was told one, and no pass needed it.
    // SSR does, and gets it from here. The range is validated at the setter rather than at the
    // frame, because the failure it prevents -- reconstructing positions of NaN from a depth
    // normalised by a zero far plane -- produces a frame that renders and is silently wrong.
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);

    const Matrix view = Matrix::CreateLookAt(Microsoft::Xna::Framework::Vector3::Zero,
                             Microsoft::Xna::Framework::Vector3(0.0f, 0.0f, -1.0f),
                             Microsoft::Xna::Framework::Vector3::Up);
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, 1.0f, 100.0f);

    EXPECT_THROW(pipeline.setCamera(view, projection, 0.0f, 100.0f), std::invalid_argument);
    EXPECT_THROW(pipeline.setCamera(view, projection, -1.0f, 100.0f), std::invalid_argument);
    EXPECT_THROW(pipeline.setCamera(view, projection, 10.0f, 10.0f), std::invalid_argument);
    EXPECT_THROW(pipeline.setCamera(view, projection, 100.0f, 1.0f), std::invalid_argument);
    EXPECT_NO_THROW(pipeline.setCamera(view, projection, 1.0f, 100.0f));
}

// =====================================================================================
// Shadow integration (MOD-858)
// =====================================================================================

TEST(RenderPipelineTest, TheShadowPassRunsBeforeTheSceneTargetIsBound)
{
    // Ordering is the whole assertion. ShadowMap::end() restores the back buffer, so a shadow pass
    // running after the scene target was bound would unbind it and send the frame to the screen --
    // a mistake whose symptom is post-processing silently doing nothing.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setHDREnabled(true);
    pipeline.getSettings().setShadowsEnabled(true);

    CNA::Graphics::ShadowMap shadowMap(gd, CNA::Graphics::ShadowQuality::Low);
    const Texture* boundWhenCastersDrew = nullptr;
    pipeline.setShadowScene(&shadowMap, CNA::Graphics::DirectionalLightEXT{},
                            Microsoft::Xna::Framework::BoundingBox(
                                Microsoft::Xna::Framework::Vector3(-1.0f, -1.0f, -1.0f),
                                Microsoft::Xna::Framework::Vector3(1.0f, 1.0f, 1.0f)),
                            [&] {
                                const auto bound = gd.GetRenderTargets();
                                boundWhenCastersDrew =
                                    bound.empty() ? nullptr
                                                  : bound[0].getRenderTargetProperty();
                            });

    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_TRUE(pipeline.didShadowPassRun());
    EXPECT_EQ(boundWhenCastersDrew, static_cast<const Texture*>(shadowMap.getShadowTexture()))
        << "the casters were drawn into something other than the shadow map";
    EXPECT_EQ(pipeline.getShadowMap(), &shadowMap);
}

TEST(RenderPipelineTest, EachMissingIngredientLeavesTheShadowPassUnrun)
{
    // Three separate reasons not to run one, and the app can tell them apart from what it set.
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    CNA::Graphics::ShadowMap shadowMap(gd, CNA::Graphics::ShadowQuality::Low);
    const Microsoft::Xna::Framework::BoundingBox bounds(
        Microsoft::Xna::Framework::Vector3(-1.0f, -1.0f, -1.0f),
        Microsoft::Xna::Framework::Vector3(1.0f, 1.0f, 1.0f));

    int drawCount = 0;
    const auto draw = [&] { ++drawCount; };

    // Settings off.
    pipeline.setShadowScene(&shadowMap, CNA::Graphics::DirectionalLightEXT{}, bounds, draw);
    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_FALSE(pipeline.didShadowPassRun());

    // Settings on, but no map.
    pipeline.getSettings().setShadowsEnabled(true);
    pipeline.setShadowScene(nullptr, CNA::Graphics::DirectionalLightEXT{}, bounds, draw);
    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_FALSE(pipeline.didShadowPassRun());
    EXPECT_EQ(pipeline.getShadowMap(), nullptr);

    // Map, but nothing to draw into it -- the pipeline cannot invent the app's geometry.
    pipeline.setShadowScene(&shadowMap, CNA::Graphics::DirectionalLightEXT{}, bounds, {});
    pipeline.begin(Color::Black);
    pipeline.end();
    EXPECT_FALSE(pipeline.didShadowPassRun());

    EXPECT_EQ(drawCount, 0);
}

TEST(RenderPipelineTest, AShadowPassAloneDoesNotForceASceneTarget)
{
    // Shadows and post-processing are independent: a game that wants shadows and no HDR must not
    // start paying for an off-screen target it has no use for.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setShadowsEnabled(true);

    CNA::Graphics::ShadowMap shadowMap(gd, CNA::Graphics::ShadowQuality::Low);
    int drawCount = 0;
    pipeline.setShadowScene(&shadowMap, CNA::Graphics::DirectionalLightEXT{},
                            Microsoft::Xna::Framework::BoundingBox(
                                Microsoft::Xna::Framework::Vector3(-1.0f, -1.0f, -1.0f),
                                Microsoft::Xna::Framework::Vector3(1.0f, 1.0f, 1.0f)),
                            [&] { ++drawCount; });

    pipeline.begin(Color::Black);
    EXPECT_FALSE(pipeline.isUsingSceneTarget());
    pipeline.end();

    EXPECT_TRUE(pipeline.didShadowPassRun());
    EXPECT_EQ(drawCount, 1);
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u);
}

TEST(RenderPipelineTest, TheSkyIsDrawnInsideBeginAndReportsItself)
{
    // MOD-1104. The ordering matters and is the reason didSkyboxDraw() exists: an app cannot see
    // from outside whether the sky went in before its geometry or not at all.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    auto cube = std::make_unique<Microsoft::Xna::Framework::Graphics::TextureCube>(
        gd, 4, false, SurfaceFormat::Color);
    const std::vector<Color> texels(16, Color::White);
    for (int face = 0; face < 6; ++face)
        cube->SetData(static_cast<Microsoft::Xna::Framework::Graphics::CubeMapFace>(face),
                      texels.data(), static_cast<int>(texels.size()));

    CNA::Graphics::Skybox sky(gd, cube.get());
    if (!sky.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the sky shader";

    pipeline.setSkybox(&sky);
    pipeline.setSkyboxCamera(Matrix::getIdentityProperty(),
                             Matrix::CreatePerspectiveFieldOfView(1.0f, 1.0f, 0.1f, 10.0f));
    EXPECT_EQ(pipeline.getSkybox(), &sky);

    pipeline.begin(Color::Black);
    EXPECT_TRUE(pipeline.didSkyboxDraw()) << "the sky did not go in during begin()";
    pipeline.end();

    // Detaching it is immediate, and a frame without one is not a frame that failed.
    pipeline.setSkybox(nullptr);
    pipeline.begin(Color::Black);
    EXPECT_FALSE(pipeline.didSkyboxDraw());
    pipeline.end();
}

TEST(RenderPipelineTest, ASkyAloneDoesNotForceASceneTarget)
{
    // A game that wants a sky and no post-processing must not start paying for an off-screen
    // target: the sky goes straight to the back buffer, exactly like the rest of that frame.
    GraphicsDevice gd;
    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);

    CNA::Graphics::Skybox sky(gd, nullptr);
    pipeline.setSkybox(&sky);

    pipeline.begin(Color::Black);
    EXPECT_FALSE(pipeline.isUsingSceneTarget());
    pipeline.end();

    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u);
}

} // namespace

#endif // CNA_CNAEXT
