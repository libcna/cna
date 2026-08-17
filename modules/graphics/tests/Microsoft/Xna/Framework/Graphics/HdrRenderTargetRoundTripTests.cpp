// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-131: the end-to-end proof that a float render target keeps values above 1.0.
//
// Everything else in Phase 1 is a promise about this one property -- the capability query, the
// per-format verdict, the GL storage, the readback. A test that only checked those parts
// individually could pass while the values were still clamped somewhere in between, which is
// exactly the failure the whole phase exists to remove. So this renders into the target and reads
// the numbers back.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::PackedVector::HalfVector4;

namespace {

constexpr int kSize = 4;

/// The scene-referred colour under test: brighter than white in two channels, which is the entire
/// point of rendering to a float target and the entire thing an 8-bit one cannot represent.
constexpr float kRed   = 4.0f;
constexpr float kGreen = 2.0f;
constexpr float kBlue  = 1.0f;

TEST(HdrRenderTargetRoundTripTest, AFloatTargetKeepsValuesAboveOne)
{
    GraphicsDevice gd;
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<Vector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const Vector4& texel : pixels)
    {
        EXPECT_FLOAT_EQ(texel.X, kRed);
        EXPECT_FLOAT_EQ(texel.Y, kGreen);
        EXPECT_FLOAT_EQ(texel.Z, kBlue);
        EXPECT_FLOAT_EQ(texel.W, 1.0f);
    }
}

TEST(HdrRenderTargetRoundTripTest, AColourTargetClampsTheSameRender)
{
    // The control. Without it, a passing test above could mean "float targets work" or merely
    // "this driver never clamped anything anyway"; this shows the two formats genuinely differ,
    // and documents what an 8-bit target does with the same draw.
    GraphicsDevice gd;

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    // Color has no default constructor (XNA's own struct has no parameterless one either), so the
    // buffer is filled with a value that would survive the readback unnoticed if it never ran.
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const Color& texel : pixels)
    {
        EXPECT_EQ(texel.getRProperty(), 255);
        EXPECT_EQ(texel.getGProperty(), 255);
        EXPECT_EQ(texel.getBProperty(), 255);
        EXPECT_EQ(texel.getAProperty(), 255);
    }
}

TEST(HdrRenderTargetRoundTripTest, AHalfFloatTargetKeepsValuesAboveOne)
{
    // HdrBlendable is the format the CNAEXT engine layer's HDR scene target actually allocates, so
    // it gets its own round trip rather than riding on the RGBA32F case. Its readback element is
    // HalfVector4 -- the shared layer pairs each float format with the element type that matches
    // its storage exactly, and refuses a mismatched one rather than widening behind the caller.
    GraphicsDevice gd;
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTarget2D target(gd, kSize, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);

    gd.SetRenderTarget(&target);
    gd.Clear(kRed, kGreen, kBlue, 1.0f);
    gd.SetRenderTarget(nullptr);

    std::vector<HalfVector4> pixels(static_cast<std::size_t>(kSize) * kSize);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));

    for (const HalfVector4& packed : pixels)
    {
        const Vector4 texel = packed.ToVector4();
        // Half precision: 4.0, 2.0 and 1.0 are all exactly representable, so this is not a
        // tolerance dodge -- an off-by-a-little result here would mean a real conversion bug.
        EXPECT_FLOAT_EQ(texel.X, kRed);
        EXPECT_FLOAT_EQ(texel.Y, kGreen);
        EXPECT_FLOAT_EQ(texel.Z, kBlue);
        EXPECT_FLOAT_EQ(texel.W, 1.0f);
    }
}

TEST(HdrRenderTargetRoundTripTest, AFloatCubeTargetIsCreatedInTheRequestedFormat)
{
    // MOD-107: the cube path carried the same silent substitution the 2D one did -- and it is the
    // path image-based lighting needs, since an irradiance or prefiltered-specular cube is rendered
    // face by face into float storage. A cube that reported HdrBlendable while holding 8-bit texels
    // would make every IBL product quietly wrong.
    GraphicsDevice gd;
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
        GTEST_SKIP() << "this renderer/driver has no RGBA16F render targets";

    RenderTargetCube cube(gd, kSize, false, SurfaceFormat::HdrBlendable, DepthFormat::None);

    EXPECT_EQ(cube.getFormatProperty(), SurfaceFormat::HdrBlendable);
    EXPECT_EQ(cube.getSizeProperty(), kSize);

    // Every face must be bindable and clearable in that format, not just face +X.
    for (const CubeMapFace face : {CubeMapFace::PositiveX, CubeMapFace::NegativeX,
                                   CubeMapFace::PositiveY, CubeMapFace::NegativeY,
                                   CubeMapFace::PositiveZ, CubeMapFace::NegativeZ})
    {
        gd.SetRenderTarget(&cube, face);
        gd.Clear(kRed, kGreen, kBlue, 1.0f);
    }
    gd.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
}

TEST(HdrRenderTargetRoundTripTest, AnUnsupportedCubeFormatIsRefused)
{
    GraphicsDevice gd;
    if (gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Dxt1))
        GTEST_SKIP() << "this renderer claims a compressed render-target format; test not applicable";

    EXPECT_ANY_THROW({
        RenderTargetCube cube(gd, kSize, false, SurfaceFormat::Dxt1, DepthFormat::None);
        (void)cube.getFormatProperty();
    });
}

} // namespace
