// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

// -----------------------------------------------------------------------
// Default constructor — dimensions and base-class properties
// -----------------------------------------------------------------------

TEST(Texture2DTest, DefaultConstructorWidthIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getWidthProperty(), 0);
}

TEST(Texture2DTest, DefaultConstructorHeightIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getHeightProperty(), 0);
}

TEST(Texture2DTest, DefaultConstructorFormatIsColor)
{
    Texture2D tex;
    EXPECT_EQ(tex.getFormatProperty(), SurfaceFormat::Color);
}

TEST(Texture2DTest, DefaultConstructorLevelCountIsOne)
{
    Texture2D tex;
    EXPECT_EQ(tex.getLevelCountProperty(), 1);
}

// -----------------------------------------------------------------------
// getBoundsProperty
// -----------------------------------------------------------------------

TEST(Texture2DTest, DefaultBoundsXIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().X, 0);
}

TEST(Texture2DTest, DefaultBoundsYIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().Y, 0);
}

TEST(Texture2DTest, DefaultBoundsWidthIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().Width, 0);
}

TEST(Texture2DTest, DefaultBoundsHeightIsZero)
{
    Texture2D tex;
    EXPECT_EQ(tex.getBoundsProperty().Height, 0);
}

// -----------------------------------------------------------------------
// Copy / move semantics
// -----------------------------------------------------------------------

TEST(Texture2DTest, CopyConstructorPreservesWidth)
{
    Texture2D src;
    Texture2D dst(src);
    EXPECT_EQ(dst.getWidthProperty(), src.getWidthProperty());
}

TEST(Texture2DTest, CopyConstructorPreservesHeight)
{
    Texture2D src;
    Texture2D dst(src);
    EXPECT_EQ(dst.getHeightProperty(), src.getHeightProperty());
}

TEST(Texture2DTest, MoveConstructorPreservesWidth)
{
    Texture2D src;
    Texture2D dst(std::move(src));
    EXPECT_EQ(dst.getWidthProperty(), 0);
}

TEST(Texture2DTest, CopyAssignmentPreservesFormat)
{
    Texture2D src;
    Texture2D dst;
    dst = src;
    EXPECT_EQ(dst.getFormatProperty(), SurfaceFormat::Color);
}

// -----------------------------------------------------------------------
// GetData(Color*, int startIndex, int elementCount) — error guards
// -----------------------------------------------------------------------

TEST(Texture2DTest, GetDataNullPtrThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.GetData(nullptr, 0, 1), std::invalid_argument);
}

TEST(Texture2DTest, GetDataZeroElementCountThrowsInvalidArgument)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(buf, 0, 0), std::invalid_argument);
}

TEST(Texture2DTest, GetDataNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(buf, 0, 1), std::runtime_error);
}

// 2-param overload delegates to 3-param; same guards apply
TEST(Texture2DTest, GetData2ParamNullPtrThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.GetData(nullptr, 1), std::invalid_argument);
}

TEST(Texture2DTest, GetData2ParamNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(buf, 1), std::runtime_error);
}

// -----------------------------------------------------------------------
// GetData(int level, const Rectangle*, Color*, int, int) — error guards
// -----------------------------------------------------------------------

TEST(Texture2DTest, GetDataLevelNullDataThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.GetData(0, nullptr, nullptr, 0, 1), std::invalid_argument);
}

TEST(Texture2DTest, GetDataLevelZeroElementCountThrowsInvalidArgument)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(0, nullptr, buf, 0, 0), std::invalid_argument);
}

TEST(Texture2DTest, GetDataNegativeLevelThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_THROW(tex.GetData(-1, nullptr, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, GetDataLevelNoCpuPixelsThrowsRuntimeError)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    // getMipBufferConst(0) returns nullptr when cpuPixels_ is empty
    EXPECT_THROW(tex.GetData(0, nullptr, buf, 0, 1), std::runtime_error);
}

// -----------------------------------------------------------------------
// SetData(const Color*, int) — no backend, returns early (no throw)
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataSimpleWithNullDataDoesNotThrow)
{
    // graphicsDevice_ is null → early return, null data check skipped
    Texture2D tex;
    EXPECT_NO_THROW(tex.SetData(nullptr, 0));
}

TEST(Texture2DTest, SetDataSimpleWithZeroCountDoesNotThrow)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.SetData(buf, 0));
}

// -----------------------------------------------------------------------
// SetData(int level, const Rectangle*, const Color*, int, int) — error guards
//
// These validations fire before touching the CPU pixel buffer, so they are
// safe to test even on a default-constructed (zero-sized) Texture2D.
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataLevelNullDataThrowsInvalidArgument)
{
    Texture2D tex;
    EXPECT_THROW(tex.SetData(0, nullptr, nullptr, 0, 1), std::invalid_argument);
}

TEST(Texture2DTest, SetDataLevelZeroElementCountThrowsInvalidArgument)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(0, nullptr, buf, 0, 0), std::invalid_argument);
}

TEST(Texture2DTest, SetDataLevelNegativeStartIndexThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(0, nullptr, buf, -1, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataNegativeLevelThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(-1, nullptr, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelExtraElementsDoesNotThrow)
{
    // Default texture: mipDim(0,0)=1, effective region is 1×1 = 1 pixel.
    // Providing elementCount=2 (> region size) is allowed — XNA ignores extras.
    Texture2D tex;
    Color buf[2] = { Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.SetData(0, nullptr, buf, 0, 2));
}

TEST(Texture2DTest, SetDataLevelInsufficientElementsThrowsOutOfRange)
{
    // Default texture: mipDim(0,0)=1, effective region is 1×1 = 1 pixel.
    // Providing elementCount=0 is rejected by the elementCount <= 0 guard above,
    // but that already throws invalid_argument. Rectangle(0,0,2,1) also exceeds
    // levelW=1 (x+w=2>1), so the rect-bounds guard fires first here — both guards
    // throw std::out_of_range, so this still exercises the same failure mode.
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle wide(0, 0, 2, 1);
    EXPECT_THROW(tex.SetData(0, &wide, buf, 0, 1), std::out_of_range);
}

// -----------------------------------------------------------------------
// SetData(int level, const Rectangle*, ...) — rect-bounds guard (Task 266)
//
// Mirrors the equivalent GetData bounds check (rectangle out of texture bounds).
// Fixes a heap buffer overflow write: prior to this guard, a caller-supplied
// rect that exceeded the mip level's dimensions would write past the end of
// the CPU-side mip buffer (found in the Task 261 Texture2D audit).
// -----------------------------------------------------------------------

TEST(Texture2DTest, SetDataLevelRectXOutOfBoundsThrowsOutOfRange)
{
    // Default texture: levelW=levelH=1 (mipDim clamp). x+w=1+1=2 > levelW=1.
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(1, 0, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectYOutOfBoundsThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, 1, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectNegativeXThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(-1, 0, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectNegativeYThrowsOutOfRange)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, -1, 1, 1);
    EXPECT_THROW(tex.SetData(0, &rect, buf, 0, 1), std::out_of_range);
}

TEST(Texture2DTest, SetDataLevelRectWithinBoundsDoesNotThrow)
{
    Texture2D tex;
    Color buf[1] = { Color(0,0,0,0) };
    const Rectangle rect(0, 0, 1, 1);
    EXPECT_NO_THROW(tex.SetData(0, &rect, buf, 0, 1));
}

// -----------------------------------------------------------------------
// SetData(const Color*, int elementCount) — undersized-buffer guard (Task 266)
//
// Fixes a heap buffer overflow read: prior to this guard, calling SetData
// with fewer elements than width*height built an ImageData that claimed the
// full texture dimensions over an undersized pixel buffer, which the EasyGL
// backend's set_image_2d then over-read (found in the Task 261 audit).
// Requires a real GraphicsDevice + backend, since the guard only runs when
// graphicsDevice_ is non-null.
// -----------------------------------------------------------------------

class SetDataSimpleGuardTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

TEST_F(SetDataSimpleGuardTest, InsufficientElementCountThrowsOutOfRange)
{
    Texture2D tex(gd, 4, 4);
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_THROW(tex.SetData(buf, 4), std::out_of_range);
}

TEST_F(SetDataSimpleGuardTest, ExactElementCountDoesNotThrow)
{
    Texture2D tex(gd, 2, 2);
    Color buf[4] = { Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0), Color(0,0,0,0) };
    EXPECT_NO_THROW(tex.SetData(buf, 4));
}
