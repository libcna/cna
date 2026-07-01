// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "System/IO/MemoryStream.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using System::IO::MemoryStream;

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

// -----------------------------------------------------------------------
// FromStream — format support verification (Task 262)
//
// Round-trips through Texture2D::SaveAsPng/SaveAsJpeg (PNG/JPEG) and a
// hand-built minimal file (BMP) to empirically confirm which encoded
// formats Texture2D::FromStream can decode via the linked SDL3_image build.
// -----------------------------------------------------------------------

namespace
{
    // Minimal uncompressed 24bpp BMP, solid colour, no padding beyond the
    // mandatory 4-byte row alignment. width/height must keep row bytes a
    // multiple of 4 for this helper's simplicity (e.g. 2x2 uses 2-byte padding).
    std::vector<std::uint8_t> BuildSolidColorBmp(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const int rowBytes = w * 3;
        const int rowPad = (4 - (rowBytes % 4)) % 4;
        const int rowStride = rowBytes + rowPad;
        const int pixelDataSize = rowStride * h;
        const int pixelDataOffset = 14 + 40;
        const int fileSize = pixelDataOffset + pixelDataSize;

        std::vector<std::uint8_t> buf(static_cast<std::size_t>(fileSize), 0);

        auto w32 = [&](int off, std::uint32_t v) {
            buf[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
            buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
            buf[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
            buf[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
        };
        auto w16 = [&](int off, std::uint16_t v) {
            buf[off + 0] = static_cast<std::uint8_t>(v & 0xFF);
            buf[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
        };

        // BITMAPFILEHEADER (14 bytes)
        buf[0] = 'B'; buf[1] = 'M';
        w32(2, static_cast<std::uint32_t>(fileSize));
        w32(10, static_cast<std::uint32_t>(pixelDataOffset));

        // BITMAPINFOHEADER (40 bytes)
        w32(14, 40);
        w32(18, static_cast<std::uint32_t>(w));
        w32(22, static_cast<std::uint32_t>(h)); // positive height => bottom-up rows
        w16(26, 1);   // planes
        w16(28, 24);  // bitCount
        w32(30, 0);   // compression = BI_RGB

        for (int row = 0; row < h; ++row)
        {
            const int base = pixelDataOffset + row * rowStride;
            for (int col = 0; col < w; ++col)
            {
                buf[base + col * 3 + 0] = b;
                buf[base + col * 3 + 1] = g;
                buf[base + col * 3 + 2] = r;
            }
        }
        return buf;
    }
}

class Texture2DFromStreamFormatTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;

    static bool IsCloseTo(Color c, std::uint8_t r, std::uint8_t g, std::uint8_t b, int tolerance)
    {
        return std::abs(c.getRProperty() - r) <= tolerance &&
               std::abs(c.getGProperty() - g) <= tolerance &&
               std::abs(c.getBProperty() - b) <= tolerance;
    }
};

TEST_F(Texture2DFromStreamFormatTest, PngRoundTripDecodesCorrectSizeAndColor)
{
    Texture2D src(gd, 4, 4);
    std::vector<Color> red(16, Color(255, 0, 0, 255));
    src.SetData(red.data(), 16);

    MemoryStream writeStream;
    src.SaveAsPng(&writeStream, 4, 4);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
    Color px[1] = { Color(0, 0, 0, 0) };
    loaded.GetData(px, 0, 1);
    EXPECT_TRUE(IsCloseTo(px[0], 255, 0, 0, 5)); // PNG is lossless
}

TEST_F(Texture2DFromStreamFormatTest, JpegRoundTripDecodesCorrectSizeAndColor)
{
    Texture2D src(gd, 4, 4);
    std::vector<Color> green(16, Color(0, 255, 0, 255));
    src.SetData(green.data(), 16);

    MemoryStream writeStream;
    src.SaveAsJpeg(&writeStream, 4, 4);
    auto bytes = writeStream.GetBuffer();

    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
    Color px[1] = { Color(0, 0, 0, 0) };
    loaded.GetData(px, 0, 1);
    EXPECT_TRUE(IsCloseTo(px[0], 0, 255, 0, 40)); // JPEG is lossy — wider tolerance
}

TEST_F(Texture2DFromStreamFormatTest, BmpDecodesCorrectSizeAndColor)
{
    auto bytes = BuildSolidColorBmp(2, 2, 0, 0, 255); // solid blue
    MemoryStream readStream(bytes.data(), static_cast<System::IO::intcs>(bytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream);

    EXPECT_EQ(loaded.getWidthProperty(), 2);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
    Color px[1] = { Color(0, 0, 0, 0) };
    loaded.GetData(px, 0, 1);
    EXPECT_TRUE(IsCloseTo(px[0], 0, 0, 255, 0)); // BMP is uncompressed — exact
}

// -----------------------------------------------------------------------
// FromStream(device, stream, width, height, zoom) — resize/crop overload (Task 262)
//
// Source is an 8x4 (landscape) solid-colour PNG so the fit-vs-cover branch in
// the width/height computation is exercised (matches FNA3D_Image_Load's
// forceW/forceH/zoom logic — see Texture2D.cpp).
// -----------------------------------------------------------------------

class Texture2DFromStreamResizeTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
    std::vector<std::uint8_t> pngBytes;

    void SetUp() override
    {
        Texture2D src(gd, 8, 4);
        std::vector<Color> yellow(32, Color(255, 255, 0, 255));
        src.SetData(yellow.data(), 32);

        MemoryStream writeStream;
        src.SaveAsPng(&writeStream, 8, 4);
        pngBytes = writeStream.GetBuffer();
    }
};

TEST_F(Texture2DFromStreamResizeTest, FitPreservesAspectRatio)
{
    // scaleWidth = (8>4) = true; scale = 4/8 = 0.5 -> finalW=4, finalH=2.
    MemoryStream readStream(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream, 4, 4, false);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 2);
}

TEST_F(Texture2DFromStreamResizeTest, ZoomFillsExactRequestedSize)
{
    MemoryStream readStream(pngBytes.data(), static_cast<System::IO::intcs>(pngBytes.size()));
    Texture2D loaded = Texture2D::FromStream(gd, readStream, 4, 4, true);

    EXPECT_EQ(loaded.getWidthProperty(), 4);
    EXPECT_EQ(loaded.getHeightProperty(), 4);
}
