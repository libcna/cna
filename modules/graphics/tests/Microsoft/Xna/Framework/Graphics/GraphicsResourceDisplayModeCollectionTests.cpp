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
#include "System/ArgumentOutOfRangeException.hpp"
#include <array>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DisplayModeCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::CubeMapFace;
using Microsoft::Xna::Framework::Graphics::DisplayMode;
using Microsoft::Xna::Framework::Graphics::DisplayModeCollection;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;

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

TEST(DisplayModeCollectionTest, IndexOutOfBoundsThrowsArgumentOutOfRange)
{
    DisplayModeCollection col;
    EXPECT_THROW({ [[maybe_unused]] const DisplayMode& m = col[0]; }, System::ArgumentOutOfRangeException);
}

TEST(DisplayModeCollectionTest, NegativeIndexThrowsArgumentOutOfRange)
{
    std::vector<DisplayMode> modes = { DisplayMode(640, 480, SurfaceFormat::Color) };
    DisplayModeCollection col(std::move(modes));
    EXPECT_THROW({ [[maybe_unused]] const DisplayMode& m = col[-1]; }, System::ArgumentOutOfRangeException);
}

TEST(DisplayModeCollectionTest, IndexEqualToCountThrowsArgumentOutOfRange)
{
    std::vector<DisplayMode> modes = { DisplayMode(640, 480, SurfaceFormat::Color) };
    DisplayModeCollection col(std::move(modes));
    EXPECT_THROW({ [[maybe_unused]] const DisplayMode& m = col[1]; }, System::ArgumentOutOfRangeException);
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

TEST(GraphicsResourceTest, CopiedTexturesAreTrackedAndCanOutliveOwningDevice)
{
    std::optional<Texture2D> survivingCopy;
    std::optional<TextureCube> survivingCubeCopy;
    {
        auto device = std::make_unique<GraphicsDevice>();
        const std::size_t baseline = device->GetTrackedResourceCount();
        Texture2D original(*device, 1, 1);
        survivingCopy.emplace(original);
        TextureCube originalCube(*device, 1, false, SurfaceFormat::Color);
        survivingCubeCopy.emplace(originalCube);

        EXPECT_EQ(survivingCopy->getGraphicsDeviceProperty(), device.get());
        EXPECT_FALSE(survivingCopy->getIsDisposedProperty());
        EXPECT_EQ(survivingCubeCopy->getGraphicsDeviceProperty(), device.get());
        EXPECT_FALSE(survivingCubeCopy->getIsDisposedProperty());
        EXPECT_EQ(device->GetTrackedResourceCount(), baseline + 4);
    }

    // HND-TQ-47 R2: the device's disposal released each tracked copy's native object without
    // disposing it (Refresh ReleaseAllDeviceResources IL_009e-00a7); a later use is refused.
    EXPECT_FALSE(survivingCopy->getIsDisposedProperty());
    EXPECT_FALSE(survivingCubeCopy->getIsDisposedProperty());
    EXPECT_FALSE(survivingCopy->HasRenderer());
    Color pixel;
    EXPECT_THROW(survivingCopy->GetData(&pixel, 1), System::ObjectDisposedException);
    std::array<Color, 1> face{};
    EXPECT_THROW(survivingCubeCopy->GetData(CubeMapFace::PositiveX, face.data(), 1),
                 System::ObjectDisposedException);
    EXPECT_NO_THROW(survivingCopy.reset());
    EXPECT_NO_THROW(survivingCubeCopy.reset());
}

TEST(GraphicsResourceTest, CopyAssignedTexturesTransferDeviceTracking)
{
    GraphicsDevice first;
    GraphicsDevice second;
    const std::size_t firstBaseline = first.GetTrackedResourceCount();
    const std::size_t secondBaseline = second.GetTrackedResourceCount();

    {
        Texture2D destination(first, 1, 1);
        Texture2D source(second, 2, 2);
        TextureCube cubeDestination(first, 1, false, SurfaceFormat::Color);
        TextureCube cubeSource(second, 1, false, SurfaceFormat::Color);
        first.getTexturesProperty()(0, &destination);

        EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline + 2);
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 2);

        destination = source;
        EXPECT_EQ(first.getTexturesProperty()[0], nullptr);
        EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline + 1);
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 3);

        cubeDestination = cubeSource;
        EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline);
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 4);

        source = destination;
        cubeSource = cubeDestination;
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 4);

        destination.Dispose();
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 3);
        destination = source;
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 4);
    }

    EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline);
    EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline);
}

TEST(GraphicsResourceTest, CrossDeviceMoveAssignmentTransfersTrackingAndDetachesOldBindings)
{
    GraphicsDevice first;
    GraphicsDevice second;
    const std::size_t firstBaseline = first.GetTrackedResourceCount();
    const std::size_t secondBaseline = second.GetTrackedResourceCount();

    {
        VertexBuffer destination(first, 3);
        VertexBuffer source(second, 4);
        first.SetVertexBuffer(&destination);
        second.SetVertexBuffer(&source);
        destination = std::move(source);
        EXPECT_EQ(first.GetVertexBuffer(), nullptr);
        EXPECT_EQ(second.GetVertexBuffer(), &destination);
        EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline);
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 1);
    }

    {
        IndexBuffer destination(first, 3);
        IndexBuffer source(second, 4);
        first.SetIndexBuffer(&destination);
        second.SetIndexBuffer(&source);
        destination = std::move(source);
        EXPECT_EQ(first.GetIndexBuffer(), nullptr);
        EXPECT_EQ(second.GetIndexBuffer(), &destination);
        EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline);
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 1);
    }

    {
        Texture2D destination(first, 1, 1);
        Texture2D source(second, 2, 2);
        first.getTexturesProperty()(0, &destination);
        second.getTexturesProperty()(0, &source);
        destination = std::move(source);
        EXPECT_EQ(first.getTexturesProperty()[0], nullptr);
        EXPECT_EQ(second.getTexturesProperty()[0], &destination);
        EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline);
        EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline + 1);
    }

    EXPECT_EQ(first.GetTrackedResourceCount(), firstBaseline);
    EXPECT_EQ(second.GetTrackedResourceCount(), secondBaseline);
}

TEST(GraphicsResourceTest, MoveAssignmentRefusesToOverwriteABoundRenderTarget)
{
    GraphicsDevice device;
    RenderTarget2D destination(device, 2, 2);
    RenderTarget2D source(device, 4, 4);
    device.SetRenderTarget(&destination);

    EXPECT_THROW(destination = std::move(source), System::InvalidOperationException);
    ASSERT_EQ(device.GetRenderTargets().size(), 1u);
    EXPECT_EQ(device.GetRenderTargets()[0].getRenderTargetProperty(), &destination);
    EXPECT_EQ(destination.getWidthProperty(), 2);
    EXPECT_EQ(source.getWidthProperty(), 4);

    device.SetRenderTarget(nullptr);
}

TEST(DisplayModeCollectionTest, FilteringAndEnumerationRetainWholeModesDuplicatesAndOrder)
{
    // Refresh get_Item IL_001c-0033 filters only by format and appends in list order;
    // GetEnumerator IL_0000-0010 returns that list. Equal entries are not collapsed.
    const std::vector<DisplayMode> input = {
        DisplayMode(1280, 720, SurfaceFormat::Bgr565),
        DisplayMode(640, 480, SurfaceFormat::Color),
        DisplayMode(1280, 720, SurfaceFormat::Bgr565),
        DisplayMode(1280, 800, SurfaceFormat::Bgr565)};
    const DisplayModeCollection collection(input);
    const std::vector<DisplayMode> enumerated(collection.begin(), collection.end());
    ASSERT_EQ(enumerated.size(), 4u);
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        EXPECT_EQ(enumerated[i].getWidthProperty(), input[i].getWidthProperty());
        EXPECT_EQ(enumerated[i].getHeightProperty(), input[i].getHeightProperty());
        EXPECT_EQ(enumerated[i].getFormatProperty(), input[i].getFormatProperty());
    }
    auto filtered = collection[SurfaceFormat::Bgr565];
    ASSERT_EQ(filtered.size(), 3u);
    const int heights[] = {720, 720, 800};
    for (std::size_t i = 0; i < filtered.size(); ++i)
    {
        EXPECT_EQ(filtered[i].getWidthProperty(), 1280);
        EXPECT_EQ(filtered[i].getHeightProperty(), heights[i]);
        EXPECT_EQ(filtered[i].getFormatProperty(), SurfaceFormat::Bgr565);
    }
    EXPECT_TRUE(collection[static_cast<SurfaceFormat>(-1)].empty());
    filtered.clear();
    EXPECT_EQ(collection.getCountProperty(), 4);
    EXPECT_EQ(collection[SurfaceFormat::Bgr565].size(), 3u);
}
