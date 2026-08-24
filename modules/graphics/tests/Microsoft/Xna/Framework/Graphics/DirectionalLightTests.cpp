// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;

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
