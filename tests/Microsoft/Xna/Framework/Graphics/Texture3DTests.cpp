// SPDX-License-Identifier: MS-PL
// Task 271: Texture3D audit — FNA API conformance tests.
//
// Tests cover:
//   • Constructor properties (Width/Height/Depth/Format/LevelCount), including mipMap
//     level-count math (Task 271 audit finding: previously hardcoded to 1 regardless of mipMap).
//   • GetTypeName.
//   • SetData/GetData argument guards (null data, elementCount<=0, negative startIndex,
//     negative level, invalid box) for all overloads — previously missing entirely
//     (Task 271 audit finding: null data caused a crash, negative startIndex caused an
//     out-of-bounds read/write, matching the class of bug fixed for Texture2D in Tasks 265/266).
//   • SetDataPointerEXT null-data guard.
//   • Dispose marks the resource as disposed.
//
// Happy-path SetData/GetData round-trip coverage (per-slice colour verification) lives in
// the EasyGL pixel-readback integration test: examples/easygl_texture3d_slices_test.cpp.

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture3D;

// -----------------------------------------------------------------------
// Constructor / properties
// -----------------------------------------------------------------------

class Texture3DTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(Texture3DTest, ConstructorSetsWidth)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getWidthProperty(), 2);
}

TEST_F(Texture3DTest, ConstructorSetsHeight)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getHeightProperty(), 3);
}

TEST_F(Texture3DTest, ConstructorSetsDepth)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getDepthProperty(), 4);
}

TEST_F(Texture3DTest, ConstructorSetsFormat)
{
    Texture3D tex(gd, 2, 3, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
}

TEST_F(Texture3DTest, GetTypeNameReturnsFullyQualifiedName)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.GetTypeName(), "Microsoft.Xna.Framework.Graphics.Texture3D");
}

// -----------------------------------------------------------------------
// LevelCount — mipmapped vs non-mipmapped construction (Task 271)
//
// Mirrors FNA's Texture3D constructor: LevelCount = mipMap ? CalculateMipLevels(width, height) : 1
// (depth does not participate in the mip-level count, matching FNA/Texture2D's formula).
// Previously hardcoded to 1 regardless of mipMap — a real bug fixed by this task.
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, MipMapFalseIsAlwaysOne)
{
    EXPECT_EQ(Texture3D(gd, 8, 8, 4, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture3D(gd, 3, 5, 2, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
}

TEST_F(Texture3DTest, MipMapTrueSquarePowerOfTwo)
{
    EXPECT_EQ(Texture3D(gd, 1, 1, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(Texture3D(gd, 4, 4, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture3D(gd, 16, 16, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 5);
}

TEST_F(Texture3DTest, MipMapTrueNonPowerOfTwo)
{
    EXPECT_EQ(Texture3D(gd, 3, 5, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(Texture3D(gd, 7, 11, 2, true, SurfaceFormat::Color).getLevelCountProperty(), 4);
}

// -----------------------------------------------------------------------
// SetData(Color*, int elementCount) / SetData(Color*, int, int) — argument guards
// (both delegate to the 10-arg overload; guards live there)
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, SetDataSimpleNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(nullptr, 8), std::invalid_argument);
}

TEST_F(Texture3DTest, SetDataSimpleZeroElementCountThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(buf, 0), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataStartIndexNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(nullptr, 0, 8), std::invalid_argument);
}

TEST_F(Texture3DTest, SetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(8, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(buf.data(), -1, 8), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataExactElementCountDoesNotThrow)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(8, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(buf.data(), 8));
    EXPECT_NO_THROW(tex.SetData(buf.data(), 0, 8));
}

// -----------------------------------------------------------------------
// SetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount)
// — argument guards (Task 271: previously none of these existed at all)
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, SetDataBoxNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 2, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(Texture3DTest, SetDataBoxZeroElementCountThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf, 0, 0), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxNegativeStartIndexThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxNegativeLevelThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(-1, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxNegativeLeftThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, -1, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxLeftNotLessThanRightThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, 2, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxFrontNotLessThanBackThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 1, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, SetDataBoxWithinBoundsDoesNotThrow)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4));
}

// Task 913: elementCount must cover the full requested region (right-left)*(bottom-top)*
// (back-front) — previously unvalidated, so a too-small elementCount caused the backend to
// write/read past the caller-supplied buffer (confirmed via a live heap-corruption crash while
// building Task 663's DDS test fixture for the analogous TextureCube gap).
TEST_F(Texture3DTest, SetDataBoxElementCountLessThanRegionThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(1, 2, 3, 4) };
    // Region is 2x2x1=4 voxels; only 1 element provided.
    EXPECT_THROW(tex.SetData(0, 0, 0, 2, 2, 0, 1, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// SetDataPointerEXT — null-data guard
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, SetDataPointerEXTNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetDataPointerEXT(0, 0, 0, 2, 2, 0, 1, nullptr, 16), std::invalid_argument);
}

// -----------------------------------------------------------------------
// GetData — argument guards (mirrors SetData's guards)
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, GetDataSimpleNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(nullptr, 8), std::invalid_argument);
}

TEST_F(Texture3DTest, GetDataSimpleZeroElementCountThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.GetData(buf, 0), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(8, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(buf.data(), -1, 8), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataBoxNullDataThrowsInvalidArgument)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(0, 0, 0, 2, 2, 0, 2, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(Texture3DTest, GetDataBoxNegativeLevelThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(-1, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataBoxLeftNotLessThanRightThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(0, 2, 0, 2, 2, 0, 1, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(Texture3DTest, GetDataBoxWithinBoundsDoesNotThrow)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_NO_THROW(tex.GetData(0, 0, 0, 2, 2, 0, 1, buf.data(), 0, 4));
}

// Task 913: see the identical SetData test above for the full rationale.
TEST_F(Texture3DTest, GetDataBoxElementCountLessThanRegionThrowsOutOfRange)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    // Region is 2x2x1=4 voxels; only 1 element provided.
    EXPECT_THROW(tex.GetData(0, 0, 0, 2, 2, 0, 1, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// Dispose
// -----------------------------------------------------------------------

TEST_F(Texture3DTest, DisposeMarksResourceDisposed)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    EXPECT_FALSE(tex.getIsDisposedProperty());
    tex.Dispose();
    EXPECT_TRUE(tex.getIsDisposedProperty());
}

TEST_F(Texture3DTest, DoubleDisposeDoesNotThrow)
{
    Texture3D tex(gd, 2, 2, 2, false, SurfaceFormat::Color);
    tex.Dispose();
    EXPECT_NO_THROW(tex.Dispose());
}
