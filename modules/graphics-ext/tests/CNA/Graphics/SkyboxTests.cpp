// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1100..MOD-1111: the skybox.
//
// The interesting failures here are all orientations, and an orientation failure renders a sky --
// just one facing the wrong way, or turning at the wrong rate as the camera moves. So the ray
// reconstruction is checked against arithmetic, and the cube's face convention is checked by
// pointing a camera down each of the six axes at a six-colour cube and naming the colour that
// must come back. Neither could be caught by looking at one picture.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/Skybox.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::GraphicsCapability;
using CNA::Graphics::Skybox;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int kFrame = 64;
constexpr int kCube  = 4;

/// One distinct colour per face, so a rendered pixel names the face it came from.
const std::array<Color, 6> kFaceColors{
    Color(255, 0, 0, 255),      // +X red
    Color(0, 255, 0, 255),      // -X green
    Color(0, 0, 255, 255),      // +Y blue
    Color(255, 255, 0, 255),    // -Y yellow
    Color(255, 0, 255, 255),    // +Z magenta
    Color(0, 255, 255, 255),    // -Z cyan
};

std::unique_ptr<TextureCube> MakeSixColorCube(GraphicsDevice& gd)
{
    auto cube = std::make_unique<TextureCube>(gd, kCube, false, SurfaceFormat::Color);
    for (int face = 0; face < 6; ++face)
    {
        const std::vector<Color> texels(static_cast<std::size_t>(kCube) * kCube,
                                        kFaceColors[static_cast<std::size_t>(face)]);
        cube->SetData(static_cast<CubeMapFace>(face), texels.data(),
                      static_cast<int>(texels.size()));
    }
    return cube;
}

Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
}

/// A camera at the origin looking along @p forward.
Matrix LookAlong(const Vector3& forward)
{
    const Vector3 up = std::abs(forward.Y) > 0.9f ? Vector3(0.0f, 0.0f, 1.0f)
                                                  : Vector3(0.0f, 1.0f, 0.0f);
    return Matrix::CreateLookAt(Vector3::Zero, forward, up);
}

Color CentrePixel(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
    const Rectangle region(0, 0, kFrame, kFrame);
    target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
    return pixels[static_cast<std::size_t>(kFrame / 2) * kFrame + kFrame / 2];
}

class SkyboxTest : public ::testing::Test
{
protected:
    GraphicsDevice device;

    void SetUp() override
    {
        if (!device.SupportsCapability(GraphicsCapability::CustomEffects))
            GTEST_SKIP() << "this renderer cannot compile the sky shader";
    }
};

// =====================================================================================
// The ray, against arithmetic (MOD-1102)
// =====================================================================================

TEST(SkyboxMathTest, TheCentreRayIsTheCameraForward)
{
    // Looking down -Z, the middle of the screen must look down -Z. A reconstruction that is merely
    // close still renders a sky; it just turns at the wrong rate as the camera moves.
    const Vector3 ray = Skybox::computeViewRay(LookAlong(Vector3(0.0f, 0.0f, -1.0f)), Projection(),
                                               0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(ray.X, 0.0f, 1e-3f);
    EXPECT_NEAR(ray.Y, 0.0f, 1e-3f);
    EXPECT_NEAR(ray.Z, -1.0f, 1e-3f);
}

TEST(SkyboxMathTest, MovingTheCameraDoesNotChangeTheRay)
{
    // The whole point of stripping the view's translation: the sky is infinitely far away, so
    // walking must not move it. Keeping the translation makes it slide past like a backdrop.
    const Matrix atOrigin = Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f),
                                                 Vector3(0.0f, 1.0f, 0.0f));
    const Matrix milesAway = Matrix::CreateLookAt(Vector3(500.0f, -300.0f, 900.0f),
                                                  Vector3(500.0f, -300.0f, 899.0f),
                                                  Vector3(0.0f, 1.0f, 0.0f));

    const Vector3 a = Skybox::computeViewRay(atOrigin, Projection(), 0.4f, -0.2f, 0.0f);
    const Vector3 b = Skybox::computeViewRay(milesAway, Projection(), 0.4f, -0.2f, 0.0f);
    EXPECT_NEAR(a.X, b.X, 1e-3f);
    EXPECT_NEAR(a.Y, b.Y, 1e-3f);
    EXPECT_NEAR(a.Z, b.Z, 1e-3f);
}

TEST(SkyboxMathTest, TurningTheCameraTurnsTheRay)
{
    // The other half of the same property: rotation *must* move it, or the sky would be painted on
    // the screen rather than around the world.
    const Vector3 forward = Skybox::computeViewRay(LookAlong(Vector3(0.0f, 0.0f, -1.0f)),
                                                   Projection(), 0.0f, 0.0f, 0.0f);
    const Vector3 right = Skybox::computeViewRay(LookAlong(Vector3(1.0f, 0.0f, 0.0f)),
                                                 Projection(), 0.0f, 0.0f, 0.0f);
    EXPECT_NEAR(right.X, 1.0f, 1e-3f);
    EXPECT_NEAR(forward.Z, -1.0f, 1e-3f);
}

TEST(SkyboxMathTest, TheScreenEdgesSpanTheFieldOfView)
{
    // At a 45-degree vertical field of view the top edge is 22.5 degrees above the centre. A ray
    // reconstruction that ignored the projection would put it at 45, or at 0.
    const Vector3 top = Skybox::computeViewRay(LookAlong(Vector3(0.0f, 0.0f, -1.0f)), Projection(),
                                               0.0f, 1.0f, 0.0f);
    const float angle = std::atan2(top.Y, -top.Z);
    EXPECT_NEAR(angle, MathHelper::PiOver4 * 0.5f, 1e-2f);
}

TEST(SkyboxMathTest, YawRotatesTheSampledDirectionAboutY)
{
    const Vector3 straight = Skybox::computeViewRay(LookAlong(Vector3(0.0f, 0.0f, -1.0f)),
                                                    Projection(), 0.0f, 0.0f, 0.0f);
    const Vector3 quarter = Skybox::computeViewRay(LookAlong(Vector3(0.0f, 0.0f, -1.0f)),
                                                   Projection(), 0.0f, 0.0f,
                                                   MathHelper::PiOver2);
    EXPECT_NEAR(straight.Z, -1.0f, 1e-3f);
    // A quarter turn takes -Z onto -X, and leaves Y alone.
    EXPECT_NEAR(quarter.X, -1.0f, 1e-3f);
    EXPECT_NEAR(quarter.Y, 0.0f, 1e-3f);
    EXPECT_NEAR(std::abs(quarter.Z), 0.0f, 1e-3f);
}

// =====================================================================================
// Settings (MOD-1100, MOD-1106, MOD-1107, MOD-1111)
// =====================================================================================

TEST_F(SkyboxTest, TheEnvironmentIsBorrowedByDefault)
{
    auto cube = MakeSixColorCube(device);
    Skybox sky(device, cube.get());
    EXPECT_EQ(sky.getEnvironment(), cube.get());

    sky.setEnvironment(nullptr);
    EXPECT_EQ(sky.getEnvironment(), nullptr);
    // The borrowed cube is still alive: the skybox never owned it.
    EXPECT_EQ(cube->getSizeProperty(), kCube);
}

TEST_F(SkyboxTest, AnOwnedEnvironmentIsHeldAndReplaceable)
{
    Skybox sky(device, nullptr);
    sky.setOwnedEnvironment(MakeSixColorCube(device));
    ASSERT_NE(sky.getEnvironment(), nullptr);

    // Attaching a borrowed one over an owned one has to release the owned one, or it stays alive
    // with nothing referring to it -- a leak with no symptom.
    auto borrowed = MakeSixColorCube(device);
    sky.setEnvironment(borrowed.get());
    EXPECT_EQ(sky.getEnvironment(), borrowed.get());
}

TEST_F(SkyboxTest, TheSettingsRoundTripAndIntensityIsClamped)
{
    Skybox sky(device, nullptr);
    EXPECT_FLOAT_EQ(sky.getYaw(), 0.0f);
    EXPECT_FLOAT_EQ(sky.getIntensity(), 1.0f);
    EXPECT_FLOAT_EQ(sky.getTint().X, 1.0f);

    sky.setYaw(1.25f);
    sky.setIntensity(3.5f);
    sky.setTint(Vector3(0.5f, 0.25f, 0.75f));
    EXPECT_FLOAT_EQ(sky.getYaw(), 1.25f);
    EXPECT_FLOAT_EQ(sky.getIntensity(), 3.5f);
    EXPECT_FLOAT_EQ(sky.getTint().Z, 0.75f);

    // Negative brightness is not a dark sky, it is a sign error; clamped rather than propagated.
    sky.setIntensity(-2.0f);
    EXPECT_FLOAT_EQ(sky.getIntensity(), 0.0f);
}

TEST_F(SkyboxTest, ANonPositiveTargetSizeIsRejected)
{
    Skybox sky(device, nullptr);
    EXPECT_THROW(sky.draw(Matrix::getIdentityProperty(), Projection(), 0, kFrame),
                 std::invalid_argument);
    EXPECT_THROW(sky.draw(Matrix::getIdentityProperty(), Projection(), kFrame, -1),
                 std::invalid_argument);
}

TEST_F(SkyboxTest, DrawingWithNoEnvironmentIsSkippedRatherThanFailing)
{
    // MOD-1108. A game switching the sky on before its environment has loaded should get a scene
    // without a sky, not an exception on the first frame.
    Skybox sky(device, nullptr);
    EXPECT_NO_THROW(sky.draw(LookAlong(Vector3(0.0f, 0.0f, -1.0f)), Projection(), kFrame, kFrame));
}

// =====================================================================================
// Rendering (MOD-1103, MOD-1110)
// =====================================================================================

class SkyboxRenderTest : public SkyboxTest
{
protected:
    void SetUp() override
    {
        SkyboxTest::SetUp();
        if (::testing::Test::IsSkipped())
            return;
        if (!device.SupportsCapability(GraphicsCapability::ThreeD))
            GTEST_SKIP() << "this renderer does not raster 3D triangles";
    }
};

TEST_F(SkyboxRenderTest, EachCameraDirectionSamplesItsOwnFace)
{
    // MOD-1110, and the only test here that could catch a cube whose faces are wired in the wrong
    // order: six cameras, six colours, named individually. A cube map with two faces swapped still
    // renders a sky from every angle.
    auto cube = MakeSixColorCube(device);
    Skybox sky(device, cube.get());
    if (!sky.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the sky shader";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color, DepthFormat::None);

    struct Case { Vector3 forward; std::size_t face; const char* name; };
    const Case cases[] = {
        {Vector3( 1.0f,  0.0f,  0.0f), 0, "+X"},
        {Vector3(-1.0f,  0.0f,  0.0f), 1, "-X"},
        {Vector3( 0.0f,  1.0f,  0.0f), 2, "+Y"},
        {Vector3( 0.0f, -1.0f,  0.0f), 3, "-Y"},
        {Vector3( 0.0f,  0.0f,  1.0f), 4, "+Z"},
        {Vector3( 0.0f,  0.0f, -1.0f), 5, "-Z"},
    };

    for (const Case& c : cases)
    {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        sky.draw(LookAlong(c.forward), Projection(), kFrame, kFrame);
        device.SetRenderTarget(nullptr);

        const Color got = CentrePixel(target);
        const Color want = kFaceColors[c.face];
        EXPECT_EQ(got.getRProperty(), want.getRProperty()) << "looking " << c.name;
        EXPECT_EQ(got.getGProperty(), want.getGProperty()) << "looking " << c.name;
        EXPECT_EQ(got.getBProperty(), want.getBProperty()) << "looking " << c.name;
    }
}

TEST_F(SkyboxRenderTest, YawTurnsTheSkyWithoutTurningTheCamera)
{
    // MOD-1106 through the renderer rather than through the CPU twin: a quarter turn must show the
    // face the camera would have seen after turning a quarter itself.
    auto cube = MakeSixColorCube(device);
    Skybox sky(device, cube.get());
    if (!sky.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the sky shader";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color, DepthFormat::None);
    const Matrix view = LookAlong(Vector3(0.0f, 0.0f, -1.0f));

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sky.draw(view, Projection(), kFrame, kFrame);
    device.SetRenderTarget(nullptr);
    const Color straight = CentrePixel(target);

    sky.setYaw(MathHelper::PiOver2);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sky.draw(view, Projection(), kFrame, kFrame);
    device.SetRenderTarget(nullptr);
    const Color turned = CentrePixel(target);

    EXPECT_EQ(straight.getBProperty(), 255) << "looking -Z should show the cyan -Z face";
    EXPECT_EQ(straight.getRProperty(), 0);
    // A quarter turn takes the lookup to -X, which is the green face.
    EXPECT_EQ(turned.getGProperty(), 255) << "a quarter yaw should bring the -X face into view";
    EXPECT_EQ(turned.getBProperty(), 0);
}

TEST_F(SkyboxRenderTest, IntensityAndTintScaleTheSampledColour)
{
    auto cube = MakeSixColorCube(device);
    Skybox sky(device, cube.get());
    if (!sky.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the sky shader";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color, DepthFormat::None);
    const Matrix view = LookAlong(Vector3(0.0f, 0.0f, -1.0f));   // the cyan -Z face

    sky.setIntensity(0.5f);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sky.draw(view, Projection(), kFrame, kFrame);
    device.SetRenderTarget(nullptr);
    const Color halved = CentrePixel(target);
    EXPECT_NEAR(halved.getGProperty(), 128, 4);

    sky.setIntensity(1.0f);
    sky.setTint(Vector3(1.0f, 0.25f, 1.0f));
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sky.draw(view, Projection(), kFrame, kFrame);
    device.SetRenderTarget(nullptr);
    const Color tinted = CentrePixel(target);
    EXPECT_NEAR(tinted.getGProperty(), 64, 4);
    EXPECT_EQ(tinted.getBProperty(), 255);
}

TEST_F(SkyboxRenderTest, TheSkyNeverOccludesGeometry)
{
    // MOD-1103's guarantee, which this gets by drawing the sky first rather than by depth state --
    // recorded as a deviation in the plan. The assertion is the same either way: a foreground
    // object is still there afterwards.
    auto cube = MakeSixColorCube(device);
    Skybox sky(device, cube.get());
    if (!sky.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the sky shader";

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Color,
                          DepthFormat::Depth24);
    const Matrix view = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero,
                                             Vector3(0.0f, 1.0f, 0.0f));

    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sky.draw(view, Projection(), kFrame, kFrame);

    // A quad squarely in front of the camera, unlit and white, drawn after the sky.
    const Vector3 normal(0.0f, 0.0f, 1.0f);
    const Vector2 uv(0.0f, 0.0f);
    const std::array<VertexPositionNormalTexture, 6> quad{
        VertexPositionNormalTexture(Vector3(-1.0f, -1.0f, 0.0f), normal, uv),
        VertexPositionNormalTexture(Vector3( 1.0f, -1.0f, 0.0f), normal, uv),
        VertexPositionNormalTexture(Vector3( 1.0f,  1.0f, 0.0f), normal, uv),
        VertexPositionNormalTexture(Vector3(-1.0f, -1.0f, 0.0f), normal, uv),
        VertexPositionNormalTexture(Vector3( 1.0f,  1.0f, 0.0f), normal, uv),
        VertexPositionNormalTexture(Vector3(-1.0f,  1.0f, 0.0f), normal, uv),
    };

    BasicEffect effect(device);
    effect.setLightingEnabledProperty(false);
    effect.setTextureEnabledProperty(false);
    effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
    effect.setWorldProperty(Matrix::getIdentityProperty());
    effect.setViewProperty(view);
    effect.setProjectionProperty(Projection());

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);
    effect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    device.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
    const Rectangle region(0, 0, kFrame, kFrame);
    target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

    const Color centre = pixels[static_cast<std::size_t>(kFrame / 2) * kFrame + kFrame / 2];
    const Color corner = pixels[2];
    EXPECT_EQ(centre.getRProperty(), 255) << "the sky covered the foreground object";
    EXPECT_EQ(centre.getGProperty(), 255);
    // And the sky is still there around it, so this is not simply a frame with no sky in it.
    EXPECT_EQ(corner.getGProperty(), 255) << "the sky did not render at all";
    EXPECT_EQ(corner.getRProperty(), 0);
}

TEST_F(SkyboxRenderTest, AnHdrSkyWritesValuesAboveOneIntoAFloatTarget)
{
    // MOD-1105. The whole reason a sky is worth rendering into an HDR target: a panorama's sun is
    // several times white, and that is what gives bloom and tonemapping something to do. Against an
    // 8-bit target the same draw clamps to 255 and the information is gone before the first pass.
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    auto cube = MakeSixColorCube(device);
    Skybox sky(device, cube.get());
    if (!sky.isSupported())
        GTEST_SKIP() << "this renderer cannot compile the sky shader";

    // Intensity is what carries a source above 1 here: an 8-bit cube cannot store it, but the sky
    // multiplies before writing, so the float target receives the product.
    sky.setIntensity(4.0f);

    RenderTarget2D target(device, kFrame, kFrame, false, SurfaceFormat::Vector4, DepthFormat::None);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    sky.draw(LookAlong(Vector3(0.0f, 0.0f, -1.0f)), Projection(), kFrame, kFrame);
    device.SetRenderTarget(nullptr);

    std::vector<Microsoft::Xna::Framework::Vector4> pixels(
        static_cast<std::size_t>(kFrame) * kFrame);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    // The -Z face is cyan, so green and blue are 1.0 before the multiply and 4.0 after.
    const auto& centre = pixels[static_cast<std::size_t>(kFrame / 2) * kFrame + kFrame / 2];
    EXPECT_NEAR(centre.Y, 4.0f, 0.05f) << "the sky was clamped on its way into a float target";
    EXPECT_NEAR(centre.Z, 4.0f, 0.05f);
    EXPECT_NEAR(centre.X, 0.0f, 0.05f);
}

} // namespace

#endif // CNA_CNAEXT
