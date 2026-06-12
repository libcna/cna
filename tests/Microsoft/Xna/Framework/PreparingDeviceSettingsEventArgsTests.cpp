// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/PreparingDeviceSettingsEventArgs.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"

using Microsoft::Xna::Framework::GraphicsDeviceInformation;
using Microsoft::Xna::Framework::PreparingDeviceSettingsEventArgs;

TEST(PreparingDeviceSettingsEventArgsTest, ConstructorStoresReference)
{
    GraphicsDeviceInformation gdi;
    PreparingDeviceSettingsEventArgs args(gdi);
    EXPECT_EQ(&args.getGraphicsDeviceInformationProperty(), &gdi);
}

TEST(PreparingDeviceSettingsEventArgsTest, ConstGetterReturnsSameObject)
{
    GraphicsDeviceInformation gdi;
    const PreparingDeviceSettingsEventArgs args(gdi);
    EXPECT_EQ(&args.getGraphicsDeviceInformationProperty(), &gdi);
}
