// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2094: projected decals.
//
// The row's acceptance criterion is one sentence -- a decal lands on the surface under it and not
// on the surface behind it -- and it is the whole test. Two quads are drawn at very different
// depths, one filling the left half of the view and one the right, and a decal box is placed around
// the near one only. Painting the left half and leaving the right alone is the difference between a
// decal and a screen-space stain; a pass that forgot the box's depth extent would paint both, and
// would look entirely convincing in any screenshot with only one surface in it.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DecalPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::DecalPass;
using CNA::Graphics::DepthNormalPrepass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kSize      = 64;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 100.0f;
constexpr float kNearZ     = -20.0f;
constexpr float kFarZ      = -60.0f;

Matrix View() { return Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f), Vector3::Up); }
Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNearPlane, kFarPlane);
}

/// Six vertices of one axis-aligned quad facing the camera.
std::array<VertexPositionNormalTexture, 6> Quad(const float lowX, const float highX,
                                                const float extentY, const float z)
{
    const Vector3 facing(0.0f, 0.0f, 1.0f);
    const auto vertex = [&](const float x, const float y) {
        return VertexPositionNormalTexture(Vector3(x, y, z), facing, Vector2(0.0f, 0.0f));
    };
    return {vertex(lowX, -extentY),  vertex(highX, -extentY), vertex(highX, extentY),
            vertex(lowX, -extentY),  vertex(highX, extentY),  vertex(lowX, extentY)};
}

/// The near quad fills the left half of the view; the far one fills the right.
void RunPrepass(GraphicsDevice& device, DepthNormalPrepass& prepass)
{
    const auto nearQuad = Quad(-9.0f, 0.0f, 9.0f, kNearZ);
    const auto farQuad  = Quad(0.0f, 30.0f, 30.0f, kFarZ);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetVertexBuffer(nullptr);

    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
    {
        prepass.begin(pass, View(), Projection(), kNearPlane, kFarPlane);
        ShaderEffect* effect = prepass.getPrepassEffect();
        ASSERT_NE(effect, nullptr);
        effect->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, nearQuad.data(), 0, 2);
        effect->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, farQuad.data(), 0, 2);
        prepass.end();
    }
}

/// A decal box around the near quad's plane, projecting along world -Z so it faces the surface.
Matrix NearDecal()
{
    return Matrix::CreateScale(40.0f, 40.0f, 2.0f) * Matrix::CreateRotationY(MathHelper::Pi) *
           Matrix::CreateTranslation(0.0f, 0.0f, kNearZ);
}

struct Halves
{
    int left = 0;
    int right = 0;
};

Halves CountPaintedHalves(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    Halves halves;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const Color& texel = pixels[static_cast<std::size_t>(y) * kSize + x];
            if (texel.getRProperty() < 128) continue;
            if (x < kSize / 2) ++halves.left; else ++halves.right;
        }
    return halves;
}

std::unique_ptr<Texture2D> WhiteDecal(GraphicsDevice& device)
{
    auto texture = std::make_unique<Texture2D>(device, 2, 2);
    const std::array<Color, 4> texels{Color::White, Color::White, Color::White, Color::White};
    texture->SetData(texels.data(), 4);
    return texture;
}

/// Runs the whole route and returns what the decal painted.
Halves PaintDecal(GraphicsDevice& device, DecalPass& pass, const Matrix& decalWorld,
                  const bool useNormals)
{
    DepthNormalPrepass prepass(device, kSize, kSize);
    RunPrepass(device, prepass);

    pass.setPrepassInputs(prepass.getDepthTexture(),
                          useNormals ? prepass.getNormalTexture() : nullptr);
    pass.setCamera(View(), Projection(), kFarPlane);

    const auto decal = WhiteDecal(device);
    RenderTarget2D target(device, kSize, kSize);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    pass.draw(decal.get(), decalWorld, kSize, kSize);
    device.SetRenderTarget(nullptr);
    return CountPaintedHalves(target);
}

TEST(DecalPassTest, TheBoxTestIsAvailableWithoutAGpu)
{
    EXPECT_TRUE(DecalPass::isInsideDecalBox(Vector3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(DecalPass::isInsideDecalBox(Vector3(0.5f, -0.5f, 0.5f)))
        << "the faces belong to the box";
    EXPECT_FALSE(DecalPass::isInsideDecalBox(Vector3(0.51f, 0.0f, 0.0f)));
    EXPECT_FALSE(DecalPass::isInsideDecalBox(Vector3(0.0f, 0.0f, -0.6f)))
        << "depth is a dimension of the box like any other, and it is the one that matters";
}

TEST(DecalPassTest, TheSettingsRoundTripAndAreClamped)
{
    GraphicsDevice device;
    DecalPass pass(device);

    pass.setOpacity(0.25f);
    EXPECT_FLOAT_EQ(pass.getOpacity(), 0.25f);
    pass.setOpacity(-1.0f);
    EXPECT_FLOAT_EQ(pass.getOpacity(), 0.0f);
    pass.setOpacity(4.0f);
    EXPECT_FLOAT_EQ(pass.getOpacity(), 1.0f);

    pass.setMaxSlopeAngle(0.5f);
    EXPECT_FLOAT_EQ(pass.getMaxSlopeAngle(), 0.5f);
    pass.setMaxSlopeAngle(-0.5f);
    EXPECT_FLOAT_EQ(pass.getMaxSlopeAngle(), 0.0f);
    pass.setMaxSlopeAngle(9.0f);
    EXPECT_FLOAT_EQ(pass.getMaxSlopeAngle(), MathHelper::PiOver2);

    pass.setTint(Vector3(0.5f, 0.25f, 0.125f));
    EXPECT_FLOAT_EQ(pass.getTint().X, 0.5f);
    EXPECT_FLOAT_EQ(pass.getTint().Z, 0.125f);
}

TEST(DecalPassTest, DrawingRefusesWhatItCannotDraw)
{
    GraphicsDevice device;
    DecalPass pass(device);
    const auto decal = WhiteDecal(device);

    EXPECT_THROW(pass.draw(nullptr, NearDecal(), kSize, kSize), std::invalid_argument);
    EXPECT_THROW(pass.draw(decal.get(), NearDecal(), 0, kSize), std::invalid_argument);
    EXPECT_THROW(pass.draw(decal.get(), NearDecal(), kSize, -1), std::invalid_argument);
}

TEST(DecalPassTest, WithNoPrepassDepthNothingIsPainted)
{
    // The pass never invents a surface: with no depth there is nothing to project onto, and the
    // honest result is an untouched frame rather than a full-screen wash.
    GraphicsDevice device;
    DecalPass pass(device);
    if (!pass.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    pass.setCamera(View(), Projection(), kFarPlane);
    const auto decal = WhiteDecal(device);
    RenderTarget2D target(device, kSize, kSize);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    pass.draw(decal.get(), NearDecal(), kSize, kSize);
    device.SetRenderTarget(nullptr);

    const Halves painted = CountPaintedHalves(target);
    EXPECT_EQ(painted.left, 0);
    EXPECT_EQ(painted.right, 0);
}

TEST(DecalPassTest, ADecalLandsOnTheSurfaceUnderItAndNotOnTheOneBehind)
{
    GraphicsDevice device;
    DecalPass pass(device);
    if (!pass.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    const Halves painted = PaintDecal(device, pass, NearDecal(), /*useNormals=*/false);
    EXPECT_GT(painted.left, kSize * kSize / 8)
        << "the near surface, which the decal box encloses, was not painted";
    EXPECT_EQ(painted.right, 0)
        << "the far surface was painted too, so the box's depth extent is not being tested";
}

TEST(DecalPassTest, ADecalWhoseBoxReachesNeitherSurfacePaintsNothing)
{
    // The control for the case above: the same scene and the same decal, moved so its box lies
    // between the two surfaces. A pass that painted whatever was in front of it would still paint
    // the near quad here.
    GraphicsDevice device;
    DecalPass pass(device);
    if (!pass.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    const Matrix between = Matrix::CreateScale(40.0f, 40.0f, 2.0f) *
                           Matrix::CreateRotationY(MathHelper::Pi) *
                           Matrix::CreateTranslation(0.0f, 0.0f, -40.0f);
    const Halves painted = PaintDecal(device, pass, between, /*useNormals=*/false);
    EXPECT_EQ(painted.left, 0);
    EXPECT_EQ(painted.right, 0);
}

TEST(DecalPassTest, TheSlopeTestDropsASurfaceTheDecalOnlyGrazes)
{
    GraphicsDevice device;
    DecalPass pass(device);
    if (!pass.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    // Facing the surface: the decal projects along -Z and the quads face +Z, so every pixel of the
    // near quad is head-on and the slope test lets all of it through.
    const Halves facing = PaintDecal(device, pass, NearDecal(), /*useNormals=*/true);
    ASSERT_GT(facing.left, kSize * kSize / 8) << "the head-on case must survive for this to mean anything";

    // The same box turned a quarter turn, so it projects along the surface instead of into it.
    // Nothing about the box changed -- every pixel is still inside it -- and yet nothing may be
    // painted, because a decal applied edge-on smears rather than sticks.
    const Matrix grazing = Matrix::CreateScale(40.0f, 40.0f, 40.0f) *
                           Matrix::CreateRotationY(MathHelper::PiOver2) *
                           Matrix::CreateTranslation(0.0f, 0.0f, kNearZ);
    const Halves grazed = PaintDecal(device, pass, grazing, /*useNormals=*/true);
    EXPECT_EQ(grazed.left, 0);
    EXPECT_EQ(grazed.right, 0);
}

TEST(DecalPassTest, ZeroOpacityLeavesTheFrameAlone)
{
    GraphicsDevice device;
    DecalPass pass(device);
    if (!pass.isSupported()) GTEST_SKIP() << "this renderer does not execute effect source";

    pass.setOpacity(0.0f);
    const Halves painted = PaintDecal(device, pass, NearDecal(), /*useNormals=*/false);
    EXPECT_EQ(painted.left, 0);
    EXPECT_EQ(painted.right, 0);
}

} // namespace

#endif // CNA_CNAEXT
