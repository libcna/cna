// SPDX-License-Identifier: MS-PL
//
// Task 129: unit tests for the enums that govern Texture3D, TextureCube,
// RenderTarget2D, and RenderTargetCube.
//
// Texture3D / TextureCube / RenderTarget2D / RenderTargetCube all require a
// GraphicsDevice to construct and have no throw-before-access guards, so
// constructor / property / GetData / SetData tests are covered by the
// EasyGL and Vulkan integration tests (easygl_render_target_test, house3d_demo,
// etc.).  What CAN be verified without a GPU are the enum values that all four
// types depend on.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"

using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;

// -----------------------------------------------------------------------
// CubeMapFace — XNA 4.0 specifies explicit integer values 0-5
// -----------------------------------------------------------------------

TEST(CubeMapFaceTest, PositiveXIsZero)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::PositiveX), 0);
}

TEST(CubeMapFaceTest, NegativeXIsOne)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::NegativeX), 1);
}

TEST(CubeMapFaceTest, PositiveYIsTwo)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::PositiveY), 2);
}

TEST(CubeMapFaceTest, NegativeYIsThree)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::NegativeY), 3);
}

TEST(CubeMapFaceTest, PositiveZIsFour)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::PositiveZ), 4);
}

TEST(CubeMapFaceTest, NegativeZIsFive)
{
    EXPECT_EQ(static_cast<int>(CubeMapFace::NegativeZ), 5);
}

TEST(CubeMapFaceTest, AllSixFacesAreDistinct)
{
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeX);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::PositiveY);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::PositiveY);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::NegativeX, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::NegativeY, CubeMapFace::PositiveZ);
    EXPECT_NE(CubeMapFace::NegativeY, CubeMapFace::NegativeZ);
    EXPECT_NE(CubeMapFace::PositiveZ, CubeMapFace::NegativeZ);
}

TEST(CubeMapFaceTest, OppositeFacesDiffer)
{
    EXPECT_NE(CubeMapFace::PositiveX, CubeMapFace::NegativeX);
    EXPECT_NE(CubeMapFace::PositiveY, CubeMapFace::NegativeY);
    EXPECT_NE(CubeMapFace::PositiveZ, CubeMapFace::NegativeZ);
}

// -----------------------------------------------------------------------
// DepthFormat — XNA 4.0 specifies values: None=0, Depth16=1, Depth24=2,
//               Depth24Stencil8=3
// -----------------------------------------------------------------------

TEST(DepthFormatTest, NoneIsZero)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::None), 0);
}

TEST(DepthFormatTest, Depth16IsOne)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::Depth16), 1);
}

TEST(DepthFormatTest, Depth24IsTwo)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::Depth24), 2);
}

TEST(DepthFormatTest, Depth24Stencil8IsThree)
{
    EXPECT_EQ(static_cast<int>(DepthFormat::Depth24Stencil8), 3);
}

TEST(DepthFormatTest, AllFormatsAreDistinct)
{
    EXPECT_NE(DepthFormat::None,           DepthFormat::Depth16);
    EXPECT_NE(DepthFormat::None,           DepthFormat::Depth24);
    EXPECT_NE(DepthFormat::None,           DepthFormat::Depth24Stencil8);
    EXPECT_NE(DepthFormat::Depth16,        DepthFormat::Depth24);
    EXPECT_NE(DepthFormat::Depth16,        DepthFormat::Depth24Stencil8);
    EXPECT_NE(DepthFormat::Depth24,        DepthFormat::Depth24Stencil8);
}

TEST(DepthFormatTest, NoneIsDifferentFromAllDepthFormats)
{
    EXPECT_NE(DepthFormat::None, DepthFormat::Depth16);
    EXPECT_NE(DepthFormat::None, DepthFormat::Depth24);
    EXPECT_NE(DepthFormat::None, DepthFormat::Depth24Stencil8);
}

// -----------------------------------------------------------------------
// RenderTargetUsage — XNA 4.0 specifies:
//   DiscardContents=0, PreserveContents=1, PlatformContents=2
// -----------------------------------------------------------------------

TEST(RenderTargetUsageTest, DiscardContentsIsZero)
{
    EXPECT_EQ(static_cast<int>(RenderTargetUsage::DiscardContents), 0);
}

TEST(RenderTargetUsageTest, PreserveContentsIsOne)
{
    EXPECT_EQ(static_cast<int>(RenderTargetUsage::PreserveContents), 1);
}

TEST(RenderTargetUsageTest, PlatformContentsIsTwo)
{
    EXPECT_EQ(static_cast<int>(RenderTargetUsage::PlatformContents), 2);
}

TEST(RenderTargetUsageTest, AllUsagesAreDistinct)
{
    EXPECT_NE(RenderTargetUsage::DiscardContents,  RenderTargetUsage::PreserveContents);
    EXPECT_NE(RenderTargetUsage::DiscardContents,  RenderTargetUsage::PlatformContents);
    EXPECT_NE(RenderTargetUsage::PreserveContents, RenderTargetUsage::PlatformContents);
}

TEST(RenderTargetUsageTest, DefaultIsDiscardContents)
{
    // Matches the default parameter in RenderTarget2D and RenderTargetCube constructors.
    EXPECT_EQ(RenderTargetUsage::DiscardContents, static_cast<RenderTargetUsage>(0));
}
