// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-162 -- Headless GraphicsDevice.GetBackBufferData must REJECT, not fabricate.
//
// Headless rasterizes nothing and owns no backbuffer pixel storage. It formerly filled the caller's
// buffer with the last Clear() colour and returned success, so a caller could not tell a
// non-rasterizing device from a real black (or last-cleared) frame -- exactly the
// fabricate-rather-than-refuse behaviour REMED-GFX-127/130 removed from the Texture and
// render-target readbacks. This fixture pins the backbuffer counterpart of that contract:
//
//   * on HEADLESS every VALID GetBackBufferData request raises System::NotSupportedException and
//     leaves the caller's destination COMPLETELY untouched (prefix, requested range and suffix all
//     hold their distinct guard values);
//   * an INVALID request keeps the authoritative validation precedence -- a null destination throws
//     std::invalid_argument, an out-of-bounds rectangle throws std::out_of_range and an undersized
//     element count throws std::runtime_error -- i.e. capability rejection stays LAST, after every
//     argument check, the same order the Texture path uses;
//   * on a rasterizing renderer the same public calls SUCCEED and write the whole requested range,
//     so the rejection is specific to the non-rasterizing renderer, not a universal break.
//
// Each leg runs in its own process (fork+exec) so its first read is genuinely first and a crash or
// hang is a distinct, attributable result; a leg's exit code is the only PASS/FAIL signal.
//
// Scope: this is the CAPABILITY-rejection contract, distinct from REMED-GFX-165 (dimension source),
// REMED-GFX-161 (first-read completion) and REMED-GFX-149 (startIndex/element-count asymmetry). The
// non-zero-startIndex leg uses a non-zero startIndex ONLY as a destination guard; GFX-149 stays
// separate. GetBackBufferData is Color-only (no templated element type), so there is no per-type
// matrix -- the same capability boundary governs the one supported element type.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "System/NotSupportedException.hpp"

#include <climits>
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
#define CNA_GFX162_CAN_FORK 1
#else
#define CNA_GFX162_CAN_FORK 0
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
#elif defined(CNA_RENDERER_SKIA)
    constexpr const char* kRendererName = "SKIA";
    constexpr bool kRasterizes = true;
#else
#error "REMED-GFX-162: this renderer has no declared backbuffer-readback capability contract."
#endif

    // BGFX honours only a full-surface backbuffer read; a sub-rectangle reads back zeros (a
    // pre-existing gap declared by REMED-GFX-165). The sub-rectangle success legs declare it as a
    // boundary rather than failing it. It has no bearing on the Headless rejection contract.
#if defined(CNA_RENDERER_BGFX)
    constexpr bool kSupportsSubRectangleBackbufferRead = false;
#else
    constexpr bool kSupportsSubRectangleBackbufferRead = true;
#endif

    // Odd, non-square, not a common default -- matches REMED-GFX-165/161's canonical size.
    constexpr int kW = 37;
    constexpr int kH = 23;

    // The clear colour the rasterizing controls read back. Alpha 0xFF, so a guard value (alpha 0xCD)
    // can never be mistaken for it.
    const Color kClear(0x11, 0x22, 0x33, 0xFF);

    // A DISTINCT guard per destination element (not one uniform sentinel): the three channels each
    // vary with the index, alpha fixed at 0xCD (never 0xFF, so a guard can never equal kClear). This
    // localises any mutation exactly -- a partial zero-fill, a memcpy of the caller's own data back
    // over itself, or a stray aliased write through the exception object all leave at least one
    // element differing from its own guard.
    Color Guard(std::size_t i)
    {
        return Color(static_cast<std::uint8_t>(0x40 + i),
                     static_cast<std::uint8_t>(0x80 + i * 3),
                     static_cast<std::uint8_t>(0xC0 + i * 5),
                     static_cast<std::uint8_t>(0xCD));
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    // The classified outcome of one GetBackBufferData call.
    enum class Outcome { Success, NotSupported, InvalidArgument, OutOfRange, RuntimeError, OtherStd, OtherUnknown };

    const char* OutcomeName(Outcome o)
    {
        switch (o)
        {
            case Outcome::Success:         return "success";
            case Outcome::NotSupported:    return "System::NotSupportedException";
            case Outcome::InvalidArgument: return "std::invalid_argument";
            case Outcome::OutOfRange:      return "std::out_of_range";
            case Outcome::RuntimeError:    return "std::runtime_error";
            case Outcome::OtherStd:        return "std::exception (other)";
            case Outcome::OtherUnknown:    return "non-std exception";
        }
        return "?";
    }
}

/**
 * @brief REMED-GFX-162's Headless backbuffer-rejection contract, one leg per process.
 */
class BackbufferHeadlessRejectTest : public Game
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

    void info(const std::string& text) const
    {
        std::printf("[INFO] %s\n", text.c_str());
        std::fflush(stdout);
    }

    [[nodiscard]] bool wants(const char* id) const { return onlyLeg_.empty() || onlyLeg_ == id; }

    // ------------------------------------------------------------------ call + classify

    struct ReadResult
    {
        Outcome outcome = Outcome::Success;
        std::string what;
        std::vector<Color> buf;   ///< the whole destination, exactly as it stands after the call
    };

    /// Builds a guard-filled buffer of @p capacity and reads @p count elements at @p startIndex,
    /// optionally through an explicit rectangle. Returns the classified outcome and the final buffer.
    ReadResult Read(GraphicsDevice& dev, const Rectangle* rect, int startIndex, int count, std::size_t capacity)
    {
        ReadResult r;
        r.buf.reserve(capacity);
        for (std::size_t i = 0; i < capacity; ++i) r.buf.push_back(Guard(i));
        try
        {
            if (rect)
                dev.GetBackBufferData(rect, r.buf.data(), startIndex, count);
            else if (startIndex == 0)
                dev.GetBackBufferData(r.buf.data(), count);
            else
                dev.GetBackBufferData(r.buf.data(), startIndex, count);
        }
        catch (const System::NotSupportedException&) { r.outcome = Outcome::NotSupported; }
        catch (const std::invalid_argument& e)       { r.outcome = Outcome::InvalidArgument; r.what = e.what(); }
        catch (const std::out_of_range& e)           { r.outcome = Outcome::OutOfRange;      r.what = e.what(); }
        catch (const std::runtime_error& e)          { r.outcome = Outcome::RuntimeError;    r.what = e.what(); }
        catch (const std::exception& e)              { r.outcome = Outcome::OtherStd;        r.what = e.what(); }
        catch (...)                                  { r.outcome = Outcome::OtherUnknown; }
        return r;
    }

    /// Reports the exact mutated range, if any, of [first, first+len) against the guard baseline.
    /// @return true iff every element in the window still equals its own guard.
    bool WindowIntact(const ReadResult& r, std::size_t first, std::size_t len, const std::string& label)
    {
        int changed = 0;
        std::size_t firstBad = 0;
        std::string detail;
        for (std::size_t i = first; i < first + len && i < r.buf.size(); ++i)
        {
            if (!Same(r.buf[i], Guard(i)))
            {
                if (changed == 0) { firstBad = i; detail = " first-changed idx " + std::to_string(i) +
                                    " guard " + ColorText(Guard(i)) + " got " + ColorText(r.buf[i]); }
                ++changed;
            }
        }
        if (changed > 0)
            info(label + ": MUTATED " + std::to_string(changed) + " element(s) from idx " +
                 std::to_string(firstBad) + detail);
        return changed == 0;
    }

    /// @return true iff every element in the window was overwritten with @p want (no guard survives).
    bool WindowIsColor(const ReadResult& r, std::size_t first, std::size_t len, const Color& want,
                       const std::string& label)
    {
        int unwritten = 0, wrong = 0;
        std::string detail;
        for (std::size_t i = first; i < first + len && i < r.buf.size(); ++i)
        {
            if (Same(r.buf[i], want)) continue;
            if (Same(r.buf[i], Guard(i))) ++unwritten;
            else { ++wrong; if (detail.empty()) detail = " first-wrong idx " + std::to_string(i) +
                            " want " + ColorText(want) + " got " + ColorText(r.buf[i]); }
        }
        if (unwritten || wrong)
            info(label + ": " + std::to_string(unwritten) + " unwritten, " + std::to_string(wrong) +
                 " wrong" + detail);
        return unwritten == 0 && wrong == 0;
    }

    /// The core capability assertion for a VALID request: Headless must reject (NotSupported) and
    /// leave [first, first+len) untouched; a rasterizing renderer must succeed and fill it with kClear.
    void AssertValid(const ReadResult& r, std::size_t first, std::size_t len, const std::string& label)
    {
        if (!kRasterizes)
        {
            check(r.outcome == Outcome::NotSupported,
                  label + ": Headless rejects with " + std::string(OutcomeName(Outcome::NotSupported)) +
                      " (got " + OutcomeName(r.outcome) +
                      (r.what.empty() ? "" : ": " + r.what) + ")");
            check(WindowIntact(r, first, len, label),
                  label + ": destination requested range left completely untouched");
        }
        else
        {
            check(r.outcome == Outcome::Success,
                  label + ": rasterizing renderer succeeds (got " + OutcomeName(r.outcome) +
                      (r.what.empty() ? "" : ": " + r.what) + ")");
            if (r.outcome == Outcome::Success)
                check(WindowIsColor(r, first, len, kClear, label),
                      label + ": requested range fully written to the clear colour");
        }
    }

    /// A precedence assertion for an INVALID request: the argument exception fires on EVERY renderer,
    /// BEFORE the renderer capability is consulted -- so Headless must NOT answer NotSupported here.
    void AssertPrecedence(const ReadResult& r, Outcome expected, const std::string& label)
    {
        check(r.outcome == expected,
              label + ": " + OutcomeName(expected) + " (arg validation precedes capability) -- got " +
                  OutcomeName(r.outcome) + (r.what.empty() ? "" : ": " + r.what));
        if (!kRasterizes)
            check(r.outcome != Outcome::NotSupported,
                  label + ": Headless does not short-circuit argument validation with NotSupported");
    }

    void PrepareFrame(GraphicsDevice& dev)
    {
        dev.setViewportProperty(Viewport(0, 0, kW, kH));
        dev.Clear(kClear);
    }

    // ------------------------------------------------------------------ legs (overload matrix)

    /// R1 -- rectangle-less full read, startIndex 0, exact element count.
    void LegRectlessFull(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        ReadResult r = Read(dev, nullptr, 0, kW * kH, static_cast<std::size_t>(kW) * kH);
        AssertValid(r, 0, static_cast<std::size_t>(kW) * kH, "R1 rectangle-less full");
    }

    /// R2 -- explicit full rectangle.
    void LegRectFull(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        Rectangle full(0, 0, kW, kH);
        ReadResult r = Read(dev, &full, 0, kW * kH, static_cast<std::size_t>(kW) * kH);
        AssertValid(r, 0, static_cast<std::size_t>(kW) * kH, "R2 explicit full rectangle");
    }

    /// R3 -- a valid interior sub-rectangle.
    void LegSubRect(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        if (kRasterizes && !kSupportsSubRectangleBackbufferRead)
        {
            boundary("R3: sub-rectangle backbuffer read is a pre-existing gap on " +
                     std::string(kRendererName) + " -- recorded, not exercised");
            return;
        }
        Rectangle sub(3, 5, 20, 11);
        ReadResult r = Read(dev, &sub, 0, sub.Width * sub.Height,
                        static_cast<std::size_t>(sub.Width) * sub.Height);
        AssertValid(r, 0, static_cast<std::size_t>(sub.Width) * sub.Height, "R3 sub-rectangle");
    }

    /// R4 -- one-pixel rectangle at the bottom-right corner.
    void LegOnePixel(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        if (kRasterizes && !kSupportsSubRectangleBackbufferRead)
        {
            boundary("R4: sub-rectangle backbuffer read is a pre-existing gap on " +
                     std::string(kRendererName) + " -- recorded, not exercised");
            return;
        }
        Rectangle one(kW - 1, kH - 1, 1, 1);
        ReadResult r = Read(dev, &one, 0, 1, 1);
        AssertValid(r, 0, 1, "R4 one-pixel rectangle");
    }

    /// R5 -- rectangle-less read at a NON-ZERO startIndex, with a poison prefix and suffix. The
    /// requested range must land at [prefix, prefix+w*h); prefix and suffix must be untouched on
    /// every renderer, and the range itself follows the capability contract.
    void LegPrefixSuffix(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        const int prefix = 5, suffix = 3;
        const std::size_t count = static_cast<std::size_t>(kW) * kH;
        ReadResult r = Read(dev, nullptr, prefix, static_cast<int>(count), prefix + count + suffix);

        check(WindowIntact(r, 0, prefix, "R5 prefix"),
              "R5 destination prefix before startIndex untouched on " + std::string(kRendererName));
        check(WindowIntact(r, prefix + count, suffix, "R5 suffix"),
              "R5 destination suffix after the range untouched on " + std::string(kRendererName));
        AssertValid(r, prefix, count, "R5 range at non-zero startIndex");
    }

    /// R6 -- OVERSIZED destination: exact element count into a buffer with extra trailing capacity.
    /// The extra capacity must never be touched; the range follows the capability contract.
    void LegOversized(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        const std::size_t count = static_cast<std::size_t>(kW) * kH;
        const std::size_t extra = 64;
        ReadResult r = Read(dev, nullptr, 0, static_cast<int>(count), count + extra);
        check(WindowIntact(r, count, extra, "R6 tail"),
              "R6 oversized destination: trailing capacity untouched on " + std::string(kRendererName));
        AssertValid(r, 0, count, "R6 exact count into oversized buffer");
    }

    /// D1 -- determinism: two rectangle-less reads in one process reach the identical capability
    /// OUTCOME. The first read follows the full contract; the SECOND read's CONTENT is not asserted
    /// on a rasterizing renderer, because a double-buffered backbuffer (bgfx) hands back the
    /// swapped/empty buffer on a second read with no new frame (a pre-existing property, GFX-161's
    /// A3). On Headless nothing is ever written, so both reads reject and leave the destination
    /// untouched, which is asserted for both.
    void LegRepeated(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        const std::size_t count = static_cast<std::size_t>(kW) * kH;
        ReadResult a = Read(dev, nullptr, 0, static_cast<int>(count), count);
        ReadResult b = Read(dev, nullptr, 0, static_cast<int>(count), count);
        check(a.outcome == b.outcome,
              "D1 repeated reads are deterministic (" + std::string(OutcomeName(a.outcome)) + " both)");
        AssertValid(a, 0, count, "D1 first read");
        if (!kRasterizes)
            AssertValid(b, 0, count, "D1 second read (rejection is content-free)");
        else
            check(b.outcome == Outcome::Success, "D1 second read also succeeds");
        info("D1: a second GraphicsDevice in one process is not part of the single-device Game "
             "harness; per-leg fork+exec gives every leg a fresh first-of-process device instead");
    }

    // ------------------------------------------------------------------ legs (validation precedence)

    /// P1 -- null destination -> std::invalid_argument on every renderer (before capability).
    void LegNull(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        ReadResult r;
        try { dev.GetBackBufferData(nullptr, kW * kH); r.outcome = Outcome::Success; }
        catch (const System::NotSupportedException&) { r.outcome = Outcome::NotSupported; }
        catch (const std::invalid_argument& e) { r.outcome = Outcome::InvalidArgument; r.what = e.what(); }
        catch (const std::out_of_range& e)     { r.outcome = Outcome::OutOfRange;      r.what = e.what(); }
        catch (const std::runtime_error& e)    { r.outcome = Outcome::RuntimeError;    r.what = e.what(); }
        catch (const std::exception& e)        { r.outcome = Outcome::OtherStd;        r.what = e.what(); }
        catch (...)                            { r.outcome = Outcome::OtherUnknown; }
        AssertPrecedence(r, Outcome::InvalidArgument, "P1 null destination");
    }

    /// P2 -- undersized element count (one element too small for a full read) -> std::runtime_error.
    void LegUndersized(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        const int count = kW * kH;
        std::vector<Color> buf;
        buf.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) buf.push_back(Guard(static_cast<std::size_t>(i)));
        ReadResult r;
        try { dev.GetBackBufferData(buf.data(), count - 1); r.outcome = Outcome::Success; }
        catch (const System::NotSupportedException&) { r.outcome = Outcome::NotSupported; }
        catch (const std::invalid_argument& e) { r.outcome = Outcome::InvalidArgument; r.what = e.what(); }
        catch (const std::out_of_range& e)     { r.outcome = Outcome::OutOfRange;      r.what = e.what(); }
        catch (const std::runtime_error& e)    { r.outcome = Outcome::RuntimeError;    r.what = e.what(); }
        catch (const std::exception& e)        { r.outcome = Outcome::OtherStd;        r.what = e.what(); }
        catch (...)                            { r.outcome = Outcome::OtherUnknown; }
        r.buf = std::move(buf);
        AssertPrecedence(r, Outcome::RuntimeError, "P2 undersized element count");
        check(WindowIntact(r, 0, static_cast<std::size_t>(count), "P2 buf"),
              "P2 undersized request leaves the destination untouched");
    }

    /// P3 -- negative rectangle origin -> std::out_of_range (before capability).
    void LegNegativeRect(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        Rectangle bad(-1, 0, kW, kH);
        ReadResult r = Read(dev, &bad, 0, kW * kH, static_cast<std::size_t>(kW) * kH);
        AssertPrecedence(r, Outcome::OutOfRange, "P3 negative rectangle origin");
        check(WindowIntact(r, 0, static_cast<std::size_t>(kW) * kH, "P3 buf"),
              "P3 out-of-range request leaves the destination untouched");
    }

    /// P4 -- rectangle extending beyond the backbuffer -> std::out_of_range.
    void LegBeyondRect(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        Rectangle bad(0, 0, kW + 4, kH + 4);
        ReadResult r = Read(dev, &bad, 0, (kW + 4) * (kH + 4),
                        static_cast<std::size_t>(kW + 4) * (kH + 4));
        AssertPrecedence(r, Outcome::OutOfRange, "P4 rectangle beyond backbuffer");
    }

    /// P5 -- overflow-SHAPED rectangle (INT_MAX extent) -> deterministic std::out_of_range, no crash.
    /// x=0 keeps x+w == INT_MAX (no signed overflow in the bounds test) so the request is cleanly
    /// rejected as out-of-range rather than reaching w*h or the renderer.
    void LegOverflowShaped(GraphicsDevice& dev)
    {
        PrepareFrame(dev);
        Rectangle huge(0, 0, INT_MAX, 1);
        ReadResult r = Read(dev, &huge, 0, 1, 1);   // element count is irrelevant; bounds reject first
        AssertPrecedence(r, Outcome::OutOfRange, "P5 INT_MAX-width rectangle");
        Rectangle huge2(0, 0, 1, INT_MAX);
        ReadResult r2 = Read(dev, &huge2, 0, 1, 1);
        AssertPrecedence(r2, Outcome::OutOfRange, "P5 INT_MAX-height rectangle");
    }

    // ------------------------------------------------------------------ driver

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        info(std::string(kRendererName) + ": backbuffer-readback capability = " +
             (kRasterizes ? "supported" : "UNSUPPORTED (non-rasterizing, GFX-162 rejection)"));
        try
        {
            if (wants("R1")) LegRectlessFull(dev);
            if (wants("R2")) LegRectFull(dev);
            if (wants("R3")) LegSubRect(dev);
            if (wants("R4")) LegOnePixel(dev);
            if (wants("R5")) LegPrefixSuffix(dev);
            if (wants("R6")) LegOversized(dev);
            if (wants("D1")) LegRepeated(dev);
            if (wants("P1")) LegNull(dev);
            if (wants("P2")) LegUndersized(dev);
            if (wants("P3")) LegNegativeRect(dev);
            if (wants("P4")) LegBeyondRect(dev);
            if (wants("P5")) LegOverflowShaped(dev);
        }
        catch (const std::exception& e)
        {
            check(false, std::string("leg ") + onlyLeg_ + ": unexpected escaped exception: " + e.what());
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
    explicit BackbufferHeadlessRejectTest(std::string onlyLeg) : onlyLeg_(std::move(onlyLeg))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kW);
        gdm_->setPreferredBackBufferHeightProperty(kH);
    }

    [[nodiscard]] int Result() const { return result_; }
};

namespace
{
    const char* const kLegIds[] = {
        "R1", "R2", "R3", "R4", "R5", "R6", "D1", "P1", "P2", "P3", "P4", "P5"
    };

#if CNA_GFX162_CAN_FORK
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

#if CNA_GFX162_CAN_FORK
    if (onlyLeg.empty())
    {
        std::printf("[INFO] REMED-GFX-162 supervisor: %zu legs, each in its own process\n",
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

    BackbufferHeadlessRejectTest game(onlyLeg);
    game.Run();
    return game.Result();
}
