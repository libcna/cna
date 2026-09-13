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

TEST(DirectionalLightTests, ParameterBoundConstructorClonesCachedValues)
{
    DirectionalLight source;
    source.setDirectionProperty(Vector3(0.25f, -0.75f, 0.5f));
    source.setDiffuseColorProperty(Vector3(0.2f, 0.4f, 0.6f));
    source.setSpecularColorProperty(Vector3(0.1f, 0.3f, 0.5f));
    source.setEnabledProperty(true);

    EffectParameter direction = VectorParameter("Direction");
    EffectParameter diffuse = VectorParameter("Diffuse");
    EffectParameter specular = VectorParameter("Specular");
    const DirectionalLight clone(&direction, &diffuse, &specular, &source);

    EXPECT_TRUE(clone.getEnabledProperty());
    EXPECT_EQ(clone.getDirectionProperty(), source.getDirectionProperty());
    EXPECT_EQ(clone.getDiffuseColorProperty(), source.getDiffuseColorProperty());
    EXPECT_EQ(clone.getSpecularColorProperty(), source.getSpecularColorProperty());
    EXPECT_EQ(direction.GetValueVector3(), source.getDirectionProperty());
    EXPECT_EQ(diffuse.GetValueVector3(), source.getDiffuseColorProperty());
    EXPECT_EQ(specular.GetValueVector3(), source.getSpecularColorProperty());
}
