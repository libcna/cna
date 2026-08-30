// SPDX-License-Identifier: MS-PL
// REMED-GFX-127: every public Texture2D::GetData call must have exactly ONE honest outcome --
// it returns the resource's real content, or it rejects the request deterministically. It must
// never fabricate content out of the shared layer's own scratch memory.
//
// The defect this reproduces (shared, renderer-neutral, in Texture2D::GetData itself):
//
//     std::vector<uint8_t> pixels(total * 4, 0);   // zero-initialized by the SHARED layer
//     renderer_->GetData(0, 0, 0, w, h, pixels.data(), size);  // interface default = no-op
//     for (i) data[i] = Color(pixels[..]);          // converted for the caller REGARDLESS
//
// A renderer that overrides nothing therefore does not "leave the caller's buffer untouched": it
// returns a complete, confidently-wrong, uniformly transparent-black frame. That is strictly worse
// than an empty result, because
//   * the obvious oracle "did GetData overwrite my sentinel destination?" PASSES on it, and
//   * any assertion whose expected content happens to be transparent black also PASSES on it.
// Both facts are asserted below (checks Z1/Z2) so this file records WHY sentinel-only testing is
// not a valid success oracle, rather than merely avoiding it.
//
// What every renderer must satisfy after the fix:
//   * an ordinary Texture2D populated by SetData reads back EXACTLY, on every renderer, for the full
//     level, an arbitrary rectangle, an arbitrary destination offset and repeated calls;
//   * a RenderTarget2D readback either returns the exact rendered pattern, or throws a deterministic
//     System::NotSupportedException WITHOUT having touched one byte of the caller's destination;
//   * nothing in between: no fabricated frame, no partially written destination, no silent success.
//
// Each renderer's render-target readback capability is declared here explicitly (kRtContract) rather
// than inferred at runtime, so "this renderer cannot read render targets back" is a reviewed claim
// this test enforces, not an escape hatch that lets any result pass.
//
// The rendered/uploaded pattern deliberately carries opaque red, opaque green, opaque blue, a block
// whose alpha is neither 0 nor 255, and a semi-transparent background, so a fabricated (0,0,0,0)
// frame, a fallback white frame, an untouched destination and a channel-swapped readback all fail
// decisively and distinguishably.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBBW = 64;   ///< Backbuffer width.
    constexpr int kBBH = 32;   ///< Backbuffer height.
    constexpr int kW = 16;     ///< Texture / render-target width.
    constexpr int kH = 8;      ///< Texture / render-target height.
    constexpr int kBlock = 4;  ///< Side of each corner colour block.

    /**
     * @brief What this renderer's RenderTarget2D colour readback is required to do.
     *
     * `Exact` — GetData must return the rendered pattern byte for byte.
     * `Unsupported` — GetData must throw System::NotSupportedException and leave the caller's
     * destination byte-for-byte untouched. Reserved for renderers that perform no rasterization at
     * all, so a render target's colour attachment has no content they could honestly return.
     */
    enum class RtContract
    {
        Exact,
        Unsupported,
    };

#if defined(CNA_RENDERER_HEADLESS)
    // The Headless renderer deliberately executes no rasterization: it records API usage and
    // resource state for validation/tracing. A render target's colour attachment is produced only
    // by rendering, so this renderer has nothing real to return and must say so.
    constexpr RtContract kRtContract = RtContract::Unsupported;
    constexpr const char* kRendererName = "HEADLESS";
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "SOFTWARE";
#elif defined(CNA_RENDERER_EASYGL)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "EASYGL";
#elif defined(CNA_RENDERER_BGFX)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "BGFX";
#elif defined(CNA_RENDERER_VULKAN)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "VULKAN";
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "WEBGPU";
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "SDL_GPU";
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "SDL_RENDERER";
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "FREEDIRECT";
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "DIRECTX9";
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "DIRECTX11";
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "DIRECTX12";
#elif defined(CNA_RENDERER_CANVAS)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "CANVAS";
#else
#error "REMED-GFX-127: this renderer has no declared Texture2D::GetData render-target contract."
#endif

    /// Two unrelated destination pre-fills. Neither equals any pattern colour, so "still the
    /// sentinel" and "a real pixel" can never be confused for one another.
    Color SentinelCD() { return Color(0xCD, 0xCD, 0xCD, 0xCD); }
    Color SentinelA5() { return Color(0xA5, 0xA5, 0xA5, 0xA5); }

    /// The fabricated frame the pre-fix shared layer produces from a no-op renderer.
    Color Fabricated() { return Color(0, 0, 0, 0); }

    // Every RGB component is 0 or 255 and only the ALPHA carries an intermediate value. Alpha is
    // linear in every colour space these renderers use, while an intermediate RGB component is not:
    // WebGPU picks an *UnormSrgb surface/render-target format, so a mid-tone written through its
    // render pass comes back gamma-encoded (measured: 128 -> 188). That colour-space difference is
    // a separate, pre-existing concern; encoding it into this file's expectations would make a
    // readback-honesty regression fail for an unrelated reason on one renderer.
    Color BlockTopLeft()     { return Color(255, 0, 0, 255); }     ///< opaque red
    Color BlockTopRight()    { return Color(0, 255, 0, 255); }     ///< opaque green
    Color BlockBottomLeft()  { return Color(0, 0, 255, 255); }     ///< opaque blue
    Color BlockBottomRight() { return Color(255, 0, 255, 64); }    ///< alpha neither 0 nor 255
    Color Background()       { return Color(0, 255, 255, 128); }   ///< alpha neither 0 nor 255

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    /// The colour the pattern holds at (x, y). Used both to build the plain texture and to state
    /// what a render-target readback must return.
    Color PatternPixel(int x, int y)
    {
        const bool left = x < kBlock;
        const bool right = x >= kW - kBlock;
        const bool top = y < kBlock;
        if (left && top) return BlockTopLeft();
        if (right && top) return BlockTopRight();
        if (left && !top) return BlockBottomLeft();
        if (right && !top) return BlockBottomRight();
        return Background();
    }

    std::vector<Color> PatternPixels()
    {
        std::vector<Color> px;
        px.reserve(static_cast<std::size_t>(kW) * kH);
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x) px.push_back(PatternPixel(x, y));
        return px;
    }
}

class Texture2DGetDataContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    bool done_ = false;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    static void ResetState(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
    }

    template <typename ExceptionT, typename Fn>
    static bool Throws(Fn&& fn)
    {
        try { fn(); }
        catch (const ExceptionT&) { return true; }
        catch (...) { return false; }
        return false;
    }

    template <typename Fn>
    static bool ThrowsAnything(Fn&& fn)
    {
        try { fn(); }
        catch (...) { return true; }
        return false;
    }

    /// Fills one rectangle of exactly @p tint into whatever target is bound. BlendState::Opaque
    /// stores the source colour verbatim, alpha included.
    void FillRect(GraphicsDevice& dev, const Rectangle& destination, const Color& tint)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    /// Renders the canonical pattern into @p target and leaves the backbuffer active again.
    ///
    /// The asserted background is DRAWN, not cleared: `GraphicsDevice::Clear` on a bound render
    /// target interacts with each renderer's RenderTargetUsage handling, and this file is about
    /// readback honesty, not about clear semantics. The opaque-black Clear below only establishes a
    /// deterministic base that every asserted pixel is then painted over.
    void ProducePattern(GraphicsDevice& dev, RenderTarget2D& target)
    {
        dev.SetRenderTarget(&target);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        FillRect(dev, Rectangle(0, 0, kW, kH), Background());
        FillRect(dev, Rectangle(0, 0, kBlock, kBlock), BlockTopLeft());
        FillRect(dev, Rectangle(kW - kBlock, 0, kBlock, kBlock), BlockTopRight());
        FillRect(dev, Rectangle(0, kH - kBlock, kBlock, kBlock), BlockBottomLeft());
        FillRect(dev, Rectangle(kW - kBlock, kH - kBlock, kBlock, kBlock), BlockBottomRight());
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);
    }

    static std::size_t CountSame(const std::vector<Color>& pixels, const Color& c)
    {
        std::size_t n = 0;
        for (const Color& p : pixels)
            if (Same(p, c)) ++n;
        return n;
    }

    /// One classified render-target readback attempt: what the call did, in full.
    struct Readback
    {
        bool threwNotSupported = false;   ///< the deterministic "cannot read this back" rejection
        bool threwSomethingElse = false;  ///< any other exception
        std::string otherWhat;
        std::vector<Color> pixels;        ///< the destination after the call
        std::size_t sentinelSurvivors = 0;
        std::size_t fabricatedPixels = 0;
        std::size_t exactPixels = 0;
        std::size_t total = 0;
    };

    /// Performs a whole-level render-target readback into a destination pre-filled with @p sentinel
    /// and records every fact needed to classify the outcome -- without judging it yet.
    static Readback ProbeFullReadback(RenderTarget2D& target, const Color& sentinel)
    {
        Readback r;
        r.total = static_cast<std::size_t>(kW) * kH;
        r.pixels.assign(r.total, sentinel);
        try
        {
            target.GetData(r.pixels.data(), 0, static_cast<int>(r.total));
        }
        catch (const System::NotSupportedException&)
        {
            r.threwNotSupported = true;
        }
        catch (const std::exception& e)
        {
            r.threwSomethingElse = true;
            r.otherWhat = e.what();
        }
        catch (...)
        {
            r.threwSomethingElse = true;
            r.otherWhat = "(non-std exception)";
        }

        r.sentinelSurvivors = CountSame(r.pixels, sentinel);
        r.fabricatedPixels = CountSame(r.pixels, Fabricated());
        for (int y = 0; y < kH; ++y)
            for (int x = 0; x < kW; ++x)
                if (Same(r.pixels[static_cast<std::size_t>(y) * kW + x], PatternPixel(x, y)))
                    ++r.exactPixels;
        return r;
    }

    /// Asserts the single honest outcome this renderer declared, and prints the measured facts either
    /// way so a failure identifies which wrong outcome occurred.
    void CheckHonestOutcome(const Readback& r, const std::string& label)
    {
        const std::string facts =
            " [threwNotSupported=" + std::string(r.threwNotSupported ? "yes" : "no") +
            " otherThrow=" + (r.threwSomethingElse ? ("yes:" + r.otherWhat) : std::string("no")) +
            " sentinelSurvivors=" + std::to_string(r.sentinelSurvivors) + "/" +
            std::to_string(r.total) + " fabricated(0,0,0,0)=" + std::to_string(r.fabricatedPixels) +
            " exact=" + std::to_string(r.exactPixels) + "]";

        if (kRtContract == RtContract::Exact)
        {
            check(!r.threwNotSupported && !r.threwSomethingElse && r.exactPixels == r.total,
                  label + ": returned the exact rendered pattern" + facts);
        }
        else
        {
            check(r.threwNotSupported && !r.threwSomethingElse &&
                      r.sentinelSurvivors == r.total,
                  label + ": rejected readback deterministically without touching the caller's "
                          "destination" + facts);
        }

        // Independent of which contract applies: a fully fabricated frame is never acceptable.
        check(!(r.fabricatedPixels == r.total && !r.threwNotSupported && !r.threwSomethingElse),
              label + ".notfabricated: the call did not silently return a uniform (0,0,0,0) frame" +
                  facts);
    }

    // ---- Section A: ordinary Texture2D readback (CPU-shadow path, universal) --------------------

    void SectionA(GraphicsDevice& dev)
    {
        const std::vector<Color> pattern = PatternPixels();
        Texture2D tex(dev, kW, kH);
        tex.SetData(pattern.data(), static_cast<int>(pattern.size()));

        // A1/A2: full level into two different sentinel destinations.
        for (const Color sentinel : {SentinelCD(), SentinelA5()})
        {
            std::vector<Color> got(pattern.size(), sentinel);
            tex.GetData(got.data(), 0, static_cast<int>(got.size()));
            std::string detail;
            bool exact = true;
            for (std::size_t i = 0; i < got.size(); ++i)
                if (!Same(got[i], pattern[i]))
                {
                    exact = false;
                    detail = " first mismatch at index " + std::to_string(i) + ": " +
                             ColorText(got[i]) + " != " + ColorText(pattern[i]);
                    break;
                }
            check(exact, std::string("A1 plain Texture2D full readback (sentinel ") +
                             ColorText(sentinel) + ") is exact" + detail);
        }

        // A3: single pixel, middle rectangle, final row, final column, lower-right corner.
        struct RectCase { Rectangle rect; const char* name; };
        const RectCase cases[] = {
            {Rectangle(0, 0, 1, 1), "A3a one pixel at the origin"},
            {Rectangle(5, 2, 4, 3), "A3b middle rectangle"},
            {Rectangle(0, kH - 1, kW, 1), "A3c final row"},
            {Rectangle(kW - 1, 0, 1, kH), "A3d final column"},
            {Rectangle(kW - 1, kH - 1, 1, 1), "A3e lower-right corner pixel"},
            {Rectangle(0, 0, kW, kH), "A3f whole level through the rectangle overload"},
        };
        for (const RectCase& c : cases)
        {
            const int count = c.rect.Width * c.rect.Height;
            std::vector<Color> got(static_cast<std::size_t>(count), SentinelCD());
            tex.GetData(0, &c.rect, got.data(), 0, count);
            bool exact = true;
            std::string detail;
            for (int row = 0; row < c.rect.Height && exact; ++row)
                for (int col = 0; col < c.rect.Width; ++col)
                {
                    const Color expected = PatternPixel(c.rect.X + col, c.rect.Y + row);
                    const Color actual = got[static_cast<std::size_t>(row) * c.rect.Width + col];
                    if (!Same(actual, expected))
                    {
                        exact = false;
                        detail = " at (" + std::to_string(col) + "," + std::to_string(row) + "): " +
                                 ColorText(actual) + " != " + ColorText(expected);
                        break;
                    }
                }
            check(exact, std::string(c.name) + " is exact" + detail);
        }

        // A4: nonzero destination offset into an oversized destination -- the surrounding sentinels
        // must survive untouched. `startIndex` is a DESTINATION element offset on BOTH overloads;
        // when this check was written the whole-level overload applied it to the SOURCE instead,
        // which is why the note here used to exempt it (REMED-GFX-128 / REMED-GFX-149, both now
        // closed -- A4b below asserts the whole-level overload through the same oracle).
        {
            const Rectangle rect(3, 1, 5, 2);
            const int count = rect.Width * rect.Height;
            const int start = 7;
            std::vector<Color> got(static_cast<std::size_t>(count + start + 9), SentinelA5());
            tex.GetData(0, &rect, got.data(), start, static_cast<int>(got.size()) - start);

            bool leading = true;
            for (int i = 0; i < start; ++i)
                if (!Same(got[static_cast<std::size_t>(i)], SentinelA5())) leading = false;
            bool trailing = true;
            for (std::size_t i = static_cast<std::size_t>(start + count); i < got.size(); ++i)
                if (!Same(got[i], SentinelA5())) trailing = false;
            bool body = true;
            std::string detail;
            for (int row = 0; row < rect.Height && body; ++row)
                for (int col = 0; col < rect.Width; ++col)
                {
                    const Color expected = PatternPixel(rect.X + col, rect.Y + row);
                    const Color actual = got[static_cast<std::size_t>(start + row * rect.Width + col)];
                    if (!Same(actual, expected))
                    {
                        body = false;
                        detail = " at (" + std::to_string(col) + "," + std::to_string(row) + "): " +
                                 ColorText(actual) + " != " + ColorText(expected);
                        break;
                    }
                }
            check(body && leading && trailing,
                  "A4 destination offset 7 writes only its own range, leaving both sentinel margins "
                  "intact" + detail + (leading ? "" : " [leading margin overwritten]") +
                      (trailing ? "" : " [trailing margin overwritten]"));
        }

        // A4b: REMED-GFX-149 -- the SAME guarantee through the whole-level overload, which used to
        // reject every non-zero startIndex on a render target and, on a plain texture, apply it to
        // the source instead. The excess capacity beyond the region must also stay untouched.
        {
            const int start = 7;
            const int spare = 9;
            std::vector<Color> got(static_cast<std::size_t>(start) + pattern.size() + spare,
                                   SentinelA5());
            tex.GetData(got.data(), start, static_cast<int>(got.size()) - start);

            bool leading = true;
            for (int i = 0; i < start; ++i)
                if (!Same(got[static_cast<std::size_t>(i)], SentinelA5())) leading = false;
            bool trailing = true;
            for (std::size_t i = static_cast<std::size_t>(start) + pattern.size(); i < got.size(); ++i)
                if (!Same(got[i], SentinelA5())) trailing = false;
            bool body = true;
            std::string detail;
            for (std::size_t i = 0; i < pattern.size(); ++i)
                if (!Same(got[static_cast<std::size_t>(start) + i], pattern[i]))
                {
                    body = false;
                    detail = " at element " + std::to_string(i) + ": " +
                             ColorText(got[static_cast<std::size_t>(start) + i]) +
                             " != " + ColorText(pattern[i]);
                    break;
                }
            check(body && leading && trailing,
                  "A4b whole-level overload at destination offset 7 with excess capacity writes "
                  "only [7,7+region)" + detail + (leading ? "" : " [leading margin overwritten]") +
                      (trailing ? "" : " [trailing margin overwritten]"));
        }

        // A5: two consecutive calls from differently pre-filled destinations agree.
        {
            std::vector<Color> a(pattern.size(), SentinelCD());
            std::vector<Color> b(pattern.size(), SentinelA5());
            tex.GetData(a.data(), 0, static_cast<int>(a.size()));
            tex.GetData(b.data(), 0, static_cast<int>(b.size()));
            bool identical = true;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (!Same(a[i], b[i])) { identical = false; break; }
            check(identical, "A5 consecutive readbacks are identical from two different pre-fills");
        }

        // A6: content follows a partial SetData.
        {
            const Rectangle rect(2, 2, 3, 2);
            const std::vector<Color> patch(6, Color(9, 200, 77, 33));
            tex.SetData(0, &rect, patch.data(), 0, static_cast<int>(patch.size()));
            std::vector<Color> got(6, SentinelCD());
            tex.GetData(0, &rect, got.data(), 0, 6);
            bool ok = true;
            for (const Color& c : got)
                if (!Same(c, Color(9, 200, 77, 33))) ok = false;

            // ... and only that rectangle changed.
            std::vector<Color> whole(pattern.size(), SentinelCD());
            tex.GetData(whole.data(), 0, static_cast<int>(whole.size()));
            bool outsideIntact = true;
            for (int y = 0; y < kH && outsideIntact; ++y)
                for (int x = 0; x < kW; ++x)
                {
                    const bool inside = x >= rect.X && x < rect.X + rect.Width &&
                                        y >= rect.Y && y < rect.Y + rect.Height;
                    if (inside) continue;
                    if (!Same(whole[static_cast<std::size_t>(y) * kW + x], PatternPixel(x, y)))
                    {
                        outsideIntact = false;
                        break;
                    }
                }
            check(ok && outsideIntact,
                  "A6 readback reflects a partial SetData and leaves the rest of the level intact");
        }

        // A7: two independent textures do not alias.
        {
            Texture2D other(dev, kW, kH);
            const std::vector<Color> solid(static_cast<std::size_t>(kW) * kH, Color(1, 2, 3, 4));
            other.SetData(solid.data(), static_cast<int>(solid.size()));
            std::vector<Color> gotOther(solid.size(), SentinelCD());
            other.GetData(gotOther.data(), 0, static_cast<int>(gotOther.size()));
            bool otherOk = true;
            for (const Color& c : gotOther)
                if (!Same(c, Color(1, 2, 3, 4))) otherOk = false;

            std::vector<Color> gotFirst(solid.size(), SentinelCD());
            tex.GetData(gotFirst.data(), 0, static_cast<int>(gotFirst.size()));
            const bool firstUnaffected = Same(gotFirst[0], BlockTopLeft());
            check(otherOk && firstUnaffected,
                  "A7 two live textures read back their own content, neither aliasing the other");
        }
    }

    // ---- Section B: render-target readback -- the honest-outcome contract -----------------------

    void SectionB(GraphicsDevice& dev)
    {
        RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        ProducePattern(dev, target);

        const Readback cd = ProbeFullReadback(target, SentinelCD());
        CheckHonestOutcome(cd, "B1 render-target full readback (0xCD sentinel)");

        // Z1/Z2: the recorded reason sentinel-only testing cannot be trusted here. Pre-fix the
        // destination IS fully overwritten (with fabricated zeros) and no exception is raised, so
        // both of the two obvious oracles report success on an unimplemented renderer. These two
        // checks pass under BOTH honest outcomes and would only fail if the fabrication returned.
        check(!(cd.sentinelSurvivors == 0 && cd.fabricatedPixels == cd.total &&
                !cd.threwNotSupported && !cd.threwSomethingElse),
              "Z1 'the destination was overwritten' is not satisfied by a fabricated frame");
        check(!(cd.exactPixels != cd.total && !cd.threwNotSupported && !cd.threwSomethingElse),
              "Z2 a call that neither threw nor returned the exact pattern is impossible");

        const Readback a5 = ProbeFullReadback(target, SentinelA5());
        CheckHonestOutcome(a5, "B2 render-target full readback (0xA5 sentinel)");

        // B3: the outcome is stable across repeated calls.
        {
            bool stable = cd.threwNotSupported == a5.threwNotSupported &&
                          cd.threwSomethingElse == a5.threwSomethingElse;
            if (stable && kRtContract == RtContract::Exact)
                for (std::size_t i = 0; i < cd.pixels.size(); ++i)
                    if (!Same(cd.pixels[i], a5.pixels[i])) { stable = false; break; }
            check(stable, "B3 two consecutive render-target readbacks produced the same outcome");
        }

        // B4: rectangle overload, destination offset, surrounding sentinels.
        {
            const Rectangle rect(kW - kBlock, kH - kBlock, kBlock, kBlock);  // the alpha!=255 block
            const int count = rect.Width * rect.Height;
            const int start = 5;
            std::vector<Color> got(static_cast<std::size_t>(count + start + 4), SentinelCD());
            bool threwNotSupported = false;
            bool threwOther = false;
            std::string what;
            try
            {
                target.GetData(0, &rect, got.data(), start, static_cast<int>(got.size()) - start);
            }
            catch (const System::NotSupportedException&) { threwNotSupported = true; }
            catch (const std::exception& e) { threwOther = true; what = e.what(); }

            if (kRtContract == RtContract::Exact)
            {
                bool exact = !threwNotSupported && !threwOther;
                std::string detail;
                for (int row = 0; row < rect.Height && exact; ++row)
                    for (int col = 0; col < rect.Width; ++col)
                    {
                        const Color expected = PatternPixel(rect.X + col, rect.Y + row);
                        const Color actual =
                            got[static_cast<std::size_t>(start + row * rect.Width + col)];
                        if (!Same(actual, expected))
                        {
                            exact = false;
                            detail = " at (" + std::to_string(col) + "," + std::to_string(row) +
                                     "): " + ColorText(actual) + " != " + ColorText(expected);
                            break;
                        }
                    }
                bool margins = true;
                for (int i = 0; i < start; ++i)
                    if (!Same(got[static_cast<std::size_t>(i)], SentinelCD())) margins = false;
                for (std::size_t i = static_cast<std::size_t>(start + count); i < got.size(); ++i)
                    if (!Same(got[i], SentinelCD())) margins = false;
                check(exact && margins,
                      "B4 render-target rectangle readback at destination offset 5 is exact and "
                      "leaves both margins intact" + detail +
                          (margins ? "" : " [a sentinel margin was overwritten]") +
                          (threwOther ? (" [threw " + what + "]") : ""));
            }
            else
            {
                const bool untouched = CountSame(got, SentinelCD()) == got.size();
                check(threwNotSupported && !threwOther && untouched,
                      "B4 render-target rectangle readback rejected deterministically with the "
                      "destination untouched" + (threwOther ? (" [threw " + what + "]") : ""));
            }
        }

        // B8: the same rectangle set Section A applies to an ordinary texture -- one pixel, the
        // final row, the final column and the lower-right corner -- so an off-by-one in a
        // renderer's row-pitch or origin handling cannot hide in the interior.
        if (kRtContract == RtContract::Exact)
        {
            struct RectCase { Rectangle rect; const char* name; };
            const RectCase cases[] = {
                {Rectangle(0, 0, 1, 1), "B8a one pixel at the origin"},
                {Rectangle(kW - 1, kH - 1, 1, 1), "B8b lower-right corner pixel"},
                {Rectangle(0, kH - 1, kW, 1), "B8c final row"},
                {Rectangle(kW - 1, 0, 1, kH), "B8d final column"},
                {Rectangle(5, 2, 6, 4), "B8e middle rectangle"},
            };
            for (const RectCase& c : cases)
            {
                const int count = c.rect.Width * c.rect.Height;
                std::vector<Color> got(static_cast<std::size_t>(count), SentinelA5());
                bool threw = false;
                try { target.GetData(0, &c.rect, got.data(), 0, count); }
                catch (...) { threw = true; }
                bool exact = !threw;
                std::string detail;
                for (int row = 0; row < c.rect.Height && exact; ++row)
                    for (int col = 0; col < c.rect.Width; ++col)
                    {
                        const Color expected = PatternPixel(c.rect.X + col, c.rect.Y + row);
                        const Color actual = got[static_cast<std::size_t>(row) * c.rect.Width + col];
                        if (!Same(actual, expected))
                        {
                            exact = false;
                            detail = " at (" + std::to_string(col) + "," + std::to_string(row) +
                                     "): " + ColorText(actual) + " != " + ColorText(expected);
                            break;
                        }
                    }
                check(exact, std::string(c.name) + " of the render target is exact" + detail +
                                 (threw ? " [threw]" : ""));
            }
        }

        // B9: reading a target that is STILL BOUND, on a target of its own so the rebind cannot
        // disturb anything above (RenderTargetUsage::DiscardContents legitimately wipes a target
        // when it is bound again). Renderers differ on whether they can serve an active-target read
        // -- some flush their pending work and can, others cannot -- so this asserts only the
        // property this finding is about: whichever way it goes, the answer must be honest, the
        // exact content or a deterministic rejection, never a fabricated frame reported as success.
        {
            RenderTarget2D live(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&live);
            ResetState(dev);
            dev.Clear(Color(0, 0, 0, 255));
            FillRect(dev, Rectangle(0, 0, kW, kH), Background());
            FillRect(dev, Rectangle(0, 0, kBlock, kBlock), BlockTopLeft());
            FillRect(dev, Rectangle(kW - kBlock, 0, kBlock, kBlock), BlockTopRight());
            FillRect(dev, Rectangle(0, kH - kBlock, kBlock, kBlock), BlockBottomLeft());
            FillRect(dev, Rectangle(kW - kBlock, kH - kBlock, kBlock, kBlock), BlockBottomRight());

            std::vector<Color> got(static_cast<std::size_t>(kW) * kH, SentinelCD());
            bool threw = false;
            std::string what;
            try { live.GetData(got.data(), 0, static_cast<int>(got.size())); }
            catch (const std::exception& e) { threw = true; what = e.what(); }
            catch (...) { threw = true; what = "(non-std exception)"; }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            std::size_t exactPixels = 0;
            for (int y = 0; y < kH; ++y)
                for (int x = 0; x < kW; ++x)
                    if (Same(got[static_cast<std::size_t>(y) * kW + x], PatternPixel(x, y)))
                        ++exactPixels;
            const bool honest = threw ? (CountSame(got, SentinelCD()) == got.size())
                                      : (exactPixels == got.size());
            check(honest,
                  "B9 reading a still-bound render target either returns its exact content or is "
                  "rejected with the destination untouched" +
                      (threw ? (" [threw " + what + "]")
                             : (" [exact " + std::to_string(exactPixels) + "/" +
                                std::to_string(got.size()) + "]")));
        }

        // B5: two live targets keep separate storage.
        {
            RenderTarget2D second(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&second);
            ResetState(dev);
            dev.Clear(Color(255, 255, 0, 200));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            ResetState(dev);

            std::vector<Color> got(static_cast<std::size_t>(kW) * kH, SentinelCD());
            bool threwNotSupported = false;
            try { second.GetData(got.data(), 0, static_cast<int>(got.size())); }
            catch (const System::NotSupportedException&) { threwNotSupported = true; }
            catch (...) {}

            if (kRtContract == RtContract::Exact)
            {
                const bool secondOk = CountSame(got, Color(255, 255, 0, 200)) == got.size();
                const Readback again = ProbeFullReadback(target, SentinelCD());
                check(secondOk && again.exactPixels == again.total,
                      "B5 a second live target reads back its own clear colour and the first "
                      "target is unaffected");
            }
            else
            {
                check(threwNotSupported && CountSame(got, SentinelCD()) == got.size(),
                      "B5 a second live target rejects readback the same deterministic way");
            }
        }

        // B6: a disposed target must be rejected, never read.
        {
            RenderTarget2D doomed(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            ProducePattern(dev, doomed);
            doomed.Dispose();
            std::vector<Color> got(static_cast<std::size_t>(kW) * kH, SentinelCD());
            const bool threw = ThrowsAnything([&] {
                doomed.GetData(got.data(), 0, static_cast<int>(got.size()));
            });
            check(threw && CountSame(got, SentinelCD()) == got.size(),
                  "B6 reading a disposed render target throws and leaves the destination untouched");
        }

        // B7: a mip level this target does not have must be rejected, not fabricated.
        {
            std::vector<Color> got(static_cast<std::size_t>(kW) * kH, SentinelCD());
            const Rectangle whole(0, 0, kW, kH);
            const bool threw = ThrowsAnything([&] {
                target.GetData(3, &whole, got.data(), 0, static_cast<int>(got.size()));
            });
            check(threw && CountSame(got, SentinelCD()) == got.size(),
                  "B7 a nonexistent mip level is rejected with the destination untouched");
        }
    }

    // ---- Section C: validation, on both an ordinary texture and a render target -----------------

    void SectionC(GraphicsDevice& dev)
    {
        const std::vector<Color> pattern = PatternPixels();
        Texture2D tex(dev, kW, kH);
        tex.SetData(pattern.data(), static_cast<int>(pattern.size()));

        const std::size_t total = static_cast<std::size_t>(kW) * kH;
        std::vector<Color> dst(total, SentinelCD());
        const Rectangle whole(0, 0, kW, kH);

        check(ThrowsAnything([&] { tex.GetData(nullptr, 0, static_cast<int>(total)); }),
              "C1 null destination is rejected");
        check(ThrowsAnything([&] { tex.GetData(dst.data(), -1, static_cast<int>(total)); }),
              "C2 negative start index is rejected");
        check(ThrowsAnything([&] { tex.GetData(dst.data(), 0, 0); }),
              "C3 zero element count is rejected");
        check(ThrowsAnything([&] { tex.GetData(-1, &whole, dst.data(), 0, static_cast<int>(total)); }),
              "C4 negative mip level is rejected");
        check(ThrowsAnything([&] {
                  const Rectangle outside(kW - 2, kH - 2, 8, 8);
                  tex.GetData(0, &outside, dst.data(), 0, static_cast<int>(total));
              }),
              "C5 a rectangle leaving the level is rejected");
        check(ThrowsAnything([&] {
                  const Rectangle negative(-1, -1, 4, 4);
                  tex.GetData(0, &negative, dst.data(), 0, static_cast<int>(total));
              }),
              "C6 a negative-origin rectangle is rejected");
        check(ThrowsAnything([&] {
                  const Rectangle mid(4, 2, 6, 4);
                  tex.GetData(0, &mid, dst.data(), 0, 5);
              }),
              "C7 an element count below the requested region is rejected");
        check(ThrowsAnything([&] {
                  const Rectangle huge(0, 0, std::numeric_limits<int>::max(),
                                       std::numeric_limits<int>::max());
                  tex.GetData(0, &huge, dst.data(), 0, static_cast<int>(total));
              }),
              "C8 an int32-overflowing rectangle is rejected rather than wrapping");

        check(CountSame(dst, SentinelCD()) == dst.size(),
              "C9 every rejected request above left the destination byte-for-byte untouched");

        {
            Texture2D disposed(dev, kW, kH);
            disposed.SetData(pattern.data(), static_cast<int>(pattern.size()));
            disposed.Dispose();
            std::vector<Color> got(total, SentinelA5());
            const bool threw = ThrowsAnything([&] {
                got[0] = SentinelA5();
                disposed.GetData(got.data(), 0, static_cast<int>(total));
            });
            check(threw || CountSame(got, SentinelA5()) != got.size(),
                  "C10 a disposed ordinary texture either rejects the read or still has its shadow");
        }
    }

public:
    Texture2DGetDataContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kBBW);
        gdm_->setPreferredBackBufferHeightProperty(kBBH);
    }

    int getResult() const { return result_; }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1,
                        std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        ResetState(dev);

        std::printf("renderer=%s declaredRenderTargetReadback=%s\n", kRendererName,
                    kRtContract == RtContract::Exact ? "exact" : "unsupported");

        SectionA(dev);
        SectionB(dev);
        SectionC(dev);

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }
};

int main()
{
    Texture2DGetDataContractTest game;
    game.Run();
    return game.getResult();
}
