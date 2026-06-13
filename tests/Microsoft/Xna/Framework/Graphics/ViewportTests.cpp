// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::Viewport;

static constexpr float kEps = 1e-4f;

// --- Constructors ---

TEST(ViewportTest, DefaultConstructorZeroFields)
{
    Viewport vp;
    EXPECT_EQ(vp.x, 0);
    EXPECT_EQ(vp.y, 0);
    EXPECT_EQ(vp.getWidthProperty(), 0);
    EXPECT_EQ(vp.getHeightProperty(), 0);
    EXPECT_FLOAT_EQ(vp.minDepth, 0.0f);
    EXPECT_FLOAT_EQ(vp.maxDepth, 1.0f);
}

TEST(ViewportTest, XYWidthHeightConstructor)
{
    Viewport vp(10, 20, 800, 600);
    EXPECT_EQ(vp.x, 10);
    EXPECT_EQ(vp.y, 20);
    EXPECT_EQ(vp.getWidthProperty(), 800);
    EXPECT_EQ(vp.getHeightProperty(), 600);
    EXPECT_FLOAT_EQ(vp.minDepth, 0.0f);
    EXPECT_FLOAT_EQ(vp.maxDepth, 1.0f);
}

TEST(ViewportTest, RectangleConstructor)
{
    Rectangle r(5, 15, 1024, 768);
    Viewport vp(r);
    EXPECT_EQ(vp.x, 5);
    EXPECT_EQ(vp.y, 15);
    EXPECT_EQ(vp.getWidthProperty(), 1024);
    EXPECT_EQ(vp.getHeightProperty(), 768);
    EXPECT_FLOAT_EQ(vp.minDepth, 0.0f);
    EXPECT_FLOAT_EQ(vp.maxDepth, 1.0f);
}

// --- AspectRatio ---

TEST(ViewportTest, AspectRatioStandard)
{
    Viewport vp(0, 0, 800, 600);
    EXPECT_NEAR(vp.getAspectRatioProperty(), 800.0f / 600.0f, kEps);
}

TEST(ViewportTest, AspectRatioZeroDimensions)
{
    Viewport vp;
    EXPECT_FLOAT_EQ(vp.getAspectRatioProperty(), 0.0f);
}

TEST(ViewportTest, AspectRatioZeroHeight)
{
    Viewport vp(0, 0, 800, 0);
    EXPECT_FLOAT_EQ(vp.getAspectRatioProperty(), 0.0f);
}

// --- Bounds ---

TEST(ViewportTest, GetBoundsReturnsMatchingRectangle)
{
    Viewport vp(3, 7, 640, 480);
    Rectangle b = vp.getBoundsProperty();
    EXPECT_EQ(b.X, 3);
    EXPECT_EQ(b.Y, 7);
    EXPECT_EQ(b.Width, 640);
    EXPECT_EQ(b.Height, 480);
}

TEST(ViewportTest, SetBoundsUpdatesAllFields)
{
    Viewport vp(0, 0, 100, 100);
    Rectangle r(10, 20, 320, 240);
    vp.setBoundsProperty(r);
    EXPECT_EQ(vp.x, 10);
    EXPECT_EQ(vp.y, 20);
    EXPECT_EQ(vp.getWidthProperty(), 320);
    EXPECT_EQ(vp.getHeightProperty(), 240);
}

// --- TitleSafeArea ---

TEST(ViewportTest, TitleSafeAreaEqualsBounds)
{
    Viewport vp(0, 0, 800, 600);
    Rectangle b = vp.getBoundsProperty();
    Rectangle t = vp.getTitleSafeAreaProperty();
    EXPECT_EQ(t.X, b.X);
    EXPECT_EQ(t.Y, b.Y);
    EXPECT_EQ(t.Width, b.Width);
    EXPECT_EQ(t.Height, b.Height);
}

// --- Project ---

TEST(ViewportTest, ProjectOriginWithIdentityMatrices)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 result = vp.Project(Vector3(0.0f, 0.0f, 0.0f), id, id, id);
    EXPECT_NEAR(result.X, 400.0f, kEps);
    EXPECT_NEAR(result.Y, 300.0f, kEps);
    EXPECT_NEAR(result.Z, 0.0f, kEps);
}

TEST(ViewportTest, ProjectCornerWithIdentityMatrices)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 result = vp.Project(Vector3(1.0f, 1.0f, 1.0f), id, id, id);
    EXPECT_NEAR(result.X, 800.0f, kEps);
    EXPECT_NEAR(result.Y, 0.0f, kEps);
    EXPECT_NEAR(result.Z, 1.0f, kEps);
}

TEST(ViewportTest, ProjectNegativeCornerWithIdentityMatrices)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 result = vp.Project(Vector3(-1.0f, -1.0f, 0.0f), id, id, id);
    EXPECT_NEAR(result.X, 0.0f, kEps);
    EXPECT_NEAR(result.Y, 600.0f, kEps);
    EXPECT_NEAR(result.Z, 0.0f, kEps);
}

// --- Unproject ---

TEST(ViewportTest, UnprojectScreenCenterWithIdentityMatrices)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 result = vp.Unproject(Vector3(400.0f, 300.0f, 0.0f), id, id, id);
    EXPECT_NEAR(result.X, 0.0f, kEps);
    EXPECT_NEAR(result.Y, 0.0f, kEps);
    EXPECT_NEAR(result.Z, 0.0f, kEps);
}

TEST(ViewportTest, UnprojectTopRightWithIdentityMatrices)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 result = vp.Unproject(Vector3(800.0f, 0.0f, 1.0f), id, id, id);
    EXPECT_NEAR(result.X, 1.0f, kEps);
    EXPECT_NEAR(result.Y, 1.0f, kEps);
    EXPECT_NEAR(result.Z, 1.0f, kEps);
}

// --- Project / Unproject round-trip ---

TEST(ViewportTest, ProjectUnprojectRoundTrip)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 original(0.5f, -0.3f, 0.7f);
    Vector3 screen  = vp.Project(original, id, id, id);
    Vector3 back    = vp.Unproject(screen, id, id, id);
    EXPECT_NEAR(back.X, original.X, kEps);
    EXPECT_NEAR(back.Y, original.Y, kEps);
    EXPECT_NEAR(back.Z, original.Z, kEps);
}

// --- ToString ---

TEST(ViewportTest, ToStringFormat)
{
    Viewport vp(1, 2, 640, 480);
    std::string s = vp.ToString();
    EXPECT_NE(s.find("1"), std::string::npos);
    EXPECT_NE(s.find("2"), std::string::npos);
    EXPECT_NE(s.find("640"), std::string::npos);
    EXPECT_NE(s.find("480"), std::string::npos);
}
