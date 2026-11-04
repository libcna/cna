// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::EffectParameter;
using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
using Microsoft::Xna::Framework::Graphics::EffectParameterType;

namespace
{
    EffectParameter VectorParameter(const std::string& name)
    {
        return EffectParameter(name, "", 1, 3,
                               EffectParameterClass::Vector,
                               EffectParameterType::Single);
    }
}

TEST(DirectionalLightTests, ConstructorUsesMicrosoftXnaDefaults)
{
    const DirectionalLight light;

    EXPECT_FALSE(light.getEnabledProperty());
    EXPECT_EQ(light.getDirectionProperty(), Vector3::Down);
    EXPECT_EQ(light.getDiffuseColorProperty(), Vector3::One);
    EXPECT_EQ(light.getSpecularColorProperty(), Vector3::Zero);
}

TEST(DirectionalLightTests, PropertiesRoundTrip)
{
    DirectionalLight light;
    const Vector3 direction(0.25f, -0.5f, -0.75f);
    const Vector3 diffuse(0.1f, 0.2f, 0.3f);
    const Vector3 specular(0.4f, 0.5f, 0.6f);

    light.setDirectionProperty(direction);
    light.setDiffuseColorProperty(diffuse);
    light.setSpecularColorProperty(specular);
    light.setEnabledProperty(true);

    EXPECT_TRUE(light.getEnabledProperty());
    EXPECT_EQ(light.getDirectionProperty(), direction);
    EXPECT_EQ(light.getDiffuseColorProperty(), diffuse);
    EXPECT_EQ(light.getSpecularColorProperty(), specular);
}

TEST(DirectionalLightTests, ParameterBoundConstructorAndPropertiesSynchronizeEffectParameters)
{
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    DirectionalLight light(&direction, &diffuse, &specular, nullptr);

    EXPECT_EQ(direction.GetValueVector3(), Vector3::Down);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3::Zero);
    EXPECT_EQ(specular.GetValueVector3(), Vector3::Zero);

    const Vector3 enabledDiffuse(0.1f, 0.2f, 0.3f);
    const Vector3 enabledSpecular(0.4f, 0.5f, 0.6f);
    light.setDiffuseColorProperty(enabledDiffuse);
    light.setSpecularColorProperty(enabledSpecular);
    light.setEnabledProperty(true);
    EXPECT_EQ(diffuse.GetValueVector3(), enabledDiffuse);
    EXPECT_EQ(specular.GetValueVector3(), enabledSpecular);

    const Vector3 changedDirection(-0.25f, -0.5f, 0.75f);
    light.setDirectionProperty(changedDirection);
    EXPECT_EQ(direction.GetValueVector3(), changedDirection);

    light.setEnabledProperty(false);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3::Zero);
    EXPECT_EQ(specular.GetValueVector3(), Vector3::Zero);

    const Vector3 disabledDiffuse(0.7f, 0.8f, 0.9f);
    light.setDiffuseColorProperty(disabledDiffuse);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3::Zero);
    light.setEnabledProperty(true);
    EXPECT_EQ(diffuse.GetValueVector3(), disabledDiffuse);
}

// XNA DirectionalLight::.ctor IL_001b-0053: with a clone source the constructor copies the four
// cached fields (enabled, direction, diffuse, specular) with stfld and calls no setter, so none of
// the three parameters is written -- in a stock-effect clone they already hold the source's values,
// copied by Effect(Effect). FNA assigns through the setters instead. This test used to expect the
// parameters to receive the source values, which was FNA's behaviour rather than XNA's (TQ-041).
TEST(DirectionalLightTests, ParameterBoundConstructorClonesCachedValues)
{
    DirectionalLight source;
    source.setDirectionProperty(Vector3(0.25f, -0.75f, 0.5f));
    source.setDiffuseColorProperty(Vector3(0.2f, 0.4f, 0.6f));
    source.setSpecularColorProperty(Vector3(0.1f, 0.3f, 0.5f));
    source.setEnabledProperty(true);

    const Vector3 untouched(7.0f, 7.0f, 7.0f);
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    direction.SetValue(untouched);
    diffuse.SetValue(untouched);
    specular.SetValue(untouched);
    const DirectionalLight clone(&direction, &diffuse, &specular, &source);

    EXPECT_TRUE(clone.getEnabledProperty());
    EXPECT_EQ(clone.getDirectionProperty(), source.getDirectionProperty());
    EXPECT_EQ(clone.getDiffuseColorProperty(), source.getDiffuseColorProperty());
    EXPECT_EQ(clone.getSpecularColorProperty(), source.getSpecularColorProperty());
    EXPECT_EQ(direction.GetValueVector3(), untouched);
    EXPECT_EQ(diffuse.GetValueVector3(), untouched);
    EXPECT_EQ(specular.GetValueVector3(), untouched);
}

// The copied enabled flag is the field, not a setter call: a disabled source gives a disabled clone
// whose parameters are left alone, and enabling the clone later publishes the copied colours
// (set_Enabled IL_0010-0045 writes the cached diffuse and specular values).
TEST(DirectionalLightTests, ParameterBoundCloneOfADisabledLightKeepsItsColoursForLater)
{
    DirectionalLight source;
    source.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.125f));
    source.setSpecularColorProperty(Vector3(0.75f, 0.5f, 0.25f));
    ASSERT_FALSE(source.getEnabledProperty());

    const Vector3 untouched(7.0f, 7.0f, 7.0f);
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    diffuse.SetValue(untouched);
    specular.SetValue(untouched);
    DirectionalLight clone(&direction, &diffuse, &specular, &source);

    EXPECT_FALSE(clone.getEnabledProperty());
    EXPECT_EQ(diffuse.GetValueVector3(), untouched);
    EXPECT_EQ(specular.GetValueVector3(), untouched);

    clone.setEnabledProperty(true);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3(0.5f, 0.25f, 0.125f));
    EXPECT_EQ(specular.GetValueVector3(), Vector3(0.75f, 0.5f, 0.25f));
}

// Without a clone source the constructor runs set_Direction(Down), set_DiffuseColor(One) and
// set_SpecularColor(Zero) (IL_0054-0075) while `enabled` is still false: set_Direction writes its
// parameter unconditionally (IL_0000-000f), the two colour setters are gated on Enabled
// (IL_0000-0006) and write nothing. Pre-seeded values tell "not written" apart from "written zero".
TEST(DirectionalLightTests, ParameterBoundDefaultConstructionWritesOnlyTheDirection)
{
    const Vector3 untouched(7.0f, 7.0f, 7.0f);
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    direction.SetValue(untouched);
    diffuse.SetValue(untouched);
    specular.SetValue(untouched);

    const DirectionalLight light(&direction, &diffuse, &specular, nullptr);

    EXPECT_EQ(direction.GetValueVector3(), Vector3::Down);
    EXPECT_EQ(diffuse.GetValueVector3(), untouched);
    EXPECT_EQ(specular.GetValueVector3(), untouched);
    EXPECT_EQ(light.getDiffuseColorProperty(), Vector3::One);
    EXPECT_EQ(light.getSpecularColorProperty(), Vector3::Zero);
}

// set_Enabled IL_0000-0007 returns at once when the value does not change, so re-enabling an enabled
// light (or re-disabling a disabled one) leaves a parameter written by someone else alone.
TEST(DirectionalLightTests, AssigningTheCurrentEnabledValueWritesNothing)
{
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    DirectionalLight light(&direction, &diffuse, &specular, nullptr);
    light.setEnabledProperty(true);

    const Vector3 raw(3.0f, 4.0f, 5.0f);
    diffuse.SetValue(raw);
    specular.SetValue(raw);
    light.setEnabledProperty(true);
    EXPECT_EQ(diffuse.GetValueVector3(), raw);
    EXPECT_EQ(specular.GetValueVector3(), raw);

    light.setEnabledProperty(false);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3::Zero);
    diffuse.SetValue(raw);
    light.setEnabledProperty(false);
    EXPECT_EQ(diffuse.GetValueVector3(), raw);
}

// set_Direction has no Enabled gate (IL_0000-000f); set_SpecularColor has one (IL_0000-0006), so a
// disabled light publishes a new direction at once and its new specular colour only when enabled.
TEST(DirectionalLightTests, ADisabledLightPublishesDirectionButCachesSpecular)
{
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    DirectionalLight light(&direction, &diffuse, &specular, nullptr);
    ASSERT_FALSE(light.getEnabledProperty());

    const Vector3 newDirection(0.0f, 0.0f, -1.0f);
    const Vector3 newSpecular(0.25f, 0.5f, 0.75f);
    light.setDirectionProperty(newDirection);
    light.setSpecularColorProperty(newSpecular);
    EXPECT_EQ(direction.GetValueVector3(), newDirection);
    EXPECT_EQ(specular.GetValueVector3(), Vector3::Zero);
    EXPECT_EQ(light.getSpecularColorProperty(), newSpecular);

    light.setEnabledProperty(true);
    EXPECT_EQ(specular.GetValueVector3(), newSpecular);
}

// EnvironmentMapEffect binds its lights without a specular parameter (its CacheEffectParameters
// passes null, IL_0155). Every setter checks each parameter for null (set_Enabled IL_0018-0037 and
// IL_004b-0069), so the light still caches the colour and drives the parameters it has.
TEST(DirectionalLightTests, AMissingSpecularParameterIsSkipped)
{
    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    DirectionalLight light(&direction, &diffuse, nullptr, nullptr);

    light.setSpecularColorProperty(Vector3(0.5f, 0.5f, 0.5f));
    light.setDiffuseColorProperty(Vector3(0.25f, 0.5f, 1.0f));
    light.setEnabledProperty(true);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3(0.25f, 0.5f, 1.0f));
    EXPECT_EQ(light.getSpecularColorProperty(), Vector3(0.5f, 0.5f, 0.5f));

    light.setEnabledProperty(false);
    EXPECT_EQ(diffuse.GetValueVector3(), Vector3::Zero);
    EXPECT_EQ(direction.GetValueVector3(), Vector3::Down);
}
