// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2081: a grid of probes, blended so indirect light varies through a space.
//
// The thing that has to hold is that the blend is *smooth and correct at the ends*: at a probe's
// own position the volume must return that probe unchanged, and between two probes it must move
// monotonically from one to the other. A grid that got the index arithmetic wrong still produces a
// smooth gradient -- just between the wrong pair of probes -- so both halves are asserted.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>
#include <stdexcept>

namespace {

using CNA::Graphics::LightProbeEXT;
using CNA::Graphics::LightProbeVolumeEXT;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Vector3;

BoundingBox UnitRoom()
{
    return BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 4.0f, 6.0f));
}

/// A probe whose only light is a constant term of the given brightness.
LightProbeEXT Ambient(const float brightness)
{
    LightProbeEXT probe;
    probe.setCoefficient(0, Vector3(brightness, brightness, brightness));
    return probe;
}

TEST(LightProbeVolumeEXTTest, TheGridSpansTheBoxCornerToCorner)
{
    const LightProbeVolumeEXT volume(UnitRoom(), 3, 2, 2);
    EXPECT_EQ(volume.getCountX(), 3);
    EXPECT_EQ(volume.getCountY(), 2);
    EXPECT_EQ(volume.getCountZ(), 2);
    EXPECT_EQ(volume.getProbeCount(), 12);
    EXPECT_TRUE(volume.isZero());

    // The probes sit at the grid's corners, so the first and last are on the box's own faces.
    EXPECT_FLOAT_EQ(volume.getProbePosition(0, 0, 0).X, 0.0f);
    EXPECT_FLOAT_EQ(volume.getProbePosition(2, 0, 0).X, 10.0f);
    EXPECT_FLOAT_EQ(volume.getProbePosition(1, 0, 0).X, 5.0f);
    EXPECT_FLOAT_EQ(volume.getProbePosition(0, 1, 1).Y, 4.0f);
    EXPECT_FLOAT_EQ(volume.getProbePosition(0, 1, 1).Z, 6.0f);

    // And a probe knows where it is, without anyone having told it.
    EXPECT_FLOAT_EQ(volume.getProbe(2, 1, 1).getPosition().X, 10.0f);
}

TEST(LightProbeVolumeEXTTest, ASingleProbeOnAnAxisCoversThatWholeAxis)
{
    // A one-probe axis has no span to interpolate across, and the answer is that one probe
    // everywhere rather than a division by zero.
    LightProbeVolumeEXT volume(UnitRoom(), 1, 1, 1);
    volume.setProbe(0, 0, 0, Ambient(2.0f));

    for (const float x : {0.0f, 5.0f, 10.0f})
        EXPECT_FLOAT_EQ(volume.sampleProbe(Vector3(x, 2.0f, 3.0f)).getCoefficient(0).X, 2.0f);
}

TEST(LightProbeVolumeEXTTest, SamplingAtAProbeReturnsThatProbeExactly)
{
    // The end condition. An off-by-one in the index arithmetic still gives a smooth gradient --
    // between the wrong pair -- so the ends are what pins which probes are being blended.
    LightProbeVolumeEXT volume(UnitRoom(), 3, 2, 2);
    float brightness = 1.0f;
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
                volume.setProbe(x, y, z, Ambient(brightness++));

    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 3; ++x)
            {
                const Vector3 position = volume.getProbePosition(x, y, z);
                EXPECT_NEAR(volume.sampleProbe(position).getCoefficient(0).X,
                            volume.getProbe(x, y, z).getCoefficient(0).X, 1e-4f)
                    << "at grid " << x << "," << y << "," << z;
            }
}

TEST(LightProbeVolumeEXTTest, ASurfaceMovingBetweenTwoProbesChangesSmoothly)
{
    // The row's own acceptance criterion, and the reason a volume exists at all.
    LightProbeVolumeEXT volume(UnitRoom(), 2, 1, 1);
    volume.setProbe(0, 0, 0, Ambient(1.0f));
    volume.setProbe(1, 0, 0, Ambient(5.0f));

    float previous = -1.0f;
    for (int step = 0; step <= 10; ++step)
    {
        const float x = 10.0f * static_cast<float>(step) / 10.0f;
        const Vector3 irradiance = volume.irradiance(Vector3(x, 2.0f, 3.0f),
                                                     Vector3(0.0f, 1.0f, 0.0f));
        EXPECT_GT(irradiance.X, previous) << "the ambient did not increase at x = " << x;
        previous = irradiance.X;
    }

    // Halfway is halfway, not merely somewhere between: the blend is linear in the coefficients.
    const float half = volume.sampleProbe(Vector3(5.0f, 2.0f, 3.0f)).getCoefficient(0).X;
    EXPECT_NEAR(half, 3.0f, 1e-4f);
}

TEST(LightProbeVolumeEXTTest, EveryAxisBlendsIndependently)
{
    // Trilinear means three separate weights, and a volume that used one axis' weight for another
    // would still interpolate -- along the wrong direction. Each axis is checked on its own.
    LightProbeVolumeEXT volume(UnitRoom(), 2, 2, 2);
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                volume.setProbe(x, y, z, Ambient(0.0f));

    volume.setProbe(1, 0, 0, Ambient(1.0f));
    EXPECT_NEAR(volume.sampleProbe(Vector3(10.0f, 0.0f, 0.0f)).getCoefficient(0).X, 1.0f, 1e-4f);
    EXPECT_NEAR(volume.sampleProbe(Vector3(0.0f, 0.0f, 0.0f)).getCoefficient(0).X, 0.0f, 1e-4f);
    EXPECT_NEAR(volume.sampleProbe(Vector3(10.0f, 4.0f, 0.0f)).getCoefficient(0).X, 0.0f, 1e-4f)
        << "moving along y changed nothing about which probe is at the far x";

    volume.setProbe(1, 0, 0, Ambient(0.0f));
    volume.setProbe(0, 1, 0, Ambient(1.0f));
    EXPECT_NEAR(volume.sampleProbe(Vector3(0.0f, 4.0f, 0.0f)).getCoefficient(0).X, 1.0f, 1e-4f);

    volume.setProbe(0, 1, 0, Ambient(0.0f));
    volume.setProbe(0, 0, 1, Ambient(1.0f));
    EXPECT_NEAR(volume.sampleProbe(Vector3(0.0f, 0.0f, 6.0f)).getCoefficient(0).X, 1.0f, 1e-4f);
}

TEST(LightProbeVolumeEXTTest, OutsideTheVolumeClampsRatherThanGoingDark)
{
    // A character stepping one unit past the last probe should not lose its ambient entirely.
    // Clamping is wrong slowly; falling back to nothing is wrong suddenly.
    LightProbeVolumeEXT volume(UnitRoom(), 2, 1, 1);
    volume.setProbe(0, 0, 0, Ambient(1.0f));
    volume.setProbe(1, 0, 0, Ambient(5.0f));

    EXPECT_FALSE(volume.contains(Vector3(-100.0f, 2.0f, 3.0f)));
    EXPECT_TRUE(volume.contains(Vector3(5.0f, 2.0f, 3.0f)));

    EXPECT_NEAR(volume.sampleProbe(Vector3(-100.0f, 2.0f, 3.0f)).getCoefficient(0).X, 1.0f, 1e-4f);
    EXPECT_NEAR(volume.sampleProbe(Vector3(999.0f, 2.0f, 3.0f)).getCoefficient(0).X, 5.0f, 1e-4f);

    // And the blended probe reports a position inside the box, so a caller cannot be told the
    // light came from somewhere the volume does not reach.
    EXPECT_TRUE(volume.contains(volume.sampleProbe(Vector3(-100.0f, -50.0f, 900.0f)).getPosition()));
}

TEST(LightProbeVolumeEXTTest, AProbeCannotBePlacedSomewhereTheGridDoesNotSayItIs)
{
    // The interpolation weights come from the grid. A probe carrying a different position would
    // make the weights describe one arrangement and the light another.
    LightProbeVolumeEXT volume(UnitRoom(), 2, 1, 1);
    LightProbeEXT elsewhere = Ambient(3.0f);
    elsewhere.setPosition(Vector3(-999.0f, -999.0f, -999.0f));
    volume.setProbe(1, 0, 0, elsewhere);

    EXPECT_FLOAT_EQ(volume.getProbe(1, 0, 0).getPosition().X, 10.0f);
    EXPECT_FLOAT_EQ(volume.getProbe(1, 0, 0).getCoefficient(0).X, 3.0f)
        << "the light itself must survive the position being corrected";
}

TEST(LightProbeVolumeEXTTest, ANonsensicalGridIsRefused)
{
    EXPECT_THROW(LightProbeVolumeEXT(UnitRoom(), 0, 1, 1), std::invalid_argument);
    EXPECT_THROW(LightProbeVolumeEXT(UnitRoom(), 1, -1, 1), std::invalid_argument);
    EXPECT_THROW(LightProbeVolumeEXT(UnitRoom(), 100, 100, 100), std::invalid_argument);
    EXPECT_THROW(LightProbeVolumeEXT(BoundingBox(Vector3(5.0f, 0.0f, 0.0f),
                                                 Vector3(0.0f, 1.0f, 1.0f)), 2, 2, 2),
                 std::invalid_argument);
    EXPECT_NO_THROW(LightProbeVolumeEXT(UnitRoom(), 1, 1, 1));
}

TEST(LightProbeVolumeEXTTest, AnIndexOutsideTheGridIsRefused)
{
    LightProbeVolumeEXT volume(UnitRoom(), 2, 2, 2);
    EXPECT_THROW((void)volume.getProbe(2, 0, 0), std::out_of_range);
    EXPECT_THROW((void)volume.getProbe(0, -1, 0), std::out_of_range);
    EXPECT_THROW((void)volume.getProbePosition(0, 0, 2), std::out_of_range);
    EXPECT_THROW(volume.setProbe(0, 0, 2, LightProbeEXT()), std::out_of_range);
}

// ── Leak reduction (MOD-2083) ────────────────────────────────────────────────

TEST(LightProbeVolumeEXTTest, ALitProbeBehindAWallDoesNotLightWhatIsOnTheOtherSide)
{
    // The defect that makes a naive probe grid unusable indoors. Two probes ten units apart with a
    // wall between them: the lit one has recorded geometry one unit away in the direction of the
    // dark room, so it cannot be lighting anything three units into it.
    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f)),
                               2, 1, 1);

    LightProbeEXT lit = Ambient(10.0f);
    // A flat wall: almost no variance, so the cut-off is sharp. Recorded along +X, which is the
    // direction the dark room lies in.
    lit.setVisibility(0, 1.0f, 1.0f);
    volume.setProbe(0, 0, 0, lit);
    volume.setProbe(1, 0, 0, Ambient(0.0f));

    // Just past the wall, most of the trilinear weight still belongs to the lit probe.
    const float justPast = volume.sampleProbe(Vector3(2.0f, 0.0f, 0.0f)).getCoefficient(0).X;
    EXPECT_LT(justPast, 0.5f)
        << "the lit room leaked through the wall: " << justPast << " of a possible 10";

    // And with the wall removed -- the same probe with nothing recorded -- it leaks, which is what
    // makes the test above about the visibility test rather than about the geometry.
    LightProbeVolumeEXT leaky(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f)),
                              2, 1, 1);
    leaky.setProbe(0, 0, 0, Ambient(10.0f));
    leaky.setProbe(1, 0, 0, Ambient(0.0f));
    EXPECT_GT(leaky.sampleProbe(Vector3(2.0f, 0.0f, 0.0f)).getCoefficient(0).X, 7.0f);
}

TEST(LightProbeVolumeEXTTest, RejectingACornerRedistributesRatherThanDarkens)
{
    // A surface beside a wall should be lit by the probes that *can* see it, not by a fraction of
    // the ones that cannot. Both probes are equally bright, so if the weights were not
    // renormalised the result would fall below their common value.
    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f)),
                               2, 1, 1);

    LightProbeEXT blocked = Ambient(4.0f);
    blocked.setVisibility(0, 0.5f, 0.25f);
    volume.setProbe(0, 0, 0, blocked);
    volume.setProbe(1, 0, 0, Ambient(4.0f));

    EXPECT_NEAR(volume.sampleProbe(Vector3(3.0f, 0.0f, 0.0f)).getCoefficient(0).X, 4.0f, 1e-3f)
        << "rejecting a corner darkened the result instead of redistributing its share";
}

TEST(LightProbeVolumeEXTTest, APointEnclosedOnEverySideLeaksRatherThanGoesBlack)
{
    // Every corner rejected. A leak is wrong; a hole in the lighting is *more* visible, so the leak
    // is the deliberate choice -- and it is asserted rather than left as an accident of the code.
    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f)),
                               2, 1, 1);

    LightProbeEXT walled = Ambient(6.0f);
    for (int direction = 0; direction < LightProbeEXT::kVisibilityDirections; ++direction)
        walled.setVisibility(direction, 0.01f, 0.0001f);
    volume.setProbe(0, 0, 0, walled);
    volume.setProbe(1, 0, 0, walled);

    EXPECT_NEAR(volume.sampleProbe(Vector3(5.0f, 0.0f, 0.0f)).getCoefficient(0).X, 6.0f, 1e-3f)
        << "a point walled in on every side went black instead of falling back to the blend";
}

TEST(LightProbeVolumeEXTTest, AProbeWithNoRecordedVisibilityIsTrustedCompletely)
{
    // Every probe in every volume built before MOD-2083 has no visibility recorded. They must keep
    // behaving exactly as they did, or the fix would darken every existing scene.
    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(10.0f, 0.0f, 0.0f)),
                               2, 1, 1);
    volume.setProbe(0, 0, 0, Ambient(2.0f));
    volume.setProbe(1, 0, 0, Ambient(6.0f));

    EXPECT_FALSE(volume.getProbe(0, 0, 0).hasVisibility());
    EXPECT_NEAR(volume.sampleProbe(Vector3(5.0f, 0.0f, 0.0f)).getCoefficient(0).X, 4.0f, 1e-3f);
}

TEST(LightProbeEXTTest, TheVisibilityTestFadesWithVarianceAndIsBoundedAtBothEnds)
{
    LightProbeEXT probe;
    EXPECT_FLOAT_EQ(probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 5.0f), 1.0f)
        << "a probe told nothing about its surroundings must not become useless";

    // A flat wall two units away: everything nearer is seen, everything past it is not.
    probe.setVisibility(0, 2.0f, 4.0f);
    EXPECT_FLOAT_EQ(probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 2.0f), 1.0f);
    EXPECT_LT(probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 4.0f), 0.01f);

    // A cluttered direction -- the same mean, far more variance -- fades instead of cutting off.
    LightProbeEXT cluttered;
    cluttered.setVisibility(0, 2.0f, 20.0f);
    EXPECT_GT(cluttered.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 4.0f), 0.5f);

    // The opposite direction was never recorded, so it is not tested against.
    EXPECT_FLOAT_EQ(probe.visibilityWeight(Vector3(-1.0f, 0.0f, 0.0f), 9.0f), 1.0f);

    // No distribution has negative variance, and one that appeared to would push the test outside
    // [0, 1] -- which is a probe contributing negatively.
    LightProbeEXT impossible;
    impossible.setVisibility(0, 3.0f, 1.0f);
    EXPECT_GE(impossible.getVisibilityMeanSquared(0), 9.0f);
    const float weight = impossible.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 10.0f);
    EXPECT_GE(weight, 0.0f);
    EXPECT_LE(weight, 1.0f);

    EXPECT_THROW(probe.setVisibility(6, 1.0f, 1.0f), std::out_of_range);
    EXPECT_THROW((void)probe.getVisibilityMean(-1), std::out_of_range);
    EXPECT_THROW((void)probe.getVisibilityMeanSquared(6), std::out_of_range);
}

TEST(LightProbeVolumeEXTTest, TheVisibilityWeightBlendsAcrossAxesRatherThanSnapping)
{
    // Snapping the direction to its nearest axis makes the weight jump as a surface turns, and a
    // discontinuity in an ambient term is more visible than the leak it was fixing. A direction
    // between two axes has to give an answer between their two.
    LightProbeEXT probe;
    probe.setVisibility(0, 1.0f, 1.0f);        // +X blocked close
    probe.setVisibility(4, 100.0f, 10000.0f);  // +Z wide open

    const float alongX = probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 5.0f);
    const float alongZ = probe.visibilityWeight(Vector3(0.0f, 0.0f, 1.0f), 5.0f);
    const float between = probe.visibilityWeight(Vector3(1.0f, 0.0f, 1.0f), 5.0f);

    EXPECT_LT(alongX, 0.05f);
    EXPECT_FLOAT_EQ(alongZ, 1.0f);
    EXPECT_GT(between, alongX);
    EXPECT_LE(between, alongZ);
}

} // namespace

#endif // CNA_CNAEXT
