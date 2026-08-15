// SPDX-License-Identifier: MS-PL
//
// PLAT-139: SGR-1006 mouse input. Pseudo-terminals make every input and lifecycle assertion run
// in CI instead of skipping merely because the test process itself has redirected descriptors.

#include "../../../src/Terminal/TerminalKeyboard.hpp"
#include "../../../src/Terminal/TerminalMouse.hpp"
#include "../../../src/Terminal/TerminalPlatform.hpp"
#include "PseudoTerminalHarness.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <array>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Terminal;
using CNA::Platform::TestSupport::PseudoTerminal;

void SendMouseBytes(const PseudoTerminal& pty, const std::string& bytes)
{
    ASSERT_EQ(write(pty.Controller(), bytes.data(), bytes.size()),
              static_cast<ssize_t>(bytes.size()));
}

TEST(TerminalMouseTest, SgrReportsDecodeButtonsMotionAndBothWheelAxes)
{
    SgrMouseReport report;
    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<0;3;2M", report));
    EXPECT_EQ(report.kind, SgrMouseReportKind::Button);
    EXPECT_EQ(report.button, 1u);
    EXPECT_TRUE(report.pressed);
    EXPECT_EQ(report.column, 3u);
    EXPECT_EQ(report.row, 2u);

    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<2;3;2m", report));
    EXPECT_EQ(report.button, 3u);
    EXPECT_FALSE(report.pressed);

    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<32;4;5M", report));
    EXPECT_EQ(report.kind, SgrMouseReportKind::Motion);
    EXPECT_EQ(report.column, 4u);
    EXPECT_EQ(report.row, 5u);

    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<64;4;5M", report));
    EXPECT_EQ(report.kind, SgrMouseReportKind::Wheel);
    EXPECT_EQ(report.wheelY, 1);
    EXPECT_EQ(report.wheelX, 0);
    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<65;4;5M", report));
    EXPECT_EQ(report.wheelY, -1);
    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<66;4;5M", report));
    EXPECT_EQ(report.wheelX, -1);
    ASSERT_TRUE(DecodeSgrMouseReport("\x1b[<67;4;5M", report));
    EXPECT_EQ(report.wheelX, 1);
}

TEST(TerminalMouseTest, MalformedReportsAreRejectedWithoutTouchingTheOutput)
{
    SgrMouseReport report;
    report.kind = SgrMouseReportKind::Wheel;
    report.column = 99;

    EXPECT_FALSE(DecodeSgrMouseReport("not mouse input", report));
    EXPECT_EQ(report.kind, SgrMouseReportKind::Wheel);
    EXPECT_EQ(report.column, 99u);
    EXPECT_FALSE(DecodeSgrMouseReport("\x1b[<0;0;2M", report));
    EXPECT_FALSE(DecodeSgrMouseReport("\x1b[<0;2;0M", report));
    EXPECT_FALSE(DecodeSgrMouseReport("\x1b[<0;2;2;3M", report));
    EXPECT_FALSE(DecodeSgrMouseReport("\x1b[<256;2;2M", report));
    EXPECT_FALSE(DecodeSgrMouseReport("\x1b[<64;2;2m", report))
        << "the protocol sends no release for a wheel step";
}

TEST(TerminalMouseTest, SnapshotAndEventsUseNominalCellSizedClientCoordinates)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/false);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalMouse mouse(decoder);

    mouse.Update();
    const std::string prologue = pty.DrainOutput();
    EXPECT_NE(prologue.find("\x1b[?1000h"), std::string::npos);
    EXPECT_NE(prologue.find("\x1b[?1003h"), std::string::npos);
    EXPECT_NE(prologue.find("\x1b[?1006h"), std::string::npos);
    EXPECT_EQ(prologue.find("\x1b[>15u"), std::string::npos)
        << "a mouse-only lease must not turn on Kitty keyboard reporting";

    SendMouseBytes(pty, "\x1b[<0;2;3M\x1b[<32;4;5M\x1b[<64;4;5M\x1b[<0;4;5m");
    mouse.Update();

    const MouseSnapshot& snapshot = mouse.GetSnapshot();
    EXPECT_EQ(snapshot.x, 24) << "column four is zero-based 3 * nominal 8 pixels";
    EXPECT_EQ(snapshot.y, 64) << "row five is zero-based 4 * nominal 16 pixels";
    EXPECT_EQ(snapshot.buttons, 0u);
    EXPECT_EQ(snapshot.scrollX, 0);
    EXPECT_EQ(snapshot.scrollY, 120);

    std::vector<PlatformEvent> events;
    decoder->DrainEvents(events, 42);
    ASSERT_EQ(events.size(), 4u);

    const MouseButtonEvent& press = std::get<MouseButtonEvent>(events[0]);
    EXPECT_EQ(press.window, 42u);
    EXPECT_EQ(press.button, 1u);
    EXPECT_TRUE(press.pressed);
    EXPECT_FLOAT_EQ(press.x, 8.0f);
    EXPECT_FLOAT_EQ(press.y, 32.0f);

    const MouseMotionEvent& motion = std::get<MouseMotionEvent>(events[1]);
    EXPECT_FLOAT_EQ(motion.x, 24.0f);
    EXPECT_FLOAT_EQ(motion.y, 64.0f);
    EXPECT_FLOAT_EQ(motion.deltaX, 16.0f);
    EXPECT_FLOAT_EQ(motion.deltaY, 32.0f);

    const MouseWheelEvent& wheel = std::get<MouseWheelEvent>(events[2]);
    EXPECT_FLOAT_EQ(wheel.x, 0.0f);
    EXPECT_FLOAT_EQ(wheel.y, 1.0f);

    const MouseButtonEvent& release = std::get<MouseButtonEvent>(events[3]);
    EXPECT_FALSE(release.pressed);
    EXPECT_EQ(release.button, 1u);
}

TEST(TerminalMouseTest, KittyKeysAndSgrMouseShareOneOrderedByteStream)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/true);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);

    decoder->PumpAll();
    (void)pty.DrainOutput();
    SendMouseBytes(pty, "\x1b[119;1:1u\x1b[<0;2;2M\x1b[119;1:3u");
    decoder->PumpAll();

    std::vector<PlatformEvent> events;
    decoder->DrainEvents(events, 9);
    ASSERT_EQ(events.size(), 3u);
    ASSERT_TRUE(std::holds_alternative<KeyEvent>(events[0]));
    EXPECT_TRUE(std::get<KeyEvent>(events[0]).pressed);
    ASSERT_TRUE(std::holds_alternative<MouseButtonEvent>(events[1]));
    EXPECT_EQ(std::get<MouseButtonEvent>(events[1]).window, 9u);
    ASSERT_TRUE(std::holds_alternative<KeyEvent>(events[2]));
    EXPECT_FALSE(std::get<KeyEvent>(events[2]).pressed);
}

TEST(TerminalMouseTest, UnsupportedPointerControlsRefuseDeterministically)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    auto sessions = std::make_shared<TerminalSessionController>(
        pty.Device(), pty.Device(), /*kittyKeyboardSupported=*/false);
    auto decoder = std::make_shared<TerminalInputDecoder>(sessions);
    TerminalMouse mouse(decoder);

    EXPECT_THROW(mouse.SetCursorVisible(true), PlatformException);
    try
    {
        mouse.SetCursor(SystemCursor::Pointer);
        FAIL() << "cursor shapes cannot work through SGR mouse reporting";
    }
    catch (const PlatformNotSupportedException& error)
    {
        EXPECT_EQ(error.GetCapability(), PlatformCapability::CursorShapes);
    }
    const std::array<std::uint32_t, 1> pixel{0xFFFFFFFFu};
    try
    {
        mouse.SetCursor(CursorImage{1, 1, 0, 0, pixel});
        FAIL() << "image cursors cannot work through SGR mouse reporting";
    }
    catch (const PlatformNotSupportedException& error)
    {
        EXPECT_EQ(error.GetCapability(), PlatformCapability::CursorShapes);
    }
    EXPECT_THROW(mouse.SetRelativeMode(1, true), PlatformNotSupportedException);
    EXPECT_FALSE(mouse.IsRelativeMode());
    EXPECT_THROW((void)mouse.SetCapture(true), PlatformNotSupportedException);

    float x = 3.0f;
    float y = 4.0f;
    EXPECT_THROW((void)mouse.TryGetGlobalPosition(x, y), PlatformNotSupportedException);
    EXPECT_FLOAT_EQ(x, 3.0f);
    EXPECT_FLOAT_EQ(y, 4.0f);
    EXPECT_THROW((void)mouse.SetGlobalPosition(1.0f, 2.0f), PlatformNotSupportedException);
}

TEST(TerminalMouseTest, PlatformExposesMouseButReportsItsCoordinatesAsInexact)
{
    PseudoTerminal pty;
    ASSERT_TRUE(pty.IsOpen());
    TerminalPlatform platform(pty.Device(), pty.Device());
    const PlatformCapabilities capabilities = platform.GetCapabilities();
    EXPECT_FALSE(capabilities.pixelAccurateMouse);
    ASSERT_NE(platform.GetMouse(), nullptr);

    std::unique_ptr<IPlatformWindow> window = platform.CreateWindow(WindowDescription{});
    platform.GetMouse()->Update();
    (void)pty.DrainOutput();
    SendMouseBytes(pty, "\x1b[<0;2;2M");

    std::vector<PlatformEvent> events;
    platform.PollEvents(events);
    ASSERT_EQ(events.size(), 1u);
    const MouseButtonEvent& button = std::get<MouseButtonEvent>(events.front());
    EXPECT_EQ(button.window, window->GetId());
    EXPECT_TRUE(button.pressed);
}

} // namespace
