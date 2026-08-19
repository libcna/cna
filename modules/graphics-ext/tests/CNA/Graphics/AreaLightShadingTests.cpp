// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2062: shading with a light that has area.
//
// The diffuse half is exact -- a Lambertian surface reflects a clamped cosine, and the irradiance a
// polygon delivers to one is a closed-form sum over its edges. Exactness is a claim that can be
// checked against numbers written down beforehand, so that is what most of this file does: an
// infinite plane of light delivers all of it, a light seen edge-on delivers none, and a small
// distant light delivers what the inverse square says it should.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/AreaLightShading.hpp"
#include "Microsoft/Xna/Framework/Graphics/AreaLightEXT.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cmath>

namespace {

using CNA::Graphics::AreaLightShading;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AreaLightEXT;
using Microsoft::Xna::Framework::Graphics::AreaLightShapeEXT;

constexpr float kPi = 3.14159265359f;

/// A rectangle of the given half-extents, facing down at the origin from height `height`.
AreaLightEXT Overhead(const float halfWidth, const float height)
{
    AreaLightEXT light;
    light.Shape = AreaLightShapeEXT::Rectangle;
    light.Position = Vector3(0.0f, height, 0.0f);
    light.RightAxis = Vector3(halfWidth, 0.0f, 0.0f);
    light.UpAxis = Vector3(0.0f, 0.0f, halfWidth);
    light.Range = 1000.0f;
    light.TwoSided = true;
    return light;
}

float DiffuseCoverage(const AreaLightEXT& light, const Vector3& surface, const Vector3& normal)
{
    return AreaLightShading::coverage(AreaLightShading::quadOf(light, surface), surface, normal,
                                      1.0f, light.TwoSided);
}

// ── The diffuse form factor, against numbers written down beforehand ──────────

TEST(AreaLightShadingTest, AnEnormousLightOverheadDeliversTheWholeHemisphere)
{
    // A plane of light filling the sky delivers a form factor of 1: everything the clamped cosine
    // could receive. Anything less than that is energy the integrator lost.
    const float coverage = DiffuseCoverage(Overhead(10000.0f, 1.0f), Vector3(0.0f, 0.0f, 0.0f),
                                           Vector3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(coverage, 1.0f, 1e-3f);
}

TEST(AreaLightShadingTest, ALightSeenEdgeOnDeliversNothing)
{
    // The surface normal lies in the light's own plane, so every direction to the light is exactly
    // on the horizon. This is the case the horizon clipping exists for, and getting it wrong gives
    // a negative form factor rather than zero.
    const float coverage = DiffuseCoverage(Overhead(4.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
                                           Vector3(0.0f, 1.0f, 0.0f));
    EXPECT_NEAR(coverage, 0.0f, 1e-3f);
}

TEST(AreaLightShadingTest, ASurfaceFacingAwayReceivesNothing)
{
    const float coverage = DiffuseCoverage(Overhead(4.0f, 3.0f), Vector3(0.0f, 0.0f, 0.0f),
                                           Vector3(0.0f, -1.0f, 0.0f));
    EXPECT_FLOAT_EQ(coverage, 0.0f);
}

TEST(AreaLightShadingTest, ASmallDistantLightMatchesTheInverseSquare)
{
    // Far enough away, a rectangle is a point: its form factor is its area over pi times the
    // distance squared. That is the classical answer, and it is what an integrator that lost a
    // factor somewhere would miss.
    const float halfWidth = 0.05f;
    for (const float height : {4.0f, 8.0f, 16.0f})
    {
        const float coverage = DiffuseCoverage(Overhead(halfWidth, height),
                                               Vector3(0.0f, 0.0f, 0.0f),
                                               Vector3(0.0f, 1.0f, 0.0f));
        const float area = 4.0f * halfWidth * halfWidth;
        const float expected = area / (kPi * height * height);
        EXPECT_NEAR(coverage, expected, expected * 0.02f) << "at height " << height;
    }
}

TEST(AreaLightShadingTest, TheFormFactorFallsAsTheLightTiltsAway)
{
    const AreaLightEXT light = Overhead(2.0f, 4.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);

    float previous = 2.0f;
    for (const float tilt : {0.0f, 0.4f, 0.8f, 1.2f})
    {
        const Vector3 normal(std::sin(tilt), std::cos(tilt), 0.0f);
        const float coverage = DiffuseCoverage(light, surface, normal);
        EXPECT_LT(coverage, previous) << "at tilt " << tilt;
        previous = coverage;
    }
}

TEST(AreaLightShadingTest, AOneSidedLightEmitsFromOneFaceOnly)
{
    AreaLightEXT light = Overhead(4.0f, 3.0f);
    light.TwoSided = false;

    const Vector3 below(0.0f, 0.0f, 0.0f);
    const Vector3 above(0.0f, 6.0f, 0.0f);
    const float lit = AreaLightShading::coverage(AreaLightShading::quadOf(light, below), below,
                                                 Vector3(0.0f, 1.0f, 0.0f), 1.0f, false);
    const float dark = AreaLightShading::coverage(AreaLightShading::quadOf(light, above), above,
                                                  Vector3(0.0f, -1.0f, 0.0f), 1.0f, false);
    EXPECT_GT(lit, 0.1f);
    EXPECT_FLOAT_EQ(dark, 0.0f) << "a light with a backing lit the surface behind it";

    // The same light made two-sided lights both.
    light.TwoSided = true;
    EXPECT_GT(AreaLightShading::coverage(AreaLightShading::quadOf(light, above), above,
                                         Vector3(0.0f, -1.0f, 0.0f), 1.0f, true),
              0.1f);
}

// ── The shapes ───────────────────────────────────────────────────────────────

TEST(AreaLightShadingTest, ADiscDeliversLessThanTheRectangleItFitsIn)
{
    // pi*a*b against 4*a*b: a disc encloses about 79% of the rectangle around it, and a disc that
    // delivered the same irradiance would be a rectangle wearing the wrong name.
    AreaLightEXT rectangle = Overhead(0.05f, 6.0f);
    AreaLightEXT disc = rectangle;
    disc.Shape = AreaLightShapeEXT::Disc;

    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const float rectangular = DiffuseCoverage(rectangle, surface, normal);
    const float circular = DiffuseCoverage(disc, surface, normal);
    EXPECT_NEAR(circular / rectangular, kPi / 4.0f, 0.02f);
}

TEST(AreaLightShadingTest, ATubeTurnsToFaceWhateverItLights)
{
    // A cylinder looks the same from every direction around its axis, so two surfaces at the same
    // distance on opposite sides of it must receive the same amount.
    AreaLightEXT tube;
    tube.Shape = AreaLightShapeEXT::Tube;
    tube.Position = Vector3(0.0f, 4.0f, 0.0f);
    tube.RightAxis = Vector3(3.0f, 0.0f, 0.0f);     // the axis, half-length 3
    tube.UpAxis = Vector3(0.0f, 0.2f, 0.0f);        // the radius
    tube.Range = 1000.0f;
    tube.TwoSided = true;

    const float below = DiffuseCoverage(tube, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    const float beside = DiffuseCoverage(tube, Vector3(0.0f, 4.0f, -4.0f),
                                         Vector3(0.0f, 0.0f, 1.0f));
    EXPECT_GT(below, 0.0f);
    EXPECT_NEAR(below, beside, below * 0.05f)
        << "the tube did not present the same face to both surfaces";
}

// ── The specular lobe ────────────────────────────────────────────────────────

TEST(AreaLightShadingTest, ASmoothSurfaceSeesTheLightsShapeAndARoughOneDoesNot)
{
    // This is the property a punctual light cannot produce: on a smooth surface the highlight is
    // the *shape of the light*, so it has an inside where coverage is near 1 and an outside where
    // it is near 0. On a rough surface the same light smears into a gradient with no edge.
    AreaLightEXT light = Overhead(2.0f, 3.0f);
    light.TwoSided = true;

    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const auto specularAt = [&](const float x, const float roughness) {
        const Vector3 surface(x, 0.0f, 0.0f);
        return AreaLightShading::coverage(AreaLightShading::quadOf(light, surface), surface, normal,
                                          AreaLightShading::lobeScaleFor(roughness), true);
    };

    // Smooth: inside the light's footprint it is nearly fully covered, just outside nearly not.
    EXPECT_GT(specularAt(0.0f, 0.05f), 0.9f) << "the mirror did not see the light it is under";
    EXPECT_LT(specularAt(6.0f, 0.05f), 0.1f) << "the highlight has no edge";

    // Rough: the same two points are far closer together, which is what "no edge" means.
    const float roughInside = specularAt(0.0f, 0.9f);
    const float roughOutside = specularAt(6.0f, 0.9f);
    EXPECT_LT(roughInside, 0.9f) << "a rough lobe should not be saturated by one small light";
    EXPECT_GT(roughOutside, specularAt(6.0f, 0.05f))
        << "a rough surface must spread the light further than a smooth one";
}

// ── The whole contribution ───────────────────────────────────────────────────

TEST(AreaLightShadingTest, AnInvalidOrDistantLightContributesNothing)
{
    AreaLightEXT degenerate = Overhead(2.0f, 3.0f);
    degenerate.UpAxis = degenerate.RightAxis;      // parallel axes, no area

    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 eye(0.0f, 0.0f, 5.0f);
    const Vector3 base(0.8f, 0.8f, 0.8f);

    EXPECT_FLOAT_EQ(
        AreaLightShading::contribution(degenerate, surface, normal, eye, base, 0.0f, 0.5f).X, 0.0f);

    AreaLightEXT distant = Overhead(2.0f, 300.0f);
    distant.Range = 10.0f;
    EXPECT_FLOAT_EQ(
        AreaLightShading::contribution(distant, surface, normal, eye, base, 0.0f, 0.5f).X, 0.0f);
}

TEST(AreaLightShadingTest, TheContributionIsTheFormFactorTimesTheAlbedoForAMatteSurface)
{
    // With no specular to speak of, the answer is one multiplication and can be written down.
    AreaLightEXT light = Overhead(3.0f, 4.0f);
    light.Color = Vector3(1.0f, 0.5f, 0.25f);
    light.Intensity = 2.0f;

    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 eye(0.0f, 0.0f, 6.0f);
    const Vector3 base(0.6f, 0.6f, 0.6f);

    const float coverage = DiffuseCoverage(light, surface, normal);
    const Vector3 result = AreaLightShading::contribution(light, surface, normal, eye, base, 0.0f,
                                                          1.0f);

    // Roughness 1 leaves a specular term, so the diffuse part is a lower bound rather than the
    // whole -- but it must be present in full, and the channels must keep the light's colour ratio.
    EXPECT_GT(result.X, coverage * base.X * light.Color.X * light.Intensity * 0.99f);
    EXPECT_NEAR(result.Y / result.X, 0.5f, 0.15f) << "the light's colour did not survive";
}

TEST(AreaLightShadingTest, AMetallicSurfaceLosesItsDiffuseAndKeepsItsSpecular)
{
    AreaLightEXT light = Overhead(2.0f, 3.0f);
    const Vector3 surface(0.0f, 0.0f, 0.0f);
    const Vector3 normal(0.0f, 1.0f, 0.0f);
    const Vector3 eye(0.0f, 0.0f, 4.0f);
    const Vector3 base(0.9f, 0.9f, 0.9f);

    const Vector3 matte = AreaLightShading::contribution(light, surface, normal, eye, base, 0.0f,
                                                         0.3f);
    const Vector3 metal = AreaLightShading::contribution(light, surface, normal, eye, base, 1.0f,
                                                         0.3f);
    EXPECT_GT(matte.X, metal.X) << "a metal reflected more than a matte surface of the same colour";
    EXPECT_GT(metal.X, 0.0f) << "a metal reflected nothing at all";
}

TEST(AreaLightShadingTest, TheLobeScaleIsMonotoneAndNeverZero)
{
    EXPECT_GT(AreaLightShading::lobeScaleFor(0.0f), 0.0f)
        << "a mirror still needs a lobe with a width, or the integral has no polygon";
    float previous = 0.0f;
    for (const float roughness : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        const float scale = AreaLightShading::lobeScaleFor(roughness);
        EXPECT_GE(scale, previous);
        previous = scale;
    }
    EXPECT_FLOAT_EQ(AreaLightShading::lobeScaleFor(1.0f), 1.0f);
}

} // namespace

#endif // CNA_CNAEXT
