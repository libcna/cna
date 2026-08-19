// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2010..MOD-2014: depth of field.
//
// The optics are testable on their own, which is why they are a static function rather than only a
// line of GLSL: `circleOfConfusionMillimetres` is checked against the thin-lens formula computed
// here, and the shader is then checked against *it*, so a divergence between the two shows up as a
// failure rather than as a frame nobody measured.
//
// The image tests use a frame split into a near half and a far half, with the lens focused on one
// of them. Which half is sharp is the whole claim, and it is read from the frame rather than
// assumed: a flat colour cannot show a blur, so each half carries a checkerboard whose contrast
// falls as it is blurred.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthOfFieldPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::DepthOfFieldPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int   kSize     = 64;
constexpr float kFarPlane = 100.0f;
constexpr float kNearHalfDepth = 5.0f;    // world units
constexpr float kFarHalfDepth  = 60.0f;

bool InNearHalf(const int row) { return row < kSize / 2; }

/// Puts per-pixel colours into a render target, which is what a real pipeline hands the pass.
std::unique_ptr<RenderTarget2D> MakeImage(GraphicsDevice& gd,
                                          const std::function<Color(int, int)>& colourAt)
{
    auto staging = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            texels.push_back(colourAt(x, y));
    staging->SetData(texels.data(), static_cast<int>(texels.size()));

    auto target = std::make_unique<RenderTarget2D>(gd, kSize, kSize);
    CNA::Graphics::FullscreenPass blit(gd);
    blit.draw(staging.get(), target.get(), nullptr, kSize, kSize);
    return target;
}

int DepthByte(const float worldDepth)
{
    return static_cast<int>((worldDepth / kFarPlane) * 255.0f + 0.5f);
}

/// Near half close to the camera, far half well beyond it.
std::unique_ptr<RenderTarget2D> MakeSplitDepth(GraphicsDevice& gd)
{
    return MakeImage(gd, [](int, const int y) {
        const int value = DepthByte(InNearHalf(y) ? kNearHalfDepth : kFarHalfDepth);
        return Color(value, value, value, 255);
    });
}

/// A two-pixel checkerboard in both halves. Contrast is what a blur destroys, so contrast is what
/// the tests measure -- a flat colour blurs into itself and shows nothing.
std::unique_ptr<RenderTarget2D> MakeCheckerboard(GraphicsDevice& gd)
{
    return MakeImage(gd, [](const int x, const int y) {
        const bool light = ((x / 2) + (y / 2)) % 2 == 0;
        return light ? Color(240, 240, 240, 255) : Color(15, 15, 15, 255);
    });
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// The spread of red values over a band of rows: high where the checkerboard survived, low where it
/// was blurred into a flat grey.
double ContrastOverRows(const std::vector<Color>& pixels, const int firstRow, const int lastRow)
{
    double sum = 0.0;
    double sumSquares = 0.0;
    int count = 0;
    for (int y = firstRow; y <= lastRow; ++y)
        for (int x = 4; x < kSize - 4; ++x)   // clear of the frame border, where taps are dropped
        {
            const double v = pixels[static_cast<std::size_t>(y) * kSize + x].getRProperty();
            sum += v;
            sumSquares += v * v;
            ++count;
        }
    const double mean = sum / count;
    return std::sqrt(sumSquares / count - mean * mean);
}

PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination)
{
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.farPlane    = kFarPlane;
    context.nearPlane   = 1.0f;
    return context;
}

// ── The optics (MOD-2010) ────────────────────────────────────────────────────

TEST(DepthOfFieldTest, TheCircleOfConfusionMatchesTheThinLensFormula)
{
    // Computed here from the formula rather than compared against recorded numbers, so the test
    // states the optics instead of pinning whatever the implementation happened to produce.
    const float focus = 10.0f;      // metres
    const float f     = 50.0f;      // millimetres
    const float n     = 2.8f;

    for (const float depth : {1.0f, 5.0f, 9.5f, 10.0f, 10.5f, 25.0f, 80.0f})
    {
        const float focusMm = focus * 1000.0f;
        const float depthMm = depth * 1000.0f;
        const float expected = (f * f / (n * (focusMm - f))) * std::fabs(depthMm - focusMm) / depthMm;
        EXPECT_NEAR(DepthOfFieldPass::circleOfConfusionMillimetres(depth, focus, f, n), expected,
                    1e-4f)
            << "at depth " << depth;
    }
}

TEST(DepthOfFieldTest, NothingAtTheFocusDistanceIsBlurred)
{
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(10.0f, 10.0f, 50.0f, 2.8f), 0.0f);
}

TEST(DepthOfFieldTest, TheCircleGrowsOnBothSidesOfTheFocusPlane)
{
    // A subject in front of the focus plane and one behind it are both out of focus. A formula that
    // dropped the absolute value would blur only one side and leave the other perfectly sharp,
    // which reads as "depth of field works" until someone walks towards the camera.
    const auto coc = [](const float d) {
        return DepthOfFieldPass::circleOfConfusionMillimetres(d, 10.0f, 50.0f, 2.8f);
    };
    EXPECT_GT(coc(5.0f), 0.0f);
    EXPECT_GT(coc(20.0f), 0.0f);
    EXPECT_GT(coc(2.0f), coc(5.0f)) << "closer than focus must blur more";
    EXPECT_GT(coc(40.0f), coc(20.0f)) << "further than focus must blur more";
}

TEST(DepthOfFieldTest, AWiderApertureShortensTheDepthOfField)
{
    // The f-number is the one setting a photographer reaches for, and it must behave: smaller
    // number, more blur at the same distance.
    const auto coc = [](const float n) {
        return DepthOfFieldPass::circleOfConfusionMillimetres(30.0f, 10.0f, 50.0f, n);
    };
    EXPECT_GT(coc(1.4f), coc(2.8f));
    EXPECT_GT(coc(2.8f), coc(11.0f));
}

TEST(DepthOfFieldTest, DegenerateConfigurationsAnswerZeroRatherThanInfinity)
{
    // Every one of these divides by zero or takes the wrong branch of the lens equation. Answering
    // zero is the honest reading -- there is no image, so there is no circle of confusion -- and it
    // is what keeps a NaN out of the blur radius, where it would silently disable the whole pass.
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(0.0f, 10.0f, 50.0f, 2.8f), 0.0f);
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(-5.0f, 10.0f, 50.0f, 2.8f), 0.0f);
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(10.0f, 0.0f, 50.0f, 2.8f), 0.0f);
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(10.0f, 10.0f, 0.0f, 2.8f), 0.0f);
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(10.0f, 10.0f, 50.0f, 0.0f), 0.0f);
    // Focused inside the focal length: the lens equation has no solution there.
    EXPECT_FLOAT_EQ(DepthOfFieldPass::circleOfConfusionMillimetres(10.0f, 0.04f, 50.0f, 2.8f), 0.0f);
}

// ── The image (MOD-2011, MOD-2012) ───────────────────────────────────────────

TEST(DepthOfFieldTest, TheFocusedHalfKeepsItsDetailAndTheOtherLosesIt)
{
    // The claim the pass exists for, and it is read from the frame rather than assumed: with the
    // lens on the near half, the near checkerboard keeps its contrast and the far one loses it.
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd);
    auto source = MakeCheckerboard(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setFocusDistance(kNearHalfDepth);
    // A long lens on purpose. The optics are correct at any focal length, but a 50 mm lens puts
    // this scene's background circle at 0.7% of the frame -- under half a pixel at 64 -- so a test
    // at this size would be measuring rounding rather than blur. 135 mm puts it at about 5%, three
    // pixels here and the same fraction of a real frame.
    pass.setFocalLength(135.0f);
    pass.setFNumber(1.4f);
    pass.setMaxRadius(0.06f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // The rows are read well inside each half, clear of the depth step where the two meet.
    const double nearContrast = ContrastOverRows(pixels, 4, kSize / 2 - 8);
    const double farContrast  = ContrastOverRows(pixels, kSize / 2 + 8, kSize - 5);

    EXPECT_GT(nearContrast, 60.0) << "the focused half lost its detail";
    EXPECT_LT(farContrast, nearContrast * 0.6)
        << "the out-of-focus half kept its detail: " << farContrast << " against " << nearContrast;
}

TEST(DepthOfFieldTest, MovingTheFocusMovesWhichHalfIsSharp)
{
    // The anti-vacuity partner of the test above. If the frame came back with the near half sharp
    // whatever the settings said -- because the pass did nothing, or because the depth image was
    // being misread -- this fails.
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd);
    auto source = MakeCheckerboard(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    // A long lens on purpose. The optics are correct at any focal length, but a 50 mm lens puts
    // this scene's background circle at 0.7% of the frame -- under half a pixel at 64 -- so a test
    // at this size would be measuring rounding rather than blur. 135 mm puts it at about 5%, three
    // pixels here and the same fraction of a real frame.
    pass.setFocalLength(135.0f);
    pass.setFNumber(1.4f);
    pass.setMaxRadius(0.06f);
    pass.setFocusDistance(kFarHalfDepth);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    const double nearContrast = ContrastOverRows(pixels, 4, kSize / 2 - 8);
    const double farContrast  = ContrastOverRows(pixels, kSize / 2 + 8, kSize - 5);

    EXPECT_GT(farContrast, 60.0) << "the newly focused half did not come into focus";
    EXPECT_LT(nearContrast, farContrast * 0.6)
        << "focusing on the far half left the near half sharp too";
}

TEST(DepthOfFieldTest, AFocusedSubjectDoesNotSmearIntoTheBlurredHalf)
{
    // plan_modern.md MOD-2012. A gather weighted only by the centre pixel's blur pulls the sharp
    // half's colour across the boundary, and the sharp half visibly bleeds into the soft one -- the
    // giveaway of a naive implementation. The test looks at the blurred rows *immediately* past the
    // step and asks whether they still look like their own half.
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth = MakeSplitDepth(gd);
    // The sharp half is white, the blurred half black: anything the sharp half smears across the
    // boundary shows up as a raised value in rows that should have stayed dark.
    auto source = MakeImage(gd, [](int, const int y) {
        return InNearHalf(y) ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setFocusDistance(kNearHalfDepth);
    // A long lens on purpose. The optics are correct at any focal length, but a 50 mm lens puts
    // this scene's background circle at 0.7% of the frame -- under half a pixel at 64 -- so a test
    // at this size would be measuring rounding rather than blur. 135 mm puts it at about 5%, three
    // pixels here and the same fraction of a real frame.
    pass.setFocalLength(135.0f);
    pass.setFNumber(1.4f);
    pass.setMaxRadius(0.06f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // Two rows into the blurred half. A gather with a 0.06 radius reaches roughly four rows, so
    // without the bleed test this row would carry a substantial share of the white half.
    const std::size_t justPastTheStep = static_cast<std::size_t>(kSize / 2 + 2) * kSize + kSize / 2;
    EXPECT_LT(pixels[justPastTheStep].getRProperty(), 60)
        << "the focused half bled across the depth step into the blurred half";
}

TEST(DepthOfFieldTest, TheShaderMatchesTheCpuReference)
{
    // The formula exists twice -- once in C++ so a test can check the optics, once in GLSL because
    // a shader cannot call the first -- and two copies drift. This pins them together without a
    // debug output, by using `maxRadius` as a probe: it caps whatever the shader computed, so
    // raising it stops changing the frame at exactly the point where it passes the shader's own
    // radius. If that point is where the CPU formula says it is, the two agree.
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd);
    auto source = MakeCheckerboard(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    constexpr float kFocalLength = 135.0f;
    constexpr float kFNumber     = 1.4f;
    const float diameterMm = DepthOfFieldPass::circleOfConfusionMillimetres(
        kFarHalfDepth, kNearHalfDepth, kFocalLength, kFNumber);
    const float cpuRadius = 0.5f * diameterMm / DepthOfFieldPass::kSensorHeightMillimetres;
    ASSERT_GT(cpuRadius, 0.01f) << "the reference radius is too small for this frame to resolve";

    const auto farContrastAtCap = [&](const float cap) {
        pass.setFocusDistance(kNearHalfDepth);
        pass.setFocalLength(kFocalLength);
        pass.setFNumber(kFNumber);
        pass.setMaxRadius(cap);
        PostProcessContext context = MakeContext(*source, destination);
        context.sourceDepth = depth.get();
        pass.apply(context);
        return ContrastOverRows(ReadTarget(destination), kSize / 2 + 8, kSize - 5);
    };

    const double belowTheRadius = farContrastAtCap(cpuRadius * 0.4f);
    const double atTheRadius    = farContrastAtCap(cpuRadius);
    const double aboveTheRadius = farContrastAtCap(cpuRadius * 2.0f);

    // Below the shader's own radius the cap is doing the work, so the frame is measurably sharper.
    EXPECT_GT(belowTheRadius, atTheRadius * 1.2)
        << "capping below the computed radius did not reduce the blur, so the shader is not using "
        << "the radius the CPU formula gives";
    // At and above it the cap is inert, so the frame stops changing. That is the agreement.
    EXPECT_NEAR(aboveTheRadius, atTheRadius, atTheRadius * 0.15 + 1.0)
        << "raising the cap past the CPU radius still changed the frame, so the shader is asking "
        << "for a larger circle than the formula does";
}

// ── The settings and the fallback (MOD-2013, MOD-2014) ───────────────────────

TEST(DepthOfFieldTest, TheSettingsBagWinsOverThePassLocalDefaults)
{
    // Every pass in this layer honours the bag in preference to its own fields, so a pipeline that
    // applied a preset is not overruled by a default nobody set.
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd);
    auto source = MakeCheckerboard(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    // The pass is told to focus near; the bag says focus far, and the bag must win.
    pass.setFocusDistance(kNearHalfDepth);
    RenderPipelineSettings settings;
    settings.setDOFFocusDistance(kFarHalfDepth);
    settings.setDOFFocalLength(135.0f);
    settings.setDOFFNumber(1.4f);
    settings.setDOFMaxRadius(0.06f);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    context.settings    = &settings;
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_GT(ContrastOverRows(pixels, kSize / 2 + 8, kSize - 5),
              ContrastOverRows(pixels, 4, kSize / 2 - 8))
        << "the pass used its own focus distance instead of the settings bag's";
}

TEST(DepthOfFieldTest, TheSettingsRoundTripAndNonsenseIsIgnored)
{
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);

    EXPECT_GT(pass.getFocusDistance(), 0.0f);
    EXPECT_GT(pass.getFocalLength(), 0.0f);
    EXPECT_GT(pass.getFNumber(), 0.0f);
    EXPECT_GT(pass.getMaxRadius(), 0.0f);

    pass.setFocusDistance(3.5f);
    pass.setFocalLength(85.0f);
    pass.setFNumber(1.8f);
    pass.setMaxRadius(0.05f);
    EXPECT_FLOAT_EQ(pass.getFocusDistance(), 3.5f);
    EXPECT_FLOAT_EQ(pass.getFocalLength(), 85.0f);
    EXPECT_FLOAT_EQ(pass.getFNumber(), 1.8f);
    EXPECT_FLOAT_EQ(pass.getMaxRadius(), 0.05f);

    // A zero or negative lens is nonsense rather than a value with an obvious meaning, so it is
    // refused; the radius is a budget with a well-defined ceiling, so it is clamped.
    pass.setFocusDistance(0.0f);
    pass.setFocalLength(-1.0f);
    pass.setFNumber(0.0f);
    EXPECT_FLOAT_EQ(pass.getFocusDistance(), 3.5f);
    EXPECT_FLOAT_EQ(pass.getFocalLength(), 85.0f);
    EXPECT_FLOAT_EQ(pass.getFNumber(), 1.8f);

    pass.setMaxRadius(5.0f);
    EXPECT_FLOAT_EQ(pass.getMaxRadius(), 0.25f);
    pass.setMaxRadius(-1.0f);
    EXPECT_FLOAT_EQ(pass.getMaxRadius(), 0.0f);
}

TEST(DepthOfFieldTest, WithoutDepthTheFrameIsPassedThroughUnchanged)
{
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(130, 70, 40, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = nullptr;
    EXPECT_NO_THROW(pass.apply(context));

    const std::vector<Color> pixels = ReadTarget(destination);
    const std::size_t middle = static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2;
    EXPECT_NEAR(pixels[middle].getRProperty(), 130, 4);
    EXPECT_NEAR(pixels[middle].getGProperty(), 70, 4);
}

TEST(DepthOfFieldTest, WithoutACameraTheFrameIsPassedThroughUnchanged)
{
    // The far plane is what turns a normalised depth back into metres, so without it the optics
    // have no distances to work with. Copying through beats guessing a lens.
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto depth  = MakeSplitDepth(gd);
    auto source = MakeImage(gd, [](int, int) { return Color(130, 70, 40, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    context.sourceDepth = depth.get();
    context.farPlane    = 0.0f;
    EXPECT_NO_THROW(pass.apply(context));

    const std::vector<Color> pixels = ReadTarget(destination);
    const std::size_t middle = static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2;
    EXPECT_NEAR(pixels[middle].getRProperty(), 130, 4);
}

TEST(DepthOfFieldTest, TheNameIsStable)
{
    GraphicsDevice gd;
    DepthOfFieldPass pass(gd);
    EXPECT_EQ(pass.getName(), "DepthOfField");
}

} // namespace

#endif // CNA_CNAEXT
