// SPDX-License-Identifier: MS-PL
// Task 272: TextureCube audit — FNA API conformance tests.
//
// Tests cover:
//   • Constructor properties (Size/Format/LevelCount), including mipMap level-count math
//     (Task 272 audit finding: previously hardcoded to 1 regardless of mipMap, same bug class
//     as Texture3D's Task 271 finding).
//   • GetTypeName.
//   • The previously-missing SetData/GetData(face, data, startIndex, elementCount) overload
//     (Task 272 audit finding: FNA has 3 SetData/GetData overload arities for TextureCube;
//     CNA only had 2 — the middle one was missing entirely).
//   • SetData/GetData argument guards (null data, elementCount<=0, negative startIndex,
//     negative level, invalid rect) — previously missing entirely (Task 272 audit finding,
//     same class of bug fixed for Texture2D in Tasks 265/266 and Texture3D in Task 271).
//   • The rect=nullptr-at-level>0 mip-dimension bug (Task 272 audit finding: SetData/GetData
//     used the full face Size regardless of level when no explicit rect was given, instead of
//     Size>>level like Texture2D/Texture3D's mipDim() pattern — verified both that the fix
//     accepts a correctly-sized call at level>0 and rejects one sized for the full face).
//   • Dispose marks the resource as disposed.
//
// Happy-path SetData/GetData round-trip coverage (per-face colour verification, level 0 only)
// lives in the EasyGL pixel-readback integration test: examples/easygl_texturecube_faces_test.cpp.

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::TextureCube;

// -----------------------------------------------------------------------
// Constructor / properties
// -----------------------------------------------------------------------

class TextureCubeTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(TextureCubeTest, ConstructorSetsSize)
{
    TextureCube tex(gd, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getSizeProperty(), 4);
}

TEST_F(TextureCubeTest, ConstructorSetsFormat)
{
    TextureCube tex(gd, 4, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
}

TEST_F(TextureCubeTest, GetTypeNameReturnsFullyQualifiedName)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_EQ(tex.GetTypeName(), "Microsoft.Xna.Framework.Graphics.TextureCube");
}

// -----------------------------------------------------------------------
// LevelCount — mipmapped vs non-mipmapped construction (Task 272)
//
// Mirrors FNA's TextureCube constructor: LevelCount = mipMap ? CalculateMipLevels(size) : 1.
// Previously hardcoded to 1 regardless of mipMap — a real bug fixed by this task (same class
// as Texture3D's Task 271 finding).
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, MipMapFalseIsAlwaysOne)
{
    EXPECT_EQ(TextureCube(gd, 8, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(TextureCube(gd, 3, false, SurfaceFormat::Color).getLevelCountProperty(), 1);
}

TEST_F(TextureCubeTest, MipMapTruePowerOfTwo)
{
    EXPECT_EQ(TextureCube(gd, 1, true, SurfaceFormat::Color).getLevelCountProperty(), 1);
    EXPECT_EQ(TextureCube(gd, 4, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
    EXPECT_EQ(TextureCube(gd, 16, true, SurfaceFormat::Color).getLevelCountProperty(), 5);
}

TEST_F(TextureCubeTest, MipMapTrueNonPowerOfTwo)
{
    EXPECT_EQ(TextureCube(gd, 3, true, SurfaceFormat::Color).getLevelCountProperty(), 2);
    EXPECT_EQ(TextureCube(gd, 7, true, SurfaceFormat::Color).getLevelCountProperty(), 3);
}

// -----------------------------------------------------------------------
// SetData(face, data, elementCount) / SetData(face, data, startIndex, elementCount)
// — argument guards (both delegate to the 6-arg overload; guards live there)
//
// The 4-arg (face,data,startIndex,elementCount) overload was previously missing entirely
// (Task 272 audit finding) — its presence here is itself part of the fix.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataSimpleNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, nullptr, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, SetDataSimpleZeroElementCountThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, buf, 0), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataStartIndexNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, SetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataExactElementCountDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), 4));
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, buf.data(), 0, 4));
}

// -----------------------------------------------------------------------
// SetData(face, level, rect, data, startIndex, elementCount) — argument guards
// (Task 272: previously none of these existed at all)
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataRectNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, SetDataRectZeroElementCountThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, buf, 0, 0), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectNegativeStartIndexThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, nullptr, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectNegativeLevelThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, -1, nullptr, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectOutOfBoundsThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(1, 1, 2, 2); // extends past the 2x2 face
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, SetDataRectWithinBoundsDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(1, 2, 3, 4) };
    const Rectangle rect(0, 0, 1, 1);
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1));
}

// -----------------------------------------------------------------------
// Mip-level dimension bug (Task 272): rect=nullptr at level>0 must use the
// mip-reduced face size (Size>>level), not the full face Size.
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, SetDataNullRectAtMipLevelUsesReducedSize)
{
    // size=4, mipMap=true -> levels 4x4, 2x2, 1x1. Level 1 is 2x2 = 4 elements.
    TextureCube tex(gd, 4, true, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(1, 2, 3, 4));
    EXPECT_NO_THROW(tex.SetData(CubeMapFace::PositiveX, 1, nullptr, buf.data(), 0, 4));
}

TEST_F(TextureCubeTest, SetDataNullRectAtMipLevelRejectsFullFaceSizedElementCount)
{
    // Level 1 of a size=4 cube is 2x2 (4 elements) — an elementCount sized for the *full*
    // 4x4 face (16) must be rejected as exceeding the level's actual bounds, proving level is
    // no longer ignored when rect is null.
    TextureCube tex(gd, 4, true, SurfaceFormat::Color);
    std::vector<Color> buf(16, Color(1, 2, 3, 4));
    const Rectangle fullFaceRect(0, 0, 4, 4); // valid for level 0, not level 1
    EXPECT_THROW(tex.SetData(CubeMapFace::PositiveX, 1, &fullFaceRect, buf.data(), 0, 16),
                 std::out_of_range);
}

// -----------------------------------------------------------------------
// GetData — argument guards (mirrors SetData's guards)
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, GetDataSimpleNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, nullptr, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, GetDataStartIndexNegativeStartIndexThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, buf.data(), -1, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectNullDataThrowsInvalidArgument)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 0, nullptr, nullptr, 0, 4), std::invalid_argument);
}

TEST_F(TextureCubeTest, GetDataRectNegativeLevelThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    std::vector<Color> buf(4, Color(0, 0, 0, 0));
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, -1, nullptr, buf.data(), 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectOutOfBoundsThrowsOutOfRange)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(1, 1, 2, 2);
    EXPECT_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 4), std::out_of_range);
}

TEST_F(TextureCubeTest, GetDataRectWithinBoundsDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    Color buf[1] = { Color(0, 0, 0, 0) };
    const Rectangle rect(0, 0, 1, 1);
    EXPECT_NO_THROW(tex.GetData(CubeMapFace::PositiveX, 0, &rect, buf, 0, 1));
}

// -----------------------------------------------------------------------
// Dispose
// -----------------------------------------------------------------------

TEST_F(TextureCubeTest, DisposeMarksResourceDisposed)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    EXPECT_FALSE(tex.getIsDisposedProperty());
    tex.Dispose();
    EXPECT_TRUE(tex.getIsDisposedProperty());
}

TEST_F(TextureCubeTest, DoubleDisposeDoesNotThrow)
{
    TextureCube tex(gd, 2, false, SurfaceFormat::Color);
    tex.Dispose();
    EXPECT_NO_THROW(tex.Dispose());
}
