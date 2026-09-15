// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0110: the Wayland backend's pure decisions, with no compositor.
//
// Everything here is arithmetic or a table the backend applies to what a compositor sends: the
// version a global is bound at, how a scale becomes a buffer size, what a configure means for the
// content size, where the built-in title bar's buttons are, how a wheel notch is counted, which
// XKB state is which modifier. Each is the kind of rule that is easy to get subtly wrong and hard
// to see wrong on one desktop, so each is pinned here rather than trusted.

#include <gtest/gtest.h>

#include "../../../src/Wayland/WaylandConnection.hpp"
#include "../../../src/Wayland/WaylandDataDevice.hpp"
#include "../../../src/Wayland/WaylandFrame.hpp"
#include "../../../src/Wayland/WaylandKeyboard.hpp"
#include "../../../src/Wayland/WaylandMouse.hpp"
#include "../../../src/Wayland/WaylandOutputs.hpp"
#include "../../../src/Wayland/WaylandScaling.hpp"
#include "../../../src/Wayland/WaylandShm.hpp"
#include "../../../src/Wayland/WaylandTextInput.hpp"

#include <linux/input-event-codes.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Wayland;

// --- version negotiation (WAYLAND-0030) ------------------------------------------------------------

TEST(WaylandVersionNegotiation, TheLowestOfTheThreeVersionsIsBound)
{
    // A compositor newer than the backend: the backend's own maximum.
    EXPECT_EQ(NegotiateVersion(9, 6, 9), 6u);
    // A compositor older than the backend: the compositor's.
    EXPECT_EQ(NegotiateVersion(3, 6, 9), 3u);
    // Headers older than the backend's code path: never a version the bindings cannot marshal.
    EXPECT_EQ(NegotiateVersion(9, 9, 7), 7u);
    EXPECT_EQ(NegotiateVersion(1, 1, 1), 1u);
}

TEST(WaylandVersionNegotiation, IsUsableAtCompileTime)
{
    static_assert(NegotiateVersion(5, 4, 6) == 4u);
    SUCCEED();
}

// --- scaling (D-13, WAYLAND-0042) -----------------------------------------------------------------

TEST(WaylandScaling, AnApplicationThatDidNotAskForHighDpiIsNeverScaled)
{
    ScaleInputs inputs;
    inputs.highDpi = false;
    inputs.viewporter = true;
    inputs.fractional120 = 180;
    inputs.integer = 2;
    const ScaleDecision decision = DecideScale(inputs);
    EXPECT_EQ(decision.method, ScaleMethod::Unscaled);
    EXPECT_DOUBLE_EQ(decision.scale, 1.0);
    EXPECT_EQ(decision.bufferScale, 1);
}

TEST(WaylandScaling, AFractionalScaleGoesThroughTheViewport)
{
    ScaleInputs inputs;
    inputs.highDpi = true;
    inputs.viewporter = true;
    inputs.fractional120 = 150;  // 1.25
    inputs.integer = 2;          // what wl_output.scale says on a 125 % desktop
    const ScaleDecision decision = DecideScale(inputs);
    EXPECT_EQ(decision.method, ScaleMethod::Viewport);
    EXPECT_DOUBLE_EQ(decision.scale, 1.25);
    EXPECT_EQ(decision.bufferScale, 1) << "set_buffer_scale must stay 1 whenever a viewport scales";
}

TEST(WaylandScaling, AnIntegerScaleAlsoUsesTheViewportWhereOneExists)
{
    // set_buffer_scale(2) makes an odd-sized buffer a fatal protocol error; an EGL buffer one
    // frame behind a resize is exactly such a buffer.
    ScaleInputs inputs;
    inputs.highDpi = true;
    inputs.viewporter = true;
    inputs.integer = 2;
    const ScaleDecision decision = DecideScale(inputs);
    EXPECT_EQ(decision.method, ScaleMethod::Viewport);
    EXPECT_DOUBLE_EQ(decision.scale, 2.0);
    EXPECT_EQ(decision.bufferScale, 1);
}

TEST(WaylandScaling, WithoutAViewporterTheIntegerScaleIsTheBufferScale)
{
    ScaleInputs inputs;
    inputs.highDpi = true;
    inputs.viewporter = false;
    inputs.fractional120 = 150;  // cannot be honoured without a viewport
    inputs.integer = 2;
    const ScaleDecision decision = DecideScale(inputs);
    EXPECT_EQ(decision.method, ScaleMethod::BufferScale);
    EXPECT_DOUBLE_EQ(decision.scale, 2.0);
    EXPECT_EQ(decision.bufferScale, 2);
}

TEST(WaylandScaling, ScaleOneIsUnscaledWhateverTheRoute)
{
    ScaleInputs inputs;
    inputs.highDpi = true;
    inputs.viewporter = true;
    inputs.fractional120 = 120;
    EXPECT_EQ(DecideScale(inputs).method, ScaleMethod::Unscaled);
    inputs.viewporter = false;
    inputs.integer = 1;
    EXPECT_EQ(DecideScale(inputs).method, ScaleMethod::Unscaled);
    // A nonsensical integer scale from a broken compositor is treated as 1, not as a divisor.
    inputs.integer = 0;
    EXPECT_EQ(DecideScale(inputs).bufferScale, 1);
}

TEST(WaylandScaling, BufferExtentsRoundHalfAwayFromZero)
{
    // fractional-scale-v1: round(surface size × scale), halfway away from zero.
    EXPECT_EQ(ScaledExtent(800, 1.25), 1000);
    EXPECT_EQ(ScaledExtent(801, 1.25), 1001);  // 1001.25
    EXPECT_EQ(ScaledExtent(2, 1.25), 3);       // 2.5 -> 3
    EXPECT_EQ(ScaledExtent(6, 1.75), 11);      // 10.5 -> 11
    EXPECT_EQ(ScaledExtent(333, 1.5), 500);    // 499.5 -> 500
    EXPECT_EQ(ScaledExtent(640, 2.0), 1280);
}

TEST(WaylandScaling, ExtentsNeverCollapseToZero)
{
    EXPECT_EQ(ScaledExtent(1, 0.25), 1);
    EXPECT_EQ(ScaledExtent(0, 2.0), 0);
    EXPECT_EQ(ScaledExtent(-5, 2.0), 0);
}

// --- toplevel configure (D-9, WAYLAND-0033) -------------------------------------------------------

TEST(WaylandToplevelStates, EveryKnownStateIsRecognised)
{
    const std::array<std::uint32_t, 9> all = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    const ToplevelStates states = ParseToplevelStates(all);
    EXPECT_TRUE(states.maximized);
    EXPECT_TRUE(states.fullscreen);
    EXPECT_TRUE(states.resizing);
    EXPECT_TRUE(states.activated);
    EXPECT_TRUE(states.tiled);
    EXPECT_TRUE(states.suspended);
}

TEST(WaylandToplevelStates, UnknownValuesAreIgnoredAsTheProtocolRequires)
{
    const std::array<std::uint32_t, 3> future = {0, 42, 1000};
    const ToplevelStates states = ParseToplevelStates(future);
    EXPECT_FALSE(states.maximized || states.fullscreen || states.resizing || states.activated || states.tiled ||
                 states.suspended);
}

TEST(WaylandToplevelStates, EachTiledEdgeCountsAsTiled)
{
    for (std::uint32_t edge = 5; edge <= 8; ++edge)
    {
        const std::array<std::uint32_t, 1> one = {edge};
        EXPECT_TRUE(ParseToplevelStates(one).tiled) << edge;
    }
}

TEST(WaylandConfigureSize, AZeroDimensionKeepsTheCurrentSize)
{
    const LogicalSize size = ResolveConfigureSize(0, 0, {}, {640, 480}, 0, {}, {});
    EXPECT_EQ(size.width, 640);
    EXPECT_EQ(size.height, 480);
    const LogicalSize half = ResolveConfigureSize(800, 0, {}, {640, 480}, 0, {}, {});
    EXPECT_EQ(half.width, 800);
    EXPECT_EQ(half.height, 480);
}

TEST(WaylandConfigureSize, AFloatingSizeIsBoundedByTheWindowsLimits)
{
    const LogicalSize small = ResolveConfigureSize(100, 50, {}, {640, 480}, 0, {200, 150}, {});
    EXPECT_EQ(small.width, 200);
    EXPECT_EQ(small.height, 150);
    const LogicalSize large = ResolveConfigureSize(3000, 2000, {}, {640, 480}, 0, {}, {1024, 768});
    EXPECT_EQ(large.width, 1024);
    EXPECT_EQ(large.height, 768);
}

TEST(WaylandConfigureSize, AConstrainedWindowTakesTheSizeItIsTold)
{
    // xdg_toplevel: a maximized or fullscreen window that is not exactly the configured size is a
    // protocol error on some compositors and a misplaced window on the rest.
    ToplevelStates maximized;
    maximized.maximized = true;
    const LogicalSize size = ResolveConfigureSize(1920, 1080, maximized, {640, 480}, 0, {}, {800, 600});
    EXPECT_EQ(size.width, 1920);
    EXPECT_EQ(size.height, 1080);

    ToplevelStates tiled;
    tiled.tiled = true;
    const LogicalSize half = ResolveConfigureSize(960, 1080, tiled, {640, 480}, 0, {1000, 0}, {});
    EXPECT_EQ(half.width, 960);
}

TEST(WaylandConfigureSize, TheTitleBarIsTakenOffTheGeometryHeight)
{
    const LogicalSize size = ResolveConfigureSize(800, 632, {}, {640, 480}, 32, {}, {});
    EXPECT_EQ(size.width, 800);
    EXPECT_EQ(size.height, 600);
}

TEST(WaylandConfigureSize, TheResultIsNeverEmpty)
{
    const LogicalSize size = ResolveConfigureSize(0, 10, {}, {0, 0}, 32, {}, {});
    EXPECT_GE(size.width, 1);
    EXPECT_GE(size.height, 1);
}

// --- outputs (WAYLAND-0040) ------------------------------------------------------------------

TEST(WaylandOutputScale, TheLogicalSizeGivesTheFractionalScale)
{
    WaylandOutputState state;
    state.modeWidth = 2560;
    state.modeHeight = 1440;
    state.scale = 2;  // wl_output.scale is the ceiling of 1.25
    state.hasLogical = true;
    state.logicalWidth = 2048;
    state.logicalHeight = 1152;
    EXPECT_FLOAT_EQ(OutputContentScale(state), 1.25f);
}

TEST(WaylandOutputScale, ARotatedOutputComparesTheRightAxis)
{
    WaylandOutputState state;
    state.modeWidth = 1920;
    state.modeHeight = 1080;
    state.transform = 1;  // WL_OUTPUT_TRANSFORM_90: the logical width is the mode's height
    state.hasLogical = true;
    state.logicalWidth = 1080;
    state.logicalHeight = 1920;
    EXPECT_FLOAT_EQ(OutputContentScale(state), 1.0f);
}

TEST(WaylandOutputScale, WithoutXdgOutputTheIntegerScaleStands)
{
    WaylandOutputState state;
    state.modeWidth = 3840;
    state.scale = 2;
    EXPECT_FLOAT_EQ(OutputContentScale(state), 2.0f);
    state.scale = 0;  // a broken compositor
    EXPECT_FLOAT_EQ(OutputContentScale(state), 1.0f);
}

// --- the built-in frame (D-22, WAYLAND-0090) ------------------------------------------------------

TEST(WaylandFrameHitTest, ButtonsAreSquareCellsFromTheRight)
{
    constexpr int width = 400;
    constexpr int height = 32;
    EXPECT_EQ(HitTitleBar(399, width, height, true, true), FrameHit::Close);
    EXPECT_EQ(HitTitleBar(width - 32, width, height, true, true), FrameHit::Close);
    EXPECT_EQ(HitTitleBar(width - 33, width, height, true, true), FrameHit::Maximize);
    EXPECT_EQ(HitTitleBar(width - 64, width, height, true, true), FrameHit::Maximize);
    EXPECT_EQ(HitTitleBar(width - 65, width, height, true, true), FrameHit::Minimize);
    EXPECT_EQ(HitTitleBar(width - 97, width, height, true, true), FrameHit::Title);
    EXPECT_EQ(HitTitleBar(0, width, height, true, true), FrameHit::Title);
}

TEST(WaylandFrameHitTest, AnActionTheCompositorDoesNotOfferHasNoButton)
{
    constexpr int width = 400;
    constexpr int height = 32;
    // No maximize: minimize moves into its cell.
    EXPECT_EQ(HitTitleBar(width - 40, width, height, false, true), FrameHit::Minimize);
    // Neither: only close remains.
    EXPECT_EQ(HitTitleBar(width - 40, width, height, false, false), FrameHit::Title);
    EXPECT_EQ(HitTitleBar(width - 10, width, height, false, false), FrameHit::Close);
}

TEST(WaylandFrameHitTest, PointsOutsideTheBarHitNothing)
{
    EXPECT_EQ(HitTitleBar(-1, 400, 32, true, true), FrameHit::None);
    EXPECT_EQ(HitTitleBar(400, 400, 32, true, true), FrameHit::None);
}

TEST(WaylandFrameHitTest, TheResizeBorderNamesEdgesAndCorners)
{
    constexpr int width = 640;
    constexpr int height = 512;
    constexpr int border = 8;
    constexpr int corner = 16;
    EXPECT_EQ(ResizeEdgeAt(-4, 200, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_LEFT);
    EXPECT_EQ(ResizeEdgeAt(644, 200, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_RIGHT);
    EXPECT_EQ(ResizeEdgeAt(300, -4, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_TOP);
    EXPECT_EQ(ResizeEdgeAt(300, 515, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM);
    EXPECT_EQ(ResizeEdgeAt(-4, -4, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT);
    EXPECT_EQ(ResizeEdgeAt(644, 515, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT);
    // Along an edge but within `corner` of the corner is the corner, as every toolkit does it.
    EXPECT_EQ(ResizeEdgeAt(-4, 10, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT);
    EXPECT_EQ(ResizeEdgeAt(630, -4, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT);
    EXPECT_EQ(ResizeEdgeAt(-4, 505, width, height, border, corner), XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT);
}

TEST(WaylandFrameHitTest, InsideTheWindowOrBeyondTheBorderIsNoEdge)
{
    EXPECT_EQ(ResizeEdgeAt(100, 100, 640, 512, 8, 16), XDG_TOPLEVEL_RESIZE_EDGE_NONE);
    EXPECT_EQ(ResizeEdgeAt(-9, 100, 640, 512, 8, 16), XDG_TOPLEVEL_RESIZE_EDGE_NONE);
    EXPECT_EQ(ResizeEdgeAt(100, 520, 640, 512, 8, 16), XDG_TOPLEVEL_RESIZE_EDGE_NONE);
}

// --- the pointer (D-17, WAYLAND-0055) -------------------------------------------------------------

TEST(WaylandButtonMapping, LinuxButtonCodesMapToTheContractsNumbers)
{
    EXPECT_EQ(ButtonFromLinuxCode(BTN_LEFT), 1);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_MIDDLE), 2);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_RIGHT), 3);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_SIDE), 4);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_BACK), 4);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_EXTRA), 5);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_FORWARD), 5);
    EXPECT_EQ(ButtonFromLinuxCode(BTN_TASK), 0);
    EXPECT_EQ(ButtonFromLinuxCode(0), 0);
}

TEST(WaylandWheelMapping, Value120IsTakenAsItIs)
{
    AxisFrame frame;
    frame.hasValue120 = true;
    frame.value120 = 60;  // half a notch of a high-resolution wheel
    frame.hasDiscrete = true;
    frame.discrete = 1;   // never both, but value120 wins if a compositor sent them
    frame.hasValue = true;
    frame.value = 5.0;
    EXPECT_EQ(AxisTo120(frame), 60);
}

TEST(WaylandWheelMapping, DiscreteStepsAreWholeNotches)
{
    AxisFrame frame;
    frame.hasDiscrete = true;
    frame.discrete = -2;
    frame.hasValue = true;
    frame.value = -30.0;  // what the continuous value says about the same motion is ignored
    EXPECT_EQ(AxisTo120(frame), -240);
}

TEST(WaylandWheelMapping, AContinuousValueIsTenUnitsPerNotch)
{
    // A touchpad's smooth scroll becomes fractional notches rather than nothing -- the X11 wheel
    // defect this backend was asked not to repeat is losing exactly these.
    AxisFrame frame;
    frame.hasValue = true;
    frame.value = 10.0;
    EXPECT_EQ(AxisTo120(frame), 120);
    frame.value = 2.5;
    EXPECT_EQ(AxisTo120(frame), 30);
    frame.value = -0.5;
    EXPECT_EQ(AxisTo120(frame), -6);
}

TEST(WaylandWheelMapping, AnEmptyFrameIsNoScroll)
{
    EXPECT_EQ(AxisTo120(AxisFrame{}), 0);
}

// --- the keyboard (WAYLAND-0051) ------------------------------------------------------------------

class WaylandModifierMapping : public ::testing::Test
{
protected:
    void SetUp() override
    {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        ASSERT_NE(context_, nullptr);
        const xkb_rule_names names = {"evdev", "pc105", "us,cz", "", "grp:alt_shift_toggle,lv3:ralt_switch"};
        keymap_ = xkb_keymap_new_from_names(context_, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap_ == nullptr)
        {
            GTEST_SKIP() << "xkeyboard-config data is not installed; no keymap can be compiled";
        }
        state_ = xkb_state_new(keymap_);
        ASSERT_NE(state_, nullptr);
    }

    void TearDown() override
    {
        if (state_ != nullptr) { xkb_state_unref(state_); }
        if (keymap_ != nullptr) { xkb_keymap_unref(keymap_); }
        if (context_ != nullptr) { xkb_context_unref(context_); }
    }

    /// Presses or releases an evdev key the way a wl_keyboard.key would be applied to the state.
    void Key(const std::uint32_t evdev, const bool down)
    {
        xkb_state_update_key(state_, evdev + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

    [[nodiscard]] bool Has(const KeyModifier modifier) const
    {
        return (ModifiersFromXkbState(state_) & static_cast<std::uint16_t>(modifier)) != 0;
    }

    xkb_context* context_ = nullptr;
    xkb_keymap* keymap_ = nullptr;
    xkb_state* state_ = nullptr;
};

TEST_F(WaylandModifierMapping, NoStateIsNoModifier)
{
    EXPECT_EQ(ModifiersFromXkbState(nullptr), 0);
    EXPECT_EQ(ModifiersFromXkbState(state_), 0);
}

TEST_F(WaylandModifierMapping, HeldModifiersAreReported)
{
    Key(KEY_LEFTSHIFT, true);
    EXPECT_TRUE(Has(KeyModifier::Shift));
    Key(KEY_LEFTSHIFT, false);
    EXPECT_FALSE(Has(KeyModifier::Shift));

    Key(KEY_RIGHTCTRL, true);
    EXPECT_TRUE(Has(KeyModifier::Control));
    Key(KEY_RIGHTCTRL, false);

    Key(KEY_LEFTALT, true);
    EXPECT_TRUE(Has(KeyModifier::Alt));
    Key(KEY_LEFTALT, false);

    Key(KEY_LEFTMETA, true);
    EXPECT_TRUE(Has(KeyModifier::Gui));
    Key(KEY_LEFTMETA, false);
    EXPECT_EQ(ModifiersFromXkbState(state_), 0);
}

TEST_F(WaylandModifierMapping, LocksAreLatchedStates)
{
    Key(KEY_CAPSLOCK, true);
    Key(KEY_CAPSLOCK, false);
    EXPECT_TRUE(Has(KeyModifier::CapsLock)) << "Caps Lock stays on after the key is released";
    Key(KEY_NUMLOCK, true);
    Key(KEY_NUMLOCK, false);
    EXPECT_TRUE(Has(KeyModifier::NumLock));
}

TEST_F(WaylandModifierMapping, AltGrIsTheModeModifierAndNotAlt)
{
    // lv3:ralt_switch makes Right Alt ISO_Level3_Shift, as the Czech and most European layouts
    // do. It is AltGr (Mode), which a game reading Alt must not see.
    Key(KEY_RIGHTALT, true);
    EXPECT_TRUE(Has(KeyModifier::Mode));
    EXPECT_FALSE(Has(KeyModifier::Alt));
    Key(KEY_RIGHTALT, false);
    EXPECT_FALSE(Has(KeyModifier::Mode));
}

TEST(WaylandCommittedText, ControlCharactersAreNotText)
{
    EXPECT_FALSE(IsCommittableText(""));
    EXPECT_FALSE(IsCommittableText("\r"));
    EXPECT_FALSE(IsCommittableText("\b"));
    EXPECT_FALSE(IsCommittableText("\x1b"));
    EXPECT_FALSE(IsCommittableText("\x7f"));
    EXPECT_TRUE(IsCommittableText(" "));
    EXPECT_TRUE(IsCommittableText("a"));
    EXPECT_TRUE(IsCommittableText("\xc5\x99"));  // ř
    EXPECT_TRUE(IsCommittableText("\xe2\x82\xac"));  // €
}

// --- text input (WAYLAND-0054) --------------------------------------------------------------------

TEST(WaylandTextInputMath, ByteOffsetsBecomeCodePointCounts)
{
    // text-input-v3 reports the preedit cursor in bytes; the contract's TextEditingEvent counts
    // characters.
    const std::string text = "p\xc5\x99\xc3\xad\xc5\xa1ern\xc3\xad";  // "příšerní"
    EXPECT_EQ(CodePointsBefore(text, 0), 0);
    EXPECT_EQ(CodePointsBefore(text, 1), 1);   // after "p"
    EXPECT_EQ(CodePointsBefore(text, 3), 2);   // after "př"
    EXPECT_EQ(CodePointsBefore(text, 5), 3);   // after "pří"
    EXPECT_EQ(CodePointsBefore(text, static_cast<std::int32_t>(text.size())), 8);
}

TEST(WaylandTextInputMath, OutOfRangeOffsetsAreClamped)
{
    EXPECT_EQ(CodePointsBefore("abc", -1), 0);  // -1 is the protocol's "hide the cursor"
    EXPECT_EQ(CodePointsBefore("abc", 99), 3);
    EXPECT_EQ(CodePointsBefore("", 5), 0);
}

// --- the clipboard (WAYLAND-0070) -----------------------------------------------------------------

TEST(WaylandMimeTypes, UtfEightTextIsOfferedUnderEveryNameReadersUse)
{
    const std::vector<std::string>& names = TextMimeTypes();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names.front(), "text/plain;charset=utf-8");
    for (const char* expected : {"text/plain", "UTF8_STRING", "TEXT", "STRING"})
    {
        EXPECT_NE(std::find(names.begin(), names.end(), expected), names.end()) << expected;
    }
}

TEST(WaylandTransferWrite, AFullPipeStopsWithoutFailing)
{
    int pipes[2];
    ASSERT_EQ(::pipe2(pipes, O_CLOEXEC | O_NONBLOCK), 0);
    const std::vector<std::uint8_t> data(1 << 20, 0x5a);  // far more than a pipe holds
    std::size_t offset = 0;
    EXPECT_TRUE(WriteSome(pipes[1], data, offset));
    EXPECT_GT(offset, 0u);
    EXPECT_LT(offset, data.size()) << "a pipe cannot hold a megabyte; EAGAIN must stop the write";

    // Drain what was written, then the rest goes.
    std::vector<std::uint8_t> sink(offset);
    std::size_t drained = 0;
    while (drained < offset)
    {
        const ssize_t got = ::read(pipes[0], sink.data(), sink.size());
        ASSERT_GT(got, 0);
        drained += static_cast<std::size_t>(got);
    }
    const std::size_t before = offset;
    EXPECT_TRUE(WriteSome(pipes[1], data, offset));
    EXPECT_GT(offset, before);
    ::close(pipes[0]);
    ::close(pipes[1]);
}

TEST(WaylandTransferWrite, AClosedReaderEndsTheTransfer)
{
    int pipes[2];
    ASSERT_EQ(::pipe2(pipes, O_CLOEXEC | O_NONBLOCK), 0);
    ::close(pipes[0]);
    const std::vector<std::uint8_t> data(16, 1);
    std::size_t offset = 0;
    // A paste target that closed its end early: the write fails with EPIPE. Under SIGPIPE's
    // default disposition -- which a game has, and which this test process has -- a plain write()
    // would have killed the process here instead.
    EXPECT_FALSE(WriteSome(pipes[1], data, offset));
    sigset_t pending;
    sigemptyset(&pending);
    sigpending(&pending);
    EXPECT_EQ(sigismember(&pending, SIGPIPE), 0) << "the write's SIGPIPE must not be left pending";
    ::close(pipes[1]);
}

// --- shared memory (WAYLAND-0082) -----------------------------------------------------------------

TEST(WaylandShmFile, AnAnonymousFileHasTheRequestedSizeAndIsCloseOnExec)
{
    const int descriptor = CreateAnonymousFile(4096 * 3);
    ASSERT_GE(descriptor, 0);
    struct stat info {};
    ASSERT_EQ(::fstat(descriptor, &info), 0);
    EXPECT_EQ(info.st_size, 4096 * 3);
    EXPECT_NE(::fcntl(descriptor, F_GETFD) & FD_CLOEXEC, 0);
    void* mapping = ::mmap(nullptr, 4096 * 3, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    static_cast<unsigned char*>(mapping)[4096 * 3 - 1] = 7;
    ::munmap(mapping, 4096 * 3);
    ::close(descriptor);
}

TEST(WaylandShmFile, TheFileCannotBeShrunkUnderTheCompositor)
{
    // A client that truncates a pool the compositor has mapped makes the compositor fault (and
    // kill the client); F_SEAL_SHRINK makes that impossible even by accident.
    const int descriptor = CreateAnonymousFile(8192);
    ASSERT_GE(descriptor, 0);
    const int seals = ::fcntl(descriptor, F_GET_SEALS);
    if (seals < 0)
    {
        ::close(descriptor);
        GTEST_SKIP() << "memfd sealing is unavailable; the shm_open fallback cannot be sealed";
    }
    EXPECT_NE(seals & F_SEAL_SHRINK, 0);
    EXPECT_NE(::ftruncate(descriptor, 4096), 0);
    ::close(descriptor);
}

} // namespace
