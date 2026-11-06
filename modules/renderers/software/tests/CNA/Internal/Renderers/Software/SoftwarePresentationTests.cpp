// SPDX-License-Identifier: MS-PL
//
// TCR-2/TCR-3: the producer half of the surface-presenter seam.
//
// IPlatformSurfacePresenter had a complete, tested consumer side and no producer at all once
// BLEND2D was retired, so a CPU frame could not reach a screen. These tests pin the contract
// SoftwareRenderer now keeps when it is handed a presenter, deliberately WITHOUT a terminal in
// sight: what is asserted here is the generic CPU-framebuffer handover -- dimensions, stride,
// channel order, which framebuffer is presented, and what happens when there is nothing to present
// to. The terminal's own half is TerminalPresenterTests; the end-to-end path is
// TerminalSoftwareDemoIntegration.

#if defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_PRESENT_SOFTWARE)

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    using CNA::Internal::Renderers::Software::SoftwareRenderer;
    using CNA::Platform::IPlatformSurfacePresenter;
    using CNA::Platform::PresentFilter;
    using CNA::Platform::PresentScaleMode;
    using CNA::Platform::SurfaceFrame;

    /// Records what the renderer actually handed over, rather than merely that it called Present().
    /// A copy is taken because the renderer keeps ownership of its framebuffer and is free to draw
    /// into it again the moment Present() returns.
    class RecordingPresenter final : public IPlatformSurfacePresenter
    {
    public:
        struct Record
        {
            int width = 0;
            int height = 0;
            int strideBytes = 0;
            std::vector<std::uint8_t> pixels;
        };

        void SetScaleMode(PresentScaleMode, PresentFilter) override {}

        bool SetVSync(bool) override { return false; }

        void Present(const SurfaceFrame& frame) override
        {
            Record record;
            record.width = frame.width;
            record.height = frame.height;
            record.strideBytes = frame.strideBytes;
            const int stride = frame.strideBytes > 0 ? frame.strideBytes : frame.width * 4;
            // A cropped last row has only width*4 readable bytes, not a whole parent pitch.
            for (int y = 0; y < frame.height; ++y)
                record.pixels.insert(record.pixels.end(), frame.pixels + y * stride,
                                     frame.pixels + y * stride + frame.width * 4);
            frames.push_back(std::move(record));
        }

        void GetTargetSize(int& width, int& height) const override
        {
            width = 320;
            height = 240;
        }

        std::vector<Record> frames;
    };

    /// The first pixel's four bytes, which is where a channel-order mistake shows up first.
    void ExpectFirstPixel(const RecordingPresenter::Record& record,
                          const std::uint8_t r, const std::uint8_t g,
                          const std::uint8_t b, const std::uint8_t a)
    {
        ASSERT_GE(record.pixels.size(), 4u);
        EXPECT_EQ(record.pixels[0], r);
        EXPECT_EQ(record.pixels[1], g);
        EXPECT_EQ(record.pixels[2], b);
        EXPECT_EQ(record.pixels[3], a);
    }
}

// The configuration every non-terminal build is in: nothing can display a CPU frame, so no
// presenter is attached. Present() must stay the no-op it always was -- this is the test that a
// renderer used off-screen is unaffected by the repair.
TEST(SoftwarePresentation, PresentIsANoOpWithoutAPresenter)
{
    SoftwareRenderer renderer(64, 32);
    renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NO_THROW(renderer.Present());
    EXPECT_NO_THROW(renderer.Present());
    // Inert presentation must preserve the caller-authored red RGBA framebuffer.
    const auto& pixels = renderer.CurrentFramebuffer().color;
    ASSERT_EQ(pixels.size(), 64u * 32u * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        EXPECT_EQ(pixels[i], 255); EXPECT_EQ(pixels[i + 1], 0);
        EXPECT_EQ(pixels[i + 2], 0); EXPECT_EQ(pixels[i + 3], 255);
    }
}

// Attaching null explicitly is the same thing, because that is literally what
// GraphicsRendererCreateArgs::surfacePresenter holds in every configuration that cannot present.
TEST(SoftwarePresentation, AttachingANullPresenterLeavesPresentInert)
{
    SoftwareRenderer renderer(64, 32);
    renderer.AttachSurfacePresenter(nullptr);
    renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_NO_THROW(renderer.Present());
    // Explicit null attachment must preserve every green pixel, including alpha.
    const auto& pixels = renderer.CurrentFramebuffer().color;
    ASSERT_EQ(pixels.size(), 64u * 32u * 4u);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        EXPECT_EQ(pixels[i], 0); EXPECT_EQ(pixels[i + 1], 255);
        EXPECT_EQ(pixels[i + 2], 0); EXPECT_EQ(pixels[i + 3], 255);
    }
}

// The frame's geometry must describe the backbuffer exactly. A stride of 0 is the contract's way of
// saying "tightly packed", which is what SoftwareFramebuffer::color is -- claiming a stride it does
// not have would make the presenter read a skewed picture.
TEST(SoftwarePresentation, TheFirstFrameDescribesTheBackbufferExactly)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(64, 32);
    renderer.AttachSurfacePresenter(&presenter);

    renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 1u);
    EXPECT_EQ(presenter.frames[0].width, 64);
    EXPECT_EQ(presenter.frames[0].height, 32);
    EXPECT_EQ(presenter.frames[0].strideBytes, 0);
    EXPECT_EQ(presenter.frames[0].pixels.size(), static_cast<std::size_t>(64 * 32 * 4));
}

// Channel order is the one thing a terminal cannot reveal by looking wrong in a subtle way: a
// red/blue swap simply shows the wrong colour. Asserted here against a colour whose four channels
// are all different, so no two of them can be transposed without the test noticing.
TEST(SoftwarePresentation, PixelsReachThePresenterAsRgba8)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(4, 2);
    renderer.AttachSurfacePresenter(&presenter);

    // 0x33 / 0x66 / 0x99 / 0xCC -- exactly representable as unorm8 from these ratios.
    renderer.Clear(51.0f / 255.0f, 102.0f / 255.0f, 153.0f / 255.0f, 204.0f / 255.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 1u);
    ExpectFirstPixel(presenter.frames[0], 51, 102, 153, 204);
}

// Every row must be present and correct, not just the first: a wrong row pitch produces a frame
// that is right at the top-left and skewed everywhere else.
TEST(SoftwarePresentation, EveryRowOfTheFrameIsTightlyPacked)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(8, 4);
    renderer.AttachSurfacePresenter(&presenter);

    renderer.Clear(1.0f, 102.0f / 255.0f, 153.0f / 255.0f, 1.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 1u);
    const auto& pixels = presenter.frames[0].pixels;
    ASSERT_EQ(pixels.size(), static_cast<std::size_t>(8 * 4 * 4));
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 8; ++column)
        {
            const std::size_t index = (static_cast<std::size_t>(row) * 8u +
                                       static_cast<std::size_t>(column)) * 4u;
            EXPECT_EQ(pixels[index + 0], 255) << "row " << row << " column " << column;
            // Literal RGBA8 channels: 255*(102/255)=102, 255*(153/255)=153.
            EXPECT_EQ(pixels[index + 1], 102) << "row " << row << " column " << column;
            EXPECT_EQ(pixels[index + 2], 153) << "row " << row << " column " << column;
            EXPECT_EQ(pixels[index + 3], 255) << "row " << row << " column " << column;
        }
    }
}

// A game presents once per frame forever. Each call must deliver the frame as it is now, not the
// one the presenter already saw.
TEST(SoftwarePresentation, ConsecutiveFramesEachReachThePresenter)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(16, 8);
    renderer.AttachSurfacePresenter(&presenter);

    renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
    renderer.Present();
    renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
    renderer.Present();
    renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 3u);
    ExpectFirstPixel(presenter.frames[0], 255, 0, 0, 255);
    ExpectFirstPixel(presenter.frames[1], 0, 255, 0, 255);
    ExpectFirstPixel(presenter.frames[2], 0, 0, 255, 255);
}

// The property the damage-tracking presenter depends on: a frame that changed must arrive
// different. A producer that presented a stale buffer would leave the terminal showing frame one
// forever while every diff came out empty.
TEST(SoftwarePresentation, AChangedFrameArrivesChanged)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(16, 8);
    renderer.AttachSurfacePresenter(&presenter);

    renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
    renderer.Present();
    renderer.Clear(1.0f, 1.0f, 1.0f, 1.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 2u);
    EXPECT_NE(presenter.frames[0].pixels, presenter.frames[1].pixels);
}

// An unchanged frame must arrive unchanged, which is the other half of the same property: the
// presenter is entitled to conclude from equal frames that nothing needs redrawing.
TEST(SoftwarePresentation, AnUnchangedFrameArrivesUnchanged)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(16, 8);
    renderer.AttachSurfacePresenter(&presenter);

    renderer.Clear(0.25f, 0.5f, 0.75f, 1.0f);
    renderer.Present();
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 2u);
    EXPECT_EQ(presenter.frames[0].pixels, presenter.frames[1].pixels);
}

// A terminal resize reaches the renderer as a virtual-resolution change, exactly as a window
// resize does. The next frame must describe the new backbuffer, and its byte count must follow --
// a frame whose dimensions moved without its storage is how a presenter reads past the end.
TEST(SoftwarePresentation, ResizingTheBackbufferResizesTheFrame)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(64, 32);
    renderer.AttachSurfacePresenter(&presenter);

    renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
    renderer.Present();

    renderer.SetVirtualResolution(100, 50);
    renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
    renderer.Present();

    renderer.SetVirtualResolution(20, 10);
    renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 3u);
    EXPECT_EQ(presenter.frames[0].width, 64);
    EXPECT_EQ(presenter.frames[0].height, 32);
    EXPECT_EQ(presenter.frames[0].pixels.size(), static_cast<std::size_t>(64 * 32 * 4));
    EXPECT_EQ(presenter.frames[1].width, 100);
    EXPECT_EQ(presenter.frames[1].height, 50);
    EXPECT_EQ(presenter.frames[1].pixels.size(), static_cast<std::size_t>(100 * 50 * 4));
    EXPECT_EQ(presenter.frames[2].width, 20);
    EXPECT_EQ(presenter.frames[2].height, 10);
    EXPECT_EQ(presenter.frames[2].pixels.size(), static_cast<std::size_t>(20 * 10 * 4));
}

// Multisampling keeps the displayable pixels in a resolved cache that is only refreshed on demand.
// Presenting without resolving would show whatever that cache last held -- stale, or on the first
// frame, nothing that was ever drawn.
TEST(SoftwarePresentation, AMultisampledFrameIsResolvedBeforeItIsPresented)
{
    RecordingPresenter presenter;
    SoftwareRenderer renderer(16, 8);
    renderer.AttachSurfacePresenter(&presenter);
    ASSERT_EQ(renderer.ApplyMultiSampleCount(4), 4);

    renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
    renderer.Present();

    ASSERT_EQ(presenter.frames.size(), 1u);
    ExpectFirstPixel(presenter.frames[0], 255, 0, 0, 255);
}

// The renderer must not touch the presenter after the last Present(), and destroying it while a
// presenter is still attached must be uneventful -- GraphicsDevice destroys the renderer first and
// the presenter second, so a producer reaching back during teardown would be a use-after-free in
// the other order.
TEST(SoftwarePresentation, DestroyingTheRendererPresentsNothingFurther)
{
    RecordingPresenter presenter;
    {
        SoftwareRenderer renderer(16, 8);
        renderer.AttachSurfacePresenter(&presenter);
        renderer.Clear(1.0f, 0.0f, 0.0f, 1.0f);
        renderer.Present();
    }
    EXPECT_EQ(presenter.frames.size(), 1u);
}


namespace {
using Region = CNA::Internal::Renderers::IGraphicsRenderer::RendererPresentRegionEXT;
using CNA::Platform::SurfaceDestination;
class RegionPresenter final : public IPlatformSurfacePresenter
{
public:
    bool accept = true;
    int ordinaryCalls = 0;
    int regionCalls = 0;
    SurfaceDestination destination{};
    std::vector<std::uint8_t> pixels;
    void SetScaleMode(PresentScaleMode, PresentFilter) override {}
    bool SetVSync(bool) override { return false; }
    void GetTargetSize(int& w, int& h) const override { w = 16; h = 12; }
    void Present(const SurfaceFrame&) override { ++ordinaryCalls; }
    bool PresentRegion(const SurfaceFrame& frame, const SurfaceDestination& dest) override
    {
        if (!accept) return false;
        ++regionCalls;
        destination = dest;
        const int pitch = frame.strideBytes == 0 ? frame.width * 4 : frame.strideBytes;
        for (int y = 0; y < frame.height; ++y)
            pixels.insert(pixels.end(), frame.pixels + y * pitch,
                          frame.pixels + y * pitch + frame.width * 4);
        return true;
    }
};
void PaintQuadrants(SoftwareRenderer& renderer)
{
    // Literal 4x4 RGBA: top-left red, top-right green, bottom-left blue, bottom-right white.
    auto& bytes = renderer.CurrentFramebuffer().color;
    const std::uint8_t colors[4][4] = {{255,0,0,255},{0,255,0,255},
                                     {0,0,255,255},{255,255,255,255}};
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            for (int c = 0; c < 4; ++c)
                bytes[(y * 4 + x) * 4 + c] = colors[(y / 2) * 2 + x / 2][c];
}
Region MappedRegion()
{
    Region r;
    r.hasSourceRectangle = true;
    r.sourceX = 2; r.sourceY = 1; r.sourceWidth = 2; r.sourceHeight = 3;
    r.hasDestinationRectangle = true;
    r.destinationX = -2; r.destinationY = 3; r.destinationWidth = 8; r.destinationHeight = 6;
    return r;
}
}

TEST(Gap08SoftwareRegion, CroppedRowsAndOriginalDestinationReachThePresenter)
{
    RegionPresenter presenter;
    SoftwareRenderer renderer(4, 4);
    renderer.AttachSurfacePresenter(&presenter);
    PaintQuadrants(renderer);
    // Literal rows y=1,2,3 at x=2,3: green/green, white/white, white/white.
    ASSERT_TRUE(renderer.PresentRegionEXT(MappedRegion()));
    EXPECT_EQ(presenter.regionCalls, 1);
    EXPECT_EQ(presenter.ordinaryCalls, 0);
    EXPECT_EQ(presenter.destination.x, -2); EXPECT_EQ(presenter.destination.y, 3);
    EXPECT_EQ(presenter.destination.width, 8); EXPECT_EQ(presenter.destination.height, 6);
    const std::vector<std::uint8_t> expected{
        0,255,0,255, 0,255,0,255,
        255,255,255,255, 255,255,255,255,
        255,255,255,255, 255,255,255,255};
    EXPECT_EQ(presenter.pixels, expected);
}

TEST(Gap08SoftwareRegion, OverridePresenterReceivesPixelsAndOwnPresenterStaysUntouched)
{
    RegionPresenter own, other;
    SoftwareRenderer renderer(4, 4);
    renderer.AttachSurfacePresenter(&own);
    PaintQuadrants(renderer);
    Region r = MappedRegion();
    r.overrideWindowHandle = 0x1234;
    r.targetPresenter = &other;
    ASSERT_TRUE(renderer.PresentRegionEXT(r));
    EXPECT_EQ(other.regionCalls, 1);
    EXPECT_EQ(own.regionCalls, 0); EXPECT_EQ(own.ordinaryCalls, 0);
    ASSERT_EQ(other.pixels.size(), 24u);
    EXPECT_EQ(other.pixels[0], 0); EXPECT_EQ(other.pixels[1], 255);
    renderer.Present();
    EXPECT_EQ(own.ordinaryCalls, 1); EXPECT_EQ(other.regionCalls, 1);
}

TEST(Gap08SoftwareRegion, MissingDestinationUsesTheSelectedTargetsPhysicalSize)
{
    RegionPresenter other;
    SoftwareRenderer renderer(4, 4);
    renderer.Clear(1, 0, 0, 1);
    Region r;
    r.overrideWindowHandle = 0x1234;
    r.targetPresenter = &other;
    ASSERT_TRUE(renderer.PresentRegionEXT(r));
    EXPECT_EQ(other.destination.x, 0); EXPECT_EQ(other.destination.y, 0);
    EXPECT_EQ(other.destination.width, 16); EXPECT_EQ(other.destination.height, 12);
    EXPECT_EQ(other.pixels.size(), 64u);
}

TEST(Gap08SoftwareRegion, SourceOnlyFillsTheWholePhysicalTarget)
{
    RegionPresenter presenter;
    SoftwareRenderer renderer(4,4);
    renderer.AttachSurfacePresenter(&presenter);
    PaintQuadrants(renderer);
    Region r;
    r.hasSourceRectangle=true;
    r.sourceX=2; r.sourceY=0; r.sourceWidth=2; r.sourceHeight=4;
    ASSERT_TRUE(renderer.PresentRegionEXT(r));
    EXPECT_EQ(presenter.regionCalls,1); EXPECT_EQ(presenter.ordinaryCalls,0);
    EXPECT_EQ(presenter.destination.x,0); EXPECT_EQ(presenter.destination.y,0);
    EXPECT_EQ(presenter.destination.width,16); EXPECT_EQ(presenter.destination.height,12);
    ASSERT_EQ(presenter.pixels.size(),32u);
    const std::vector<std::uint8_t> expected{
        0,255,0,255,0,255,0,255,0,255,0,255,0,255,0,255,
        255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255};
    EXPECT_EQ(presenter.pixels,expected);
}

TEST(Gap08SoftwareRegion, RegionPresentationResolvesMultisampledPixels)
{
    RegionPresenter presenter;
    SoftwareRenderer renderer(4,4);
    renderer.AttachSurfacePresenter(&presenter);
    ASSERT_EQ(renderer.ApplyMultiSampleCount(4),4);
    renderer.Clear(1,0,0,1);
    auto& cache=renderer.CurrentFramebuffer().color;
    std::fill(cache.begin(),cache.end(),0);
    Region r=MappedRegion();
    ASSERT_TRUE(renderer.PresentRegionEXT(r));
    // Author a stale resolved cache independently of the four current red samples. Clear alone
    // updates both stores and would let removal of ResolveColor survive this test.
    const std::vector<std::uint8_t> expected{
        255,0,0,255,255,0,0,255,255,0,0,255,
        255,0,0,255,255,0,0,255,255,0,0,255};
    EXPECT_EQ(presenter.pixels,expected);
}

TEST(Gap08SoftwareRegion, AnUnsupportedPresenterRefusesBeforeAnOrdinaryPresent)
{
    RegionPresenter presenter;
    presenter.accept = false;
    SoftwareRenderer renderer(4, 4);
    renderer.AttachSurfacePresenter(&presenter);
    renderer.Clear(1, 0, 0, 1);
    EXPECT_FALSE(renderer.PresentRegionEXT(MappedRegion()));
    EXPECT_EQ(presenter.ordinaryCalls, 0); EXPECT_EQ(presenter.regionCalls, 0);
    EXPECT_TRUE(presenter.pixels.empty());
    const auto& bytes = renderer.CurrentFramebuffer().color;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        EXPECT_EQ(bytes[i], 255); EXPECT_EQ(bytes[i+1], 0);
        EXPECT_EQ(bytes[i+2], 0); EXPECT_EQ(bytes[i+3], 255);
    }
}

#endif  // SOFTWARE compiled in
