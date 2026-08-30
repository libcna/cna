// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-165 -- the authoritative-backbuffer-dimension contract for GraphicsDevice.GetBackBufferData.
//
// A rectangle-less GetBackBufferData addresses the COMPLETE logical backbuffer: for a Color backbuffer
// reported by PresentationParameters as W x H, a call passing exactly W*H Color elements must succeed
// and return the whole backbuffer, byte-exact, on the FIRST observable frame -- with NO SetRenderTarget
// round trip, no extra Present, no dummy draw and no viewport reset first. Its required element count
// must come from the backbuffer description, never from the renderer's live viewport, a previously bound
// target, a stale drawable size or a lazily initialised pass extent.
//
// Pre-fix, WebGPU and SDL_GPU sized the rectangle-less region from the renderer's live viewport
// (GraphicsDevice::GetBackBufferData asked renderer_->GetViewportSize), which under the default
// FixedHeightDynamicWidth presentation mode is height-locked to the virtual resolution and width-scaled
// by the window's aspect and, before the window is realised, is derived from a stale physical size --
// so a full-surface read of exactly W*H elements was rejected with "data array too small for requested
// region" on those two renderers while it was byte-exact on Software, EasyGL, Vulkan and Bgfx.
//
// Each leg runs in its own process (fork+exec) so its FIRST frame is genuinely first: no earlier leg's
// SetRenderTarget or Present can warm the surface up. A leg's exit code is the only PASS/FAIL signal; a
// crash or a hang is a distinct, attributable result.

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
#define CNA_GFX165_CAN_FORK 1
#else
#define CNA_GFX165_CAN_FORK 0
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
#elif defined(CNA_RENDERER_SKIA)
    constexpr const char* kRendererName = "SKIA";
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
#error "REMED-GFX-165: this renderer has no declared backbuffer-readback contract."
#endif

    // REMED-GFX-165 cross-renderer control scope. These are PRE-EXISTING renderer behaviours this task
    // must NOT change (only WebGPU/SDL_GPU production changes here); they are recorded as independent
    // findings and declared as boundaries rather than failed:
    //   - BGFX's backbuffer read honours only a full-surface read; a sub-rectangle reads back zeros
    //     (the rectangle-coordinate path through GraphicsDevice::GetBackBufferData is unchanged by this
    //     task, so this is Bgfx's own ReadBackbuffer, not the shared fix).
    //   - BGFX faults inside a runtime backbuffer resize (its bgfx::reset path, cf. REMED-GFX-158).
#if defined(CNA_RENDERER_BGFX)
    constexpr bool kSupportsSubRectangleBackbufferRead = false;
    constexpr bool kSupportsRuntimeResize = false;
#else
    constexpr bool kSupportsSubRectangleBackbufferRead = true;
    constexpr bool kSupportsRuntimeResize = true;
#endif

    // Two distinctive, non-default, non-square backbuffer sizes that cannot be confused with a common
    // 800x480 default, a 64x64 fixture default or each other. Odd on purpose: an off-by-one in a
    // row-pitch or an aspect-scaled viewport width shows up immediately.
    constexpr int kW1 = 37;
    constexpr int kH1 = 23;
    constexpr int kW2 = 41;
    constexpr int kH2 = 29;

    /** @brief The four asymmetric quadrant colours -- every corner distinct, top != bottom (so an
     *  orientation flip is caught), left != right (so a vertical mirror is caught), one mid-tone. */
    const Color kTL(220, 40, 40, 255);
    const Color kTR(40, 200, 60, 255);
    const Color kBL(60, 90, 230, 255);
    const Color kBR(128, 96, 160, 255);   ///< the mid-tone quadrant

    Color ExpectedColor(int x, int y, int w, int h)
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
 * @brief REMED-GFX-165's public contract, one leg per process. The backbuffer size is chosen per leg so
 *        every leg's first frame is at a size no other leg warmed up.
 */
class BackbufferReadbackDimensionTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::string onlyLeg_;
    int  result_ = 1;
    int  passCount_ = 0;
    int  totalCount_ = 0;
    bool boundaryDeclared_ = false;


    int startW_ = kW1;
    int startH_ = kH1;

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

    /**
     * @brief Draws the four-quadrant asymmetric pattern across the WHOLE backbuffer, with NO
     *        SetRenderTarget call -- the backbuffer is the device's default destination.
     */
    void DrawPattern(GraphicsDevice& dev, int w, int h)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        // Render across the WHOLE backbuffer: a full-backbuffer viewport (matrix case 1). This is
        // rendering setup, orthogonal to the readback-dimension contract under test -- the readback
        // succeeds at W x H whatever the viewport is (legs C1 et al. prove that with a sub-viewport).
        dev.setViewportProperty(Viewport(0, 0, w, h));
        dev.Clear(Color(0, 0, 0, 255));

        // Build the exact W x H pattern and blit it 1:1 (no scaling) so every renderer's point sampler
        // maps each destination pixel to exactly one texel -- byte-exact on all renderers, including the
        // Software rasterizer whose upscaling sampler otherwise perturbs stretched fills by 1 LSB.
        std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const Color c = ExpectedColor(x, y, w, h);
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

    struct Readback
    {
        bool threwNotSupported = false;
        bool threwSomethingElse = false;
        std::string otherWhat;
        std::vector<Color> pixels;   ///< the destination buffer, sized by the caller
        int w = 0;
        int h = 0;
        int base = 0;                ///< startIndex the pixels were written at

        [[nodiscard]] bool ok() const { return !threwNotSupported && !threwSomethingElse; }
        [[nodiscard]] const Color& at(int x, int y) const
        {
            return pixels[static_cast<std::size_t>(base) + static_cast<std::size_t>(y) * w + x];
        }
    };

    /** @brief The rectangle-LESS overload: the subject of this task. Reads exactly w*h elements. */
    Readback ReadRectless(GraphicsDevice& dev, int w, int h, int startIndex = 0, int extraCapacity = 0)
    {
        Readback r;
        r.w = w; r.h = h; r.base = startIndex;
        const std::size_t count = static_cast<std::size_t>(w) * h;
        r.pixels.assign(static_cast<std::size_t>(startIndex) + count + extraCapacity,
                        Color(0xCD, 0xCD, 0xCD, 0xCD));
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

    /** @brief The explicit-rectangle overload, for the rectangle matrix. */
    Readback ReadRect(GraphicsDevice& dev, const Rectangle& rect, int capacity)
    {
        Readback r;
        r.w = rect.Width; r.h = rect.Height; r.base = 0;
        r.pixels.assign(static_cast<std::size_t>(capacity), Color(0xCD, 0xCD, 0xCD, 0xCD));
        try { dev.GetBackBufferData(&rect, r.pixels.data(), 0, capacity); }
        catch (const System::NotSupportedException&) { r.threwNotSupported = true; }
        catch (const std::exception& e) { r.threwSomethingElse = true; r.otherWhat = e.what(); }
        catch (...) { r.threwSomethingElse = true; r.otherWhat = "(non-std exception)"; }
        return r;
    }

    bool Readable(const Readback& r, const std::string& label)
    {
        if (!kRasterizes || r.threwNotSupported)
        {
            boundary(label + ": oracle unavailable on " + kRendererName + " (" +
                     (!kRasterizes ? "non-rasterizing" : "NotSupportedException") + ")");
            return false;
        }
        return true;
    }

    /** @brief Every pixel of the read region must equal the pattern; names the first mismatch. */
    void CheckWholeExact(const Readback& r, const std::string& label, int w, int h)
    {
        if (r.w != w || r.h != h)
        {
            check(false, label + ": read region " + std::to_string(r.w) + "x" + std::to_string(r.h) +
                             " but backbuffer is " + std::to_string(w) + "x" + std::to_string(h));
            return;
        }
        int good = 0;
        const int total = w * h;
        std::string first;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const Color& got = r.at(x, y);
                if (Same(got, ExpectedColor(x, y, w, h))) { ++good; continue; }
                if (first.empty())
                    first = " first (" + std::to_string(x) + "," + std::to_string(y) + ") want " +
                            ColorText(ExpectedColor(x, y, w, h)) + " got " + ColorText(got);
            }
        check(good == total, label + " (" + std::to_string(good) + "/" + std::to_string(total) + ")" + first);
    }

    /** @brief Whether the read succeeded AND has the exact size asked for. */
    void CheckSizedOk(const Readback& r, const std::string& label, int w, int h)
    {
        if (!kRasterizes)
        {
            // REMED-GFX-162: a non-rasterizing renderer rasterizes no backbuffer, so a VALID read
            // must reject with NotSupportedException rather than fabricate a correctly-sized frame
            // (it formerly filled the caller's buffer with the last Clear() colour). The dimension
            // oracle is therefore unavailable here; the honest, deterministic rejection is asserted
            // instead. The full rejection matrix lives in backbuffer_headless_reject_test.cpp.
            check(r.threwNotSupported,
                  label + ": non-rasterizing renderer rejects the readback with NotSupportedException "
                          "(GFX-162), got " + (r.ok() ? "success" : r.otherWhat));
            return;
        }
        if (!r.ok())
        {
            check(false, label + ": rejected -- " +
                             (r.threwNotSupported ? "NotSupportedException" : r.otherWhat));
            return;
        }
        check(r.w == w && r.h == h, label + ": returned " + std::to_string(r.w) + "x" +
                                        std::to_string(r.h) + " (expected " + std::to_string(w) + "x" +
                                        std::to_string(h) + ")");
    }

    /// A rectangle read is rejected deterministically, before any native copy could run.
    void CheckRejected(const Readback& r, const std::string& label)
    {
        check(r.threwSomethingElse, label + ": expected a deterministic rejection, got " +
                                        (r.ok() ? "success" : "NotSupportedException"));
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

    /// The canonical repro: first frame, no round trip, rectangle-less read of exactly W*H elements.
    void LegFirstFrame(GraphicsDevice& dev, int w, int h, const std::string& id)
    {
        DrawPattern(dev, w, h);
        ReportState(dev, id);
        step(id + ": rectangle-less GetBackBufferData of exactly " + std::to_string(w * h) + " elements");
        Readback r = ReadRectless(dev, w, h);
        CheckSizedOk(r, id, w, h);
        if (Readable(r, id) && r.ok())
            CheckWholeExact(r, id + " full backbuffer exact", w, h);
    }

    /// Differential control: ONE SetRenderTarget round trip before the read must not change the result
    /// (it must succeed WITHOUT the round trip -- this proves the round trip is not what fixes it).
    void LegRoundTripControl(GraphicsDevice& dev, int w, int h)
    {
        RenderTarget2D rt(dev, 8, 8, false, SurfaceFormat::Color, DepthFormat::None, 0,
                          RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&rt);
        dev.Clear(Color(10, 20, 30, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        DrawPattern(dev, w, h);
        Readback r = ReadRectless(dev, w, h);
        CheckSizedOk(r, "B1", w, h);
        if (Readable(r, "B1") && r.ok())
            CheckWholeExact(r, "B1 round-trip control reads the whole backbuffer", w, h);
    }

    /// A viewport SMALLER than the backbuffer must not shrink a rectangle-less read.
    void LegSubViewport(GraphicsDevice& dev, int w, int h)
    {
        DrawPattern(dev, w, h);
        dev.setViewportProperty(Viewport(3, 5, 9, 7));
        ReportState(dev, "C1");
        Readback r = ReadRectless(dev, w, h);
        CheckSizedOk(r, "C1", w, h);
        if (Readable(r, "C1") && r.ok())
            CheckWholeExact(r, "C1 sub-viewport still reads the whole backbuffer", w, h);
    }

    void LegRectangleMatrix(GraphicsDevice& dev, int w, int h)
    {
        DrawPattern(dev, w, h);
        struct RectCase { Rectangle rect; const char* name; bool sub; };
        const RectCase valid[] = {
            { Rectangle(0, 0, w, h),                  "D-full",        false },
            { Rectangle(5, 7, 1, 1),                  "D-onepx",       true  },
            { Rectangle(0, 0, 10, 6),                 "D-topleft",     true  },
            { Rectangle(w - 10, h - 6, 10, 6),        "D-bottomright", true  },
            { Rectangle(w / 3, h / 3, w / 4, h / 5),  "D-centred",     true  },
        };
        if (!kSupportsSubRectangleBackbufferRead)
            boundary("D1: sub-rectangle backbuffer read is a pre-existing gap on " +
                     std::string(kRendererName) + " (full-surface read only) -- recorded, not failed");
        for (const RectCase& c : valid)
        {
            if (c.sub && !kSupportsSubRectangleBackbufferRead) continue;
            Readback r = ReadRect(dev, c.rect, c.rect.Width * c.rect.Height);
            if (!r.ok()) { CheckSizedOk(r, c.name, c.rect.Width, c.rect.Height); continue; }
            if (!kRasterizes)
            {
                boundary(std::string(c.name) + ": read succeeded; pixel oracle unavailable on " +
                         kRendererName + " (non-rasterizing)");
                continue;
            }
            int good = 0;
            const int total = c.rect.Width * c.rect.Height;
            std::string first;
            for (int y = 0; y < c.rect.Height; ++y)
                for (int x = 0; x < c.rect.Width; ++x)
                {
                    const Color want = ExpectedColor(c.rect.X + x, c.rect.Y + y, w, h);
                    const Color& got = r.at(x, y);
                    if (Same(got, want)) { ++good; continue; }
                    if (first.empty())
                        first = " first (" + std::to_string(x) + "," + std::to_string(y) + ") want " +
                                ColorText(want) + " got " + ColorText(got);
                }
            check(good == total, std::string(c.name) + " (" + std::to_string(good) + "/" +
                                     std::to_string(total) + ")" + first);
        }

        // Invalid rectangles must be rejected deterministically, validated against the REAL backbuffer
        // bounds, before any native copy.
        const RectCase invalid[] = {
            { Rectangle(0, 0, w + 1, h),   "E-wide-by-1" },
            { Rectangle(0, 0, w, h + 1),   "E-tall-by-1" },
            { Rectangle(-1, 0, 4, 4),      "E-neg-origin" },
            { Rectangle(w, 0, 1, 1),       "E-x-at-width" },
            { Rectangle(0, h, 1, 1),       "E-y-at-height" },
        };
        for (const RectCase& c : invalid)
        {
            Readback r = ReadRect(dev, c.rect, (c.rect.Width * c.rect.Height > 0
                                                    ? c.rect.Width * c.rect.Height : 1) + 16);
            CheckRejected(r, c.name);
        }
    }

    void LegStartIndex(GraphicsDevice& dev, int w, int h)
    {
        DrawPattern(dev, w, h);
        // A non-zero destination prefix with sufficient total storage must land the pixels at the
        // prefix and leave the prefix untouched.
        const int prefix = 5;
        Readback r = ReadRectless(dev, w, h, prefix, 3);
        CheckSizedOk(r, "F1", w, h);
        if (Readable(r, "F1") && r.ok())
        {
            bool prefixIntact = true;
            for (int i = 0; i < prefix; ++i)
                if (!Same(r.pixels[i], Color(0xCD, 0xCD, 0xCD, 0xCD))) prefixIntact = false;
            check(prefixIntact, "F1 destination prefix left untouched");
            CheckWholeExact(r, "F1 pixels land at startIndex", w, h);
        }

        // One element too few must be rejected (this is the pre-fix symptom's own message, but here it
        // is CORRECT behaviour: the buffer really is too small).
        Readback few;
        few.pixels.assign(static_cast<std::size_t>(w) * h - 1, Color(0xCD, 0xCD, 0xCD, 0xCD));
        bool rejected = false;
        try { dev.GetBackBufferData(few.pixels.data(), static_cast<int>(few.pixels.size())); }
        catch (const std::exception&) { rejected = true; }
        catch (...) { rejected = true; }
        check(rejected, "F2 one element too few is rejected");
    }

    /**
     * @brief The AUTHORITATIVE readback dimension must follow a runtime resize/reset -- the required
     *        element count grows/shrinks with PresentationParameters, never staying at the old size or
     *        at a stale viewport.
     *
     * This asserts the GFX-165 dimension contract only. The PHYSICAL surface following a runtime resize
     * is a separate window-management concern: SDL_SetWindowSize is a no-op under the headless Xvfb the
     * native renderers run on here, so a window-surface renderer cannot grow its swapchain at runtime in
     * this environment. Full-surface pixel exactness after a resize is therefore verified on the
     * Software control (whose backbuffer is a CPU buffer that genuinely resizes), not here.
     */
    void LegResize(GraphicsDevice& dev, int fromW, int fromH, int toW, int toH, const std::string& id)
    {
        if (!kSupportsRuntimeResize)
        {
            boundary(id + ": runtime backbuffer resize is a pre-existing gap on " +
                     std::string(kRendererName) + " -- recorded, not exercised");
            return;
        }
        DrawPattern(dev, fromW, fromH);
        Readback before = ReadRectless(dev, fromW, fromH);
        CheckSizedOk(before, id + "-before", fromW, fromH);

        try
        {
            gdm_->setPreferredBackBufferWidthProperty(toW);
            gdm_->setPreferredBackBufferHeightProperty(toH);
            gdm_->ApplyChanges();
        }
        catch (const std::exception& e)
        {
            boundary(id + ": runtime resize not supported (" + e.what() + ")");
            return;
        }

        const auto& pp = dev.getPresentationParametersProperty();
        check(pp.getBackBufferWidthProperty() == toW && pp.getBackBufferHeightProperty() == toH,
              id + " PresentationParameters follows the resize");

        // The required element count now tracks the NEW logical backbuffer.
        Readback after = ReadRectless(dev, toW, toH);
        CheckSizedOk(after, id + "-after", toW, toH);

        // And the OLD element count is now judged against the new size -- growing means a from-sized
        // buffer is too small, shrinking means a to-sized buffer is exactly right (already covered).
        if (toW * toH > fromW * fromH)
        {
            Readback stale = ReadRectless(dev, fromW, fromH);
            check(stale.threwSomethingElse,
                  id + " a buffer sized for the OLD backbuffer is now rejected as too small");
        }
    }

    // ------------------------------------------------------------------ driver

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        try
        {
            if (wants("A1")) LegFirstFrame(dev, kW1, kH1, "A1");
            if (wants("A2")) LegFirstFrame(dev, kW2, kH2, "A2");
            if (wants("B1")) LegRoundTripControl(dev, kW1, kH1);
            if (wants("C1")) LegSubViewport(dev, kW1, kH1);
            if (wants("D1")) LegRectangleMatrix(dev, kW1, kH1);
            if (wants("F1")) LegStartIndex(dev, kW1, kH1);
            if (wants("G1")) LegResize(dev, kW1, kH1, 50, 40, "G1");
            if (wants("G2")) LegResize(dev, 50, 40, 30, 20, "G2");
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
    explicit BackbufferReadbackDimensionTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        // Legs that begin larger start there so the resize leg can shrink; everyone else starts at kW1.
        if (onlyLeg_ == "A2") { startW_ = kW2; startH_ = kH2; }
        else if (onlyLeg_ == "G2") { startW_ = 50; startH_ = 40; }
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(startW_);
        gdm_->setPreferredBackBufferHeightProperty(startH_);
    }

    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    const char* const kLegIds[] = { "A1", "A2", "B1", "C1", "D1", "F1", "G1", "G2" };

#if CNA_GFX165_CAN_FORK
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

#if CNA_GFX165_CAN_FORK
    if (onlyLeg.empty())
    {
        std::printf("[INFO] REMED-GFX-165 supervisor: %zu legs, each in its own process\n",
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

    BackbufferReadbackDimensionTest game(onlyLeg);
    game.Run();
    return game.Result();
}
