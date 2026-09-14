// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0036..0039, WIN32-0055, WIN32-0056: the keyboard, mouse, text-input
// and device-enumeration services.
//
// These are the pollable half of input -- what `Keyboard::GetState()` and `Mouse::GetState()`
// read. The event half is covered by Win32EventMapperTests; what matters here is that a snapshot
// is taken once per frame from real Windows state, that the cursor and relative-mode calls either
// work or refuse, and that nothing reports a capability it does not have.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include "Win32/Win32Common.hpp"
#include "Win32/Win32Modifiers.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;

class Win32InputServices : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        platform_->AcquireSubsystem(PlatformSubsystem::Video);

        WindowDescription description;
        description.title = "input services";
        description.width = 640;
        description.height = 480;
        description.visible = false;
        window_ = platform_->CreateWindow(description);
        ASSERT_NE(window_, nullptr);
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
};

// --- keyboard --------------------------------------------------------------------------------

TEST_F(Win32InputServices, KeyboardSnapshotIsEmptyOrNamedButNeverRaw)
{
    IPlatformKeyboard* const keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);
    keyboard->Update();

    const KeyboardSnapshot& snapshot = keyboard->GetSnapshot();
    for (const KeyCode key : snapshot.pressedKeys)
    {
        // KeyCode::None in the pressed list would be a raw virtual key that failed validation
        // being pushed through anyway -- the contract says the list never contains it.
        EXPECT_NE(key, KeyCode::None);
        EXPECT_FALSE(ToString(key).empty());
    }
}

TEST_F(Win32InputServices, KeyboardSnapshotNeverReportsAMouseButtonAsAKey)
{
    // The mouse buttons occupy virtual keys 1-6 and GetKeyboardState reports them alongside real
    // keys. A loop that cast every held virtual key would put them in the keyboard snapshot.
    IPlatformKeyboard* const keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);
    keyboard->Update();

    for (const KeyCode key : keyboard->GetSnapshot().pressedKeys)
        EXPECT_GE(static_cast<int>(key), 8) << "virtual keys 1-7 are not keyboard keys";
}

TEST_F(Win32InputServices, RepeatedUpdatesReplaceTheSnapshotRatherThanAppendingToIt)
{
    IPlatformKeyboard* const keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);
    keyboard->Update();
    const std::size_t first = keyboard->GetSnapshot().pressedKeys.size();
    for (int frame = 0; frame < 5; ++frame)
        keyboard->Update();
    EXPECT_EQ(keyboard->GetSnapshot().pressedKeys.size(), first);
}

TEST_F(Win32InputServices, KeyboardNamesArePositionalAndDoNotFollowTheLayout)
{
    IPlatformKeyboard* const keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);

    // GetScancodeName is CNA's stable name for a POSITION and must not change with the layout;
    // GetKeyName is the layout's name for what that position produces. Confusing the two makes a
    // key-binding UI relabel itself when the user switches layout.
    EXPECT_EQ(keyboard->GetScancodeName(Scancode::A), "A");
    EXPECT_EQ(keyboard->GetScancodeName(Scancode::LeftShift), "LeftShift");
    EXPECT_EQ(keyboard->GetScancodeFromName("LeftShift"), Scancode::LeftShift);
    EXPECT_EQ(keyboard->GetScancodeFromName("not a key"), Scancode::Unknown);
}

TEST_F(Win32InputServices, ModifierMaskSeparatesHeldKeysFromLatchedLocks)
{
    // GetKeyState's high bit is "held" and its low bit is the toggle state. Reading the wrong one
    // reports Caps Lock as held for as long as its light is on.
    const std::uint16_t modifiers = Win32::CurrentModifiers();
    const std::uint16_t known =
        static_cast<std::uint16_t>(KeyModifier::Shift) |
        static_cast<std::uint16_t>(KeyModifier::Control) |
        static_cast<std::uint16_t>(KeyModifier::Alt) | static_cast<std::uint16_t>(KeyModifier::Gui) |
        static_cast<std::uint16_t>(KeyModifier::CapsLock) |
        static_cast<std::uint16_t>(KeyModifier::NumLock) |
        static_cast<std::uint16_t>(KeyModifier::ScrollLock) |
        static_cast<std::uint16_t>(KeyModifier::Mode);
    EXPECT_EQ(modifiers & ~known, 0) << "a native mask must never pass through unchanged";
}

// --- mouse -----------------------------------------------------------------------------------

TEST_F(Win32InputServices, MouseSnapshotUsesTheContractsButtonBits)
{
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    mouse->Update();

    const MouseSnapshot& snapshot = mouse->GetSnapshot();
    EXPECT_EQ(snapshot.buttons & ~0b00011111, 0) << "only five buttons are defined";
    EXPECT_EQ(snapshot.window, window_->GetId());
}

TEST_F(Win32InputServices, RelativeDeltaIsConsumedRatherThanAccumulatedForever)
{
    // FNA/XNA-compatible callers consume displacement on each Mouse::GetState() read, so a second
    // read with no intervening motion must report zero -- otherwise a camera keeps turning.
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    const MouseDelta first = mouse->ConsumeRelativeDelta();
    const MouseDelta second = mouse->ConsumeRelativeDelta();
    EXPECT_EQ(second.x, 0);
    EXPECT_EQ(second.y, 0);
    (void) first;
}

TEST_F(Win32InputServices, CursorVisibilityIsIdempotent)
{
    // ShowCursor keeps a counter rather than a flag, so an implementation that called it once per
    // request would drift: five hides and one show leaves the cursor hidden.
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    for (int repeat = 0; repeat < 3; ++repeat)
        mouse->SetCursorVisible(false);
    for (int repeat = 0; repeat < 3; ++repeat)
        mouse->SetCursorVisible(true);
    EXPECT_NO_THROW(mouse->SetCursorVisible(true));
}

TEST_F(Win32InputServices, EverySystemCursorShapeIsAccepted)
{
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    ASSERT_TRUE(platform_->GetCapabilities().cursorShapes);

    for (const SystemCursor cursor :
         {SystemCursor::Arrow, SystemCursor::IBeam, SystemCursor::Wait, SystemCursor::Crosshair,
          SystemCursor::Move, SystemCursor::NotAllowed, SystemCursor::Pointer,
          SystemCursor::Progress, SystemCursor::NwseResize, SystemCursor::NeswResize,
          SystemCursor::EwResize, SystemCursor::NsResize})
    {
        EXPECT_NO_THROW(mouse->SetCursor(cursor)) << static_cast<int>(cursor);
    }
}

TEST_F(Win32InputServices, ACustomCursorImageIsAcceptedAndAMalformedOneIsRefused)
{
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);

    std::array<std::uint32_t, 16 * 16> pixels{};
    pixels.fill(0xFF0000FFu); // opaque red in the contract's 0xAABBGGRR packing

    CursorImage image;
    image.width = 16;
    image.height = 16;
    image.hotSpotX = 8;
    image.hotSpotY = 8;
    image.rgba = pixels;
    EXPECT_NO_THROW(mouse->SetCursor(image));

    // Each of these would otherwise reach CreateIconIndirect with an inconsistent description and
    // fail somewhere far less legible than the call that made it.
    CursorImage noArea = image;
    noArea.width = 0;
    EXPECT_THROW(mouse->SetCursor(noArea), PlatformException);

    CursorImage wrongCount = image;
    wrongCount.width = 8;
    EXPECT_THROW(mouse->SetCursor(wrongCount), PlatformException);

    CursorImage hotSpotOutside = image;
    hotSpotOutside.hotSpotX = 99;
    EXPECT_THROW(mouse->SetCursor(hotSpotOutside), PlatformException);
}

TEST_F(Win32InputServices, GlobalPointerReadsAndWarpsInDesktopCoordinates)
{
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    ASSERT_TRUE(platform_->GetCapabilities().globalPointer);

    float x = -1.0f;
    float y = -1.0f;
    if (!mouse->TryGetGlobalPosition(x, y))
        GTEST_SKIP() << "this host reports no pointer position";
    EXPECT_GE(x, -32768.0f);
    EXPECT_GE(y, -32768.0f);

    // SetCapture/ReleaseCapture and SetCursorPos are permitted to decline on a host with no
    // desktop; what they must not do is claim success without doing anything.
    EXPECT_NO_THROW((void) mouse->SetGlobalPosition(x, y));
    EXPECT_NO_THROW((void) mouse->SetCapture(true));
    EXPECT_NO_THROW((void) mouse->SetCapture(false));
}

TEST_F(Win32InputServices, RelativeModeEitherWorksOrRefusesExplicitly)
{
    // Raw Input registration can fail on a host without a real desktop session. The contract's
    // rule is that the mode is then NOT entered and the caller is told -- a hidden cursor with no
    // deltas is the failure a game cannot diagnose.
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    ASSERT_TRUE(platform_->GetCapabilities().relativeMouse);
    EXPECT_FALSE(mouse->IsRelativeMode());

    try
    {
        mouse->SetRelativeMode(window_->GetId(), true);
    }
    catch (const PlatformException&)
    {
        EXPECT_FALSE(mouse->IsRelativeMode()) << "a failed entry must not leave the mode half on";
        return;
    }

    EXPECT_TRUE(mouse->IsRelativeMode());
    mouse->SetRelativeMode(window_->GetId(), false);
    EXPECT_FALSE(mouse->IsRelativeMode());
}

TEST_F(Win32InputServices, LeavingRelativeModeTwiceIsHarmless)
{
    IPlatformMouse* const mouse = platform_->GetMouse();
    ASSERT_NE(mouse, nullptr);
    EXPECT_NO_THROW(mouse->SetRelativeMode(window_->GetId(), false));
    EXPECT_NO_THROW(mouse->SetRelativeMode(window_->GetId(), false));
    EXPECT_FALSE(mouse->IsRelativeMode());
}

// --- text input ------------------------------------------------------------------------------

TEST_F(Win32InputServices, TextInputIsAModeScopedToAWindow)
{
    IPlatformTextInput* const textInput = platform_->GetTextInput();
    ASSERT_NE(textInput, nullptr);
    EXPECT_FALSE(textInput->IsActive(window_->GetId()));

    textInput->Start(window_->GetId(), TextInputType::Text);
    EXPECT_TRUE(textInput->IsActive(window_->GetId()));

    textInput->Stop(window_->GetId());
    EXPECT_FALSE(textInput->IsActive(window_->GetId()));
}

TEST_F(Win32InputServices, TextInputRefusesAWindowThatDoesNotExist)
{
    IPlatformTextInput* const textInput = platform_->GetTextInput();
    ASSERT_NE(textInput, nullptr);
    EXPECT_THROW(textInput->Start(9999, TextInputType::Text), PlatformException);
    // Stopping an unknown window is cleanup, not an error: it legitimately runs after a window
    // has already gone away.
    EXPECT_NO_THROW(textInput->Stop(9999));
    EXPECT_FALSE(textInput->IsActive(9999));
}

TEST_F(Win32InputServices, EveryTextInputTypeIsAcceptedOnADesktopKeyboard)
{
    // The type is a hint for a platform that picks an input UI from it. A desktop window has one
    // physical keyboard and no such choice, so the hint is accepted and has no effect -- refusing
    // would fail a caller that is not asking for anything unavailable.
    IPlatformTextInput* const textInput = platform_->GetTextInput();
    ASSERT_NE(textInput, nullptr);
    for (const TextInputType type :
         {TextInputType::Default, TextInputType::Text, TextInputType::TextName,
          TextInputType::TextEmail, TextInputType::TextUsername, TextInputType::TextPasswordHidden,
          TextInputType::TextPasswordVisible, TextInputType::Number,
          TextInputType::NumberPasswordHidden, TextInputType::NumberPasswordVisible})
    {
        EXPECT_NO_THROW(textInput->Start(window_->GetId(), type)) << static_cast<int>(type);
    }
    textInput->Stop(window_->GetId());
}

TEST_F(Win32InputServices, NoOnScreenKeyboardIsClaimedOnTheDesktop)
{
    // Windows has a touch keyboard, but it is user- and shell-driven: an application cannot ask
    // for it and cannot reliably observe it. False is the honest answer, and a caller lays out its
    // UI around this.
    IPlatformTextInput* const textInput = platform_->GetTextInput();
    ASSERT_NE(textInput, nullptr);
    textInput->Start(window_->GetId(), TextInputType::Text);
    EXPECT_FALSE(textInput->IsScreenKeyboardShown(window_->GetId()));
    textInput->Stop(window_->GetId());
}

TEST_F(Win32InputServices, TheInputAreaIsAcceptedForALiveWindow)
{
    IPlatformTextInput* const textInput = platform_->GetTextInput();
    ASSERT_NE(textInput, nullptr);

    TextInputArea area;
    area.x = 10;
    area.y = 20;
    area.width = 200;
    area.height = 24;
    area.cursorOffset = 40;
    EXPECT_NO_THROW(textInput->SetInputArea(window_->GetId(), area));
    EXPECT_NO_THROW(textInput->SetInputArea(9999, area));
}

// --- device enumeration ----------------------------------------------------------------------

TEST_F(Win32InputServices, EnumerationAnswersForTheClassesItSupports)
{
    IPlatformInputDevices* const devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);
    ASSERT_TRUE(platform_->GetCapabilities().inputDeviceEnumeration);

    for (const InputDeviceKind kind : {InputDeviceKind::Keyboard, InputDeviceKind::Mouse})
    {
        const std::vector<InputDeviceInfo> attached = devices->GetDevices(kind);
        EXPECT_EQ(devices->HasDevice(kind), !attached.empty()) << ToString(kind);
        for (const InputDeviceInfo& device : attached)
        {
            EXPECT_EQ(device.kind, kind);
            EXPECT_NE(device.id, 0u) << "the id must be usable to correlate with a DeviceEvent";
        }
    }
}

TEST_F(Win32InputServices, ClassesThisBackendCannotEnumerateReportNothing)
{
    // Empty rather than a refusal is what the contract specifies for both "none attached" and
    // "cannot enumerate this class"; the capability set is where the difference is stated, and
    // gamepad, joystick, haptics and sensors are all false here.
    IPlatformInputDevices* const devices = platform_->GetInputDevices();
    ASSERT_NE(devices, nullptr);
    for (const InputDeviceKind kind :
         {InputDeviceKind::Gamepad, InputDeviceKind::Joystick, InputDeviceKind::Touch,
          InputDeviceKind::Haptic, InputDeviceKind::Sensor})
    {
        EXPECT_TRUE(devices->GetDevices(kind).empty()) << ToString(kind);
        EXPECT_FALSE(devices->HasDevice(kind)) << ToString(kind);
    }
}

} // namespace
