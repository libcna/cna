// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-011: GraphicsDevice.Present(Nullable<Rectangle>, Nullable<Rectangle>, IntPtr).
//
// XNA's three-argument Present maps straight onto Direct3D 9's own Present, which takes a source
// rectangle, a destination rectangle and a window handle natively; its documentation is explicit
// that a null rectangle means the whole surface and that an oversized one is clipped rather than
// refused, and Present() is exactly Present(null, null, null).
//
// CNA's renderers have no such native call, so the request is validated and clipped by
// GraphicsDevice and then offered to the renderer, which honours what it genuinely can and
// otherwise refuses by name. Two things are therefore worth testing hardest: the resolution
// GraphicsDevice does itself -- the equivalence case, the clipping, the validation, the disposed
// and bound-target checks, all of which must hold under every renderer -- and that a renderer which
// cannot honour part of the request says so instead of presenting the whole frame and calling it
// done.
//
// Which outcome a valid request gets is a property of the active renderer, so the cases below split
// into renderer-neutral ones (a bad rectangle is always an argument error; a good one is never one)
// and per-renderer ones that assert what HEADLESS, SOFTWARE and a renderer with no region support
// each actually do.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    /// The backbuffer every case below reasons about, so a clipping expectation has a known surface.
    GraphicsDevice MakeDevice(const int width = 320, const int height = 240)
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(width);
        parameters.setBackBufferHeightProperty(height);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
                              parameters);
    }

    /**
     * A request naming real pixels is either honoured or refused by name -- never rejected as a bad
     * argument. Asserting that rather than `EXPECT_NO_THROW` keeps the validation cases meaningful
     * under a renderer with no region support, where an outright refusal is the correct answer.
     */
    void ExpectValidatedAndOffered(GraphicsDevice& device, const std::optional<Rectangle>& source,
                                   const std::optional<Rectangle>& destination,
                                   const std::uintptr_t window = 0)
    {
        try
        {
            device.Present(source, destination, window);
        }
        catch (const System::ArgumentException& error)
        {
            FAIL() << "a request naming real pixels must not be an argument error: "
                   << error.what();
        }
        catch (const System::NotSupportedException&)
        {
            // The renderer cannot honour it; that is a renderer property, tested per renderer below.
        }
    }

    // Renderer-neutral capability gate, in the established form: a renderer with no real
    // RenderTarget2D storage refuses SetRenderTarget itself, long before this file's subject.
    bool BindOrSkip(GraphicsDevice& device, RenderTarget2D& target)
    {
        try
        {
            device.SetRenderTarget(&target);
            return true;
        }
        catch (const System::NotSupportedException&)
        {
            return false;
        }
    }
}

// =============================================================================
// The equivalence case: this IS Present(), under every renderer
// =============================================================================

TEST(GraphicsDevicePresentRegionTest, NullNullZeroIsExactlyPresent)
{
    // XNA's Present() is Present(null, null, null), so the three-argument form with nothing
    // specified must take the ordinary path -- which every renderer supports, including ones that
    // can honour no part of a region request at all.
    GraphicsDevice device = MakeDevice();
    EXPECT_NO_THROW(device.Present());
    EXPECT_NO_THROW(device.Present(std::nullopt, std::nullopt, 0));

    // Repeatedly, and interleaved with the no-argument form, since both are the same present.
    EXPECT_NO_THROW(device.Present(std::nullopt, std::nullopt, 0));
    EXPECT_NO_THROW(device.Present());
}

// =============================================================================
// Validation and clipping, which GraphicsDevice does itself for every renderer
// =============================================================================

TEST(GraphicsDevicePresentRegionTest, AnOversizedRectangleIsClippedRatherThanRefused)
{
    // XNA's documented behaviour: "If the rectangle exceeds the source surface, the rectangle is
    // clipped to the source surface." So this names real pixels and is not an argument error.
    GraphicsDevice device = MakeDevice(320, 240);
    ExpectValidatedAndOffered(device, Rectangle(0, 0, 4096, 4096), std::nullopt);
    ExpectValidatedAndOffered(device, Rectangle(-100, -100, 8192, 8192), std::nullopt);
    ExpectValidatedAndOffered(device, Rectangle(300, 200, 1000, 1000), std::nullopt);
}

TEST(GraphicsDevicePresentRegionTest, ARectangleWithNoAreaIsRefused)
{
    // A zero or negative extent names no pixels, so there is nothing to clip and nothing to present;
    // that is a caller mistake rather than a whole-surface present. Checked before the renderer is
    // consulted, so it holds even where the renderer would refuse the request anyway.
    GraphicsDevice device = MakeDevice();
    EXPECT_THROW(device.Present(Rectangle(0, 0, 0, 10), std::nullopt, 0), System::ArgumentException);
    EXPECT_THROW(device.Present(Rectangle(0, 0, 10, 0), std::nullopt, 0), System::ArgumentException);
    EXPECT_THROW(device.Present(Rectangle(0, 0, -5, 10), std::nullopt, 0), System::ArgumentException);
    EXPECT_THROW(device.Present(std::nullopt, Rectangle(0, 0, 10, -1), 0), System::ArgumentException);
}

TEST(GraphicsDevicePresentRegionTest, ARectangleEntirelyOutsideTheSurfaceIsRefused)
{
    // Clipping such a rectangle leaves nothing, and presenting nothing is not what was asked for.
    GraphicsDevice device = MakeDevice(320, 240);
    EXPECT_THROW(device.Present(Rectangle(320, 0, 10, 10), std::nullopt, 0),
                 System::ArgumentException);
    EXPECT_THROW(device.Present(Rectangle(0, 240, 10, 10), std::nullopt, 0),
                 System::ArgumentException);
    EXPECT_THROW(device.Present(Rectangle(-50, 0, 20, 10), std::nullopt, 0),
                 System::ArgumentException);
}

TEST(GraphicsDevicePresentRegionTest, AValidSubRectangleIsNeverAnArgumentError)
{
    GraphicsDevice device = MakeDevice(320, 240);
    ExpectValidatedAndOffered(device, Rectangle(0, 0, 320, 240), std::nullopt);
    ExpectValidatedAndOffered(device, Rectangle(16, 16, 64, 32), std::nullopt);
    ExpectValidatedAndOffered(device, Rectangle(319, 239, 1, 1), std::nullopt);
}

// =============================================================================
// The checks Present() itself makes, which this overload must make too
// =============================================================================

TEST(GraphicsDevicePresentRegionTest, ADisposedDeviceIsRefused)
{
    GraphicsDevice device = MakeDevice();
    device.Dispose();

    EXPECT_THROW(device.Present(Rectangle(0, 0, 16, 16), std::nullopt, 0),
                 System::ObjectDisposedException);
    EXPECT_THROW(device.Present(std::nullopt, std::nullopt, 42), System::ObjectDisposedException);
}

TEST(GraphicsDevicePresentRegionTest, PresentingWithRenderTargetsBoundIsRefused)
{
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device, 64, 64);
    if (!BindOrSkip(device, target))
        GTEST_SKIP() << "this renderer does not support RenderTarget2D";

    EXPECT_THROW(device.Present(Rectangle(0, 0, 16, 16), std::nullopt, 0),
                 System::InvalidOperationException);
    EXPECT_THROW(device.Present(std::nullopt, std::nullopt, 7), System::InvalidOperationException);

    // And the no-argument form still refuses too, so the two agree about the bound target.
    EXPECT_THROW(device.Present(), System::InvalidOperationException);

    // Unbinding makes it an ordinary request again -- an InvalidOperationException here would mean
    // the check had latched rather than describing the current binding.
    device.SetRenderTarget(nullptr);
    ExpectValidatedAndOffered(device, Rectangle(0, 0, 16, 16), std::nullopt);
}

// =============================================================================
// What each renderer actually answers
// =============================================================================

TEST(GraphicsDevicePresentRegionTest, HeadlessAcceptsEveryRequestBecauseItPresentsNothing)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Headless);

    // There is no frame to show part of and no window to show it in, so no part of the request can
    // be got wrong. Accepting is honest here in a way it would not be for a renderer that displays.
    GraphicsDevice device = MakeDevice(320, 240);
    EXPECT_NO_THROW(device.Present(Rectangle(8, 8, 32, 32), std::nullopt, 0));
    EXPECT_NO_THROW(device.Present(std::nullopt, Rectangle(0, 0, 100, 100), 0));
    EXPECT_NO_THROW(device.Present(std::nullopt, std::nullopt, 0x1234));
    EXPECT_NO_THROW(device.Present(Rectangle(8, 8, 32, 32), Rectangle(0, 0, 64, 64), 0x1234));
}

TEST(GraphicsDevicePresentRegionTest, SoftwareHonoursASourceRectangleAndRefusesTheRest)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Software);

    GraphicsDevice device = MakeDevice(320, 240);

    // A CPU rasteriser can present a sub-rectangle of its own backbuffer exactly, and does.
    EXPECT_NO_THROW(device.Present(Rectangle(16, 16, 64, 32), std::nullopt, 0));
    EXPECT_NO_THROW(device.Present(Rectangle(0, 0, 320, 240), std::nullopt, 0));
    EXPECT_NO_THROW(device.Present(Rectangle(0, 0, 4096, 4096), std::nullopt, 0));

    // A destination rectangle and a foreign window have no equivalent in the platform surface
    // presenter's contract, so they are refused rather than silently dropped.
    EXPECT_THROW(device.Present(std::nullopt, Rectangle(0, 0, 100, 100), 0),
                 System::NotSupportedException);
    EXPECT_THROW(device.Present(std::nullopt, std::nullopt, 0x1234), System::NotSupportedException);
    EXPECT_THROW(device.Present(Rectangle(16, 16, 64, 32), Rectangle(0, 0, 64, 32), 0),
                 System::NotSupportedException);
}

TEST(GraphicsDevicePresentRegionTest, ARendererThatCannotHonourTheRequestRefusesItByName)
{
    // A renderer that does not implement the additive region capability refuses every part of it,
    // which is the shared default. STUB is such a renderer, and its refusal must name itself and
    // say what was asked for rather than presenting the whole frame as if it had complied.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::Stub);

    GraphicsDevice device = MakeDevice(320, 240);
    EXPECT_NO_THROW(device.Present(std::nullopt, std::nullopt, 0))
        << "the equivalence case never reaches the capability";

    try
    {
        device.Present(Rectangle(0, 0, 16, 16), std::nullopt, 0);
        FAIL() << "a renderer with no region support must refuse";
    }
    catch (const System::NotSupportedException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("STUB"), std::string::npos) << "the refusal names the renderer";
        EXPECT_NE(message.find("source rectangle"), std::string::npos)
            << "and says which part it could not honour";
    }

    EXPECT_THROW(device.Present(std::nullopt, Rectangle(0, 0, 16, 16), 0),
                 System::NotSupportedException);
    EXPECT_THROW(device.Present(std::nullopt, std::nullopt, 0x99), System::NotSupportedException);
}
