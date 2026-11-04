// SPDX-License-Identifier: MS-PL
//
// TQ-041: a lit stock effect's three DirectionalLights write straight through to the effect's
// DirLight* parameters. XNA's CacheEffectParameters constructs each light over the effect's own
// parameters (BasicEffect IL_0108-01d8, SkinnedEffect IL_0125-01e9, EnvironmentMapEffect
// IL_0135-01d7, which passes null for the specular parameter it does not have); the DirectionalLight
// setters write those parameters at once (set_Direction IL_0000-000f, set_DiffuseColor/
// set_SpecularColor IL_0000-0017 while enabled, set_Enabled IL_0010-0076), and OnApply never touches
// them. Every embedded parameter starts at zero.

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

static EffectParameter& RequireLightParameter(Effect& effect, const char* name)
{
    auto* parameter = effect.getParametersProperty()[name];
    if (parameter == nullptr)
        throw std::runtime_error(std::string("missing stock parameter ") + name);
    return *parameter;
}

namespace
{
    template <typename TEffect> struct LightTraits;
    template <> struct LightTraits<BasicEffect> { static constexpr bool kSpecular = true; };
    template <> struct LightTraits<SkinnedEffect> { static constexpr bool kSpecular = true; };
    template <> struct LightTraits<EnvironmentMapEffect> { static constexpr bool kSpecular = false; };

    // The stock clone constructors are protected; a subclass reaches them as XNA's subclasses do.
    template <typename TEffect>
    class Clonable final : public TEffect
    {
    public:
        explicit Clonable(GraphicsDevice& device) : TEffect(device) {}
        explicit Clonable(const TEffect& source) : TEffect(source) {}
    };

    template <typename TEffect>
    class StockEffectLightParameterTest : public ::testing::Test
    {
    protected:
        static Vector3 Value(Effect& effect, const std::string& name)
        {
            EffectParameter* parameter = effect.getParametersProperty()[name];
            EXPECT_NE(parameter, nullptr) << name;
            return parameter == nullptr ? Vector3(-1.0f, -1.0f, -1.0f) : parameter->GetValueVector3();
        }

        static void ExpectSpecular(Effect& effect, int light, const Vector3& expected)
        {
            const std::string name = "DirLight" + std::to_string(light) + "SpecularColor";
            if (LightTraits<TEffect>::kSpecular)
                EXPECT_EQ(Value(effect, name), expected) << name;
            else
                EXPECT_EQ(effect.getParametersProperty()[name], nullptr) << name;
        }

        static void ExpectNear(const Vector3& actual, const Vector3& expected)
        {
            EXPECT_FLOAT_EQ(actual.X, expected.X);
            EXPECT_FLOAT_EQ(actual.Y, expected.Y);
            EXPECT_FLOAT_EQ(actual.Z, expected.Z);
        }

        GraphicsDevice device;
    };

    using LitStockEffects = ::testing::Types<BasicEffect, SkinnedEffect, EnvironmentMapEffect>;
}

TYPED_TEST_SUITE(StockEffectLightParameterTest, LitStockEffects);

// Construction alone (no Apply): each light constructor writes Direction = Down (DirectionalLight
// .ctor IL_0054-005a); the effect constructor then enables light 0 (BasicEffect .ctor IL_0072-0079,
// SkinnedEffect .ctor IL_0079-0080, EnvironmentMapEffect .ctor IL_0072-0079), which writes its
// cached One diffuse and Zero specular. Lights 1 and 2 stay disabled, so their colour parameters keep the zero they start at.
TYPED_TEST(StockEffectLightParameterTest, ConstructionPublishesTheDefaultLightParameters)
{
    TypeParam effect(this->device);
    for (const char* direction : {"DirLight0Direction", "DirLight1Direction", "DirLight2Direction"})
        EXPECT_EQ(this->Value(effect, direction), Vector3::Down) << direction;
    EXPECT_EQ(this->Value(effect, "DirLight0DiffuseColor"), Vector3::One);
    EXPECT_EQ(this->Value(effect, "DirLight1DiffuseColor"), Vector3::Zero);
    EXPECT_EQ(this->Value(effect, "DirLight2DiffuseColor"), Vector3::Zero);
    for (int light = 0; light < 3; ++light)
        this->ExpectSpecular(effect, light, Vector3::Zero);
}

TYPED_TEST(StockEffectLightParameterTest, LightSettersWriteTheirParametersImmediately)
{
    TypeParam effect(this->device);
    IEffectLights& lights = effect;
    DirectionalLight& light1 = lights.getDirectionalLight1Property();

    const Vector3 direction(0.0f, 0.0f, -1.0f);
    const Vector3 diffuse(0.5f, 0.25f, 0.125f);
    const Vector3 specular(0.75f, 0.5f, 0.25f);
    light1.setDirectionProperty(direction);
    light1.setDiffuseColorProperty(diffuse);
    light1.setSpecularColorProperty(specular);
    EXPECT_EQ(this->Value(effect, "DirLight1Direction"), direction);
    EXPECT_EQ(this->Value(effect, "DirLight1DiffuseColor"), Vector3::Zero) << "disabled: cached only";
    this->ExpectSpecular(effect, 1, Vector3::Zero);

    light1.setEnabledProperty(true);
    EXPECT_EQ(this->Value(effect, "DirLight1DiffuseColor"), diffuse);
    this->ExpectSpecular(effect, 1, specular);
    EXPECT_EQ(light1.getSpecularColorProperty(), specular);

    lights.getDirectionalLight0Property().setEnabledProperty(false);
    EXPECT_EQ(this->Value(effect, "DirLight0DiffuseColor"), Vector3::Zero);
    EXPECT_EQ(lights.getDirectionalLight0Property().getDiffuseColorProperty(), Vector3::One);
}

// EffectHelpers.EnableDefaultLighting IL_0000-0104 sets direction, diffuse, specular and Enabled on
// each light in turn, so the three-light rig is in the parameters before any Apply. Light 1 has no
// specular term (IL_0089-008f). BasicEffect.EnableDefaultLighting also sets LightingEnabled
// (IL_0000-0002), which the lit-only effects always are.
TYPED_TEST(StockEffectLightParameterTest, EnableDefaultLightingPublishesTheRigWithoutApply)
{
    TypeParam effect(this->device);
    effect.EnableDefaultLighting();

    this->ExpectNear(this->Value(effect, "DirLight0Direction"), Vector3(-0.5265408f, -0.5735765f, -0.6275069f));
    this->ExpectNear(this->Value(effect, "DirLight0DiffuseColor"), Vector3(1.0f, 0.9607844f, 0.8078432f));
    this->ExpectNear(this->Value(effect, "DirLight1Direction"), Vector3(0.7198464f, 0.3420201f, 0.6040227f));
    this->ExpectNear(this->Value(effect, "DirLight1DiffuseColor"), Vector3(0.9647059f, 0.7607844f, 0.4078432f));
    this->ExpectNear(this->Value(effect, "DirLight2Direction"), Vector3(0.4545195f, -0.7660444f, 0.4545195f));
    this->ExpectNear(this->Value(effect, "DirLight2DiffuseColor"), Vector3(0.3231373f, 0.3607844f, 0.3937255f));
    if (LightTraits<TypeParam>::kSpecular)
    {
        this->ExpectNear(this->Value(effect, "DirLight0SpecularColor"), Vector3(1.0f, 0.9607844f, 0.8078432f));
        EXPECT_EQ(this->Value(effect, "DirLight1SpecularColor"), Vector3::Zero);
        this->ExpectNear(this->Value(effect, "DirLight2SpecularColor"), Vector3(0.3231373f, 0.3607844f, 0.3937255f));
    }
    this->ExpectNear(effect.getAmbientLightColorProperty(), Vector3(0.05333332f, 0.09882354f, 0.1819608f));
    EXPECT_TRUE(effect.getLightingEnabledProperty());
}

// OnApply has no DirLight reference (BasicEffect::OnApply IL_0000-0183, SkinnedEffect::OnApply
// IL_0000-016b, EnvironmentMapEffect::OnApply IL_0000-0158): a raw write to a light parameter
// survives any number of Applies, even after an unrelated property change.
TYPED_TEST(StockEffectLightParameterTest, ApplyNeverRewritesTheLightParameters)
{
    TypeParam effect(this->device);
    effect.Apply();
    const Vector3 raw(9.0f, 8.0f, 7.0f);
    RequireLightParameter(effect, "DirLight0Direction").SetValue(raw);
    RequireLightParameter(effect, "DirLight0DiffuseColor").SetValue(raw);

    effect.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
    effect.Apply();
    effect.Apply();

    EXPECT_EQ(this->Value(effect, "DirLight0Direction"), raw);
    EXPECT_EQ(this->Value(effect, "DirLight0DiffuseColor"), raw);
}

// A clone's lights are built over the clone's own (cloned) parameters from the source lights'
// cached fields, writing nothing (CacheEffectParameters(cloneSource) + DirectionalLight::.ctor
// IL_001b-0053). A raw parameter value therefore survives the clone, the light state is the
// source's, and the clone's lights write only the clone's parameters.
TYPED_TEST(StockEffectLightParameterTest, CloneLightsKeepTheirStateAndDriveOnlyTheClone)
{
    TypeParam source(this->device);
    IEffectLights& sourceLights = source;
    const Vector3 diffuse(0.5f, 0.25f, 0.125f);
    sourceLights.getDirectionalLight1Property().setDiffuseColorProperty(diffuse);
    sourceLights.getDirectionalLight1Property().setEnabledProperty(true);
    const Vector3 raw(9.0f, 8.0f, 7.0f);
    RequireLightParameter(source, "DirLight1DiffuseColor").SetValue(raw);

    Clonable<TypeParam> clone(source);
    IEffectLights& cloneLights = clone;
    DirectionalLight& cloneLight1 = cloneLights.getDirectionalLight1Property();
    EXPECT_TRUE(cloneLight1.getEnabledProperty());
    EXPECT_EQ(cloneLight1.getDiffuseColorProperty(), diffuse);
    EXPECT_EQ(this->Value(clone, "DirLight1DiffuseColor"), raw);

    const Vector3 cloneOnly(0.0f, 1.0f, 0.0f);
    cloneLight1.setDiffuseColorProperty(cloneOnly);
    EXPECT_EQ(this->Value(clone, "DirLight1DiffuseColor"), cloneOnly);
    EXPECT_EQ(this->Value(source, "DirLight1DiffuseColor"), raw);
    cloneLight1.setDirectionProperty(Vector3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(this->Value(source, "DirLight1Direction"), Vector3::Down);

    // And the reverse: the source's lights still drive the source only.
    sourceLights.getDirectionalLight1Property().setEnabledProperty(false);
    EXPECT_EQ(this->Value(source, "DirLight1DiffuseColor"), Vector3::Zero);
    EXPECT_EQ(this->Value(clone, "DirLight1DiffuseColor"), cloneOnly);
}

// EnvironmentMapEffect has no DirLight*SpecularColor parameter; its lights still keep a specular
// colour, which no parameter receives.
TEST(EnvironmentMapEffectLightParameterTest, LightsCacheSpecularWithoutAParameter)
{
    GraphicsDevice device;
    EnvironmentMapEffect effect(device);
    DirectionalLight& light0 = effect.getDirectionalLight0Property();
    light0.setSpecularColorProperty(Vector3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(light0.getSpecularColorProperty(), Vector3(0.25f, 0.5f, 0.75f));
    EXPECT_EQ(effect.getParametersProperty()["DirLight0SpecularColor"], nullptr);
    light0.setEnabledProperty(false);
    light0.setEnabledProperty(true);
    EXPECT_EQ(RequireLightParameter(effect, "DirLight0DiffuseColor").GetValueVector3(), Vector3::One);
}
