// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1552: auto-exposure, the compute reduction that MOD-308 was waiting for.
//
// The reduction is checkable exactly: a texture of one uniform colour has a known log-average
// luminance, and a texture of two halves has the geometric mean of the two. Everything above that
// -- the key value, the asymmetric adaptation, the clamps -- is arithmetic that can be asserted
// without a GPU at all once the measurement is trusted.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/AutoExposureEXT.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CNA::Graphics::AutoExposureEXT;
using CNA::Graphics::RenderPipelineSettings;

namespace {

    class AutoExposureTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;

        [[nodiscard]] bool supported() const
        {
            return gd.SupportsCapability(CNA::GraphicsCapability::ComputeShaders);
        }

        /// A texture of one grey, and the luminance that grey has.
        static std::unique_ptr<Texture2D> Grey(GraphicsDevice& device, const int value)
        {
            auto texture = std::make_unique<Texture2D>(device, 64, 64);
            const std::vector<Color> pixels(64 * 64, Color(value, value, value, 255));
            texture->SetData(pixels.data(), static_cast<int>(pixels.size()));
            return texture;
        }
    };

} // namespace

TEST_F(AutoExposureTest, AUniformFrameMeasuresItsOwnLuminance)
{
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    AutoExposureEXT exposure(gd);

    for (const int grey : {32, 64, 128, 200})
    {
        auto texture = Grey(gd, grey);
        const float expected = static_cast<float>(grey) / 255.0f;
        // sRGB is not decoded here -- the reduction reads what the texture holds, which for an HDR
        // scene target is already linear. The tolerance is the 8-bit quantisation.
        EXPECT_NEAR(exposure.measureAverageLuminance(*texture), expected, 0.01f)
            << "grey " << grey;
    }
}

TEST_F(AutoExposureTest, ASplitFrameMeasuresTheGeometricMeanNotTheArithmeticOne)
{
    // The reason the reduction is a LOG average: half the frame at 1.0 and half at 0.01 has an
    // arithmetic mean of ~0.5 and a geometric mean of 0.1. A plain mean would expose for the
    // bright half and crush everything else, which is the classic auto-exposure artefact.
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    AutoExposureEXT exposure(gd);

    Texture2D texture(gd, 64, 64);
    std::vector<Color> pixels(64 * 64, Color::White);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            pixels[static_cast<std::size_t>(y) * 64 + x] =
                (x < 32) ? Color(255, 255, 255, 255) : Color(3, 3, 3, 255);
    texture.SetData(pixels.data(), static_cast<int>(pixels.size()));

    const float bright = 1.0f;
    const float dark = 3.0f / 255.0f;
    const float geometric = std::sqrt(bright * dark);
    EXPECT_NEAR(exposure.measureAverageLuminance(texture), geometric, 0.01f);
    EXPECT_LT(exposure.measureAverageLuminance(texture), 0.5f)
        << "the reduction is behaving like an arithmetic mean";
}

TEST_F(AutoExposureTest, TheExposureAimsTheFrameAtTheKeyValue)
{
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    AutoExposureEXT exposure(gd);
    EXPECT_FLOAT_EQ(exposure.getKeyValue(), 0.18f);

    auto dim = Grey(gd, 26);      // ~0.1 linear
    // A delta of 0 snaps, which is what a first frame wants: no fade-in from nothing.
    const float snapped = exposure.update(*dim, 0.0f);
    EXPECT_NEAR(snapped, 0.18f / 0.102f, 0.2f);
    EXPECT_FLOAT_EQ(exposure.getExposure(), snapped);

    exposure.setKeyValue(0.36f);
    EXPECT_FLOAT_EQ(exposure.getKeyValue(), 0.36f);
    const float brighter = exposure.update(*dim, 0.0f);
    EXPECT_NEAR(brighter, snapped * 2.0f, 0.4f)
        << "doubling the key value did not double the exposure";
}

TEST_F(AutoExposureTest, AdaptationIsGradualAndAsymmetric)
{
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    AutoExposureEXT exposure(gd);
    exposure.setAdaptationSpeeds(4.0f, 1.0f);
    EXPECT_FLOAT_EQ(exposure.getBrighteningSpeed(), 4.0f);
    EXPECT_FLOAT_EQ(exposure.getDarkeningSpeed(), 1.0f);

    auto bright = Grey(gd, 230);
    auto dim = Grey(gd, 20);

    // Settle on the bright frame, then let the scene go dark: the exposure must RISE, and slowly.
    exposure.update(*bright, 0.0f);
    const float settled = exposure.getExposure();
    const float afterOneFrame = exposure.update(*dim, 1.0f / 60.0f);
    EXPECT_GT(afterOneFrame, settled);
    const float darkTarget = exposure.update(*dim, 0.0f);   // where it is heading
    EXPECT_LT(afterOneFrame, darkTarget) << "the adaptation snapped instead of easing";

    // The other direction is faster: from the same distance, one frame covers more ground.
    AutoExposureEXT other(gd);
    other.setAdaptationSpeeds(4.0f, 1.0f);
    other.update(*dim, 0.0f);
    const float fromDark = other.getExposure();
    const float towardBright = other.update(*bright, 1.0f / 60.0f);
    const float brightTarget = other.update(*bright, 0.0f);
    const float brighteningFraction = (fromDark - towardBright) / (fromDark - brightTarget);

    AutoExposureEXT third(gd);
    third.setAdaptationSpeeds(4.0f, 1.0f);
    third.update(*bright, 0.0f);
    const float fromBright = third.getExposure();
    const float towardDark = third.update(*dim, 1.0f / 60.0f);
    const float darkening = third.update(*dim, 0.0f);
    const float darkeningFraction = (towardDark - fromBright) / (darkening - fromBright);

    EXPECT_GT(brighteningFraction, darkeningFraction)
        << "adapting to a brighter scene is supposed to be the fast direction";
}

TEST_F(AutoExposureTest, TheExposureIsClampedAndSettingsReceiveIt)
{
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    AutoExposureEXT exposure(gd);
    exposure.setExposureRange(0.5f, 2.0f);

    auto black = Grey(gd, 0);
    EXPECT_LE(exposure.update(*black, 0.0f), 2.0f) << "a black frame drove the exposure past its "
                                                      "ceiling";
    auto white = Grey(gd, 255);
    EXPECT_GE(exposure.update(*white, 0.0f), 0.5f);

    RenderPipelineSettings settings;
    exposure.setExposure(1.25f);
    exposure.applyTo(settings);
    EXPECT_FLOAT_EQ(settings.getExposure(), 1.25f);
}

TEST_F(AutoExposureTest, TheParametersAreValidated)
{
    if (!supported()) GTEST_SKIP() << "this renderer does not support compute shaders";
    AutoExposureEXT exposure(gd);
    EXPECT_THROW(exposure.setExposure(0.0f), std::invalid_argument);
    EXPECT_THROW(exposure.setKeyValue(-1.0f), std::invalid_argument);
    EXPECT_THROW(exposure.setAdaptationSpeeds(0.0f, 1.0f), std::invalid_argument);
    EXPECT_THROW(exposure.setAdaptationSpeeds(1.0f, -1.0f), std::invalid_argument);
    EXPECT_THROW(exposure.setExposureRange(0.0f, 1.0f), std::invalid_argument);
    EXPECT_THROW(exposure.setExposureRange(2.0f, 1.0f), std::invalid_argument);
}

#endif // CNA_CNAEXT
