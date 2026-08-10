// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

TEST(GraphicsDeviceInformationTest, DefaultConstructorAdapterIsNull)
{
    GraphicsDeviceInformation gdi;
    EXPECT_EQ(gdi.getAdapterProperty(), nullptr);
}

TEST(GraphicsDeviceInformationTest, DefaultConstructorProfileIsReach)
{
    GraphicsDeviceInformation gdi;
    EXPECT_EQ(gdi.getGraphicsProfileProperty(), GraphicsProfile::Reach);
}

TEST(GraphicsDeviceInformationTest, SetGetAdapter)
{
    GraphicsDeviceInformation gdi;
    GraphicsAdapter* fakeAdapter = reinterpret_cast<GraphicsAdapter*>(0x1);
    gdi.setAdapterProperty(fakeAdapter);
    EXPECT_EQ(gdi.getAdapterProperty(), fakeAdapter);
}

TEST(GraphicsDeviceInformationTest, SetGetGraphicsProfile)
{
    GraphicsDeviceInformation gdi;
    gdi.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    EXPECT_EQ(gdi.getGraphicsProfileProperty(), GraphicsProfile::HiDef);
}

TEST(GraphicsDeviceInformationTest, SetGetPresentationParameters)
{
    GraphicsDeviceInformation gdi;
    PresentationParameters pp;
    pp.setBackBufferWidthProperty(1920);
    pp.setBackBufferHeightProperty(1080);
    gdi.setPresentationParametersProperty(pp);
    EXPECT_EQ(gdi.getPresentationParametersProperty().getBackBufferWidthProperty(), 1920);
    EXPECT_EQ(gdi.getPresentationParametersProperty().getBackBufferHeightProperty(), 1080);
}

TEST(GraphicsDeviceInformationTest, CloneCopiesAllFields)
{
    GraphicsDeviceInformation gdi;
    GraphicsAdapter* fakeAdapter = reinterpret_cast<GraphicsAdapter*>(0x2);
    gdi.setAdapterProperty(fakeAdapter);
    gdi.setGraphicsProfileProperty(GraphicsProfile::HiDef);

    GraphicsDeviceInformation clone = gdi.Clone();

    EXPECT_EQ(clone.getAdapterProperty(), fakeAdapter);
    EXPECT_EQ(clone.getGraphicsProfileProperty(), GraphicsProfile::HiDef);
}

TEST(GraphicsDeviceInformationTest, CloneIsIndependent)
{
    GraphicsDeviceInformation gdi;
    gdi.setGraphicsProfileProperty(GraphicsProfile::HiDef);
    GraphicsDeviceInformation clone = gdi.Clone();

    clone.setGraphicsProfileProperty(GraphicsProfile::Reach);
    EXPECT_EQ(gdi.getGraphicsProfileProperty(), GraphicsProfile::HiDef);
}

TEST(GraphicsDeviceInformationTest, GetTypeName)
{
    GraphicsDeviceInformation gdi;
    EXPECT_EQ(gdi.GetTypeName(), "Microsoft.Xna.Framework.GraphicsDeviceInformation");
}
