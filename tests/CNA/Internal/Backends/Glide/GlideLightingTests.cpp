#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideLighting.hpp"

using CNA::Internal::Backends::Glide::ApplyGlideBasicEffectFog;
using CNA::Internal::Backends::Glide::ComposeGlideBasicEffectLitColor;
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
