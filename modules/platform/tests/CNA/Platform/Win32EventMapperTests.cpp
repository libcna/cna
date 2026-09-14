// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0031..0039: window messages -> CNA platform events.
//
// Every case here is driven from a synthetic (message, wParam, lParam) triple with no window and
// no message loop, which is the only way the interesting behaviour can be exercised
// deterministically: the maximize/restore ordering, the close-is-a-request rule, auto-repeat,
// the focus-loss key flush, and the wheel's sign conventions all depend on message *sequences* a
// human would otherwise have to produce by hand.

#include "Win32/Win32Common.hpp"
#include "Win32/Win32EventMapper.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Win32;

constexpr WindowId kWindow = 7;

class RecordingSink final : public Win32EventSink
{
public:
    void Push(PlatformEvent event) override { events.push_back(std::move(event)); }

    void Clear() { events.clear(); }

    [[nodiscard]] std::size_t Count() const { return events.size(); }

    template <typename T>
    [[nodiscard]] const T* At(const std::size_t index) const
    {
        if (index >= events.size())
            return nullptr;
        return std::get_if<T>(&events[index]);
    }

    std::vector<PlatformEvent> events;
};

std::uint16_t NoModifiers() { return 0; }

class Win32EventMapping : public ::testing::Test
{
protected:
    Win32EventMapping()
        : mapper(kWindow, sink)
    {
        // Modifier state comes from GetKeyState, which is genuine global state a test cannot
        // control. Replacing the source is what makes every key assertion below deterministic.
        mapper.SetModifierProvider(&NoModifiers);
    }

    static std::int64_t KeyLParam(const std::uint8_t scanCode, const bool extended = false,
                                  const bool wasDown = false)
    {
        std::uint64_t lParam = static_cast<std::uint64_t>(scanCode) << 16;
        if (extended)
            lParam |= 1ull << 24;
        if (wasDown)
            lParam |= 1ull << 30;
        return static_cast<std::int64_t>(lParam);
    }

    static std::int64_t PointLParam(const int x, const int y)
    {
        return static_cast<std::int64_t>((static_cast<std::uint32_t>(y & 0xFFFF) << 16) |
                                         static_cast<std::uint32_t>(x & 0xFFFF));
    }

    static std::int64_t SizeLParam(const int width, const int height)
    {
        return PointLParam(width, height);
    }

    RecordingSink sink;
    Win32EventMapper mapper;
};

// --- close is a request ------------------------------------------------------------------------

TEST_F(Win32EventMapping, CloseRequestDoesNotDestroy)
{
    // The single most important behaviour in this file. Translate() returning true is what
    // suppresses DefWindowProcW's DestroyWindow, so the application -- not the window manager --
    // decides whether the window dies.
    EXPECT_TRUE(mapper.Translate(WM_CLOSE, 0, 0));
    ASSERT_EQ(sink.Count(), 1u);
    const auto* event = sink.At<WindowEvent>(0);
    ASSERT_NE(event, nullptr);
    EXPECT_EQ(event->kind, WindowEventKind::CloseRequested);
    EXPECT_EQ(event->window, kWindow);
}

TEST_F(Win32EventMapping, WindowDestructionDoesNotQuit)
{
    // A multi-window application must not terminate because one window went away, which is why
    // WM_DESTROY produces nothing and PostQuitMessage is never called from it.
    mapper.Translate(WM_DESTROY, 0, 0);
    EXPECT_EQ(sink.Count(), 0u);
}

// --- window state ------------------------------------------------------------------------------

TEST_F(Win32EventMapping, ResizeReportsBothLogicalAndPixelSize)
{
    EXPECT_TRUE(mapper.Translate(WM_SIZE, SIZE_RESTORED, SizeLParam(800, 600)));
    ASSERT_EQ(sink.Count(), 2u);

    const auto* resized = sink.At<WindowEvent>(0);
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(resized->kind, WindowEventKind::Resized);
    EXPECT_EQ(resized->data1, 800);
    EXPECT_EQ(resized->data2, 600);

    // Kept distinct on purpose: a renderer sizes its swapchain from the drawable size, and under
    // per-monitor DPI the two can move independently.
    const auto* pixels = sink.At<WindowEvent>(1);
    ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(pixels->kind, WindowEventKind::PixelSizeChanged);
    EXPECT_EQ(pixels->data1, 800);
    EXPECT_EQ(pixels->data2, 600);
}

TEST_F(Win32EventMapping, ReadsLargeClientSizesWithoutSignExtending)
{
    // 40000 has bit 15 set. Reading the size word signed reports it as -25536, and a renderer
    // then builds a swapchain from a negative dimension.
    mapper.Translate(WM_SIZE, SIZE_RESTORED, SizeLParam(40000, 40000));
    const auto* resized = sink.At<WindowEvent>(0);
    ASSERT_NE(resized, nullptr);
    EXPECT_EQ(resized->data1, 40000);
    EXPECT_EQ(resized->data2, 40000);
}

TEST_F(Win32EventMapping, MinimizeReportsTheStateAndNotAZeroSizedResize)
{
    mapper.Translate(WM_SIZE, SIZE_MINIMIZED, SizeLParam(0, 0));
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::Minimized);
    EXPECT_EQ(mapper.GetShowState(), Win32ShowState::Minimized);
}

TEST_F(Win32EventMapping, MaximizeReportsTheStateBeforeTheSize)
{
    // Order matters to a consumer that reacts to Maximized by reading the window: the size has to
    // be the new one by the time it looks, so Maximized comes first and Resized carries it.
    mapper.Translate(WM_SIZE, SIZE_MAXIMIZED, SizeLParam(1920, 1080));
    ASSERT_EQ(sink.Count(), 3u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::Maximized);
    EXPECT_EQ(sink.At<WindowEvent>(1)->kind, WindowEventKind::Resized);
    EXPECT_EQ(sink.At<WindowEvent>(2)->kind, WindowEventKind::PixelSizeChanged);
}

TEST_F(Win32EventMapping, RestoreIsReportedOnlyWhenTheStateActuallyChanged)
{
    mapper.Translate(WM_SIZE, SIZE_MAXIMIZED, SizeLParam(1920, 1080));
    sink.Clear();

    mapper.Translate(WM_SIZE, SIZE_RESTORED, SizeLParam(800, 600));
    ASSERT_EQ(sink.Count(), 3u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::Restored);
    sink.Clear();

    // A second restored-size message is an ordinary resize. Reporting Restored again would make a
    // consumer re-run its restore handling on every drag of the window edge.
    mapper.Translate(WM_SIZE, SIZE_RESTORED, SizeLParam(640, 480));
    ASSERT_EQ(sink.Count(), 2u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::Resized);
}

TEST_F(Win32EventMapping, IgnoresSizeMessagesAboutOtherWindows)
{
    // SIZE_MAXHIDE and SIZE_MAXSHOW describe another window's maximisation. Nothing about this
    // one changed.
    mapper.Translate(WM_SIZE, SIZE_MAXHIDE, SizeLParam(0, 0));
    mapper.Translate(WM_SIZE, SIZE_MAXSHOW, SizeLParam(0, 0));
    EXPECT_EQ(sink.Count(), 0u);
    EXPECT_EQ(mapper.GetShowState(), Win32ShowState::Normal);
}

TEST_F(Win32EventMapping, MoveReportsNegativePositionsOnASecondaryMonitor)
{
    // A monitor to the left of the primary has negative desktop coordinates. Reading the position
    // words unsigned would report 65436 instead of -100.
    mapper.Translate(WM_MOVE, 0, PointLParam(-100, -50));
    const auto* moved = sink.At<WindowEvent>(0);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->kind, WindowEventKind::Moved);
    EXPECT_EQ(moved->data1, -100);
    EXPECT_EQ(moved->data2, -50);
}

TEST_F(Win32EventMapping, FocusTransitionsAreReported)
{
    mapper.Translate(WM_SETFOCUS, 0, 0);
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::FocusGained);
    sink.Clear();

    mapper.Translate(WM_KILLFOCUS, 0, 0);
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::FocusLost);
}

TEST_F(Win32EventMapping, DpiChangeReportsScaleAndPixelSize)
{
    // wParam's low word is the new X DPI. 192 is the 200% scale a 4K laptop panel typically uses.
    mapper.Translate(WM_DPICHANGED, (192u << 16) | 192u, 0);
    ASSERT_EQ(sink.Count(), 2u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::DisplayScaleChanged);
    EXPECT_EQ(sink.At<WindowEvent>(0)->data1, 192);
    EXPECT_EQ(sink.At<WindowEvent>(1)->kind, WindowEventKind::PixelSizeChanged);
}

TEST_F(Win32EventMapping, DisplayChangeIsReported)
{
    mapper.Translate(WM_DISPLAYCHANGE, 32, SizeLParam(2560, 1440));
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<WindowEvent>(0)->kind, WindowEventKind::DisplayChanged);
}

// --- keyboard ------------------------------------------------------------------------------------

TEST_F(Win32EventMapping, KeyPressCarriesBothIdentities)
{
    mapper.Translate(WM_KEYDOWN, 'W', KeyLParam(0x11));
    ASSERT_EQ(sink.Count(), 1u);
    const auto* key = sink.At<KeyEvent>(0);
    ASSERT_NE(key, nullptr);
    EXPECT_TRUE(key->pressed);
    EXPECT_FALSE(key->repeat);
    EXPECT_EQ(key->keycode, KeyCode::W) << "the layout-dependent identity";
    EXPECT_EQ(key->scancode, Scancode::W) << "the layout-independent identity";
    EXPECT_EQ(key->window, kWindow);
}

TEST_F(Win32EventMapping, AutoRepeatIsMarkedFromThePreviousKeyStateBit)
{
    mapper.Translate(WM_KEYDOWN, 'A', KeyLParam(0x1E, false, false));
    mapper.Translate(WM_KEYDOWN, 'A', KeyLParam(0x1E, false, true));
    ASSERT_EQ(sink.Count(), 2u);
    EXPECT_FALSE(sink.At<KeyEvent>(0)->repeat);
    EXPECT_TRUE(sink.At<KeyEvent>(1)->repeat);
}

TEST_F(Win32EventMapping, ReleaseIsNeverMarkedAsRepeat)
{
    // lParam bit 30 is always set on a release, because the key was by definition down. Reading
    // it unconditionally would mark every key-up as auto-repeat.
    mapper.Translate(WM_KEYUP, 'A', KeyLParam(0x1E, false, true));
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_FALSE(sink.At<KeyEvent>(0)->pressed);
    EXPECT_FALSE(sink.At<KeyEvent>(0)->repeat);
}

TEST_F(Win32EventMapping, SystemKeysAreTranslatedAndStillReachTheDefaultHandler)
{
    // Alt+F4 and the menu mnemonics live in DefWindowProcW. Reporting the key AND returning false
    // is what keeps a CNA window closable from the keyboard.
    EXPECT_FALSE(mapper.Translate(WM_SYSKEYDOWN, VK_F4, KeyLParam(0x3E)));
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<KeyEvent>(0)->keycode, KeyCode::F4);
    EXPECT_TRUE(sink.At<KeyEvent>(0)->pressed);
}

TEST_F(Win32EventMapping, SidedModifiersAreDistinguished)
{
    mapper.Translate(WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D, false));
    mapper.Translate(WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D, true));
    ASSERT_EQ(sink.Count(), 2u);
    EXPECT_EQ(sink.At<KeyEvent>(0)->keycode, KeyCode::LeftControl);
    EXPECT_EQ(sink.At<KeyEvent>(1)->keycode, KeyCode::RightControl);
}

TEST_F(Win32EventMapping, TracksHeldKeysAndReleasesThemOnFocusLoss)
{
    // The Alt+Tab bug this prevents: the window sees Alt go down and never sees it come up,
    // because the release is delivered to whatever took focus. Without the flush, Alt stays held
    // in Keyboard::GetState() forever.
    mapper.Translate(WM_KEYDOWN, 'W', KeyLParam(0x11));
    mapper.Translate(WM_SYSKEYDOWN, VK_MENU, KeyLParam(0x38));
    ASSERT_EQ(mapper.GetHeldKeys().size(), 2u);
    sink.Clear();

    mapper.Translate(WM_KILLFOCUS, 0, 0);
    EXPECT_TRUE(mapper.GetHeldKeys().empty());

    ASSERT_EQ(sink.Count(), 3u) << "two synthesised releases, then FocusLost";
    EXPECT_FALSE(sink.At<KeyEvent>(0)->pressed);
    EXPECT_FALSE(sink.At<KeyEvent>(1)->pressed);
    EXPECT_EQ(sink.At<WindowEvent>(2)->kind, WindowEventKind::FocusLost);
}

TEST_F(Win32EventMapping, AHeldKeyIsNotCountedTwiceByAutoRepeat)
{
    mapper.Translate(WM_KEYDOWN, 'A', KeyLParam(0x1E, false, false));
    mapper.Translate(WM_KEYDOWN, 'A', KeyLParam(0x1E, false, true));
    mapper.Translate(WM_KEYDOWN, 'A', KeyLParam(0x1E, false, true));
    EXPECT_EQ(mapper.GetHeldKeys().size(), 1u);

    mapper.Translate(WM_KEYUP, 'A', KeyLParam(0x1E, false, true));
    EXPECT_TRUE(mapper.GetHeldKeys().empty());
}

// --- text input ----------------------------------------------------------------------------------

TEST_F(Win32EventMapping, CommittedTextIsSuppressedUntilTextInputStarts)
{
    // Text input is a mode. Windows delivers WM_CHAR whether or not anything asked for it, so a
    // game that never called StartTextInput must not start receiving text -- which is what it
    // sees on every other backend.
    mapper.Translate(WM_CHAR, static_cast<std::uint64_t>('a'), 0);
    EXPECT_EQ(sink.Count(), 0u);

    mapper.SetTextInputActive(true);
    mapper.Translate(WM_CHAR, static_cast<std::uint64_t>('a'), 0);
    ASSERT_EQ(sink.Count(), 1u);
    const auto* text = sink.At<TextInputEvent>(0);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text, "a");
    EXPECT_EQ(text->window, kWindow);
}

TEST_F(Win32EventMapping, CommitsASurrogatePairAsOneCharacter)
{
    mapper.SetTextInputActive(true);
    mapper.Translate(WM_CHAR, 0xD83Du, 0);
    EXPECT_EQ(sink.Count(), 0u) << "a high surrogate alone is not a character";
    mapper.Translate(WM_CHAR, 0xDE00u, 0);
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<TextInputEvent>(0)->text, "\xF0\x9F\x98\x80");
}

TEST_F(Win32EventMapping, ControlCharactersAreKeyEventsRatherThanText)
{
    mapper.SetTextInputActive(true);
    for (const std::uint64_t unit : {0x08u, 0x0Du, 0x1Bu, 0x7Fu})
        mapper.Translate(WM_CHAR, unit, 0);
    EXPECT_EQ(sink.Count(), 0u)
        << "backspace, return, escape and delete are keys, not text a field should insert";

    // Tab is the exception: a text field legitimately receives it.
    mapper.Translate(WM_CHAR, static_cast<std::uint64_t>('\t'), 0);
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<TextInputEvent>(0)->text, "\t");
}

TEST_F(Win32EventMapping, StoppingTextInputDiscardsAPendingSurrogate)
{
    mapper.SetTextInputActive(true);
    mapper.Translate(WM_CHAR, 0xD83Du, 0);
    mapper.SetTextInputActive(false);
    mapper.SetTextInputActive(true);
    // Without the reset, this low surrogate would complete the stale pair and insert a character
    // that belongs to the previous editing session.
    mapper.Translate(WM_CHAR, 0xDE00u, 0);
    EXPECT_EQ(sink.Count(), 0u);
}

// --- mouse -----------------------------------------------------------------------------------------

TEST_F(Win32EventMapping, MotionReportsClientPositionAndDelta)
{
    mapper.Translate(WM_MOUSEMOVE, 0, PointLParam(100, 50));
    ASSERT_EQ(sink.Count(), 1u);
    const auto* first = sink.At<MouseMotionEvent>(0);
    ASSERT_NE(first, nullptr);
    EXPECT_FLOAT_EQ(first->x, 100.0f);
    EXPECT_FLOAT_EQ(first->y, 50.0f);
    EXPECT_FLOAT_EQ(first->deltaX, 0.0f) << "there is nothing to be relative to yet";

    mapper.Translate(WM_MOUSEMOVE, 0, PointLParam(110, 45));
    const auto* second = sink.At<MouseMotionEvent>(1);
    ASSERT_NE(second, nullptr);
    EXPECT_FLOAT_EQ(second->deltaX, 10.0f);
    EXPECT_FLOAT_EQ(second->deltaY, -5.0f);
}

TEST_F(Win32EventMapping, MotionOutsideTheClientAreaIsNegativeNotHuge)
{
    // Client coordinates go negative while a captured drag leaves the window. Reading the words
    // unsigned reports 65526 and a slider jumps to its maximum.
    mapper.Translate(WM_MOUSEMOVE, 0, PointLParam(-10, -20));
    const auto* motion = sink.At<MouseMotionEvent>(0);
    ASSERT_NE(motion, nullptr);
    EXPECT_FLOAT_EQ(motion->x, -10.0f);
    EXPECT_FLOAT_EQ(motion->y, -20.0f);
}

TEST_F(Win32EventMapping, ButtonsUseTheContractsIndices)
{
    mapper.Translate(WM_LBUTTONDOWN, 0, PointLParam(5, 6));
    mapper.Translate(WM_MBUTTONDOWN, 0, PointLParam(5, 6));
    mapper.Translate(WM_RBUTTONDOWN, 0, PointLParam(5, 6));
    ASSERT_EQ(sink.Count(), 3u);
    EXPECT_EQ(sink.At<MouseButtonEvent>(0)->button, 1);
    EXPECT_EQ(sink.At<MouseButtonEvent>(1)->button, 2);
    EXPECT_EQ(sink.At<MouseButtonEvent>(2)->button, 3);
    EXPECT_EQ(mapper.GetHeldButtons(), 0b00000111);
}

TEST_F(Win32EventMapping, XButtonsAreSeparatedByTheHighWord)
{
    mapper.Translate(WM_XBUTTONDOWN, (static_cast<std::uint64_t>(XBUTTON1) << 16), PointLParam(0, 0));
    mapper.Translate(WM_XBUTTONDOWN, (static_cast<std::uint64_t>(XBUTTON2) << 16), PointLParam(0, 0));
    ASSERT_EQ(sink.Count(), 2u);
    EXPECT_EQ(sink.At<MouseButtonEvent>(0)->button, 4);
    EXPECT_EQ(sink.At<MouseButtonEvent>(1)->button, 5);
    EXPECT_EQ(mapper.GetHeldButtons(), 0b00011000);
}

TEST_F(Win32EventMapping, ReleasingClearsOnlyThatButtonsBit)
{
    mapper.Translate(WM_LBUTTONDOWN, 0, PointLParam(0, 0));
    mapper.Translate(WM_RBUTTONDOWN, 0, PointLParam(0, 0));
    mapper.Translate(WM_LBUTTONUP, 0, PointLParam(0, 0));
    EXPECT_EQ(mapper.GetHeldButtons(), 0b00000100);
    EXPECT_TRUE(mapper.WantsPointerCapture());

    mapper.Translate(WM_RBUTTONUP, 0, PointLParam(0, 0));
    EXPECT_EQ(mapper.GetHeldButtons(), 0);
    EXPECT_FALSE(mapper.WantsPointerCapture());
}

TEST_F(Win32EventMapping, DoubleClicksReportTwoClicks)
{
    mapper.Translate(WM_LBUTTONDBLCLK, 0, PointLParam(0, 0));
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_EQ(sink.At<MouseButtonEvent>(0)->clicks, 2);
    EXPECT_TRUE(sink.At<MouseButtonEvent>(0)->pressed);
}

TEST_F(Win32EventMapping, VerticalWheelIsPositiveWhenScrollingAway)
{
    mapper.Translate(WM_MOUSEWHEEL, static_cast<std::uint64_t>(WHEEL_DELTA) << 16, 0);
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_FLOAT_EQ(sink.At<MouseWheelEvent>(0)->y, 1.0f);

    mapper.Translate(WM_MOUSEWHEEL,
                     static_cast<std::uint64_t>(static_cast<std::uint16_t>(-WHEEL_DELTA)) << 16, 0);
    EXPECT_FLOAT_EQ(sink.At<MouseWheelEvent>(1)->y, -1.0f);
}

TEST_F(Win32EventMapping, HorizontalWheelSignMatchesTheRestOfCna)
{
    // Windows reports a tilt to the right as positive; CNA's vocabulary, like SDL's, reports it as
    // negative. Flipping here is what keeps a game's horizontal scrolling identical across
    // backends instead of mirrored on exactly one of them.
    mapper.Translate(WM_MOUSEHWHEEL, static_cast<std::uint64_t>(WHEEL_DELTA) << 16, 0);
    ASSERT_EQ(sink.Count(), 1u);
    EXPECT_FLOAT_EQ(sink.At<MouseWheelEvent>(0)->x, -1.0f);
}

TEST_F(Win32EventMapping, WheelTotalsAccumulateInXnaUnits)
{
    // MouseSnapshot::scrollX/scrollY are XNA units: 120 per notch, accumulated rather than reset.
    mapper.Translate(WM_MOUSEWHEEL, static_cast<std::uint64_t>(WHEEL_DELTA) << 16, 0);
    mapper.Translate(WM_MOUSEWHEEL, static_cast<std::uint64_t>(WHEEL_DELTA) << 16, 0);
    int horizontal = 0;
    int vertical = 0;
    mapper.GetWheelTotals(horizontal, vertical);
    EXPECT_EQ(vertical, 240);
    EXPECT_EQ(horizontal, 0);
}

TEST_F(Win32EventMapping, PointerPositionIsUnknownUntilTheMouseIsSeen)
{
    int x = 0;
    int y = 0;
    EXPECT_FALSE(mapper.TryGetPointerPosition(x, y));

    mapper.Translate(WM_MOUSEMOVE, 0, PointLParam(12, 34));
    ASSERT_TRUE(mapper.TryGetPointerPosition(x, y));
    EXPECT_EQ(x, 12);
    EXPECT_EQ(y, 34);

    // Leaving the client area makes the attribution stale rather than wrong.
    mapper.Translate(WM_MOUSELEAVE, 0, 0);
    EXPECT_FALSE(mapper.TryGetPointerPosition(x, y));
}

// --- detachment ------------------------------------------------------------------------------------

TEST_F(Win32EventMapping, DetachedMapperStopsEmittingButKeepsItsState)
{
    // A window may outlive its platform. It is still a perfectly good native window; it simply
    // has nowhere to send events.
    mapper.Translate(WM_KEYDOWN, 'A', KeyLParam(0x1E));
    sink.Clear();

    mapper.DetachSink();
    mapper.Translate(WM_MOUSEMOVE, 0, PointLParam(9, 9));
    mapper.Translate(WM_LBUTTONDOWN, 0, PointLParam(9, 9));
    EXPECT_EQ(sink.Count(), 0u);

    int x = 0;
    int y = 0;
    EXPECT_TRUE(mapper.TryGetPointerPosition(x, y)) << "pollable state is still maintained";
    EXPECT_EQ(x, 9);
    EXPECT_EQ(mapper.GetHeldButtons(), 0b00000001);
}

// --- unknown messages --------------------------------------------------------------------------------

TEST_F(Win32EventMapping, UnknownMessagesProduceNothingAndAreNotClaimed)
{
    EXPECT_FALSE(mapper.Translate(WM_USER + 42, 0, 0));
    EXPECT_FALSE(mapper.Translate(WM_NULL, 0, 0));
    EXPECT_EQ(sink.Count(), 0u);
}

} // namespace
