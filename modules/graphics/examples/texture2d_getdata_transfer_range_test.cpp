// SPDX-License-Identifier: MS-PL
// REMED-GFX-149 (and REMED-GFX-128, which records the same expression): the exact XNA-compatible
// `startIndex` / `elementCount` contract of every public `Texture2D::GetData` overload.
//
// AUTHORITATIVE SEMANTICS -- established, not assumed:
//
//   * `startIndex` indexes the CALLER'S DESTINATION ARRAY. FNA pins the destination array and
//     transfers to `AddrOfPinnedObject() + startIndex * elementSizeInBytes`; the archived XNA
//     reference documents it as "Index of the first element to get", i.e. an index into `data`.
//     It is an ELEMENT index, never a byte offset, and never a source-texel offset.
//   * `elementCount` is the number of DESTINATION ELEMENTS AVAILABLE from `startIndex`. FNA hands
//     it to the native transfer as `elementCount * elementSizeInBytes`, a capacity; the transfer
//     size itself comes from the REQUESTED SOURCE REGION. `GetData<T>(T[] data)` passes
//     `data.Length`, which may exceed the region, so excess capacity is legal and must be left
//     untouched; a capacity SMALLER than the region must be rejected before any transfer.
//   * The requested region of the whole-level overloads is the COMPLETE level 0 (width x height),
//     never `elementCount` and never a previous request.
//   * Written destination range on success: exactly [startIndex, startIndex + regionPixels).
//   * `SurfaceFormat::Color` is the only format `Texture::ValidateFormat` accepts project-wide and
//     the public destination type is `Color`, so the element size is 4 bytes on every path: there
//     is no templated element type, no compressed block and no per-format pitch case to cover.
//
// THE DEFECT this file reproduces, entirely inside the shared, renderer-neutral
// `Texture2D::GetData(Color*, int startIndex, int elementCount)`:
//
//     if (gpuOnlyContent_ && renderer_ && startIndex == 0 && elementCount == total && total > 0)
//         ... for (i < elementCount) data[i] = ...;          // (1) (2) (3)
//     throw std::runtime_error("no CPU-side pixel data available");
//     ...
//     if (startIndex + elementCount > total) throw ...;      // (4) (5)
//     for (i < elementCount) { src = (startIndex + i) * 4; data[i] = ...; }   // (6) (7)
//
//   (1) a render-target read with a NON-ZERO startIndex is rejected outright;
//   (2) a render-target read with EXCESS capacity (elementCount > total) is rejected outright;
//   (3) the render-target copy writes from data[0], ignoring startIndex entirely;
//   (4) the bound compares DESTINATION indices against the SOURCE pixel count;
//   (5) so legal excess capacity is rejected here too;
//   (6) startIndex is applied to the SOURCE texel stream -- the exact opposite of the contract --
//       while the rectangle overload applies the identical parameter to the DESTINATION;
//   (7) an elementCount SMALLER than the region silently returns a partial frame instead of
//       rejecting, while the rectangle overload, TextureCube, Texture3D and GetBackBufferData all
//       reject it.
//
// ORACLE DESIGN. Every destination is pre-filled with a DISTINCT per-element guard
// `Color(i & 0xFF, i >> 8, 0xAB, 0xCD)`; no pattern colour and no render-target readback can
// produce `B == 0xAB && A == 0xCD`, so "still a guard" is decidable per element and the guard's
// own index is recoverable from it. That makes each of these separately identifiable and separately
// reported: a write BEFORE startIndex, a write AFTER the authorised range, an element left
// unwritten inside the range, a duplicated row, a wrong element stride, and a byte-versus-element
// mistake (a byte-scaled startIndex lands 4x too far and leaves an exact, reported hole).
//
// The plain-texture pattern is invertible -- R encodes the column, G the row -- so a misplaced
// element reports its EXPECTED and ACTUAL coordinates rather than just "mismatch". Its blue channel
// carries the required mid-tone plus four distinct corner values, and one interior block carries an
// alpha that is neither 0 nor 255. Dimensions are odd and non-square (13x7, and 11x5 for the render
// target) so a width/height transposition or a square-only stride assumption cannot survive.
//
// A RENDER TARGET's content is deliberately NOT asserted against absolute colours here: WebGPU uses
// an *UnormSrgb render-target format, so a mid-tone written through its render pass comes back
// gamma-encoded. Instead the ALREADY-CORRECT rectangle overload reading at startIndex 0 is the
// reference, and the whole-level overload must reproduce it byte for byte at an arbitrary offset.
// That isolates this ticket from every renderer colour-space and orientation concern.
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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

    constexpr int kTexW = 13;  ///< Plain texture width  -- odd, and not equal to the height.
    constexpr int kTexH = 7;   ///< Plain texture height -- odd, and not equal to the width.
    constexpr int kTexN = kTexW * kTexH;   ///< 91 pixels.

    constexpr int kRtW = 11;   ///< Render-target width  -- odd, non-square, and not the texture's.
    constexpr int kRtH = 5;    ///< Render-target height.
    constexpr int kRtN = kRtW * kRtH;      ///< 55 pixels.

    constexpr int kPrefix = 5; ///< Protected destination elements BEFORE the writable range.
    constexpr int kSuffix = 4; ///< Protected destination elements AFTER the writable range.

    /**
     * @brief Whether this renderer can read a render target's colour attachment back to the CPU.
     *
     * Declared per renderer rather than probed, so "this renderer cannot" is a reviewed claim this
     * file enforces (REMED-GFX-127's contract), not an escape hatch that lets any result pass.
     */
    enum class RtContract
    {
        Exact,        ///< The readback must succeed and be self-consistent.
        Unsupported,  ///< It must throw System::NotSupportedException and touch nothing.
    };

    /**
     * @brief The exact policy used when an application requests a 2D mip chain.
     *
     * Declared, not probed, for the same reason as RtContract: two renderers document that their
     * 2D texture API has no mip chain (SDL_Renderer and DIRECTX3's
     * IDirectDrawSurface), and Canvas the same. Most still create a mipmapped resource and reject
     * `SetData(level=1, ...)`. SKIA-127 now implements that upload/readback path after SKIA-126
     * opened construction. These honest policies must not be conflated by a test that terminates
     * before reporting them.
     */
    enum class MipPolicy
    {
        Supported,
        RejectUpload,
        RejectConstruction,
    };

#if defined(CNA_RENDERER_HEADLESS)
    // Headless performs no rasterization at all, so a render target's colour attachment has no
    // content it could honestly return (REMED-GFX-127 / REMED-GFX-162).
    constexpr RtContract kRtContract = RtContract::Unsupported;
    constexpr const char* kRendererName = "HEADLESS";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_SOFTWARE)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "SOFTWARE";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_EASYGL)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "EASYGL";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_BGFX)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "BGFX";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_VULKAN)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "VULKAN";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "WEBGPU";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "SDL_GPU";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_SDL_RENDERER)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "SDL_RENDERER";
    constexpr MipPolicy kMipPolicy = MipPolicy::RejectUpload;
#elif defined(CNA_RENDERER_FREEDIRECT)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "FREEDIRECT";
    constexpr MipPolicy kMipPolicy = MipPolicy::RejectUpload;
#elif defined(CNA_RENDERER_DIRECTX9)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "DIRECTX9";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_DIRECTX11)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "DIRECTX11";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_DIRECTX12)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "DIRECTX12";
    constexpr MipPolicy kMipPolicy = MipPolicy::Supported;
#elif defined(CNA_RENDERER_CANVAS)
    constexpr RtContract kRtContract = RtContract::Exact;
    constexpr const char* kRendererName = "CANVAS";
    constexpr MipPolicy kMipPolicy = MipPolicy::RejectUpload;
#else
#error "REMED-GFX-149: this renderer has no declared Texture2D::GetData render-target contract."
#endif

    // ───────────────────────────────────────────────────────────────────────────
    // Guards
    // ───────────────────────────────────────────────────────────────────────────

    constexpr std::uint8_t kGuardB = 0xAB;  ///< Never produced by the pattern (B is 0/0x40/0x80/0xC0/0xFF).
    constexpr std::uint8_t kGuardA = 0xCD;  ///< Never produced by the pattern (A is 0xFF or 0x5A).

    /// A guard whose own destination index can be read back out of it.
    Color Guard(int i)
    {
        return Color(static_cast<std::uint8_t>(i & 0xFF),
                     static_cast<std::uint8_t>((i >> 8) & 0xFF),
                     kGuardB, kGuardA);
    }

    bool IsGuard(const Color& c)
    {
        return c.getBProperty() == kGuardB && c.getAProperty() == kGuardA;
    }

    /// The destination index a surviving guard was written with -- so a MOVED guard is detectable,
    /// not just a missing one.
    int GuardIndexOf(const Color& c)
    {
        return static_cast<int>(c.getRProperty()) | (static_cast<int>(c.getGProperty()) << 8);
    }

    std::vector<Color> GuardedBuffer(std::size_t n)
    {
        std::vector<Color> v;
        v.reserve(n);
        for (std::size_t i = 0; i < n; ++i) v.push_back(Guard(static_cast<int>(i)));
        return v;
    }

    // ───────────────────────────────────────────────────────────────────────────
    // The plain texture's pattern
    // ───────────────────────────────────────────────────────────────────────────

    /**
     * @brief The colour the plain texture holds at (@p x, @p y).
     *
     * R and G encode the column and the row (+1, so no channel is ever the array index itself),
     * which makes any misplaced element report its expected AND actual coordinates. B carries the
     * required mid-tone plus four distinct corner values, so a corner swap, a vertical mirror and a
     * horizontal mirror each fail differently. One interior block carries an alpha that is neither
     * 0 nor 255.
     */
    Color PatternPixel(int x, int y)
    {
        std::uint8_t b = 0x80;                                   // mid-tone body
        if (x == 0 && y == 0)                     b = 0x00;      // asymmetric corners
        else if (x == kTexW - 1 && y == 0)        b = 0xFF;
        else if (x == 0 && y == kTexH - 1)        b = 0x40;
        else if (x == kTexW - 1 && y == kTexH - 1) b = 0xC0;
        const bool alphaBlock = (x >= 2 && x <= 4 && y >= 1 && y <= 2);
        return Color(static_cast<std::uint8_t>(x + 1),
                     static_cast<std::uint8_t>(y + 1),
                     b,
                     static_cast<std::uint8_t>(alphaBlock ? 0x5A : 0xFF));
    }

    std::vector<Color> PatternPixels()
    {
        std::vector<Color> px;
        px.reserve(kTexN);
        for (int y = 0; y < kTexH; ++y)
            for (int x = 0; x < kTexW; ++x) px.push_back(PatternPixel(x, y));
        return px;
    }

    /// The level-1 pattern, distinct from level 0 so a mip read that silently used level 0
    /// dimensions or level 0 content is caught.
    Color MipPixel(int x, int y)
    {
        return Color(static_cast<std::uint8_t>(200 + x),
                     static_cast<std::uint8_t>(100 + y),
                     static_cast<std::uint8_t>(0x40),
                     static_cast<std::uint8_t>(0xFF));
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    /// "(col,row)" decoded out of a pattern element, or "guard#N"/"?" when it is not one.
    std::string CoordText(const Color& c)
    {
        if (IsGuard(c)) return "guard#" + std::to_string(GuardIndexOf(c));
        const int x = static_cast<int>(c.getRProperty()) - 1;
        const int y = static_cast<int>(c.getGProperty()) - 1;
        if (x < 0 || x >= kTexW || y < 0 || y >= kTexH) return "?" + ColorText(c);
        return "(" + std::to_string(x) + "," + std::to_string(y) + ")";
    }
}

class Texture2DGetDataTransferRangeTest : public Game
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

    // ───────────────────────────────────────────────────────────────────────
    // The destination-range oracle
    // ───────────────────────────────────────────────────────────────────────

    /**
     * @brief Classifies one destination buffer against the exact range the call was authorised to
     *        write, and reports the FIRST failure of each distinguishable kind.
     *
     * @param got       the destination after the call
     * @param start     the authorised first index (`startIndex`)
     * @param expected  the content the range must hold, element 0 first
     * @param label     check label
     */
    bool CheckRange(const std::vector<Color>& got, int start,
                    const std::vector<Color>& expected, const std::string& label)
    {
        const int n = static_cast<int>(expected.size());
        std::string why;

        // 1. Everything before startIndex must still be its ORIGINAL guard -- not merely "a guard",
        //    so a shifted or duplicated guard is caught too.
        for (int i = 0; i < start; ++i)
        {
            if (!IsGuard(got[static_cast<std::size_t>(i)]) ||
                GuardIndexOf(got[static_cast<std::size_t>(i)]) != i)
            {
                why = "wrote BEFORE startIndex: first modified guard index " + std::to_string(i) +
                      " holds " + ColorText(got[static_cast<std::size_t>(i)]) + " (" +
                      CoordText(got[static_cast<std::size_t>(i)]) + "), expected guard#" +
                      std::to_string(i);
                break;
            }
        }

        // 2. Every authorised element must hold exactly its expected content.
        if (why.empty())
        {
            for (int i = 0; i < n; ++i)
            {
                const Color& g = got[static_cast<std::size_t>(start + i)];
                const Color& w = expected[static_cast<std::size_t>(i)];
                if (Same(g, w)) continue;
                if (IsGuard(g))
                {
                    why = "left requested element UNWRITTEN: first unwritten index " +
                          std::to_string(start + i) + " (range element " + std::to_string(i) +
                          ") still holds guard#" + std::to_string(GuardIndexOf(g)) +
                          ", expected " + ColorText(w) + " " + CoordText(w);
                }
                else
                {
                    why = "wrong content at index " + std::to_string(start + i) +
                          " (range element " + std::to_string(i) + "): expected " + ColorText(w) +
                          " " + CoordText(w) + ", got " + ColorText(g) + " " + CoordText(g);
                }
                break;
            }
        }

        // 3. Everything after the authorised range must still be its ORIGINAL guard.
        if (why.empty())
        {
            for (int i = start + n; i < static_cast<int>(got.size()); ++i)
            {
                if (!IsGuard(got[static_cast<std::size_t>(i)]) ||
                    GuardIndexOf(got[static_cast<std::size_t>(i)]) != i)
                {
                    why = "wrote AFTER the authorised range [" + std::to_string(start) + "," +
                          std::to_string(start + n) + "): first modified guard index " +
                          std::to_string(i) + " holds " +
                          ColorText(got[static_cast<std::size_t>(i)]) + " (" +
                          CoordText(got[static_cast<std::size_t>(i)]) + ")";
                    break;
                }
            }
        }

        check(why.empty(), label + (why.empty() ? "" : " -- " + why));
        return why.empty();
    }

    /// Asserts that a REJECTED call left every single destination element on its original guard.
    bool CheckUntouched(const std::vector<Color>& got, const std::string& label)
    {
        for (int i = 0; i < static_cast<int>(got.size()); ++i)
        {
            if (!IsGuard(got[static_cast<std::size_t>(i)]) ||
                GuardIndexOf(got[static_cast<std::size_t>(i)]) != i)
            {
                check(false, label + " -- rejected call still wrote: first modified index " +
                                 std::to_string(i) + " holds " +
                                 ColorText(got[static_cast<std::size_t>(i)]));
                return false;
            }
        }
        check(true, label);
        return true;
    }

    /// Runs @p fn and reports what it did, so a leg never silently accepts "either outcome".
    struct Outcome
    {
        bool threw = false;
        std::string what;
        std::string type;
    };

    template <typename Fn>
    static Outcome Attempt(Fn&& fn)
    {
        Outcome o;
        try { fn(); }
        catch (const System::NotSupportedException& e)
        { o.threw = true; o.type = "NotSupportedException"; o.what = e.what(); }
        catch (const std::out_of_range& e)
        { o.threw = true; o.type = "out_of_range"; o.what = e.what(); }
        catch (const std::invalid_argument& e)
        { o.threw = true; o.type = "invalid_argument"; o.what = e.what(); }
        catch (const std::exception& e)
        { o.threw = true; o.type = "exception"; o.what = e.what(); }
        catch (...)
        { o.threw = true; o.type = "unknown"; }
        return o;
    }

    void checkSucceeded(const Outcome& o, const std::string& label)
    {
        check(!o.threw, label + (o.threw ? " -- threw " + o.type + ": " + o.what : ""));
    }

    void checkThrew(const Outcome& o, const std::string& expectedType, const std::string& label)
    {
        check(o.threw && o.type == expectedType,
              label + " -- expected " + expectedType + ", got " +
                  (o.threw ? o.type + ": " + o.what : std::string("no exception")));
    }

    // ───────────────────────────────────────────────────────────────────────
    // Section T -- plain Texture2D, the whole-level overloads
    // ───────────────────────────────────────────────────────────────────────

    void SectionT(GraphicsDevice& dev)
    {
        std::puts("--- T: plain Texture2D, whole-level overloads (CPU-side content) ---");
        const std::vector<Color> pattern = PatternPixels();

        Texture2D tex(dev, kTexW, kTexH);
        tex.SetData(0, nullptr, pattern.data(), 0, kTexN);

        // T1: the canonical call -- startIndex 0, capacity exactly the region.
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), 0, kTexN); });
            checkSucceeded(o, "T1 startIndex=0, elementCount=91 (exact) succeeds");
            if (!o.threw) CheckRange(got, 0, pattern, "T1 writes exactly [0,91), suffix intact");
        }

        // T2: THE headline case -- a non-zero destination offset. Pre-fix this reads the SOURCE
        //     from texel 5 and writes from data[0], so both guard margins are destroyed and the
        //     content is shifted by five texels.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + kSuffix);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, kTexN); });
            checkSucceeded(o, "T2 startIndex=5, elementCount=91 succeeds");
            if (!o.threw)
                CheckRange(got, kPrefix, pattern,
                           "T2 startIndex is a DESTINATION element offset: writes exactly [5,96), "
                           "prefix [0,5) and suffix [96,100) intact");
        }

        // T3: destination length exactly startIndex + required -- no suffix to hide an overrun in.
        {
            auto got = GuardedBuffer(kPrefix + kTexN);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, kTexN); });
            checkSucceeded(o, "T3 destination length exactly startIndex+required succeeds");
            if (!o.threw)
                CheckRange(got, kPrefix, pattern, "T3 fills [5,96) exactly, prefix intact");
        }

        // T4: a much larger destination with the SAME elementCount -- the extra capacity beyond the
        //     region must stay untouched.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + 120);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, kTexN); });
            checkSucceeded(o, "T4 oversized destination, exact elementCount succeeds");
            if (!o.threw)
                CheckRange(got, kPrefix, pattern,
                           "T4 oversized destination: still only [5,96) written");
        }

        // T5: EXCESS elementCount. FNA's own GetData<T>(T[] data) passes data.Length, which may
        //     exceed the region, so this is a legal call -- and only the region may be written.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + 120);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, kTexN + 100); });
            checkSucceeded(o, "T5 excess elementCount (191 for a 91-pixel region) is accepted");
            if (!o.threw)
                CheckRange(got, kPrefix, pattern,
                           "T5 excess capacity is IGNORED, not written: only [5,96) changes");
        }

        // T6: capacity one element short of the region -- rejected before any transfer.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + kSuffix);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, kTexN - 1); });
            checkThrew(o, "out_of_range", "T6 elementCount one element too small is rejected");
            CheckUntouched(got, "T6 rejected undersized elementCount touched nothing");
        }

        // T7: a grossly short capacity must reject too -- NOT return a partial frame. This is the
        //     shape the whole-level overload silently accepted pre-fix.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + kSuffix);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, 1); });
            checkThrew(o, "out_of_range", "T7 elementCount=1 for a 91-pixel region is rejected");
            CheckUntouched(got, "T7 no partial frame was returned");
        }

        // T8: the two-argument convenience overload forwards startIndex 0 and nothing else.
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kTexN); });
            checkSucceeded(o, "T8 GetData(data, elementCount) succeeds");
            if (!o.threw) CheckRange(got, 0, pattern, "T8 two-arg overload writes exactly [0,91)");
        }

        // T9: repeated calls writing DIFFERENT destination offsets into one buffer -- no request
        //     may disturb another's range, and none may reuse a previous request's sizing.
        {
            auto got = GuardedBuffer(3 * kTexN + 40);
            const int starts[3] = { 0, kTexN + 7, 2 * kTexN + 20 };
            bool all = true;
            for (int s : starts)
            {
                const Outcome o = Attempt([&] { tex.GetData(got.data(), s, kTexN); });
                if (o.threw) { checkSucceeded(o, "T9 repeated call at startIndex " +
                                              std::to_string(s)); all = false; }
            }
            if (all)
            {
                std::string why;
                for (int k = 0; k < 3 && why.empty(); ++k)
                    for (int i = 0; i < kTexN; ++i)
                        if (!Same(got[static_cast<std::size_t>(starts[k] + i)],
                                  pattern[static_cast<std::size_t>(i)]))
                        {
                            why = "copy " + std::to_string(k) + " wrong at element " +
                                  std::to_string(i) + ": expected " +
                                  CoordText(pattern[static_cast<std::size_t>(i)]) + ", got " +
                                  CoordText(got[static_cast<std::size_t>(starts[k] + i)]);
                            break;
                        }
                // The gaps between the three copies must still be their original guards.
                const int gaps[2][2] = { { kTexN, kTexN + 7 }, { 2 * kTexN + 7, 2 * kTexN + 20 } };
                for (const auto& g : gaps)
                    for (int i = g[0]; i < g[1] && why.empty(); ++i)
                        if (!IsGuard(got[static_cast<std::size_t>(i)]) ||
                            GuardIndexOf(got[static_cast<std::size_t>(i)]) != i)
                            why = "gap element " + std::to_string(i) + " was overwritten";
                check(why.empty(), "T9 three reads at different offsets are independent and exact" +
                                       (why.empty() ? "" : " -- " + why));
            }
        }

        // T10/T11: overflow-shaped arithmetic. `startIndex + elementCount` must never be evaluated
        //          as a wrapping int -- it must reject deterministically instead.
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            const Outcome o = Attempt([&] {
                tex.GetData(got.data(), std::numeric_limits<int>::max() - 2, kTexN);
            });
            checkThrew(o, "out_of_range", "T10 overflow-shaped startIndex+elementCount is rejected");
            CheckUntouched(got, "T10 overflow-shaped request touched nothing");
        }
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            const Outcome o = Attempt([&] {
                tex.GetData(got.data(), 1, std::numeric_limits<int>::max());
            });
            checkThrew(o, "out_of_range", "T11 overflow-shaped elementCount is rejected");
            CheckUntouched(got, "T11 overflow-shaped elementCount touched nothing");
        }

        // T12-T15: the remaining argument guards, each with its exact type.
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            checkThrew(Attempt([&] { tex.GetData(got.data(), -1, kTexN); }), "out_of_range",
                       "T12 negative startIndex is rejected");
            CheckUntouched(got, "T12 negative startIndex touched nothing");
        }
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            checkThrew(Attempt([&] { tex.GetData(got.data(), 0, -1); }), "invalid_argument",
                       "T13 negative elementCount is rejected");
            CheckUntouched(got, "T13 negative elementCount touched nothing");
        }
        {
            auto got = GuardedBuffer(kTexN + kSuffix);
            checkThrew(Attempt([&] { tex.GetData(got.data(), 0, 0); }), "invalid_argument",
                       "T14 zero elementCount is rejected");
            CheckUntouched(got, "T14 zero elementCount touched nothing");
        }
        checkThrew(Attempt([&] { tex.GetData(static_cast<Color*>(nullptr), 0, kTexN); }),
                   "invalid_argument", "T15 null destination is rejected");

        // T16: startIndex is measured in ELEMENTS, not bytes. A byte-scaled startIndex would begin
        //      at 4*startIndex; an element-scaled one begins exactly at startIndex. Both the first
        //      written element and the last untouched guard are asserted, so either mistake fails
        //      with an exact index.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + 4 * kPrefix + kSuffix);
            const Outcome o = Attempt([&] { tex.GetData(got.data(), kPrefix, kTexN); });
            checkSucceeded(o, "T16 element-vs-byte probe call succeeds");
            if (!o.threw)
            {
                const bool lastGuardOk = IsGuard(got[kPrefix - 1]) &&
                                         GuardIndexOf(got[kPrefix - 1]) == kPrefix - 1;
                const bool firstWrittenOk = Same(got[kPrefix], pattern[0]);
                check(lastGuardOk && firstWrittenOk,
                      "T16 startIndex 5 begins at ELEMENT 5, not byte 5 / element 20: index 4 = " +
                          ColorText(got[kPrefix - 1]) + ", index 5 = " + ColorText(got[kPrefix]) +
                          " (expected guard#4 then " + ColorText(pattern[0]) + ")");
            }
        }
    }

    // ───────────────────────────────────────────────────────────────────────
    // Section R -- the rectangle overload: its own region, its own capacity rule
    // ───────────────────────────────────────────────────────────────────────

    void SectionR(GraphicsDevice& dev)
    {
        std::puts("--- R: rectangle overload, source regions and delegation ---");
        const std::vector<Color> pattern = PatternPixels();

        Texture2D tex(dev, kTexW, kTexH);
        tex.SetData(0, nullptr, pattern.data(), 0, kTexN);

        auto expectRect = [&](int rx, int ry, int rw, int rh) {
            std::vector<Color> want;
            want.reserve(static_cast<std::size_t>(rw) * rh);
            for (int row = 0; row < rh; ++row)
                for (int col = 0; col < rw; ++col)
                    want.push_back(pattern[static_cast<std::size_t>((ry + row) * kTexW + rx + col)]);
            return want;
        };

        // R1: the rectangle overload's level-0 / null-rectangle case DELEGATES to the whole-level
        //     overload, so it must produce the identical destination range.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + kSuffix);
            const Outcome o = Attempt([&] {
                tex.GetData(0, nullptr, got.data(), kPrefix, kTexN);
            });
            checkSucceeded(o, "R1 GetData(0, nullptr, ...) at startIndex 5 succeeds");
            if (!o.threw)
                CheckRange(got, kPrefix, pattern,
                           "R1 the delegating overload writes the same exact range");
        }

        // R2: an explicit FULL rectangle takes the non-delegating path and must agree.
        {
            auto got = GuardedBuffer(kPrefix + kTexN + kSuffix);
            const Rectangle full(0, 0, kTexW, kTexH);
            const Outcome o = Attempt([&] { tex.GetData(0, &full, got.data(), kPrefix, kTexN); });
            checkSucceeded(o, "R2 explicit full rectangle at startIndex 5 succeeds");
            if (!o.threw)
                CheckRange(got, kPrefix, pattern,
                           "R2 explicit full rectangle agrees with the rectangle-less call");
        }

        struct RectLeg { int x, y, w, h, start; const char* label; };
        const RectLeg legs[] = {
            { kTexW - 1, kTexH - 1, 1, 1, 9, "R3 one-pixel rectangle at the far corner" },
            { 0, kTexH - 1, kTexW, 1, 3, "R4 final row" },
            { kTexW - 1, 0, 1, kTexH, 3, "R5 final column" },
            { 3, 2, 5, 3, 6, "R6 centred odd-sized sub-rectangle" },
            { 1, 0, 4, kTexH, 2, "R7 odd-width-offset column band" },
        };
        for (const RectLeg& l : legs)
        {
            const std::vector<Color> want = expectRect(l.x, l.y, l.w, l.h);
            auto got = GuardedBuffer(static_cast<std::size_t>(l.start) + want.size() + kSuffix);
            const Rectangle r(l.x, l.y, l.w, l.h);
            const Outcome o = Attempt([&] {
                tex.GetData(0, &r, got.data(), l.start, static_cast<int>(want.size()));
            });
            checkSucceeded(o, std::string(l.label) + " succeeds");
            if (!o.threw)
                CheckRange(got, l.start, want,
                           std::string(l.label) + ": required elements come from the RECTANGLE's "
                           "own dimensions, not the full resource");
        }

        // R8: the capacity rule is measured against the REQUESTED REGION, so a capacity far smaller
        //     than the whole texture is perfectly legal for a small rectangle.
        {
            const Rectangle r(3, 2, 5, 3);
            const std::vector<Color> want = expectRect(3, 2, 5, 3);
            auto got = GuardedBuffer(kPrefix + want.size() + kSuffix);
            const Outcome o = Attempt([&] {
                tex.GetData(0, &r, got.data(), kPrefix, static_cast<int>(want.size()));
            });
            checkSucceeded(o, "R8 elementCount=15 (<< 91) is legal for a 5x3 rectangle");
            if (!o.threw)
                CheckRange(got, kPrefix, want, "R8 small rectangle, small capacity, exact range");
        }

        // R9: one element short of THAT rectangle is still rejected.
        {
            const Rectangle r(3, 2, 5, 3);
            auto got = GuardedBuffer(kPrefix + 15 + kSuffix);
            checkThrew(Attempt([&] { tex.GetData(0, &r, got.data(), kPrefix, 14); }), "out_of_range",
                       "R9 elementCount one short of the rectangle is rejected");
            CheckUntouched(got, "R9 rejected rectangle request touched nothing");
        }

        // R10: excess capacity on a rectangle read leaves the tail untouched.
        {
            const Rectangle r(3, 2, 5, 3);
            const std::vector<Color> want = expectRect(3, 2, 5, 3);
            auto got = GuardedBuffer(kPrefix + 60);
            const Outcome o = Attempt([&] { tex.GetData(0, &r, got.data(), kPrefix, 50); });
            checkSucceeded(o, "R10 excess capacity on a rectangle read is accepted");
            if (!o.threw)
                CheckRange(got, kPrefix, want, "R10 excess capacity leaves the tail untouched");
        }

        // R11: overflow-shaped arguments on the rectangle overload reject the same way.
        {
            const Rectangle r(3, 2, 5, 3);
            auto got = GuardedBuffer(kPrefix + 15 + kSuffix);
            checkThrew(Attempt([&] {
                           tex.GetData(0, &r, got.data(), std::numeric_limits<int>::max() - 2, 15);
                       }), "out_of_range",
                       "R11 overflow-shaped startIndex+elementCount is rejected (rectangle)");
            CheckUntouched(got, "R11 overflow-shaped rectangle request touched nothing");
        }

        // R12: mip level 1 uses ITS OWN dimensions, not level 0's. 13x7 -> 6x3 = 18 elements, so a
        //      level-0-sized capacity requirement (91) would wrongly reject this legal call and a
        //      level-0-sized region would wrongly demand 91 elements of output. A separate
        //      MIPMAPPED texture is used: uploading a level a non-mipmapped resource does not have
        //      is rejected by the renderers that validate it (WebGPU, bgfx) and is not what this
        //      check is about.
        {
            if (kMipPolicy == MipPolicy::RejectConstruction)
            {
                const Outcome creation = Attempt([&] {
                    Texture2D rejectedMipTexture(dev, kTexW, kTexH, true, SurfaceFormat::Color);
                    (void)rejectedMipTexture;
                });
                check(creation.threw,
                      "R12 this renderer rejects mipmapped Texture2D construction before accepting data" +
                          (creation.threw ? " (" + creation.what + ")"
                                           : std::string(" -- but construction SUCCEEDED")));
                return;
            }
            Texture2D mipTex(dev, kTexW, kTexH, true, SurfaceFormat::Color);
            mipTex.SetData(0, nullptr, pattern.data(), 0, kTexN);

            const int mw = kTexW >> 1, mh = kTexH >> 1;   // 6 x 3
            std::vector<Color> mip;
            mip.reserve(static_cast<std::size_t>(mw) * mh);
            for (int y = 0; y < mh; ++y)
                for (int x = 0; x < mw; ++x) mip.push_back(MipPixel(x, y));

            const Outcome upload = Attempt([&] {
                mipTex.SetData(1, nullptr, mip.data(), 0, static_cast<int>(mip.size()));
            });
            if (kMipPolicy == MipPolicy::RejectUpload)
            {
                // Pinned, not skipped: this renderer's 2D texture API documents that it has no mip
                // chain, so the level-1 upload must raise that rejection. If it ever succeeds, this
                // check fails and the declaration above has to be revisited.
                check(upload.threw,
                      "R12 this renderer has no mip storage and rejects a level-1 upload" +
                          (upload.threw ? " (" + upload.what + ")"
                                        : std::string(" -- but it SUCCEEDED")));
                return;
            }
            checkSucceeded(upload, "R12 level-1 upload succeeds on a mipmapped texture");
            if (upload.threw) return;

            auto got = GuardedBuffer(kPrefix + mip.size() + kSuffix);
            const Outcome o = Attempt([&] {
                mipTex.GetData(1, nullptr, got.data(), kPrefix, static_cast<int>(mip.size()));
            });
            checkSucceeded(o, "R12 mip level 1 read at startIndex 5 succeeds");
            if (!o.threw)
                CheckRange(got, kPrefix, mip,
                           "R12 level 1 requires 18 elements (6x3), not level 0's 91");

            // R13: one element short of THAT level is rejected -- the capacity rule follows the
            //      mip's own dimensions, not level 0's.
            auto small = GuardedBuffer(kPrefix + mip.size() + kSuffix);
            checkThrew(Attempt([&] {
                           mipTex.GetData(1, nullptr, small.data(), kPrefix,
                                          static_cast<int>(mip.size()) - 1);
                       }), "out_of_range",
                       "R13 elementCount one short of level 1's 18 elements is rejected");
            CheckUntouched(small, "R13 rejected mip request touched nothing");
        }
    }

    // ───────────────────────────────────────────────────────────────────────
    // Section G -- RenderTarget2D: GPU-only content, no CPU shadow
    // ───────────────────────────────────────────────────────────────────────

    /// Fills one rectangle of exactly @p tint into whatever target is bound.
    void FillRect(GraphicsDevice& dev, const Rectangle& destination, const Color& tint)
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
        sb.Draw(whiteTex_, destination, Rectangle(0, 0, 1, 1), tint);
        sb.End();
    }

    void SectionG(GraphicsDevice& dev)
    {
        std::puts("--- G: RenderTarget2D whole-level readback at a destination offset ---");

        RenderTarget2D target(dev, kRtW, kRtH);
        dev.SetRenderTarget(&target);
        ResetState(dev);
        dev.Clear(Color(0, 0, 0, 255));
        // Four quadrant fills with pure-channel colours: every renderer reproduces 0/255 exactly, so
        // the reference read below cannot be a uniform frame that would hide a stride error.
        FillRect(dev, Rectangle(0, 0, kRtW, kRtH), Color(0, 0, 255, 255));
        FillRect(dev, Rectangle(0, 0, kRtW / 2, kRtH / 2), Color(255, 0, 0, 255));
        FillRect(dev, Rectangle(kRtW / 2, kRtH / 2, kRtW - kRtW / 2, kRtH - kRtH / 2),
                 Color(0, 255, 0, 255));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        ResetState(dev);

        // G0: the reference. The rectangle overload at startIndex 0 already honoured the contract
        //     before this ticket, so it -- not an absolute colour table -- states what the
        //     whole-level overload must reproduce. That keeps this section independent of every
        //     renderer colour-space, orientation and MSAA concern.
        std::vector<Color> reference(kRtN, Guard(0));
        {
            const Rectangle full(0, 0, kRtW, kRtH);
            auto probe = GuardedBuffer(kRtN);
            const Outcome o = Attempt([&] { target.GetData(0, &full, probe.data(), 0, kRtN); });

            if (kRtContract == RtContract::Unsupported)
            {
                checkThrew(o, "NotSupportedException",
                           "G0 non-rasterizing renderer rejects the reference read");
                CheckUntouched(probe, "G0 rejected reference read touched nothing");
            }
            else
            {
                checkSucceeded(o, "G0 reference rectangle read succeeds");
                if (o.threw) return;
                std::size_t survivors = 0;
                for (const Color& c : probe) if (IsGuard(c)) ++survivors;
                check(survivors == 0,
                      "G0 reference read wrote every one of the " + std::to_string(kRtN) +
                          " elements (" + std::to_string(survivors) + " guards survived)");
                reference = probe;
            }
        }

        // G1: whole-level overload, startIndex 0 -- must equal the reference exactly.
        {
            auto got = GuardedBuffer(kRtN + kSuffix);
            const Outcome o = Attempt([&] { target.GetData(got.data(), 0, kRtN); });
            if (kRtContract == RtContract::Unsupported)
            {
                checkThrew(o, "NotSupportedException",
                           "G1 non-rasterizing renderer rejects the whole-level read");
                CheckUntouched(got, "G1 rejected whole-level read touched nothing");
            }
            else
            {
                checkSucceeded(o, "G1 whole-level render-target read at startIndex 0 succeeds");
                if (!o.threw)
                    CheckRange(got, 0, reference,
                               "G1 whole-level read reproduces the rectangle reference exactly");
            }
        }

        // G2: THE headline case -- a render target read at a NON-ZERO destination offset. Pre-fix
        //     the gate `startIndex == 0` makes this throw std::runtime_error("no CPU-side pixel
        //     data available") on every renderer.
        {
            auto got = GuardedBuffer(kPrefix + kRtN + kSuffix);
            const Outcome o = Attempt([&] { target.GetData(got.data(), kPrefix, kRtN); });
            if (kRtContract == RtContract::Unsupported)
            {
                checkThrew(o, "NotSupportedException",
                           "G2 non-rasterizing renderer rejects the offset read AFTER argument "
                           "validation (capability rejection stays last)");
                CheckUntouched(got, "G2 rejected offset read touched nothing");
            }
            else
            {
                checkSucceeded(o,
                    "G2 whole-level render-target read at startIndex 5 succeeds (pre-fix: the "
                    "startIndex == 0 gate threw runtime_error)");
                if (!o.threw)
                    CheckRange(got, kPrefix, reference,
                               "G2 writes exactly [5,60), prefix and suffix intact");
            }
        }

        // G3: excess capacity on a render-target read. Pre-fix the `elementCount == total` half of
        //     the same gate rejects this too.
        {
            auto got = GuardedBuffer(kPrefix + kRtN + 40);
            const Outcome o = Attempt([&] { target.GetData(got.data(), kPrefix, kRtN + 30); });
            if (kRtContract == RtContract::Unsupported)
            {
                checkThrew(o, "NotSupportedException",
                           "G3 non-rasterizing renderer rejects the excess-capacity read");
                CheckUntouched(got, "G3 rejected excess-capacity read touched nothing");
            }
            else
            {
                checkSucceeded(o,
                    "G3 excess elementCount on a render target is accepted (pre-fix: the "
                    "elementCount == total gate threw runtime_error)");
                if (!o.threw)
                    CheckRange(got, kPrefix, reference,
                               "G3 excess capacity is ignored: only the 55-pixel region is written");
            }
        }

        // G4: an undersized capacity must be rejected with the ARGUMENT error, on every renderer --
        //     including the non-rasterizing one, where it must NOT be weakened into the capability
        //     rejection. This is REMED-GFX-162's precedence, asserted from the texture side.
        {
            auto got = GuardedBuffer(kPrefix + kRtN + kSuffix);
            checkThrew(Attempt([&] { target.GetData(got.data(), kPrefix, kRtN - 1); }), "out_of_range",
                       "G4 undersized elementCount is an ARGUMENT error before any capability "
                       "decision");
            CheckUntouched(got, "G4 rejected undersized render-target read touched nothing");
        }

        // G5: the same argument precedence for a negative startIndex and a zero elementCount.
        {
            auto got = GuardedBuffer(kRtN + kSuffix);
            checkThrew(Attempt([&] { target.GetData(got.data(), -1, kRtN); }), "out_of_range",
                       "G5 negative startIndex precedes the capability decision");
            checkThrew(Attempt([&] { target.GetData(got.data(), 0, 0); }), "invalid_argument",
                       "G5 zero elementCount precedes the capability decision");
            checkThrew(Attempt([&] {
                           target.GetData(got.data(), std::numeric_limits<int>::max() - 2, kRtN);
                       }), "out_of_range",
                       "G5 overflow-shaped request precedes the capability decision");
            CheckUntouched(got, "G5 none of the three argument rejections wrote anything");
        }

        // G6: two reads at different offsets into one destination, and a repeat of the first --
        //     no read may disturb another's range or reuse another's sizing.
        if (kRtContract == RtContract::Exact)
        {
            auto got = GuardedBuffer(2 * kRtN + 30);
            const int a = 3, b = kRtN + 17;
            const Outcome o1 = Attempt([&] { target.GetData(got.data(), a, kRtN); });
            const Outcome o2 = Attempt([&] { target.GetData(got.data(), b, kRtN); });
            checkSucceeded(o1, "G6 first offset read succeeds");
            checkSucceeded(o2, "G6 second offset read succeeds");
            if (!o1.threw && !o2.threw)
            {
                std::string why;
                for (int i = 0; i < kRtN && why.empty(); ++i)
                {
                    if (!Same(got[static_cast<std::size_t>(a + i)],
                              reference[static_cast<std::size_t>(i)]))
                        why = "first copy wrong at element " + std::to_string(i);
                    else if (!Same(got[static_cast<std::size_t>(b + i)],
                                   reference[static_cast<std::size_t>(i)]))
                        why = "second copy wrong at element " + std::to_string(i);
                }
                for (int i = a + kRtN; i < b && why.empty(); ++i)
                    if (!IsGuard(got[static_cast<std::size_t>(i)]) ||
                        GuardIndexOf(got[static_cast<std::size_t>(i)]) != i)
                        why = "the gap between the two copies was overwritten at " +
                              std::to_string(i);
                check(why.empty(), "G6 two render-target reads at different offsets are "
                                   "independent and exact" + (why.empty() ? "" : " -- " + why));
            }
        }
    }

public:
    Texture2DGetDataTransferRangeTest()
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

        std::printf("renderer=%s declaredRenderTargetReadback=%s declaredMipPolicy=%s "
                    "texture=%dx%d renderTarget=%dx%d\n",
                    kRendererName, kRtContract == RtContract::Exact ? "exact" : "unsupported",
                    kMipPolicy == MipPolicy::Supported ? "supported"
                        : (kMipPolicy == MipPolicy::RejectConstruction ? "reject-construction" : "reject-upload"),
                    kTexW, kTexH, kRtW, kRtH);

        SectionT(dev);
        SectionR(dev);
        SectionG(dev);

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        Exit();
    }
};

int main()
{
    Texture2DGetDataTransferRangeTest game;
    game.Run();
    return game.getResult();
}
