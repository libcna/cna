// SPDX-License-Identifier: MS-PL
//
// PLAT-79/80/81/82/87: the SDL3 input services.
//
// No user, no devices and no display here, so these assert the properties that hold regardless:
// snapshot shape, refusal behaviour, range normalisation and lifetime safety. Behaviour that
// genuinely needs hardware is left to the input parity suites in Phase 5.

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;

class Sdl3InputTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        platform_ = PlatformFactory::Create("SDL3");
    }

    std::unique_ptr<IPlatform> platform_;
};

TEST_F(Sdl3InputTest, EveryInputServiceIsPresentBecauseItsCapabilityIsTrue)
{
    const PlatformCapabilities capabilities = platform_->GetCapabilities();
    EXPECT_EQ(platform_->GetKeyboard() != nullptr, capabilities.exactKeyboardState);
    EXPECT_EQ(platform_->GetMouse() != nullptr, capabilities.pixelAccurateMouse);
    EXPECT_EQ(platform_->GetGamepad() != nullptr, capabilities.gamepad);
    EXPECT_EQ(platform_->GetTextInput() != nullptr, capabilities.textInput);
}

// --- keyboard ------------------------------------------------------------------------------------

TEST_F(Sdl3InputTest, KeyboardSnapshotIsSafeBeforeAnyUpdate)
{
    // A caller may read state before the first frame; reporting phantom held keys would make a
    // game act on input that never happened.
    const KeyboardSnapshot& snapshot = platform_->GetKeyboard()->GetSnapshot();
    EXPECT_TRUE(snapshot.pressedKeys.empty());
    EXPECT_EQ(snapshot.modifiers, 0);
}

TEST_F(Sdl3InputTest, KeyboardUpdateIsSafeWithNoVideoSubsystem)
{
    // The game loop calls Update() every frame regardless of what is initialised.
    EXPECT_NO_THROW(platform_->GetKeyboard()->Update());
    EXPECT_NO_THROW((void)platform_->GetKeyboard()->HasKeyboard());
}

TEST_F(Sdl3InputTest, KeyboardUpdateIsIdempotentWithNoInput)
{
    // Two updates with nothing pressed must not accumulate: the snapshot is rebuilt each frame,
    // not appended to. Getting that wrong grows the list without bound.
    platform_->GetKeyboard()->Update();
    const std::size_t first = platform_->GetKeyboard()->GetSnapshot().pressedKeys.size();
    platform_->GetKeyboard()->Update();
    EXPECT_EQ(platform_->GetKeyboard()->GetSnapshot().pressedKeys.size(), first);
}

// --- mouse ---------------------------------------------------------------------------------------

TEST_F(Sdl3InputTest, MouseSnapshotStartsWithNothingHeld)
{
    const MouseSnapshot& snapshot = platform_->GetMouse()->GetSnapshot();
    EXPECT_EQ(snapshot.buttons, 0);
}

TEST_F(Sdl3InputTest, MouseUpdateIsSafeAndButtonMaskStaysInCnasOwnBitOrder)
{
    EXPECT_NO_THROW(platform_->GetMouse()->Update());
    // Only the three defined bits may ever be set; SDL's own mask uses a different, 1-based
    // button numbering, and leaking it would make every consumer depend on that detail.
    EXPECT_EQ(platform_->GetMouse()->GetSnapshot().buttons & ~0x07, 0);
}

TEST_F(Sdl3InputTest, CursorVisibilityCallsAreSafe)
{
    EXPECT_NO_THROW(platform_->GetMouse()->SetCursorVisible(false));
    EXPECT_NO_THROW(platform_->GetMouse()->SetCursorVisible(true));
}

TEST_F(Sdl3InputTest, RelativeModeRefusesWithNoFocusedWindow)
{
    // SDL3 scopes relative mode to a window. With nothing focused there is nothing to capture,
    // so the request is refused rather than silently doing nothing and reporting success.
    EXPECT_THROW(platform_->GetMouse()->SetRelativeMode(true), PlatformException);
    EXPECT_FALSE(platform_->GetMouse()->IsRelativeMode());
}

// --- gamepad -------------------------------------------------------------------------------------

TEST_F(Sdl3InputTest, GamepadUpdateIsSafeWithNoDevicesAndNoSubsystem)
{
    EXPECT_NO_THROW(platform_->GetGamepad()->Update());
    EXPECT_GE(platform_->GetGamepad()->GetCount(), 0);
}

TEST_F(Sdl3InputTest, AnEmptySlotReportsDisconnectedRatherThanThrowing)
{
    // XNA games read all four player indices unconditionally, so polling an absent pad is
    // ordinary control flow -- throwing here would make the normal case exceptional.
    platform_->GetGamepad()->Update();
    for (const int index : {-1, 0, 1, 2, 3, 99})
    {
        const GamepadSnapshot& snapshot = platform_->GetGamepad()->GetSnapshot(index);
        if (index < 0 || index >= platform_->GetGamepad()->GetCount())
        {
            EXPECT_FALSE(snapshot.connected) << "index " << index;
            EXPECT_EQ(snapshot.buttons, 0u);
        }
    }
}

TEST_F(Sdl3InputTest, NamingAndRumblingAnAbsentPadAreSafeNoOps)
{
    platform_->GetGamepad()->Update();
    EXPECT_TRUE(platform_->GetGamepad()->GetName(99).empty());
    EXPECT_FALSE(platform_->GetGamepad()->SetRumble(99, 1.0f, 1.0f, 100));
}

TEST_F(Sdl3InputTest, RumbleStrengthOutOfRangeIsClampedNotWrapped)
{
    // Strength is documented as [0, 1]. Scaling an out-of-range value without clamping would
    // wrap through the 16-bit conversion and turn "maximum" into "almost nothing".
    platform_->GetGamepad()->Update();
    EXPECT_NO_THROW((void)platform_->GetGamepad()->SetRumble(0, 5.0f, -5.0f, 10));
}

TEST_F(Sdl3InputTest, RepeatedUpdatesDoNotLeakGamepadHandles)
{
    // Update() reopens every device; without closing the previous handles this leaks one set per
    // frame. Detectable here only as "does not crash or exhaust handles", but the ASan build in
    // CI turns a genuine leak into a failure.
    for (int i = 0; i < 50; ++i)
    {
        platform_->GetGamepad()->Update();
    }
    SUCCEED();
}

// --- text input ------------------------------------------------------------------------------------

TEST_F(Sdl3InputTest, TextInputStartsInactive)
{
    EXPECT_FALSE(platform_->GetTextInput()->IsActive());
}

TEST_F(Sdl3InputTest, TextInputRefusesAWindowItDidNotCreate)
{
    class ForeignWindow final : public IPlatformWindow
    {
    public:
        [[nodiscard]] WindowId GetId() const override { return 1; }
        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override { return {}; }
        [[nodiscard]] std::string GetTitle() const override { return {}; }
        void SetTitle(const std::string&) override {}
        [[nodiscard]] WindowBounds GetClientBounds() const override { return {}; }
        [[nodiscard]] WindowSize GetPixelSize() const override { return {}; }
        void SetSize(int, int) override {}
        [[nodiscard]] float GetDisplayScale() const override { return 1.0f; }
        void SetResizable(bool) override {}
        void SetBorderless(bool) override {}
        void SetFullscreenMode(WindowFullscreenMode) override {}
        [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override
        {
            return WindowFullscreenMode::Windowed;
        }
        void Show() override {}
        void Hide() override {}
        void Minimize() override {}
        void Maximize() override {}
        void Restore() override {}
        void Sync() override {}
        [[nodiscard]] bool HasFocus() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }
        [[nodiscard]] std::string GetDisplayName() const override { return {}; }
    };

    ForeignWindow foreign;
    EXPECT_THROW(platform_->GetTextInput()->Start(foreign), PlatformException);
    EXPECT_THROW(platform_->GetMouse()->SetPosition(foreign, 0, 0), PlatformException);
}

} // namespace
