// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2053: a sky computed from the sun rather than sampled from a cube.
//
// What a parameterised sky has to earn is that its *physics* behave: a clear sky blue because short
// wavelengths scatter more, a sunset red because the light path lengthened, a haze white because
// Mie scattering has no colour. Those are all statements about ratios between channels and between
// directions, so that is what the tests measure -- not that the frame looks like a sky.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/AtmosphericSky.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::AtmosphericSky;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

constexpr int kSize = 64;

const Vector3 kUp(0.0f, 1.0f, 0.0f);
const Vector3 kMiddaySun(0.0f, -1.0f, 0.0f);          // straight down
const Vector3 kSunsetSun(0.0f, -0.08f, -1.0f);        // nearly horizontal
// High, but not overhead. A sun at the zenith is a sun the zenith tests would be *staring at*, and
// the sky around the sun is white in every real photograph -- both phase functions peak forward, so
// asking that spot to be blue or dimmer than the horizon asks the model to be wrong.
const Vector3 kHighSun(0.0f, -0.7071f, -0.7071f);     // 45 degrees up, towards -Z

// The sun is *at* the negation of its direction, which is the trap this file walked into once: a
// direction of (0, -0.08, -1) means light travelling towards -Z, so the sun is at +Z and a camera
// looking towards -Z is looking away from it.
const Vector3 kTowardsSunsetSun(0.0f, 0.2f, 1.0f);
const Vector3 kAwayFromSunsetSun(0.0f, 0.2f, -1.0f);

// ── The model (MOD-2053) ─────────────────────────────────────────────────────

TEST(AtmosphericSkyTest, AClearSkyOverheadIsBlue)
{
    // Rayleigh's coefficients fall as the fourth power of wavelength, so looking away from a high
    // sun the blue channel has to dominate. A model that got that ratio the wrong way round would
    // still produce a smooth gradient sky -- an orange one.
    const Vector3 overhead = AtmosphericSky::radiance(kUp, kHighSun, 2.0f);
    EXPECT_GT(overhead.Z, overhead.Y) << "blue did not exceed green overhead";
    EXPECT_GT(overhead.Y, overhead.X) << "green did not exceed red overhead";
    EXPECT_GT(overhead.Z, overhead.X * 1.5f) << "the sky is barely blue at all";
}

TEST(AtmosphericSkyTest, ALowSunRedensTheSky)
{
    // Not a tint applied at sunset: the light path through the air has lengthened, the blue has
    // been scattered out of it before it arrives, and what is left is red. So the *ratio* has to
    // move, not just the brightness.
    const Vector3 midday = AtmosphericSky::radiance(kUp, kHighSun, 2.0f);
    const Vector3 sunset = AtmosphericSky::radiance(kUp, kSunsetSun, 2.0f);

    const float middayRatio = midday.Z / midday.X;
    const float sunsetRatio = sunset.Z / sunset.X;
    EXPECT_LT(sunsetRatio, middayRatio)
        << "a low sun did not shift the sky towards red: " << sunsetRatio << " against "
        << middayRatio;
}

TEST(AtmosphericSkyTest, LookingTowardsTheSunIsBrighterThanLookingAway)
{
    // Both phase functions peak forward, Mie's sharply. A sky whose brightest point is not near the
    // sun is not a sky.
    const Vector3 towards = AtmosphericSky::radiance(kTowardsSunsetSun, kSunsetSun, 3.0f);
    const Vector3 away    = AtmosphericSky::radiance(kAwayFromSunsetSun, kSunsetSun, 3.0f);
    EXPECT_GT(towards.X + towards.Y + towards.Z, away.X + away.Y + away.Z)
        << "the sky is no brighter towards the sun than away from it";
}

TEST(AtmosphericSkyTest, HazeWhitensTheSkyRatherThanColouringIt)
{
    // Mie scattering does not depend on wavelength, so raising the turbidity has to move the
    // channels *together* -- the blue-to-red ratio falls towards one. A haze that reddened or
    // blued the sky would be Rayleigh wearing the wrong name.
    const Vector3 clear = AtmosphericSky::radiance(Vector3(0.0f, 0.3f, -1.0f), kMiddaySun, 1.0f);
    const Vector3 hazy  = AtmosphericSky::radiance(Vector3(0.0f, 0.3f, -1.0f), kMiddaySun, 9.0f);
    EXPECT_LT(hazy.Z / hazy.X, clear.Z / clear.X)
        << "haze did not move the sky towards white";
    EXPECT_GT(hazy.X, clear.X) << "haze did not brighten the red channel";
}

TEST(AtmosphericSkyTest, TheHorizonIsBrighterThanTheZenithUnderAHighSun)
{
    // A longer path through the air means more of it scattering into the eye, and it is what gives
    // a real sky its pale band at the horizon.
    const Vector3 zenith  = AtmosphericSky::radiance(kUp, kHighSun, 2.0f);
    const Vector3 horizon = AtmosphericSky::radiance(Vector3(0.0f, 0.02f, -1.0f), kHighSun, 2.0f);
    EXPECT_GT(horizon.X + horizon.Y + horizon.Z, zenith.X + zenith.Y + zenith.Z)
        << "the horizon was not brighter than the zenith";
}

TEST(AtmosphericSkyTest, DegenerateDirectionsDoNotProduceNaNs)
{
    // A zero direction has no normalisation, and a horizon-grazing view divides by an air mass that
    // wants to run away. Both are reachable from a game's own camera, and a NaN here would paint
    // the whole sky black rather than fail.
    const Vector3 zero(0.0f, 0.0f, 0.0f);
    for (const Vector3& sky : {AtmosphericSky::radiance(zero, kMiddaySun, 2.0f),
                               AtmosphericSky::radiance(kUp, zero, 2.0f),
                               AtmosphericSky::radiance(Vector3(0.0f, -1.0f, 0.0f), kSunsetSun, 2.0f)})
    {
        EXPECT_EQ(sky.X, sky.X) << "the red channel is NaN";
        EXPECT_EQ(sky.Y, sky.Y) << "the green channel is NaN";
        EXPECT_EQ(sky.Z, sky.Z) << "the blue channel is NaN";
        EXPECT_GE(sky.X, 0.0f);
    }
}

// ── The frame ────────────────────────────────────────────────────────────────

TEST(AtmosphericSkyTest, ItDrawsASkyRatherThanAFlatColour)
{
    GraphicsDevice gd;
    AtmosphericSky sky(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!sky.isSupported()) GTEST_SKIP() << "this renderer cannot draw the atmospheric sky";

    RenderTarget2D target(gd, kSize, kSize);
    // Towards the sun, so the frame contains the bright part of the sky and the assertions below
    // have something to measure; looking the other way at sunset is very nearly black, correctly.
    const Matrix view = Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.15f, 1.0f), kUp);
    const Matrix projection =
        Matrix::CreatePerspectiveFieldOfView(1.2f, 1.0f, 0.1f, 100.0f);

    sky.setSunDirection(kSunsetSun);
    sky.setTurbidity(3.0f);
    sky.setIntensity(1.0f);

    gd.SetRenderTarget(&target);
    gd.Clear(Color::Black);
    sky.draw(view, projection, kSize, kSize);
    gd.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    int lowest = 255, highest = 0;
    long long blue = 0, red = 0;
    for (const Color& p : pixels)
    {
        lowest = std::min(lowest, static_cast<int>(p.getBProperty()));
        highest = std::max(highest, static_cast<int>(p.getBProperty()));
        blue += p.getBProperty();
        red += p.getRProperty();
    }
    EXPECT_GT(highest, 20) << "the sky came back black";
    EXPECT_GT(highest - lowest, 10) << "the sky is a flat colour, so nothing varies with direction";
    EXPECT_GT(blue, 0) << "no blue anywhere in the sky";
    (void)red;
}

TEST(AtmosphericSkyTest, AnInvalidSizeIsRejected)
{
    GraphicsDevice gd;
    AtmosphericSky sky(gd);
    const Matrix identity = Matrix::getIdentityProperty();
    EXPECT_THROW(sky.draw(identity, identity, 0, 4), std::invalid_argument);
    EXPECT_THROW(sky.draw(identity, identity, 4, -1), std::invalid_argument);
}

TEST(AtmosphericSkyTest, TheSettingsRoundTripAndNonsenseIsIgnored)
{
    GraphicsDevice gd;
    AtmosphericSky sky(gd);

    sky.setSunDirection(Vector3(0.0f, -2.0f, 0.0f));
    EXPECT_NEAR(sky.getSunDirection().Y, -1.0f, 1e-5f) << "the direction was not normalised";
    sky.setSunDirection(Vector3(0.0f, 0.0f, 0.0f));
    EXPECT_NEAR(sky.getSunDirection().Y, -1.0f, 1e-5f) << "a zero direction must be ignored";

    sky.setTurbidity(4.0f);
    EXPECT_FLOAT_EQ(sky.getTurbidity(), 4.0f);
    sky.setTurbidity(0.0f);
    EXPECT_FLOAT_EQ(sky.getTurbidity(), 1.0f) << "turbidity below 1 is thinner than any air";
    sky.setTurbidity(99.0f);
    EXPECT_FLOAT_EQ(sky.getTurbidity(), 10.0f);

    sky.setIntensity(0.5f);
    EXPECT_FLOAT_EQ(sky.getIntensity(), 0.5f);
    sky.setIntensity(-1.0f);
    EXPECT_FLOAT_EQ(sky.getIntensity(), 0.5f);
}

} // namespace

#endif // CNA_CNAEXT
