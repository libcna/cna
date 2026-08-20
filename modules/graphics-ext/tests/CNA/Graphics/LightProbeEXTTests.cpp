// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2080: what the light looks like arriving at one point, as nine coefficients.
//
// A spherical-harmonic probe is easy to get plausibly wrong: a sign flip on one basis function, a
// missing solid-angle weight, or the wrong normalisation all produce a probe that lights a scene
// smoothly and incorrectly. So the tests compare against numbers that can be written down --
// a uniform environment delivers pi times its radiance to every normal, whatever the direction --
// and against the environment itself, by asking whether the brightest normal is the one pointing
// at the brightest face.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/LightProbeEXT.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::LightProbeEXT;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::TextureCube;

constexpr float kPi = 3.14159265359f;
constexpr int kCubeSize = 16;

/// A cube whose six faces hold the six given colours.
std::unique_ptr<TextureCube> MakeCube(GraphicsDevice& gd, const std::array<Color, 6>& faces)
{
    auto cube = std::make_unique<TextureCube>(gd, kCubeSize, false,
                                              Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
    for (int face = 0; face < 6; ++face)
    {
        const std::vector<Color> texels(static_cast<std::size_t>(kCubeSize) * kCubeSize,
                                        faces[static_cast<std::size_t>(face)]);
        cube->SetData(static_cast<CubeMapFace>(face), texels.data(),
                      static_cast<int>(texels.size()));
    }
    return cube;
}

// ── The evaluation, against numbers written down beforehand ──────────────────

TEST(LightProbeEXTTest, AProbeWithNoLightInItDeliversNone)
{
    const LightProbeEXT probe;
    EXPECT_TRUE(probe.isZero());
    for (const Vector3& normal : {Vector3(0.0f, 1.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
                                  Vector3(0.0f, 0.0f, -1.0f)})
    {
        const Vector3 irradiance = probe.irradiance(normal);
        EXPECT_FLOAT_EQ(irradiance.X, 0.0f);
        EXPECT_FLOAT_EQ(irradiance.Y, 0.0f);
        EXPECT_FLOAT_EQ(irradiance.Z, 0.0f);
    }
}

TEST(LightProbeEXTTest, AConstantTermAloneDeliversTheSameIrradianceEverywhere)
{
    // The constant coefficient is the only one whose effect can be written down exactly. A uniform
    // environment of radiance L projects onto it as L * Y00 * 4*pi -- the basis value times the
    // whole sphere's solid angle, which is L * sqrt(4*pi) -- and the irradiance that comes back has
    // to be pi * L in every direction. Every scale factor in the evaluation is pinned by that one
    // number. (The first version of this line multiplied by Y00 a second time, and the test caught
    // the test rather than the code: the end-to-end projection below already agreed with pi * L.)
    LightProbeEXT probe;
    const float radiance = 0.5f;
    const float projected = radiance * std::sqrt(4.0f * kPi);
    probe.setCoefficient(0, Vector3(projected, projected, projected));

    for (const Vector3& normal : {Vector3(0.0f, 1.0f, 0.0f), Vector3(-1.0f, 0.0f, 0.0f),
                                  Vector3(0.577f, 0.577f, 0.577f)})
    {
        const Vector3 irradiance = probe.irradiance(normal);
        EXPECT_NEAR(irradiance.X, kPi * radiance, 1e-3f);
        EXPECT_NEAR(irradiance.Y, kPi * radiance, 1e-3f);
        EXPECT_NEAR(irradiance.Z, kPi * radiance, 1e-3f);
    }
}

TEST(LightProbeEXTTest, IrradianceIsNeverNegative)
{
    // A least-squares fit to a dark environment overshoots below zero, and negative irradiance is
    // light being *removed* from a surface -- which nothing downstream is prepared for.
    LightProbeEXT probe;
    probe.setCoefficient(0, Vector3(0.05f, 0.05f, 0.05f));
    probe.setCoefficient(2, Vector3(-1.0f, -1.0f, -1.0f));   // a strong downward gradient

    const Vector3 below = probe.irradiance(Vector3(0.0f, 0.0f, 1.0f));
    EXPECT_GE(below.X, 0.0f);
    EXPECT_GE(below.Y, 0.0f);
    EXPECT_GE(below.Z, 0.0f);
}

TEST(LightProbeEXTTest, ScalingAndComparisonBehave)
{
    LightProbeEXT probe(Vector3(1.0f, 2.0f, 3.0f));
    probe.setCoefficient(0, Vector3(1.0f, 2.0f, 4.0f));

    LightProbeEXT copy = probe;
    EXPECT_EQ(copy, probe);

    copy.scale(2.0f);
    EXPECT_NE(copy, probe);
    EXPECT_FLOAT_EQ(copy.getCoefficient(0).Z, 8.0f);
    EXPECT_FLOAT_EQ(copy.getPosition().X, 1.0f) << "scaling the light must not move the probe";

    copy.scale(-1.0f);
    EXPECT_FLOAT_EQ(copy.getCoefficient(0).Z, 8.0f)
        << "a negative scale would light surfaces from the inside and must be ignored";

    LightProbeEXT elsewhere = probe;
    elsewhere.setPosition(Vector3(9.0f, 9.0f, 9.0f));
    EXPECT_NE(elsewhere, probe) << "two probes at different places are not the same probe";

    EXPECT_THROW((void)probe.getCoefficient(9), std::out_of_range);
    EXPECT_THROW((void)probe.getCoefficient(-1), std::out_of_range);
    EXPECT_THROW(probe.setCoefficient(9, Vector3()), std::out_of_range);
}

// ── The projection, against the environment it came from ─────────────────────

TEST(LightProbeEXTTest, AUniformEnvironmentProjectsToAUniformProbe)
{
    // The end-to-end version of the constant-term test: a cube that is grey everywhere has to give
    // pi times that grey to every normal. This is where the solid-angle weighting is pinned -- with
    // equal weights the total comes out wrong, and with the wrong basis normalisation it comes out
    // right in one direction and wrong in the others.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

    const Color grey(128, 128, 128, 255);
    const auto cube = MakeCube(gd, {grey, grey, grey, grey, grey, grey});

    EnvironmentProcessor processor(gd);
    const LightProbeEXT probe = processor.generateProbe(cube.get(), Vector3(1.0f, 2.0f, 3.0f));

    EXPECT_FLOAT_EQ(probe.getPosition().Y, 2.0f);
    const float radiance = 128.0f / 255.0f;
    for (const Vector3& normal : {Vector3(0.0f, 1.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
                                  Vector3(0.0f, 0.0f, -1.0f), Vector3(-0.577f, 0.577f, 0.577f)})
    {
        const Vector3 irradiance = probe.irradiance(normal);
        EXPECT_NEAR(irradiance.X, kPi * radiance, 0.02f);
        EXPECT_NEAR(irradiance.Y, kPi * radiance, 0.02f);
    }
}

TEST(LightProbeEXTTest, TheBrightestNormalPointsAtTheBrightestFace)
{
    // The directional half. Five faces black and one white: the normal facing the lit face has to
    // receive the most, and the one facing away the least. A sign error on a linear basis function
    // swaps exactly those two and changes nothing else.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

    const Color black(0, 0, 0, 255);
    const Color white(255, 255, 255, 255);
    EnvironmentProcessor processor(gd);

    // CubeMapFace order is +X, -X, +Y, -Y, +Z, -Z, and the axis each one faces is the thing being
    // checked, so all six are exercised rather than one.
    const Vector3 axes[6] = {Vector3(1.0f, 0.0f, 0.0f),  Vector3(-1.0f, 0.0f, 0.0f),
                             Vector3(0.0f, 1.0f, 0.0f),  Vector3(0.0f, -1.0f, 0.0f),
                             Vector3(0.0f, 0.0f, 1.0f),  Vector3(0.0f, 0.0f, -1.0f)};

    for (int lit = 0; lit < 6; ++lit)
    {
        std::array<Color, 6> faces{black, black, black, black, black, black};
        faces[static_cast<std::size_t>(lit)] = white;
        const auto cube = MakeCube(gd, faces);
        const LightProbeEXT probe = processor.generateProbe(cube.get());

        const Vector3 towards = probe.irradiance(axes[lit]);
        const Vector3 away = probe.irradiance(Vector3(-axes[lit].X, -axes[lit].Y, -axes[lit].Z));
        EXPECT_GT(towards.X, away.X + 0.05f)
            << "face " << lit << ": the normal facing the lit face did not receive the most";

        // And the two normals perpendicular to it land between the two.
        const Vector3 sideways = probe.irradiance(axes[(lit + 2) % 6]);
        EXPECT_LT(sideways.X, towards.X + 1e-4f) << "face " << lit;
        EXPECT_GT(sideways.X, away.X - 1e-4f) << "face " << lit;
    }
}

TEST(LightProbeEXTTest, ANullEnvironmentIsRefused)
{
    GraphicsDevice gd;
    EnvironmentProcessor processor(gd);
    EXPECT_THROW((void)processor.generateProbe(nullptr), std::invalid_argument);
}

TEST(LightProbeEXTTest, ProbesFromDifferentEnvironmentsAddLinearly)
{
    // The property a probe *volume* depends on and nothing else here would catch: the projection is
    // linear, so the average of two probes' coefficients is the projection of the average of their
    // light. Without that, interpolating between neighbours would be meaningless.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(gd);

    const Color black(0, 0, 0, 255);
    const Color grey(120, 120, 120, 255);
    EnvironmentProcessor processor(gd);

    const auto first = MakeCube(gd, {grey, black, black, black, black, black});
    const auto second = MakeCube(gd, {black, grey, black, black, black, black});
    const auto both = MakeCube(gd, {grey, grey, black, black, black, black});

    const LightProbeEXT a = processor.generateProbe(first.get());
    const LightProbeEXT b = processor.generateProbe(second.get());
    const LightProbeEXT sum = processor.generateProbe(both.get());

    for (int index = 0; index < LightProbeEXT::kCoefficientCount; ++index)
        EXPECT_NEAR(a.getCoefficient(index).X + b.getCoefficient(index).X,
                    sum.getCoefficient(index).X, 1e-4f)
            << "coefficient " << index << " is not linear in the environment";
}

} // namespace

#endif // CNA_CNAEXT
