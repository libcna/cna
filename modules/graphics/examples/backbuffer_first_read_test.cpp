// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-161 -- the FIRST GraphicsDevice.GetBackBufferData of a process/device must fully and
// synchronously write the whole requested destination range.
//
// Measured on WEBGPU (REMED-GFX-157): the very first backbuffer readback of a run returned
// successfully while leaving part of the caller's buffer holding its poison prefill; every later
// read in the same run was correct. A caller could not tell it apart from a rendered frame. This
// fixture pins that down: every destination element is pre-filled with a sentinel colour that
// cannot occur in the rendered image, one read is performed, and the EXACT unwritten range is
// reported and asserted empty.
//
// Each leg runs in its own process (fork+exec) so its FIRST read is genuinely first: no earlier
// leg's readback can warm the staging buffer up. A leg's exit code is the only PASS/FAIL signal;
// a crash or a hang is a distinct, attributable result.
//
// Scope: this is a CONTENT/COMPLETION contract, distinct from REMED-GFX-165 (dimension source) and
// REMED-GFX-149 (startIndex/element-count transfer asymmetry). The canonical legs use startIndex 0
// to keep GFX-149 separate; the prefix/suffix leg uses a non-zero startIndex ONLY to prove the fix
// does not disturb the destination outside the requested range.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX161_CAN_FORK 1
#else
#define CNA_GFX161_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_RENDERER_HEADLESS)
    constexpr const char* kRendererName = "HEADLESS";
    constexpr bool kRasterizes = false;
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr const char* kRendererName = "SOFTWARE";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EASYGL";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_BGFX)
    constexpr const char* kRendererName = "BGFX";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_VULKAN)
    constexpr const char* kRendererName = "VULKAN";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr const char* kRendererName = "WEBGPU";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr const char* kRendererName = "SDL_GPU";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr const char* kRendererName = "SDL_RENDERER";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr const char* kRendererName = "DIRECTX9";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr const char* kRendererName = "DIRECTX11";
    constexpr bool kRasterizes = true;
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr const char* kRendererName = "DIRECTX12";
    constexpr bool kRasterizes = true;
#else
#error "REMED-GFX-161: this renderer has no declared first-read contract."
#endif

    // BGFX honours only a full-surface backbuffer read; a sub-rectangle reads back zeros (a
    // pre-existing gap declared by REMED-GFX-165, NOT this task's subject). The rectangle legs
    // declare it as a boundary rather than failing it.
#if defined(CNA_RENDERER_BGFX)
    constexpr bool kSupportsSubRectangleBackbufferRead = false;
#else
    constexpr bool kSupportsSubRectangleBackbufferRead = true;
#endif

    // The distinctive poison the caller pre-fills every destination element with. Every channel is
    // 0xCD -- distinct from every quadrant colour below, and A=0xCD so a renderer that writes RGB but
    // forgets A cannot masquerade as intact poison. "Unwritten" is localised by this sentinel, so the
    // content pattern's job is only to catch an orientation flip / mirror / garbage read.
    const Color kSentinel(0xCD, 0xCD, 0xCD, 0xCD);

    bool IsSentinel(const Color& c)
    {
        return c.getRProperty() == 0xCD && c.getGProperty() == 0xCD &&
               c.getBProperty() == 0xCD && c.getAProperty() == 0xCD;
    }

    // Odd, non-square, not a common default. 37x23 matches REMED-GFX-165's canonical size.
    constexpr int kW = 37;
    constexpr int kH = 23;

    /** @brief The four asymmetric quadrant colours (REMED-GFX-165's proven-byte-exact palette):
     *  every corner distinct, top != bottom (an orientation flip is caught), left != right (a mirror
     *  is caught), one mid-tone. None equals the 0xCD sentinel. A piecewise-constant fill is the only
     *  pattern a 1:1 point-clamp blit reproduces byte-exact on EVERY renderer (the Software sprite
     *  sampler bleeds ~1 LSB across any interior colour boundary at a fractional position). */
    const Color kTL(220, 40, 40, 255);
    const Color kTR(40, 200, 60, 255);
    const Color kBL(60, 90, 230, 255);
    const Color kBR(128, 96, 160, 255);   ///< the mid-tone quadrant

    /**
     * @brief The asymmetric backbuffer pattern, addressed by ABSOLUTE backbuffer coordinate (and the
     *        WHOLE backbuffer w x h) so a sub-rectangle reads the same texels a full read would.
     */
    Color PatternColor(int x, int y, int w, int h)
    {
        const bool left = x < w / 2;
        const bool top = y < h / 2;
        return top ? (left ? kTL : kTR) : (left ? kBL : kBR);
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

/**
 * @brief REMED-GFX-161's first-read completion contract, one leg per process.
 */
class BackbufferFirstReadTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::string onlyLeg_;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    bool boundaryDeclared_ = false;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void boundary(const std::string& text)
    {
        boundaryDeclared_ = true;
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    void step(const std::string& text) const
    {
        std::printf("[STEP] %s\n", text.c_str());
        std::fflush(stdout);
    }

    [[nodiscard]] bool wants(const char* id) const { return onlyLeg_.empty() || onlyLeg_ == id; }

    // ------------------------------------------------------------------ pattern + readback

    /** @brief Draws the asymmetric pattern across the WHOLE backbuffer (no SetRenderTarget). */
    void DrawPattern(GraphicsDevice& dev, int w, int h)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.Clear(Color(0, 0, 0, 255));

        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const Color c = PatternColor(x, y, w, h);
                const std::size_t o = (static_cast<std::size_t>(y) * w + x) * 4;
                px[o + 0] = c.getRProperty(); px[o + 1] = c.getGProperty();
                px[o + 2] = c.getBProperty(); px[o + 3] = c.getAProperty();
            }
        Texture2D pattern = Texture2D::CreateFromPixels(dev, w, h, px);
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(pattern, Rectangle(0, 0, w, h), Rectangle(0, 0, w, h), Color::White);
        sb.End();
    }

    /** @brief Only a Clear -- no texture upload, no sprite -- so the first read is the very first
     *         GPU-completion-requiring operation of the process. */
    void ClearOnly(GraphicsDevice& dev, const Color& c)
    {
        dev.setViewportProperty(Viewport(0, 0, kW, kH));
        dev.Clear(c);
    }

    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;   ///< the whole destination buffer, sized by the caller
        int w = 0;
        int h = 0;
        int base = 0;                ///< startIndex the pixels were requested at

        [[nodiscard]] bool ok() const { return !threwNotSupported && !threwSomethingElse; }
        [[nodiscard]] const Color& at(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(base) + static_cast<std::size_t>(y) * w + x];
        }
    };

    /** @brief Rectangle-less read of exactly w*h elements into a poison-prefilled buffer. */
    Readback ReadRectless(GraphicsDevice& dev, int w, int h, int startIndex = 0, int extraCapacity = 0)
    {
        Readback r;
        r.w = w; r.h = h; r.base = startIndex;
        const std::size_t count = static_cast<std::size_t>(w) * h;
        r.pixels.assign(static_cast<std::size_t>(startIndex) + count + extraCapacity, kSentinel);
        try
        {
            if (startIndex == 0 && extraCapacity == 0)
                dev.GetBackBufferData(r.pixels.data(), static_cast<int>(count));
            else
                dev.GetBackBufferData(r.pixels.data(), startIndex, static_cast<int>(count));
        }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    /** @brief Explicit-rectangle read into a poison-prefilled buffer of exactly rect.w*rect.h. */
    Readback ReadRect(GraphicsDevice& dev, const Rectangle& rect)
    {
        Readback r;
        r.w = rect.Width; r.h = rect.Height; r.base = 0;
        const std::size_t count = static_cast<std::size_t>(rect.Width) * rect.Height;
        r.pixels.assign(count, kSentinel);
        try { dev.GetBackBufferData(&rect, r.pixels.data(), 0, static_cast<int>(count)); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    bool Readable(const Readback& r, const std::string& label)
    {
        if (!kRasterizes)
        {
            // REMED-GFX-162: Headless rasterizes nothing, so every backbuffer read must REJECT with
            // NotSupportedException -- it no longer fabricates the last Clear() colour. This was a
            // silent "oracle unavailable" boundary; it is now an asserted rejection so this fixture
            // can no longer pass while Headless quietly hands back a fabricated frame. The full
            // overload/precedence/destination-integrity matrix lives in
            // backbuffer_headless_reject_test.cpp (REMED-GFX-162).
            check(r.threwNotSupported,
                  label + ": Headless rejects backbuffer readback with NotSupportedException (GFX-162)");
            return false;
        }
        if (r.threwNotSupported)
        {
            boundary(label + ": oracle unavailable on " + kRendererName + " (NotSupportedException)");
            return false;
        }
        if (r.threwSomethingElse)
        {
            check(false, label + ": read threw: " + r.otherWhat);
            return false;
        }
        return true;
    }

    /**
     * @brief The core assertion: within the requested region, NO element may still hold the poison
     *        sentinel, and every element must equal the rendered pattern. Reports the EXACT unwritten
     *        range (first/last index and coordinate, count, and whether it is a suffix of whole rows)
     *        and the first wrong-content pixel.
     *
     * @param originX,originY the pattern origin the region maps to (0,0 for a full read; the rect
     *        origin for a sub-rectangle).
     * @param fullW,fullH the WHOLE backbuffer dimensions the absolute pattern is keyed on.
     */
    void CheckFullyWrittenExact(const Readback& r, const std::string& label, int originX, int originY,
                                int fullW, int fullH)
    {
        const int w = r.w, h = r.h;
        const int total = w * h;
        int unwritten = 0, wrong = 0, good = 0;
        int firstUnIdx = -1, lastUnIdx = -1, firstUnX = -1, firstUnY = -1, lastUnX = -1, lastUnY = -1;
        std::string firstWrong;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const int idx = y * w + x;
                const Color& got = r.at(x, y);
                const Color want = PatternColor(originX + x, originY + y, fullW, fullH);
                if (Same(got, want)) { ++good; continue; }
                if (IsSentinel(got))
                {
                    ++unwritten;
                    if (firstUnIdx < 0) { firstUnIdx = idx; firstUnX = x; firstUnY = y; }
                    lastUnIdx = idx; lastUnX = x; lastUnY = y;
                }
                else
                {
                    ++wrong;
                    if (firstWrong.empty())
                        firstWrong = " first-wrong (" + std::to_string(x) + "," + std::to_string(y) +
                                     ") want " + ColorText(want) + " got " + ColorText(got);
                }
            }

        if (unwritten > 0)
        {
            const bool suffixRows = (lastUnIdx == total - 1) && (firstUnIdx % w == 0);
            std::printf("[INFO] %s: UNWRITTEN %d/%d elements, idx [%d..%d] coord (%d,%d)..(%d,%d)%s\n",
                        label.c_str(), unwritten, total, firstUnIdx, lastUnIdx,
                        firstUnX, firstUnY, lastUnX, lastUnY,
                        suffixRows ? " (a trailing block of whole rows -- the final rows)" : "");
            std::fflush(stdout);
        }
        check(unwritten == 0, label + ": no element left unwritten (" + std::to_string(unwritten) +
                                  "/" + std::to_string(total) + " still poison)");
        check(wrong == 0, label + ": every written element is the exact pattern (" +
                              std::to_string(good) + "/" + std::to_string(total) + " correct)" + firstWrong);
    }

    void ReportState(GraphicsDevice& dev, const std::string& label)
    {
        const auto& pp = dev.getPresentationParametersProperty();
        const Viewport vp = dev.getViewportProperty();
        std::printf("[INFO] %s: PresentationParameters=%dx%d Viewport=%dx%d\n", label.c_str(),
                    pp.getBackBufferWidthProperty(), pp.getBackBufferHeightProperty(),
                    vp.getWidthProperty(), vp.getHeightProperty());
        std::fflush(stdout);
    }

    // ------------------------------------------------------------------ legs

    /// A1 -- the canonical repro: the very first readback of the process, rectangle-less, startIndex 0.
    void LegCanonical(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        ReportState(dev, "A1");
        step("A1: the FIRST GetBackBufferData of the process, rectangle-less, " +
             std::to_string(kW * kH) + " elements");
        Readback r = ReadRectless(dev, kW, kH);
        if (Readable(r, "A1"))
            CheckFullyWrittenExact(r, "A1 first read", 0, 0, kW, kH);
    }

    /// A3 -- first-vs-second: read #1 is discarded, then a fresh frame is drawn and read #2 (still the
    /// same process, REUSING the readback staging created on read #1) must be exact. The redraw makes
    /// this renderer-neutral: a double-buffered backbuffer (Bgfx) hands back the swapped/empty buffer on
    /// a second read that has no new frame -- a pre-existing property, not this task's subject.
    void LegSecondRead(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        (void)ReadRectless(dev, kW, kH);      // the first read; creates the readback staging, not asserted
        DrawPattern(dev, kW, kH);             // a fresh frame so a swapped backbuffer has content again
        step("A3: the SECOND readback of the process (reusing the staging) must be exact");
        Readback r = ReadRectless(dev, kW, kH);
        if (Readable(r, "A3"))
            CheckFullyWrittenExact(r, "A3 second read", 0, 0, kW, kH);
    }

    /// A3 -- first read after ONLY a Clear (no texture upload, no sprite draw before it).
    void LegClearOnly(GraphicsDevice& dev)
    {
        const Color c(0x11, 0x22, 0x33, 0xFF);   // not the sentinel, not the pattern
        ClearOnly(dev, c);
        step("A4: first read after a bare Clear -- every element must be the clear colour");
        Readback r = ReadRectless(dev, kW, kH);
        if (!Readable(r, "A4")) return;
        int unwritten = 0, wrong = 0;
        for (const Color& got : r.pixels)
        {
            if (Same(got, c)) continue;
            if (IsSentinel(got)) ++unwritten; else ++wrong;
        }
        check(unwritten == 0, "A4 clear-only first read leaves no element unwritten (" +
                                  std::to_string(unwritten) + " poison)");
        check(wrong == 0, "A4 clear-only first read is the clear colour everywhere (" +
                              std::to_string(wrong) + " off-colour)");
    }

    /// A5 -- first read after several draws (three overlapping sprite cycles), then read.
    void LegSeveralDraws(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        // Two extra small opaque overdraws that the final pattern re-covers, exercising a busier
        // first frame without changing the expected result.
        DrawPattern(dev, kW, kH);
        DrawPattern(dev, kW, kH);
        step("A6: first read after several draws");
        Readback r = ReadRectless(dev, kW, kH);
        if (Readable(r, "A6"))
            CheckFullyWrittenExact(r, "A6 first read after several draws", 0, 0, kW, kH);
    }

    /// B1 -- first read via the EXPLICIT full rectangle overload.
    void LegRectFull(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        step("B1: first read via explicit full rectangle");
        Readback r = ReadRect(dev, Rectangle(0, 0, kW, kH));
        if (Readable(r, "B1"))
            CheckFullyWrittenExact(r, "B1 explicit full rectangle", 0, 0, kW, kH);
    }

    /// B2 -- first read of a one-ROW rectangle (h == 1), at the top.
    void LegRectOneRow(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        if (!kSupportsSubRectangleBackbufferRead)
        {
            boundary("B2: sub-rectangle backbuffer read is a pre-existing gap on " +
                     std::string(kRendererName) + " -- recorded, not exercised");
            return;
        }
        step("B2: first read of a one-row rectangle");
        Readback r = ReadRect(dev, Rectangle(0, 0, kW, 1));
        if (Readable(r, "B2"))
            CheckFullyWrittenExact(r, "B2 one-row rectangle", 0, 0, kW, kH);
    }

    /// B3 -- first read of the FINAL-ROW rectangle (load-bearing last row).
    void LegRectFinalRow(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        if (!kSupportsSubRectangleBackbufferRead)
        {
            boundary("B3: sub-rectangle backbuffer read is a pre-existing gap on " +
                     std::string(kRendererName) + " -- recorded, not exercised");
            return;
        }
        step("B3: first read of the final-row rectangle");
        Readback r = ReadRect(dev, Rectangle(0, kH - 1, kW, 1));
        if (Readable(r, "B3"))
            CheckFullyWrittenExact(r, "B3 final-row rectangle", 0, kH - 1, kW, kH);
    }

    /// B4 -- first read of a one-PIXEL rectangle in the bottom-right (final row, final column).
    void LegRectOnePixel(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        if (!kSupportsSubRectangleBackbufferRead)
        {
            boundary("B4: sub-rectangle backbuffer read is a pre-existing gap on " +
                     std::string(kRendererName) + " -- recorded, not exercised");
            return;
        }
        step("B4: first read of a one-pixel rectangle");
        Readback r = ReadRect(dev, Rectangle(kW - 1, kH - 1, 1, 1));
        if (Readable(r, "B4"))
            CheckFullyWrittenExact(r, "B4 one-pixel rectangle", kW - 1, kH - 1, kW, kH);
    }

    /// C1 -- destination prefix/suffix guards: a non-zero startIndex with extra trailing capacity.
    /// The pixels must land at [startIndex, startIndex+w*h) and neither the prefix nor the suffix
    /// may be disturbed. (Non-zero startIndex is used ONLY as a guard; GFX-149 stays separate.)
    void LegPrefixSuffix(GraphicsDevice& dev)
    {
        DrawPattern(dev, kW, kH);
        const int prefix = 5, suffix = 3;
        step("C1: first read into a buffer with a poison prefix and suffix");
        Readback r = ReadRectless(dev, kW, kH, prefix, suffix);
        if (!Readable(r, "C1")) return;

        bool prefixIntact = true;
        for (int i = 0; i < prefix; ++i)
            if (!IsSentinel(r.pixels[i])) prefixIntact = false;
        check(prefixIntact, "C1 destination prefix before startIndex left untouched");

        const std::size_t suffixStart = static_cast<std::size_t>(prefix) + static_cast<std::size_t>(kW) * kH;
        bool suffixIntact = true;
        for (int i = 0; i < suffix; ++i)
            if (!IsSentinel(r.pixels[suffixStart + i])) suffixIntact = false;
        check(suffixIntact, "C1 destination suffix after the range left untouched");

        CheckFullyWrittenExact(r, "C1 pixels land at startIndex, fully written", 0, 0, kW, kH);
    }

    /// D -- row-pitch matrix: a width whose unpadded row byte count is just below / equal to / just
    /// above WebGPU's 256-byte bytesPerRow alignment boundary. w*4 aligned means w a multiple of 64.
    void LegRowPitch(GraphicsDevice& dev, int w, int h, const std::string& id)
    {
        DrawPattern(dev, w, h);
        ReportState(dev, id);
        step(id + ": first read at " + std::to_string(w) + "x" + std::to_string(h) +
             " (row bytes " + std::to_string(w * 4) + ", aligned pitch " +
             std::to_string(((w * 4 + 255) / 256) * 256) + ")");
        Readback r = ReadRectless(dev, w, h);
        if (Readable(r, id))
            CheckFullyWrittenExact(r, id + " row-pitch first read", 0, 0, w, h);
    }

    /// E1 -- the canonical repro at REMED-GFX-157's original 64x32 backbuffer size, first read.
    void LegCanonicalAt(GraphicsDevice& dev, int w, int h, const std::string& id)
    {
        DrawPattern(dev, w, h);
        ReportState(dev, id);
        step(id + ": the FIRST GetBackBufferData of the process at " + std::to_string(w) + "x" +
             std::to_string(h) + ", rectangle-less");
        Readback r = ReadRectless(dev, w, h);
        if (Readable(r, id))
            CheckFullyWrittenExact(r, id + " first read", 0, 0, w, h);
    }

    // ------------------------------------------------------------------ driver

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        try
        {
            if (wants("A1")) LegCanonical(dev);
            if (wants("E1")) LegCanonicalAt(dev, 64, 32, "E1");
            if (wants("A3")) LegSecondRead(dev);
            if (wants("A4")) LegClearOnly(dev);
            if (wants("A6")) LegSeveralDraws(dev);
            if (wants("B1")) LegRectFull(dev);
            if (wants("B2")) LegRectOneRow(dev);
            if (wants("B3")) LegRectFinalRow(dev);
            if (wants("B4")) LegRectOnePixel(dev);
            if (wants("C1")) LegPrefixSuffix(dev);
            if (wants("D63")) LegRowPitch(dev, 63, 17, "D63");   // row bytes 252, pad 4
            if (wants("D64")) LegRowPitch(dev, 64, 17, "D64");   // row bytes 256, pad 0 (aligned)
            if (wants("D65")) LegRowPitch(dev, 65, 17, "D65");   // row bytes 260, pad 252
        }
        catch (const std::exception& e)
        {
            check(false, std::string("leg ") + onlyLeg_ + ": unexpected exception: " + e.what());
        }
        Finish();
    }

    void Finish()
    {
        std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_ && (totalCount_ > 0 || boundaryDeclared_)) ? 0 : 1;
        Exit();
    }

public:
    explicit BackbufferFirstReadTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        int startW = kW, startH = kH;
        if (onlyLeg_ == "D63") { startW = 63; startH = 17; }
        else if (onlyLeg_ == "D64") { startW = 64; startH = 17; }
        else if (onlyLeg_ == "D65") { startW = 65; startH = 17; }
        else if (onlyLeg_ == "E1") { startW = 64; startH = 32; }
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(startW);
        gdm_->setPreferredBackBufferHeightProperty(startH);
    }

    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    const char* const kLegIds[] = {
        "A1", "A3", "A4", "A6", "B1", "B2", "B3", "B4", "C1", "D63", "D64", "D65", "E1"
    };

#if CNA_GFX161_CAN_FORK
    constexpr unsigned kLegTimeoutSeconds = 180;

    // Matches CNA::Examples::kSkipExitCode (common/PixelTestGame.hpp) -- a leg that exits with this
    // code decided for itself, before this supervisor is involved, that its environment can't
    // support the check (LLGL-55: e.g. the shared Vulkan-WSI-unavailable guard,
    // examples/common/LlglVulkanWsiSkipGuard.cpp). That is not the same as the leg failing, so it
    // must not be counted or reported as one.
    constexpr int kLegSkipExitCode = 77;

    bool RunLegIsolated(const char* exePath, const char* legId, bool& crashed, bool& skipped)
    {
        crashed = false;
        skipped = false;
        std::string arg = std::string("--leg=") + legId;
        const pid_t pid = fork();
        if (pid < 0) { std::printf("[FAIL] supervisor: fork() failed for leg %s\n", legId); return false; }
        if (pid == 0)
        {
            alarm(kLegTimeoutSeconds);
            char* argv[3];
            argv[0] = const_cast<char*>(exePath);
            argv[1] = const_cast<char*>(arg.c_str());
            argv[2] = nullptr;
            execv(exePath, argv);
            std::_Exit(127);
        }
        int status = 0;
        while (waitpid(pid, &status, 0) < 0) { /* EINTR */ }
        if (WIFSIGNALED(status))
        {
            const int sig = WTERMSIG(status);
            crashed = true;
            if (sig == SIGALRM)
                std::printf("[TIMEOUT] leg %s: no result within %u s\n", legId, kLegTimeoutSeconds);
            else
                std::printf("[CRASH] leg %s: killed by signal %d (%s)%s\n", legId, sig, strsignal(sig),
                            WCOREDUMP(status) ? " (core dumped)" : "");
            std::fflush(stdout);
            return false;
        }
        if (WIFEXITED(status))
        {
            const int code = WEXITSTATUS(status);
            if (code == 0) return true;
            if (code == kLegSkipExitCode)
            {
                skipped = true;
                std::printf("[SKIP] leg %s: exited %d\n", legId, code);
                std::fflush(stdout);
                return false;
            }
            std::printf("[FAIL] leg %s: exited %d\n", legId, code);
            std::fflush(stdout);
            return false;
        }
        std::printf("[FAIL] leg %s: neither exited nor signalled (status %d)\n", legId, status);
        std::fflush(stdout);
        return false;
    }
#endif
}

int main(int argc, char** argv)
{
    std::string onlyLeg;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a.rfind("--leg=", 0) == 0) onlyLeg = a.substr(6);
    }

#if CNA_GFX161_CAN_FORK
    if (onlyLeg.empty())
    {
        std::printf("[INFO] REMED-GFX-161 supervisor: %zu legs, each in its own process\n",
                    sizeof(kLegIds) / sizeof(kLegIds[0]));
        std::fflush(stdout);
        int passed = 0, crashes = 0, skips = 0;
        const int total = static_cast<int>(sizeof(kLegIds) / sizeof(kLegIds[0]));
        for (const char* leg : kLegIds)
        {
            bool crashed = false, skipped = false;
            if (RunLegIsolated(argv[0], leg, crashed, skipped)) ++passed;
            if (crashed) ++crashes;
            if (skipped) ++skips;
        }
        std::printf("[INFO] supervisor: %d/%d legs passed, %d crashed, %d skipped\n",
                    passed, total, crashes, skips);
        std::fflush(stdout);
        const int failed = total - passed - skips;
        if (failed > 0) return 1;
        return (skips > 0) ? kLegSkipExitCode : 0;
    }
#endif

    BackbufferFirstReadTest game(onlyLeg);
    game.Run();
    return game.Result();
}
