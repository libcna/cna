// SPDX-License-Identifier: MS-PL
//
// Task 131: unit tests for GraphicsAdapter, GraphicsResource, DisplayMode,
// DisplayModeCollection, and GraphicsDeviceStatus.
//
// GraphicsDeviceStatus enum: already covered in GraphicsDeviceStatusTests.cpp.
// DisplayMode properties / equality: already covered in DisplayModeTests.cpp.
// GraphicsAdapter static factory (getDefaultAdapterProperty / getAdaptersProperty):
//   requires SDL to be initialized — covered by integration tests.
//
// This file adds:
//   - DisplayMode::GetTypeName (gap in DisplayModeTests.cpp)
//   - DisplayModeCollection: all public methods
//   - GraphicsResource: all public methods, tested via Texture2D (which inherits
//     GraphicsResource through Texture).

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::DisplayModeCollection;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

// -----------------------------------------------------------------------
// DisplayMode — GetTypeName (gap from DisplayModeTests.cpp)
// -----------------------------------------------------------------------

TEST(DisplayModeTest, GetTypeNameReturnsExpectedString)
{
    DisplayMode dm;
    EXPECT_EQ(dm.GetTypeName(), "Microsoft.Xna.Framework.Graphics.DisplayMode");
}

// -----------------------------------------------------------------------
// DisplayModeCollection — constructors
// -----------------------------------------------------------------------

TEST(DisplayModeCollectionTest, DefaultConstructorCountIsZero)
{
    DisplayModeCollection col;
    EXPECT_EQ(col.getCountProperty(), 0);
}

TEST(DisplayModeCollectionTest, VectorConstructorSetsCount)
{
    std::vector<DisplayMode> modes = {
        DisplayMode(1920, 1080, SurfaceFormat::Color),
        DisplayMode(1280,  720, SurfaceFormat::Color),
    };
    DisplayModeCollection col(std::move(modes));
    EXPECT_EQ(col.getCountProperty(), 2);
}

// -----------------------------------------------------------------------
// DisplayModeCollection — operator[](int)
// -----------------------------------------------------------------------

TEST(DisplayModeCollectionTest, IndexReturnsCorrectMode)
{
    std::vector<DisplayMode> modes = {
        DisplayMode(800,  600, SurfaceFormat::Color),
        DisplayMode(1024, 768, SurfaceFormat::Color),
    };
    DisplayModeCollection col(std::move(modes));
    EXPECT_EQ(col[0].getWidthProperty(),  800);
    EXPECT_EQ(col[0].getHeightProperty(), 600);
    EXPECT_EQ(col[1].getWidthProperty(),  1024);
    EXPECT_EQ(col[1].getHeightProperty(), 768);
}

TEST(DisplayModeCollectionTest, IndexOutOfBoundsThrowsOutOfRange)
{
    DisplayModeCollection col;
    EXPECT_THROW({ [[maybe_unused]] const DisplayMode& m = col[0]; }, std::out_of_range);
}

TEST(DisplayModeCollectionTest, NegativeIndexThrowsOutOfRange)
{
    std::vector<DisplayMode> modes = { DisplayMode(640, 480, SurfaceFormat::Color) };
    DisplayModeCollection col(std::move(modes));
    EXPECT_THROW({ [[maybe_unused]] const DisplayMode& m = col[-1]; }, std::out_of_range);
}

TEST(DisplayModeCollectionTest, IndexEqualToCountThrowsOutOfRange)
{
    std::vector<DisplayMode> modes = { DisplayMode(640, 480, SurfaceFormat::Color) };
    DisplayModeCollection col(std::move(modes));
    EXPECT_THROW({ [[maybe_unused]] const DisplayMode& m = col[1]; }, std::out_of_range);
}

// -----------------------------------------------------------------------
// DisplayModeCollection — operator[](SurfaceFormat) (Task 347: FNA's real
// this[SurfaceFormat] indexer, missing from CNA until now — found during
// Task 345's GraphicsAdapter audit)
// -----------------------------------------------------------------------

TEST(DisplayModeCollectionTest, IndexBySurfaceFormatReturnsOnlyMatchingModesInOriginalOrder)
{
    std::vector<DisplayMode> modes = {
        DisplayMode(800,  600,  SurfaceFormat::Color),
        DisplayMode(1024, 768,  SurfaceFormat::Bgr565),
        DisplayMode(1280, 720,  SurfaceFormat::Color),
    };
    DisplayModeCollection col(std::move(modes));

    std::vector<DisplayMode> colorModes = col[SurfaceFormat::Color];
    ASSERT_EQ(colorModes.size(), 2u);
    EXPECT_EQ(colorModes[0].getWidthProperty(), 800);
    EXPECT_EQ(colorModes[1].getWidthProperty(), 1280);
}

TEST(DisplayModeCollectionTest, IndexBySurfaceFormatReturnsEmptyWhenNoModeMatches)
{
    std::vector<DisplayMode> modes = { DisplayMode(800, 600, SurfaceFormat::Color) };
    DisplayModeCollection col(std::move(modes));

    EXPECT_TRUE(col[SurfaceFormat::Bgr565].empty());
}

TEST(DisplayModeCollectionTest, IndexBySurfaceFormatOnEmptyCollectionReturnsEmpty)
{
    DisplayModeCollection col;
    EXPECT_TRUE(col[SurfaceFormat::Color].empty());
}

// -----------------------------------------------------------------------
// DisplayModeCollection — range-for iteration
// -----------------------------------------------------------------------

TEST(DisplayModeCollectionTest, RangeForIteratesAllModes)
{
    std::vector<DisplayMode> modes = {
        DisplayMode(640,  480, SurfaceFormat::Color),
        DisplayMode(1280, 720, SurfaceFormat::Color),
        DisplayMode(1920, 1080, SurfaceFormat::Color),
    };
    DisplayModeCollection col(std::move(modes));

    std::vector<int> widths;
    for (const DisplayMode& m : col)
        widths.push_back(m.getWidthProperty());

    ASSERT_EQ(widths.size(), 3u);
    EXPECT_EQ(widths[0],  640);
    EXPECT_EQ(widths[1], 1280);
    EXPECT_EQ(widths[2], 1920);
}

TEST(DisplayModeCollectionTest, RangeForOnEmptyDoesNotIterate)
{
    DisplayModeCollection col;
    int count = 0;
    for ([[maybe_unused]] const DisplayMode& m : col)
        ++count;
    EXPECT_EQ(count, 0);
}

// -----------------------------------------------------------------------
// DisplayModeCollection — GetTypeName
// -----------------------------------------------------------------------

TEST(DisplayModeCollectionTest, GetTypeNameReturnsExpectedString)
{
    DisplayModeCollection col;
    EXPECT_EQ(col.GetTypeName(),
              "Microsoft.Xna.Framework.Graphics.DisplayModeCollection");
}

// -----------------------------------------------------------------------
// GraphicsResource — tested through Texture2D (default constructor chains
// through Texture → GraphicsResource(nullptr))
// -----------------------------------------------------------------------

TEST(GraphicsResourceTest, DefaultNotDisposed)
{
    Texture2D tex;
    EXPECT_FALSE(tex.getIsDisposedProperty());
}

TEST(GraphicsResourceTest, DefaultGraphicsDeviceIsNull)
{
    Texture2D tex;
    EXPECT_EQ(tex.getGraphicsDeviceProperty(), nullptr);
}

TEST(GraphicsResourceTest, DefaultNameIsEmpty)
{
    Texture2D tex;
    EXPECT_EQ(tex.getNameProperty(), "");
}

TEST(GraphicsResourceTest, DefaultTagIsNull)
{
    Texture2D tex;
    EXPECT_EQ(tex.getTagProperty(), nullptr);
}

TEST(GraphicsResourceTest, SetNameRoundTrip)
{
    Texture2D tex;
    tex.setNameProperty("MyTexture");
    EXPECT_EQ(tex.getNameProperty(), "MyTexture");
}

TEST(GraphicsResourceTest, SetTagRoundTrip)
{
    Texture2D tex;
    Texture2D tagObj;
    tex.setTagProperty(&tagObj);
    EXPECT_EQ(tex.getTagProperty(), &tagObj);
}

TEST(GraphicsResourceTest, DisposeMarksDisposed)
{
    Texture2D tex;
    ASSERT_FALSE(tex.getIsDisposedProperty());
    tex.Dispose();
    EXPECT_TRUE(tex.getIsDisposedProperty());
}

TEST(GraphicsResourceTest, DisposeTwiceIsNoOp)
{
    Texture2D tex;
    tex.Dispose();
    EXPECT_NO_THROW(tex.Dispose());
    EXPECT_TRUE(tex.getIsDisposedProperty());
}
