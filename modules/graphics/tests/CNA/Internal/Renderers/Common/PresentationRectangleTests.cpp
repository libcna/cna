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
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

namespace
{
    using CNA::Internal::Renderers::CnaPresentationMode;
    using CNA::Internal::Renderers::IGraphicsRenderer;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

    using namespace ::CNA::Testing::Renderers;   // NOLINT(google-build-using-namespace)

    /// The renderers that implement a real virtual resolution, i.e. whose `GetDefaultViewportRect()`
    /// can differ from the whole drawable at all. A renderer with no scaling has nothing to centre,
    /// and asserting a centred rectangle there would be asserting someone else's contract.
    ///
    /// WEBGPU WAS DELIBERATELY ABSENT UNTIL 2026-09-06, and the history is worth keeping because it
    /// says what the fix had to contain. `plans/plan_webgpu.md` `WEBGPU-162` first added the override
    /// alone and withdrew it: with only the physical rectangle returned here, WebGPU's sprite path
    /// still classified the default viewport as a caller-supplied SUB-viewport, because its test was
    /// "does the viewport differ from the target EXTENT" rather than "from the presentation
    /// RECTANGLE". Under `FixedHeightDynamicWidth` on an 800x480 drawable with a 96x72 virtual
    /// resolution the logical size is 120x72, so that test said "custom", every sprite was baked
    /// viewport-local, and a sprite the caller placed at logical (24,15) landed somewhere the test
    /// did not read.
    ///
    /// The row closed by changing BOTH: the override, and the discriminator it depends on. WebGPU is
    /// in the set now and passes the same 5/5 the other renderers do.
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

// ---------------------------------------------------------------------------
// The second half of WEBGPU-162, and the half no existing test covered.
//
// Returning the physical rectangle from GetDefaultViewportRect() is not sufficient on its own: a
// renderer that bakes sprite geometry against GraphicsDevice.Viewport also has to know that this
// rectangle is its DEFAULT viewport and not a caller-supplied sub-viewport. XNA/FNA make SpriteBatch
// coordinates viewport-local only for a sub-Viewport the game set (CreateOrthographicOffCenter over
// Viewport.Width/Height); the presentation rectangle is not that. A renderer testing "does the
// viewport differ from the target EXTENT" answers yes for a letterboxed backbuffer, whose default
// viewport is a centred sub-rectangle by construction -- and then reads a sprite's logical
// coordinates as viewport PIXELS, putting a full-screen sprite in a corner of the letterbox.
//
// So: a sprite covering the whole logical area must FILL the letterbox rectangle, and the bars must
// stay clear. The bar check is what makes this more than "something rendered": a renderer that
// ignored the presentation rectangle entirely and drew over the whole drawable would pass the first
// assertion and fail the second.
// ---------------------------------------------------------------------------
TEST(PresentationRectangleTest, ALetterboxedDefaultViewportIsNotACustomSubViewport)
{
    if (!HasVirtualResolution())
        GTEST_SKIP() << RendererName() << " implements no virtual resolution";
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::BlendState;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::SpriteBatch;
    using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    GraphicsDevice device;
    IGraphicsRenderer& renderer = device.GetRenderer();

    // Measure the drawable first, exactly as the tests above do, so the expectation cannot disagree
    // with the window this test actually got.
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
    const Rect physical = DefaultViewportRect(renderer);
    ASSERT_GT(physical.width, 8);
    ASSERT_GT(physical.height, 8);

    // A virtual resolution whose aspect differs from the drawable's, so Letterbox produces REAL
    // bars. A square one does that for any non-square window, and the shorter physical axis decides
    // the scale.
    const int virtualSize = std::max(8, std::min(physical.width, physical.height) / 2);
    renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
    renderer.SetVirtualResolution(virtualSize, virtualSize);

    const Rect box = DefaultViewportRect(renderer);
    std::cout << "[WEBGPU-162] " << RendererName() << " drawable=" << physical.Describe()
              << " virtual=" << virtualSize << 'x' << virtualSize
              << " letterbox=" << box.Describe() << std::endl;
    if (box.width >= physical.width && box.height >= physical.height)
        GTEST_SKIP() << RendererName() << " produced no letterbox bars for this window, so there is "
                                          "nothing here to distinguish";

    // What GraphicsDevice::UpdateViewportFromWindow() would push: the default viewport IS the
    // presentation rectangle. Set explicitly because the device's own mode/virtual-resolution
    // setters are private -- this is the state under test either way, and stating it directly also
    // says plainly what the test is about.
    // In LOGICAL units, which is what the public Viewport is -- GraphicsDevice maps it into the
    // presentation rectangle on the way to the renderer. Passing the physical rectangle here would
    // map it a second time, which is a mistake worth naming: it is exactly what this test caught
    // the first time it was written.
    device.setViewportProperty(
        Microsoft::Xna::Framework::Graphics::Viewport(0, 0, virtualSize, virtualSize));

    const Color clear(9, 13, 17, 255);
    const Color ink(230, 70, 40, 255);
    device.Clear(clear);
    Texture2D texture(device, 1, 1, false, SurfaceFormat::Color);
    const Color white(255, 255, 255, 255);
    texture.SetData(&white, 1);
    {
        const SamplerState sampler = SamplerState::PointClamp;
        SpriteBatch batch(device);
        batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler, nullptr, nullptr);
        batch.Draw(texture, Rectangle(0, 0, virtualSize, virtualSize), ink);
        batch.End();
    }

    const auto read = [&device](int x, int y) {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    };
    // Where the sprite ACTUALLY landed, so a failure names the transform that was applied instead
    // of only saying the expected pixel was empty. Scanned coarsely across the whole drawable.
    {
        int minX = physical.width, minY = physical.height, maxX = -1, maxY = -1;
        const int stepX = std::max(1, physical.width / 64);
        const int stepY = std::max(1, physical.height / 64);
        for (int y = 0; y < physical.height; y += stepY)
            for (int x = 0; x < physical.width; x += stepX)
            {
                const Color p = read(x, y);
                if (std::abs(int(p.getRProperty()) - int(clear.getRProperty())) <= 6 &&
                    std::abs(int(p.getGProperty()) - int(clear.getGProperty())) <= 6 &&
                    std::abs(int(p.getBProperty()) - int(clear.getBProperty())) <= 6)
                    continue;
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
            }
        std::cout << "[WEBGPU-162] " << RendererName() << " sprite bounding box = "
                  << (maxX < 0 ? std::string("EMPTY")
                               : '(' + std::to_string(minX) + ',' + std::to_string(minY) + ")-(" +
                                 std::to_string(maxX) + ',' + std::to_string(maxY) + ')')
                  << "  expected to fill " << box.Describe() << std::endl;
    }

    const Color centre = read(box.x + box.width / 2, box.y + box.height / 2);
    EXPECT_NEAR(centre.getRProperty(), ink.getRProperty(), 12)
        << RendererName() << " did not fill the letterbox rectangle with a sprite covering the whole "
        << "logical area -- read (" << int(centre.getRProperty()) << ','
        << int(centre.getGProperty()) << ',' << int(centre.getBProperty())
        << ") at the centre of " << box.Describe()
        << ". A default viewport treated as a custom sub-viewport puts it in a corner instead.";

    // The bar, on whichever axis has one.
    const bool barsAreVertical = box.x > 2;
    const int barX = barsAreVertical ? box.x / 2 : physical.width / 2;
    const int barY = barsAreVertical ? physical.height / 2 : box.y / 2;
    if (barsAreVertical ? (box.x > 2) : (box.y > 2))
    {
        const Color bar = read(barX, barY);
        EXPECT_NEAR(bar.getRProperty(), clear.getRProperty(), 12)
            << RendererName() << " drew outside the letterbox rectangle -- the bar at (" << barX
            << ',' << barY << ") is not the clear colour";
    }
}
