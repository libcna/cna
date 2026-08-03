#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideLighting.hpp"

using CNA::Internal::Backends::Glide::ApplyGlideBasicEffectFog;
using CNA::Internal::Backends::Glide::ComposeGlideBasicEffectLitColor;
using CNA::Internal::Backends::Glide::DotGlideLightingVectors;
using CNA::Internal::Backends::Glide::EvaluateGlideBasicEffectLighting;
using CNA::Internal::Backends::Glide::GlideBasicEffectLightingState;
using CNA::Internal::Backends::Glide::GlideLightingVector;
using CNA::Internal::Backends::Glide::InvertGlideLightingWorld3x3;
using CNA::Internal::Backends::Glide::TransformGlideLightingNormal;

TEST(GlideLightingTest, MatchesFnaDirectionalBlinnPhongTerms)
{
    GlideBasicEffectLightingState state;
    state.ambient = {0.1f, 0.2f, 0.3f};
    state.eyePosition = {0.0f, 0.0f, 5.0f};
    state.materialSpecular = {0.8f, 0.5f, 0.25f};
    state.specularPower = 16.0f;
    state.lights[0] = {{0.0f, 0.0f, -1.0f}, {0.5f, 0.25f, 0.75f}, {0.25f, 0.5f, 1.0f}};

    const auto result = EvaluateGlideBasicEffectLighting(state, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});

    EXPECT_NEAR(result.diffuse.x, 0.6f, 0.00001f);
    EXPECT_NEAR(result.diffuse.y, 0.45f, 0.00001f);
    EXPECT_NEAR(result.diffuse.z, 1.05f, 0.00001f);
    EXPECT_NEAR(result.specular.x, 0.2f, 0.00001f);
    EXPECT_NEAR(result.specular.y, 0.25f, 0.00001f);
    EXPECT_NEAR(result.specular.z, 0.25f, 0.00001f);
}

TEST(GlideLightingTest, BackFacingLightCannotProduceSpecular)
{
    GlideBasicEffectLightingState state;
    state.eyePosition = {0.0f, 0.0f, 1.0f};
    state.materialSpecular = {1.0f, 1.0f, 1.0f};
    state.specularPower = 1.0f;
    state.lights[0] = {{0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};

    const auto result = EvaluateGlideBasicEffectLighting(state, {}, {0.0f, 0.0f, 1.0f});

    EXPECT_FLOAT_EQ(result.diffuse.x, 0.0f);
    EXPECT_FLOAT_EQ(result.diffuse.y, 0.0f);
    EXPECT_FLOAT_EQ(result.diffuse.z, 0.0f);
    EXPECT_FLOAT_EQ(result.specular.x, 0.0f);
    EXPECT_FLOAT_EQ(result.specular.y, 0.0f);
    EXPECT_FLOAT_EQ(result.specular.z, 0.0f);
}

TEST(GlideLightingTest, SumsAllThreeLightContributions)
{
    GlideBasicEffectLightingState state;
    state.eyePosition = {0.0f, 0.0f, 1.0f};
    state.materialSpecular = {1.0f, 1.0f, 1.0f};
    state.specularPower = 1.0f;
    state.lights[0] = {{0.0f, 0.0f, -1.0f}, {0.1f, 0.0f, 0.0f}, {0.2f, 0.0f, 0.0f}};
    state.lights[1] = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.3f, 0.0f}, {0.0f, 0.4f, 0.0f}};
    state.lights[2] = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.5f}, {0.0f, 0.0f, 0.6f}};

    const auto result = EvaluateGlideBasicEffectLighting(state, {}, {0.0f, 0.0f, 1.0f});

    EXPECT_NEAR(result.diffuse.x, 0.1f, 0.00001f);
    EXPECT_NEAR(result.diffuse.y, 0.3f, 0.00001f);
    EXPECT_NEAR(result.diffuse.z, 0.5f, 0.00001f);
    EXPECT_NEAR(result.specular.x, 0.2f, 0.00001f);
    EXPECT_NEAR(result.specular.y, 0.4f, 0.00001f);
    EXPECT_NEAR(result.specular.z, 0.6f, 0.00001f);
}

TEST(GlideLightingTest, UsesInverseTransposeForNonUniformWorldScale)
{
    const auto inverse = InvertGlideLightingWorld3x3({
        2.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f,
        0.0f, 0.0f, 4.0f});
    const GlideLightingVector normal = TransformGlideLightingNormal({1.0f, 1.0f, 1.0f}, inverse);
    const float expectedLength = std::sqrt(0.25f + (1.0f / 9.0f) + 0.0625f);

    EXPECT_NEAR(normal.x, 0.5f / expectedLength, 0.00001f);
    EXPECT_NEAR(normal.y, (1.0f / 3.0f) / expectedLength, 0.00001f);
    EXPECT_NEAR(normal.z, 0.25f / expectedLength, 0.00001f);
}

TEST(GlideLightingTest, UsesInverseTransposeForRotation)
{
    // A pure rotation is orthogonal (World^-1 == World^T), so its correct inverse-transpose
    // normal transform is identical to the ordinary row-vector point transform n * World. Any
    // implementation that instead applies the plain inverse (n * World^-1 == n * World^T for a
    // non-symmetric rotation) produces a different, independently-computable wrong answer here,
    // which this test distinguishes without reusing the production inversion arithmetic.
    const std::array<float, 9> rotationAboutZ90 = {
        0.0f, 1.0f, 0.0f,
        -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f};
    const auto inverse = InvertGlideLightingWorld3x3(rotationAboutZ90);

    const GlideLightingVector normal = TransformGlideLightingNormal({1.0f, 0.0f, 0.0f}, inverse);

    // Expected value computed independently by directly applying the rotation matrix itself
    // (row 0, since normal = (1,0,0) selects it), not by inverting or transposing anything.
    EXPECT_NEAR(normal.x, 0.0f, 0.00001f);
    EXPECT_NEAR(normal.y, 1.0f, 0.00001f);
    EXPECT_NEAR(normal.z, 0.0f, 0.00001f);
}

TEST(GlideLightingTest, UsesInverseTransposeForNonSymmetricShear)
{
    // A unit lower-triangular shear World = [[1,0,0],[a,1,0],[b,c,1]] is deliberately
    // non-symmetric (World != World^T) and its inverse can be derived independently by forward
    // substitution rather than the production adjugate/determinant formula:
    //   World * (World^-1) = I  =>  World^-1 = [[1,0,0],[-a,1,0],[a*c-b,-c,1]].
    constexpr float a = 2.0f;
    constexpr float b = 3.0f;
    constexpr float c = 5.0f;
    const std::array<float, 9> shear = {
        1.0f, 0.0f, 0.0f,
        a, 1.0f, 0.0f,
        b, c, 1.0f};
    const auto inverse = InvertGlideLightingWorld3x3(shear);

    const GlideLightingVector normal = TransformGlideLightingNormal({1.0f, 0.0f, 0.0f}, inverse);

    // Pre-normalization the inverse-transpose product is (1, -a, a*c - b) = (1, -2, 7); expected
    // values below are that vector normalized, computed independently of TransformGlideLightingNormal.
    const float expectedLength = std::sqrt(1.0f * 1.0f + 2.0f * 2.0f + 7.0f * 7.0f);
    EXPECT_NEAR(normal.x, 1.0f / expectedLength, 0.00001f);
    EXPECT_NEAR(normal.y, -2.0f / expectedLength, 0.00001f);
    EXPECT_NEAR(normal.z, 7.0f / expectedLength, 0.00001f);
}

TEST(GlideLightingTest, TransformedNormalStaysPerpendicularToShearedTangents)
{
    // Independent geometric invariant: for ANY invertible World, if n is perpendicular to
    // tangents t1/t2 before the transform, the inverse-transpose-transformed normal must remain
    // perpendicular to the ordinary point-transformed tangents (t * World). This holds regardless
    // of hand-computed coefficients, so it catches the same row/column transpose bug from a
    // different angle than the exact-value tests above.
    const std::array<float, 9> shear = {
        1.0f, 0.4f, -0.3f,
        0.0f, 1.0f, 0.6f,
        0.2f, -0.5f, 1.0f};
    const GlideLightingVector t1{1.0f, 0.0f, 0.0f};
    const GlideLightingVector t2{0.0f, 1.0f, 0.0f};
    const GlideLightingVector n{0.0f, 0.0f, 1.0f}; // perpendicular to t1 and t2 before transform

    const auto inverse = InvertGlideLightingWorld3x3(shear);
    const GlideLightingVector transformedNormal = TransformGlideLightingNormal(n, inverse);

    const auto transformPoint = [&shear](const GlideLightingVector& v) {
        return GlideLightingVector{
            v.x * shear[0] + v.y * shear[3] + v.z * shear[6],
            v.x * shear[1] + v.y * shear[4] + v.z * shear[7],
            v.x * shear[2] + v.y * shear[5] + v.z * shear[8]};
    };
    const GlideLightingVector transformedT1 = transformPoint(t1);
    const GlideLightingVector transformedT2 = transformPoint(t2);

    EXPECT_NEAR(DotGlideLightingVectors(transformedNormal, transformedT1), 0.0f, 0.0001f);
    EXPECT_NEAR(DotGlideLightingVectors(transformedNormal, transformedT2), 0.0f, 0.0001f);
}

TEST(GlideLightingTest, VertexColorModulatesEmissiveButNotSpecular)
{
    GlideBasicEffectLightingState state;
    state.eyePosition = {0.0f, 0.0f, 1.0f};
    state.materialSpecular = {1.0f, 1.0f, 1.0f};
    state.lights[0] = {{0.0f, 0.0f, -1.0f}, {}, {0.4f, 0.4f, 0.4f}};
    const auto lighting = EvaluateGlideBasicEffectLighting(state, {}, {0.0f, 0.0f, 1.0f});

    const GlideLightingVector color = ComposeGlideBasicEffectLitColor(
        {}, lighting, {0.1f * 0.25f, 0.2f * 0.5f, 0.3f * 0.75f}, 0.5f);

    EXPECT_NEAR(color.x, 0.225f, 0.00001f);
    EXPECT_NEAR(color.y, 0.3f, 0.00001f);
    EXPECT_NEAR(color.z, 0.425f, 0.00001f);
}

TEST(GlideLightingTest, AppliesSpecularBeforeAlphaAwareFog)
{
    GlideBasicEffectLightingState state;
    state.eyePosition = {0.0f, 0.0f, 1.0f};
    state.materialSpecular = {1.0f, 1.0f, 1.0f};
    state.lights[0] = {{0.0f, 0.0f, -1.0f}, {}, {0.4f, 0.2f, 0.0f}};
    const auto lighting = EvaluateGlideBasicEffectLighting(state, {}, {0.0f, 0.0f, 1.0f});

    const GlideLightingVector lit = ComposeGlideBasicEffectLitColor(
        {0.5f, 0.5f, 0.5f}, lighting, {0.1f, 0.0f, 0.0f}, 0.5f);
    const GlideLightingVector fogged = ApplyGlideBasicEffectFog(lit, 0.5f, {0.0f, 1.0f, 0.0f}, 0.25f);

    EXPECT_NEAR(fogged.x, 0.225f, 0.00001f);
    EXPECT_NEAR(fogged.y, 0.2f, 0.00001f);
    EXPECT_NEAR(fogged.z, 0.0f, 0.00001f);
}
