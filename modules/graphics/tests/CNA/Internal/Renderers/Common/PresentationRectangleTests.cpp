// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu.md WEBGPU-162 -- the PHYSICAL presentation rectangle, asked of every renderer
// that has one.
//
// `GraphicsDevice::UpdateViewportFromWindow()` pushes `IGraphicsRenderer::GetDefaultViewportRect()`
// to the native viewport. The interface's default implementation returns `(0, 0, GetViewportSize())`
// -- and on any renderer with a virtual resolution `GetViewportSize()` is the LOGICAL size. So a
// renderer that inherits the default hands a logical rectangle to a call that wants physical device
// pixels, and under Letterbox or Overscan every draw lands in a logical-sized rectangle at the
// window ORIGIN instead of the computed, centred, scaled one. `Clear` is viewport-independent and
// covers the whole drawable, so the symptom is a game rendered into a corner of a correctly cleared
// window: reported against galaxy-eggbert on 2026-08-21 as "resizing the window or going fullscreen
// did not enlarge the game", which is why `EasyGLSurfaceState`, `OpenGL2Renderer` and
// `SdlGpuRenderer` each override it.
//
// WHY THIS IS A RENDERER-LEVEL TEST AND NOT A RESIZE TEST. `GetDefaultViewportRect()` is a pure
// function of the physical drawable size, the virtual resolution and the presentation mode. Driving
// it through a real window resize adds an asynchronous X11 round trip that self-heals over several
// frames (see `viewport_reset_after_resize_test.cpp`'s own note), turning a exact statement into a
// poll loop. Setting the virtual resolution and the mode directly exercises the same function and
// gives an exact, deterministic answer -- and the physical size is MEASURED from the renderer
// rather than assumed, by asking it under `NativeBackBuffer` first, so the expectation cannot
// disagree with the window the test actually got.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace
{
    using CNA::Internal::Renderers::CnaPresentationMode;
    using CNA::Internal::Renderers::IGraphicsRenderer;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    using namespace ::CNA::Testing::Renderers;   // NOLINT(google-build-using-namespace)

    /// The renderers that implement a real virtual resolution, i.e. whose `GetDefaultViewportRect()`
    /// can differ from the whole drawable at all. A renderer with no scaling has nothing to centre,
    /// and asserting a centred rectangle there would be asserting someone else's contract.
    [[nodiscard]] bool HasVirtualResolution()
    {
        return CNA_RENDERER_IS(OpenGLES2, OpenGLES3, OpenGL33, WebGL1, WebGL2, OpenGL2, SdlGpu,
                               WebGPU);
    }

    [[nodiscard]] std::string RendererName()
    {
        return std::string(CNA::getGraphicsRendererName(
            CNA::GraphicsRendererSelection::GetSelected()));
    }

    struct Rect
    {
        int x = 0, y = 0, width = 0, height = 0;

        [[nodiscard]] std::string Describe() const
        {
            return '(' + std::to_string(x) + ',' + std::to_string(y) + ',' +
                   std::to_string(width) + 'x' + std::to_string(height) + ')';
        }
        [[nodiscard]] bool operator==(const Rect& other) const
        {
            return x == other.x && y == other.y &&
                   width == other.width && height == other.height;
        }
    };

    [[nodiscard]] Rect DefaultViewportRect(IGraphicsRenderer& renderer)
    {
        Rect rect;
        renderer.GetDefaultViewportRect(rect.x, rect.y, rect.width, rect.height);
        return rect;
    }

    /// The rectangle a uniform-scale mode must produce, computed from the MEASURED physical size.
    [[nodiscard]] Rect ExpectedUniformScaleRect(const Rect& physical, int virtualWidth,
                                                int virtualHeight, bool cover)
    {
        const double sx = static_cast<double>(physical.width) / virtualWidth;
        const double sy = static_cast<double>(physical.height) / virtualHeight;
        const double scale = cover ? std::max(sx, sy) : std::min(sx, sy);
        Rect rect;
        rect.width = static_cast<int>(std::lround(virtualWidth * scale));
        rect.height = static_cast<int>(std::lround(virtualHeight * scale));
        rect.x = static_cast<int>(std::lround((physical.width - virtualWidth * scale) * 0.5));
        rect.y = static_cast<int>(std::lround((physical.height - virtualHeight * scale) * 0.5));
        return rect;
    }
}

// ---------------------------------------------------------------------------
// The physical size this renderer actually has, and the invariant every mode shares: the default
// rectangle is in DEVICE PIXELS. NativeBackBuffer is the control -- it scales nothing, so its
// rectangle must be the whole drawable at the origin, and everything below is measured against it.
// ---------------------------------------------------------------------------
TEST(PresentationRectangleTest, NativeBackBufferIsTheWholeDrawableAtTheOrigin)
{
    if (!HasVirtualResolution())
        GTEST_SKIP() << RendererName() << " implements no virtual resolution, so it has no "
                                          "presentation rectangle distinct from its drawable";
    GraphicsDevice device;
    IGraphicsRenderer& renderer = device.GetRenderer();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    const Rect physical = DefaultViewportRect(renderer);
    std::cout << "[WEBGPU-162] " << RendererName() << " physical drawable = "
              << physical.Describe() << std::endl;

    ASSERT_GT(physical.width, 0) << RendererName() << " reports a zero-width drawable";
    ASSERT_GT(physical.height, 0) << RendererName() << " reports a zero-height drawable";
    EXPECT_EQ(0, physical.x) << RendererName()
        << " offsets an unscaled presentation rectangle -- " << physical.Describe();
    EXPECT_EQ(0, physical.y) << RendererName()
        << " offsets an unscaled presentation rectangle -- " << physical.Describe();
}

// ---------------------------------------------------------------------------
// THE MEASUREMENT WEBGPU-162 EXISTS FOR. Under Letterbox the rectangle is the virtual resolution
// scaled to FIT and centred -- in physical pixels. A renderer inheriting the interface default
// returns the logical size at the origin instead, which is a different rectangle in both size and
// position whenever the two differ, and that is exactly what put the game in a corner.
// ---------------------------------------------------------------------------
TEST(PresentationRectangleTest, LetterboxIsScaledAndCentredInPhysicalPixels)
{
    if (!HasVirtualResolution())
        GTEST_SKIP() << RendererName() << " implements no virtual resolution";
    GraphicsDevice device;
    IGraphicsRenderer& renderer = device.GetRenderer();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    const Rect physical = DefaultViewportRect(renderer);
    ASSERT_GT(physical.width, 0);
    ASSERT_GT(physical.height, 0);

    // A virtual resolution deliberately SMALLER than the drawable and of a different aspect, so the
    // scaled rectangle is smaller than the drawable in one axis and centred in it -- the case the
    // inherited default gets wrong. Halving keeps the numbers exact whatever window this run got.
    const int virtualWidth = std::max(1, physical.width / 2);
    const int virtualHeight = std::max(1, physical.height / 3);
    renderer.SetVirtualResolution(virtualWidth, virtualHeight);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));

    const Rect measured = DefaultViewportRect(renderer);
    const Rect expected = ExpectedUniformScaleRect(physical, virtualWidth, virtualHeight,
                                                   /*cover=*/false);
    std::cout << "[WEBGPU-162] " << RendererName() << " Letterbox virtual " << virtualWidth << 'x'
              << virtualHeight << " in " << physical.Describe() << " -> " << measured.Describe()
              << ", expected " << expected.Describe() << std::endl;

    EXPECT_TRUE(measured == expected)
        << RendererName() << " Letterbox presentation rectangle is " << measured.Describe()
        << ", expected " << expected.Describe()
        << ". A rectangle of the LOGICAL size at the origin means GetDefaultViewportRect() is the "
           "inherited default, and every draw will land in a corner of the drawable";

    // Stated separately so the failure says WHICH half is wrong: the rectangle must not be the
    // whole drawable (nothing was scaled), and it must be centred (nothing sits at the origin).
    //
    // WHICH AXIS gets the bars depends on the two aspect ratios and is not this test's business to
    // predict: a virtual resolution wider than the drawable is letterboxed top and bottom, a
    // narrower one left and right. So the claim is made over "at least one axis" rather than by
    // guessing -- the exact rectangle is already asserted above, and these two only exist to name
    // the failure mode when it is not.
    EXPECT_TRUE(measured.width <= physical.width && measured.height <= physical.height)
        << RendererName() << " Letterbox produced a rectangle larger than the drawable -- "
        << measured.Describe() << " in " << physical.Describe()
        << ". Letterbox fits INSIDE and adds bars; covering and cropping is Overscan";
    EXPECT_TRUE(measured.width < physical.width || measured.height < physical.height)
        << RendererName() << " did not scale the virtual resolution down to fit -- "
        << measured.Describe() << " is the whole drawable, so nothing was letterboxed";
    EXPECT_TRUE(measured.x > 0 || measured.y > 0)
        << RendererName() << " left the presentation rectangle at the window origin instead of "
                             "centring it -- " << measured.Describe();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    renderer.SetVirtualResolution(physical.width, physical.height);
}

// ---------------------------------------------------------------------------
// Overscan is the same computation with `max` instead of `min`: it COVERS the drawable and crops,
// so its rectangle is larger than the drawable and its origin is negative. Included because a
// renderer that hard-coded "centre a smaller rectangle" would pass the Letterbox arm and fail here.
// ---------------------------------------------------------------------------
TEST(PresentationRectangleTest, OverscanCoversTheDrawableAndCropsSymmetrically)
{
    if (!HasVirtualResolution())
        GTEST_SKIP() << RendererName() << " implements no virtual resolution";
    GraphicsDevice device;
    IGraphicsRenderer& renderer = device.GetRenderer();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    const Rect physical = DefaultViewportRect(renderer);
    ASSERT_GT(physical.width, 0);
    ASSERT_GT(physical.height, 0);

    const int virtualWidth = std::max(1, physical.width / 2);
    const int virtualHeight = std::max(1, physical.height / 3);
    renderer.SetVirtualResolution(virtualWidth, virtualHeight);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Overscan));

    const Rect measured = DefaultViewportRect(renderer);
    const Rect expected = ExpectedUniformScaleRect(physical, virtualWidth, virtualHeight,
                                                   /*cover=*/true);
    std::cout << "[WEBGPU-162] " << RendererName() << " Overscan virtual " << virtualWidth << 'x'
              << virtualHeight << " in " << physical.Describe() << " -> " << measured.Describe()
              << ", expected " << expected.Describe() << std::endl;

    EXPECT_TRUE(measured == expected)
        << RendererName() << " Overscan presentation rectangle is " << measured.Describe()
        << ", expected " << expected.Describe();
    EXPECT_GE(measured.width, physical.width)
        << RendererName() << " Overscan failed to cover the drawable -- " << measured.Describe();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    renderer.SetVirtualResolution(physical.width, physical.height);
}

// ---------------------------------------------------------------------------
// Stretch and FixedHeightDynamicWidth scale the virtual resolution to the whole drawable, so there
// are no bars and nothing to centre: their rectangle IS the drawable. Asserted because the fix for
// Letterbox/Overscan must not start offsetting these -- the inherited default was already right
// for them, and a naive "always centre" override would break the two modes that were working.
// ---------------------------------------------------------------------------
TEST(PresentationRectangleTest, StretchAndFixedHeightFillTheWholeDrawable)
{
    if (!HasVirtualResolution())
        GTEST_SKIP() << RendererName() << " implements no virtual resolution";
    GraphicsDevice device;
    IGraphicsRenderer& renderer = device.GetRenderer();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    const Rect physical = DefaultViewportRect(renderer);
    ASSERT_GT(physical.width, 0);
    ASSERT_GT(physical.height, 0);
    renderer.SetVirtualResolution(std::max(1, physical.width / 2),
                                  std::max(1, physical.height / 3));

    for (const CnaPresentationMode mode : {CnaPresentationMode::Stretch,
                                           CnaPresentationMode::FixedHeightDynamicWidth})
    {
        renderer.SetPresentationMode(static_cast<int>(mode));
        const Rect measured = DefaultViewportRect(renderer);
        std::cout << "[WEBGPU-162] " << RendererName() << " mode " << static_cast<int>(mode)
                  << " -> " << measured.Describe() << std::endl;
        EXPECT_TRUE(measured == physical)
            << RendererName() << " mode " << static_cast<int>(mode) << " produced "
            << measured.Describe() << " instead of the whole drawable " << physical.Describe();
    }

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    renderer.SetVirtualResolution(physical.width, physical.height);
}

// ---------------------------------------------------------------------------
// The two questions are DIFFERENT questions, and the fix must not collapse them.
// GetViewportSize() is the LOGICAL size a game draws in; GetDefaultViewportRect() is the PHYSICAL
// rectangle that logical space is presented into. Under Letterbox with a smaller virtual
// resolution they must not be equal -- which is precisely what the inherited default made them.
// ---------------------------------------------------------------------------
TEST(PresentationRectangleTest, LogicalSizeAndPhysicalRectangleAreNotTheSameAnswer)
{
    if (!HasVirtualResolution())
        GTEST_SKIP() << RendererName() << " implements no virtual resolution";
    GraphicsDevice device;
    IGraphicsRenderer& renderer = device.GetRenderer();

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    const Rect physical = DefaultViewportRect(renderer);
    ASSERT_GT(physical.width, 0);
    ASSERT_GT(physical.height, 0);

    const int virtualWidth = std::max(1, physical.width / 2);
    const int virtualHeight = std::max(1, physical.height / 3);
    renderer.SetVirtualResolution(virtualWidth, virtualHeight);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));

    int logicalWidth = 0;
    int logicalHeight = 0;
    renderer.GetViewportSize(logicalWidth, logicalHeight);
    const Rect measured = DefaultViewportRect(renderer);
    std::cout << "[WEBGPU-162] " << RendererName() << " logical " << logicalWidth << 'x'
              << logicalHeight << " vs physical rect " << measured.Describe() << std::endl;

    EXPECT_EQ(virtualWidth, logicalWidth)
        << RendererName() << " GetViewportSize() stopped reporting the LOGICAL size";
    EXPECT_EQ(virtualHeight, logicalHeight)
        << RendererName() << " GetViewportSize() stopped reporting the LOGICAL size";
    EXPECT_FALSE(measured.width == logicalWidth && measured.height == logicalHeight &&
                 measured.x == 0 && measured.y == 0)
        << RendererName() << " GetDefaultViewportRect() returned the logical size at the origin ("
        << measured.Describe() << ") -- that is the inherited default, not a presentation rectangle";

    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    renderer.SetVirtualResolution(physical.width, physical.height);
}
