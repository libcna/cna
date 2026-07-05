// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::Viewport;

static constexpr float kEps = 1e-4f;

// --- Constructors ---

TEST(ViewportTest, DefaultConstructorZeroFields)
{
    Viewport vp;
    EXPECT_EQ(vp.getXProperty(), 0);
    EXPECT_EQ(vp.getYProperty(), 0);
    EXPECT_EQ(vp.getWidthProperty(), 0);
    EXPECT_EQ(vp.getHeightProperty(), 0);
    EXPECT_FLOAT_EQ(vp.getMinDepthProperty(), 0.0f);
    EXPECT_FLOAT_EQ(vp.getMaxDepthProperty(), 1.0f);
}

TEST(ViewportTest, XYWidthHeightConstructor)
{
    Viewport vp(10, 20, 800, 600);
    EXPECT_EQ(vp.getXProperty(), 10);
    EXPECT_EQ(vp.getYProperty(), 20);
    EXPECT_EQ(vp.getWidthProperty(), 800);
    EXPECT_EQ(vp.getHeightProperty(), 600);
    EXPECT_FLOAT_EQ(vp.getMinDepthProperty(), 0.0f);
    EXPECT_FLOAT_EQ(vp.getMaxDepthProperty(), 1.0f);
}

TEST(ViewportTest, RectangleConstructor)
{
    Rectangle r(5, 15, 1024, 768);
    Viewport vp(r);
    EXPECT_EQ(vp.getXProperty(), 5);
    EXPECT_EQ(vp.getYProperty(), 15);
    EXPECT_EQ(vp.getWidthProperty(), 1024);
    EXPECT_EQ(vp.getHeightProperty(), 768);
    EXPECT_FLOAT_EQ(vp.getMinDepthProperty(), 0.0f);
    EXPECT_FLOAT_EQ(vp.getMaxDepthProperty(), 1.0f);
}

// --- X / Y / Width / Height / MinDepth / MaxDepth setters ---

TEST(ViewportTest, SettersUpdateEachFieldIndependently)
{
    Viewport vp;
    vp.setXProperty(42);
    vp.setYProperty(24);
    vp.setWidthProperty(1920);
    vp.setHeightProperty(1080);
    vp.setMinDepthProperty(0.2f);
    vp.setMaxDepthProperty(0.8f);

    EXPECT_EQ(vp.getXProperty(), 42);
    EXPECT_EQ(vp.getYProperty(), 24);
    EXPECT_EQ(vp.getWidthProperty(), 1920);
    EXPECT_EQ(vp.getHeightProperty(), 1080);
    EXPECT_FLOAT_EQ(vp.getMinDepthProperty(), 0.2f);
    EXPECT_FLOAT_EQ(vp.getMaxDepthProperty(), 0.8f);
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
    EXPECT_EQ(vp.getXProperty(), 10);
    EXPECT_EQ(vp.getYProperty(), 20);
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

// --- Project / Unproject with non-identity (perspective + look-at) matrices ---
//
// The tests above only ever pass identity world/view/projection matrices, so the
// perspective-divide branch in Project/Unproject (`if (!WithinEpsilon(a, 1.0f))`) is never
// actually exercised — with identity matrices, `a` is always exactly 1.0, and the divide is
// always skipped. The expected values below are hand-derived from FNA's own formulas (confirmed
// line-by-line identical to CNA's in Task 341) using a real Matrix::CreatePerspectiveFieldOfView
// projection, so `a` is genuinely != 1.0 here. This is deliberately discriminating: if the divide
// were skipped (a plausible regression), Project would instead return (800, 0, ~4.04) for
// kOffAxisPoint below — nowhere near the expected (480, 240, ~0.808) — so the test fails loudly
// rather than coincidentally passing.
//
// Derivation for kProjection = CreatePerspectiveFieldOfView(PiOver2, 1.0, 1.0, 100.0):
//   xScale = yScale = 1/tan(PiOver2/2) = 1, M33 = 100/(1-100) = -100/99, M34 = -1,
//   M43 = (1*100)/(1-100) = -100/99, M44 = 0.
// For source (1,1,-5) with identity view/world: a = -5*M34 = 5; before divide,
//   vector = (1, 1, -5*M33+M43) = (1, 1, 400/99); after divide by a=5: (0.2, 0.2, 80/99).
// Screen-mapped into an 800x600 viewport: X=((0.2+1)*0.5)*800=480, Y=((-0.2+1)*0.5)*600=240,
//   Z=80/99 (MinDepth=0, MaxDepth=1).

static const Matrix kProjection = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver2, 1.0f, 1.0f, 100.0f);
static const Vector3 kOffAxisPoint(1.0f, 1.0f, -5.0f);
static constexpr float kExpectedScreenX = 480.0f;
static constexpr float kExpectedScreenY = 240.0f;
static constexpr float kExpectedScreenZ = 80.0f / 99.0f;

TEST(ViewportTest, ProjectWithNonIdentityPerspectiveMatrix)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 result = vp.Project(kOffAxisPoint, kProjection, id, id);
    EXPECT_NEAR(result.X, kExpectedScreenX, kEps);
    EXPECT_NEAR(result.Y, kExpectedScreenY, kEps);
    EXPECT_NEAR(result.Z, kExpectedScreenZ, kEps);
}

TEST(ViewportTest, ProjectWithNonIdentityViewAndProjectionMatrices)
{
    // A camera at (0,0,5) looking at the world origin sees that origin at view-space (0,0,-5) —
    // the same z used by kOffAxisPoint above, just on-axis (x=y=0), so the expected screen
    // position collapses to the viewport's exact center (400, 300) rather than (480, 240), while
    // still sharing the same non-trivial Z (80/99) computed via the same perspective divide.
    Viewport vp(0, 0, 800, 600);
    Matrix world = Matrix::getIdentityProperty();
    Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    Vector3 result = vp.Project(Vector3(0.0f, 0.0f, 0.0f), kProjection, view, world);
    EXPECT_NEAR(result.X, 400.0f, kEps);
    EXPECT_NEAR(result.Y, 300.0f, kEps);
    EXPECT_NEAR(result.Z, kExpectedScreenZ, kEps);
}

TEST(ViewportTest, UnprojectRecoversOriginalPointThroughNonIdentityPerspectiveMatrix)
{
    Viewport vp(0, 0, 800, 600);
    Matrix id = Matrix::getIdentityProperty();
    Vector3 screen(kExpectedScreenX, kExpectedScreenY, kExpectedScreenZ);
    Vector3 result = vp.Unproject(screen, kProjection, id, id);
    EXPECT_NEAR(result.X, kOffAxisPoint.X, kEps);
    EXPECT_NEAR(result.Y, kOffAxisPoint.Y, kEps);
    EXPECT_NEAR(result.Z, kOffAxisPoint.Z, kEps);
}

TEST(ViewportTest, ProjectUnprojectRoundTripWithNonIdentityViewAndProjectionMatrices)
{
    Viewport vp(0, 0, 800, 600);
    Matrix world = Matrix::getIdentityProperty();
    Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f));
    Vector3 original(0.5f, -0.3f, -2.0f);
    Vector3 screen = vp.Project(original, kProjection, view, world);
    Vector3 back   = vp.Unproject(screen, kProjection, view, world);
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
