// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2084: capturing probes by rendering the scene from where they stand.
//
// The interesting risk in a cube capture is that the six view matrices and whatever reads them back
// disagree about which way is which -- and a probe baked with a mirrored or rotated face still
// lights a scene smoothly, just from the wrong direction. So the tests light one direction at a
// time and ask the baked probe which way it thinks the light came from, on all six.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/LightProbeBaker.hpp"
#include "CNA/Graphics/LightProbeVolumeEXT.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::LightProbeBaker;
using CNA::Graphics::LightProbeEXT;
using CNA::Graphics::LightProbeVolumeEXT;
using Microsoft::Xna::Framework::BoundingBox;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr float kPi = 3.14159265359f;

/// The world direction a face's camera is looking, read back out of the view matrix the baker
/// built -- so the test asks the same question the baker's own reconstruction does.
Vector3 ForwardOf(const Matrix& view)
{
    return Vector3(-view.M13, -view.M23, -view.M33);
}

#define CNA_SKIP_WITHOUT_CAPTURE(baker)                                                            \
    do {                                                                                           \
        if (!(baker).isSupported())                                                                \
            GTEST_SKIP() << "this renderer cannot render to a target and read it back, so there "  \
                            "is nothing to capture a probe with";                                  \
    } while (false)

TEST(LightProbeBakerTest, ANonPositiveFaceSizeIsRefused)
{
    GraphicsDevice gd;
    EXPECT_THROW(LightProbeBaker(gd, 0), std::invalid_argument);
    EXPECT_THROW(LightProbeBaker(gd, -8), std::invalid_argument);
}

TEST(LightProbeBakerTest, ThePlanesAreValidatedAndTheDefaultsAreUsable)
{
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 8);
    EXPECT_EQ(baker.getFaceSize(), 8);
    EXPECT_EQ(LightProbeBaker::getFaceCount(), 6);
    EXPECT_GT(baker.getNearPlane(), 0.0f);
    EXPECT_GT(baker.getFarPlane(), baker.getNearPlane());

    baker.setPlanes(0.5f, 100.0f);
    EXPECT_FLOAT_EQ(baker.getNearPlane(), 0.5f);
    EXPECT_FLOAT_EQ(baker.getFarPlane(), 100.0f);
    EXPECT_THROW(baker.setPlanes(0.0f, 10.0f), std::invalid_argument);
    EXPECT_THROW(baker.setPlanes(10.0f, 1.0f), std::invalid_argument);
}

TEST(LightProbeBakerTest, TheSixFaceViewsLookAlongTheSixAxesFromTheProbe)
{
    // Before anything is rendered: the six cameras have to point six different ways, all from the
    // probe, and between them cover every axis. A capture whose faces overlap records the same part
    // of the scene twice and leaves the rest dark.
    const Vector3 position(3.0f, -2.0f, 7.0f);
    float coverage[3] = {0.0f, 0.0f, 0.0f};
    for (int face = 0; face < 6; ++face)
    {
        const Matrix view = LightProbeBaker::faceView(face, position);
        const Vector3 forward = ForwardOf(view);
        EXPECT_NEAR(std::sqrt(forward.X * forward.X + forward.Y * forward.Y +
                              forward.Z * forward.Z), 1.0f, 1e-4f);
        coverage[0] += std::fabs(forward.X);
        coverage[1] += std::fabs(forward.Y);
        coverage[2] += std::fabs(forward.Z);

        for (int other = 0; other < face; ++other)
        {
            const Vector3 previous = ForwardOf(LightProbeBaker::faceView(other, position));
            const float dot = forward.X * previous.X + forward.Y * previous.Y +
                              forward.Z * previous.Z;
            EXPECT_LT(dot, 0.99f) << "faces " << other << " and " << face << " look the same way";
        }
    }
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(coverage[axis], 2.0f, 1e-4f) << "axis " << axis << " is not covered twice";

    EXPECT_THROW((void)LightProbeBaker::faceView(6, position), std::out_of_range);
    EXPECT_THROW((void)LightProbeBaker::faceView(-1, position), std::out_of_range);
}

TEST(LightProbeBakerTest, ASceneLitFromOneDirectionBakesAProbeThatKnowsWhichOne)
{
    // The test that pins the view matrices against the direction reconstruction. Each face in turn
    // is the only one drawn white; the baked probe's brightest normal has to be the direction that
    // face was looking. A mirrored or rotated face passes every smoothness check and fails this.
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 16);
    CNA_SKIP_WITHOUT_CAPTURE(baker);

    for (int lit = 0; lit < 6; ++lit)
    {
        const Vector3 forward = ForwardOf(LightProbeBaker::faceView(lit, Vector3::Zero));
        const LightProbeEXT probe = baker.bakeProbe(
            Vector3::Zero, [&](const Matrix& view, const Matrix&) {
                // The one face looking the lit way fills itself; every other stays black.
                const Vector3 f = ForwardOf(view);
                const float dot = f.X * forward.X + f.Y * forward.Y + f.Z * forward.Z;
                if (dot > 0.99f) gd.Clear(Color::White);
            });

        const Vector3 towards = probe.irradiance(forward);
        const Vector3 away = probe.irradiance(Vector3(-forward.X, -forward.Y, -forward.Z));
        EXPECT_GT(towards.X, away.X + 0.05f)
            << "face " << lit << ": the baked probe does not know which way the light came from";
        EXPECT_GT(towards.X, 0.1f) << "face " << lit << " baked no light at all";
    }
}

TEST(LightProbeBakerTest, ASceneWhiteInEveryDirectionBakesAUniformProbe)
{
    // The energy half. Every face white is a uniform environment of radiance 1, which delivers
    // exactly pi to every normal -- the same number MOD-2080's projection is pinned against, now
    // reached through six rendered faces instead of a cube.
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 16);
    CNA_SKIP_WITHOUT_CAPTURE(baker);

    const LightProbeEXT probe = baker.bakeProbe(
        Vector3(1.0f, 2.0f, 3.0f), [&](const Matrix&, const Matrix&) { gd.Clear(Color::White); });

    EXPECT_FLOAT_EQ(probe.getPosition().Z, 3.0f);
    for (const Vector3& normal : {Vector3(0.0f, 1.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
                                  Vector3(0.0f, 0.0f, -1.0f), Vector3(0.577f, -0.577f, 0.577f)})
    {
        const Vector3 irradiance = probe.irradiance(normal);
        EXPECT_NEAR(irradiance.X, kPi, 0.05f);
        EXPECT_NEAR(irradiance.Z, kPi, 0.05f);
    }
}

TEST(LightProbeBakerTest, ADrawThatRendersNothingBakesADarkProbe)
{
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 8);
    CNA_SKIP_WITHOUT_CAPTURE(baker);

    const LightProbeEXT probe =
        baker.bakeProbe(Vector3::Zero, [](const Matrix&, const Matrix&) {});
    EXPECT_LT(probe.irradiance(Vector3(0.0f, 1.0f, 0.0f)).X, 1e-3f);
}

TEST(LightProbeBakerTest, BakingAVolumeFillsEveryProbeAtItsOwnPosition)
{
    // Each probe is captured from where the grid says it stands, and the draw is told where that
    // is through the view matrix -- so a scene that varies with position bakes a volume that does.
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 8);
    CNA_SKIP_WITHOUT_CAPTURE(baker);

    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(4.0f, 0.0f, 0.0f)),
                               2, 1, 1);
    EXPECT_TRUE(volume.isZero());

    baker.bakeLight(volume, [&](const Matrix& view, const Matrix&) {
        // The camera's world position is the probe's; brighter the further along x it stands.
        const Matrix inverse = Matrix::Invert(view);
        const float x = inverse.M41;
        const int level = static_cast<int>(x > 2.0f ? 200 : 40);
        gd.Clear(Color(level, level, level, 255));
    });

    EXPECT_FALSE(volume.isZero());
    const float dim = volume.getProbe(0, 0, 0).irradiance(Vector3(0.0f, 1.0f, 0.0f)).X;
    const float bright = volume.getProbe(1, 0, 0).irradiance(Vector3(0.0f, 1.0f, 0.0f)).X;
    EXPECT_GT(bright, dim * 3.0f) << "both probes captured the same scene";
    EXPECT_NEAR(dim, kPi * 40.0f / 255.0f, 0.05f);
    EXPECT_NEAR(bright, kPi * 200.0f / 255.0f, 0.05f);

    // And the probes stayed where the grid put them.
    EXPECT_FLOAT_EQ(volume.getProbe(1, 0, 0).getPosition().X, 4.0f);
}

TEST(LightProbeBakerTest, BakingVisibilityFillsTheMomentsTheLeakTestReads)
{
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 8);
    CNA_SKIP_WITHOUT_CAPTURE(baker);
    baker.setPlanes(0.05f, 100.0f);

    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)),
                               1, 1, 1);
    EXPECT_FALSE(volume.getProbe(0, 0, 0).hasVisibility());

    // A scene that is a uniform quarter of the far plane away in every direction.
    baker.bakeVisibility(volume, [&](const Matrix&, const Matrix&) {
        const int level = static_cast<int>(0.25f * 255.0f + 0.5f);
        gd.Clear(Color(level, level, level, 255));
    });

    const LightProbeEXT& probe = volume.getProbe(0, 0, 0);
    EXPECT_TRUE(probe.hasVisibility());
    for (int direction = 0; direction < LightProbeEXT::kVisibilityDirections; ++direction)
        EXPECT_NEAR(probe.getVisibilityMean(direction), 25.0f, 0.5f)
            << "direction " << direction;

    // And it now rejects a point beyond that distance, which is the whole purpose of the pass.
    EXPECT_FLOAT_EQ(probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 10.0f), 1.0f);
    EXPECT_LT(probe.visibilityWeight(Vector3(1.0f, 0.0f, 0.0f), 60.0f), 0.2f);
}

TEST(LightProbeBakerTest, BakingLightKeepsVisibilityAndTheOtherWayRound)
{
    // The two passes are independent and either may be run without the other, so neither may
    // discard what the other left behind.
    GraphicsDevice gd;
    LightProbeBaker baker(gd, 8);
    CNA_SKIP_WITHOUT_CAPTURE(baker);

    LightProbeVolumeEXT volume(BoundingBox(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)),
                               1, 1, 1);
    baker.bakeVisibility(volume, [&](const Matrix&, const Matrix&) {
        gd.Clear(Color(64, 64, 64, 255));
    });
    const float recorded = volume.getProbe(0, 0, 0).getVisibilityMean(0);
    ASSERT_GT(recorded, 0.0f);

    baker.bakeLight(volume, [&](const Matrix&, const Matrix&) { gd.Clear(Color::White); });
    EXPECT_NEAR(volume.getProbe(0, 0, 0).getVisibilityMean(0), recorded, 1e-3f)
        << "baking the light threw away the visibility";
    EXPECT_GT(volume.getProbe(0, 0, 0).irradiance(Vector3(0.0f, 1.0f, 0.0f)).X, 1.0f);
}

// ── What a probe grid costs (MOD-2087) ───────────────────────────────────────

TEST(LightProbeBakerTest, TheCostOfAProbeGridIsAStatedNumber)
{
    // A grid is only worth having if its price is known, and both halves of the price are here:
    // the memory a probe occupies, and the time the *layer* spends capturing one. The second is
    // measured with a draw that does nothing, so what it reports is the capture and the projection
    // rather than somebody's scene -- a real bake adds six scene draws per probe on top.
    GraphicsDevice gd;
    LightProbeBaker baker(gd, LightProbeBaker::kDefaultFaceSize);

    const std::size_t probeBytes = sizeof(LightProbeEXT);
    RecordProperty("bytesPerProbe", static_cast<int>(probeBytes));
    std::printf("--- MOD-2087: a probe is %zu bytes; an 8x4x8 grid is %zu probes, %.1f KB ---\n",
                probeBytes, static_cast<std::size_t>(8 * 4 * 8),
                static_cast<double>(probeBytes * 8 * 4 * 8) / 1024.0);

    // Nine coefficients, six visibility directions with two moments each, and a position: the
    // struct must not have quietly grown past what those account for.
    const std::size_t accounted = sizeof(float) * (9 * 3 + 6 * 2 + 3);
    EXPECT_GE(probeBytes, accounted);
    EXPECT_LE(probeBytes, accounted + 16)
        << "a probe carries more than its coefficients, visibility and position";

    CNA_SKIP_WITHOUT_CAPTURE(baker);

    constexpr int kRepeats = 4;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kRepeats; ++i)
        (void)baker.bakeProbe(Vector3::Zero, [&](const Matrix&, const Matrix&) {});
    const auto end = std::chrono::steady_clock::now();
    const double perProbe =
        std::chrono::duration<double, std::milli>(end - start).count() / kRepeats;

    RecordProperty("millisecondsPerProbeCapture", static_cast<int>(perProbe * 1000.0));
    std::printf("    capture and projection: %.3f ms per probe at %d x %d per face\n", perProbe,
                baker.getFaceSize(), baker.getFaceSize());
    EXPECT_GT(perProbe, 0.0);
}

} // namespace

#endif // CNA_CNAEXT
