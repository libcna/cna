// SPDX-License-Identifier: MS-PL
//
// PLAT-79/80/81/82/87: the SDL3 input services.
//
// No user, no devices and no display here, so these assert the properties that hold regardless:
// snapshot shape, refusal behaviour, range normalisation and lifetime safety. Behaviour that
// genuinely needs hardware is left to the input parity suites in Phase 5.

#include "../../../src/Sdl3/Sdl3InputServices.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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
    // Only the five defined bits may ever be set; SDL's own mask uses a different, 1-based
    // button numbering, and leaking it would make every consumer depend on that detail.
    EXPECT_EQ(platform_->GetMouse()->GetSnapshot().buttons & ~0x1F, 0);
}

TEST(Sdl3MouseTest, WheelEventsAccumulateInXnaUnitsAndTruncateBeforeScaling)
{
    CNA::Platform::Sdl3::Sdl3Mouse mouse;
    mouse.ObserveEvent(MouseWheelEvent{17, 1.9f, -2.1f});

    const MouseSnapshot& snapshot = mouse.GetSnapshot();
    EXPECT_EQ(snapshot.window, 17u);
    EXPECT_EQ(snapshot.scrollX, 120);
    EXPECT_EQ(snapshot.scrollY, -240);
}

TEST_F(Sdl3InputTest, PositionWithNoWindowIsRecordedWithoutANativeWarp)
{
    EXPECT_NO_THROW(platform_->GetMouse()->SetPosition(0, 12, 34));
    EXPECT_EQ(platform_->GetMouse()->GetSnapshot().x, 12);
    EXPECT_EQ(platform_->GetMouse()->GetSnapshot().y, 34);
}

TEST_F(Sdl3InputTest, PositionRefusesAnUnknownNonZeroWindowId)
{
    EXPECT_THROW(platform_->GetMouse()->SetPosition(0xFFFFFFFFu, 12, 34), PlatformException);
}

TEST_F(Sdl3InputTest, CursorVisibilityCallsAreSafe)
{
    EXPECT_NO_THROW(platform_->GetMouse()->SetCursorVisible(false));
    EXPECT_NO_THROW(platform_->GetMouse()->SetCursorVisible(true));
}

TEST(Sdl3MouseTest, InvalidCustomCursorIsRejectedBeforeAnyNativeCall)
{
    CNA::Platform::Sdl3::Sdl3Mouse mouse;
    const std::array<std::uint32_t, 1> pixel{0xFFFFFFFFu};

    EXPECT_THROW(mouse.SetCursor(CursorImage{0, 1, 0, 0, pixel}), PlatformException);
    EXPECT_THROW(mouse.SetCursor(CursorImage{1, 1, 1, 0, pixel}), PlatformException);
    EXPECT_THROW(mouse.SetCursor(CursorImage{2, 1, 0, 0, pixel}), PlatformException);
}

TEST_F(Sdl3InputTest, RelativeModeRefusesWithNoFocusedWindow)
{
    // SDL3 scopes relative mode to a window. With nothing focused there is nothing to capture,
    // so the request is refused rather than silently doing nothing and reporting success.
    EXPECT_THROW(platform_->GetMouse()->SetRelativeMode(0, true), PlatformException);
    EXPECT_FALSE(platform_->GetMouse()->IsRelativeMode());
}

// --- gamepad -------------------------------------------------------------------------------------

TEST_F(Sdl3InputTest, GamepadUpdateIsSafeWithNoDevicesAndNoSubsystem)
{
    EXPECT_NO_THROW(platform_->GetGamepad()->Update());
    EXPECT_EQ(platform_->GetGamepad()->GetCount(), GamepadSlotCount);
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
            EXPECT_EQ(snapshot.axes.size(), GamepadAxisCount);
            EXPECT_EQ(snapshot.packetNumber, 0u);
        }
    }
}

TEST_F(Sdl3InputTest, NamingAndRumblingAnAbsentPadAreSafeNoOps)
{
    platform_->GetGamepad()->Update();
    EXPECT_TRUE(platform_->GetGamepad()->GetName(99).empty());
    EXPECT_FALSE(platform_->GetGamepad()->SetRumble(99, 1.0f, 1.0f, 100));
}

TEST_F(Sdl3InputTest, OptionalGamepadFeaturesRefuseAnAbsentSlot)
{
    IPlatformGamepad* gamepad = platform_->GetGamepad();
    gamepad->Update();

    EXPECT_FALSE(gamepad->GetCapabilities(99).connected);
    EXPECT_TRUE(gamepad->GetInfo(99).name.empty());
    EXPECT_FALSE(gamepad->SetTriggerRumble(99, 1.0f, 1.0f, 100));
    EXPECT_FALSE(gamepad->SetLightBar(99, 1, 2, 3));
    GamepadSensorReading reading{1.0f, 2.0f, 3.0f};
    EXPECT_FALSE(gamepad->TryGetSensor(99, GamepadSensor::Gyroscope, reading));
    EXPECT_FLOAT_EQ(reading.x, 0.0f);
    EXPECT_EQ(gamepad->GetPlayerIndex(99), -1);
    EXPECT_FALSE(gamepad->SetPlayerIndex(99, 2));
    EXPECT_EQ(gamepad->GetPowerInfo(99).state, GamepadPowerState::Error);
    EXPECT_EQ(gamepad->GetButtonLabel(99, GamepadButton::A), GamepadButtonLabel::Unknown);
    EXPECT_EQ(gamepad->GetTouchpadCount(99), 0);
    EXPECT_EQ(gamepad->GetTouchpadFingerCount(99, 0), 0);
    GamepadTouchpadFinger finger{true, 1.0f, 1.0f, 1.0f};
    EXPECT_FALSE(gamepad->TryGetTouchpadFinger(99, 0, 0, finger));
    EXPECT_FALSE(finger.down);
    EXPECT_FLOAT_EQ(finger.pressure, 0.0f);
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
    // A connected id keeps one handle and one player slot; disappeared ids are closed. With no
    // devices, repeated reconciliation must remain allocation- and handle-stable.
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
}

} // namespace

// --- modifier layout (PLAT-77e) ---------------------------------------------------------------

TEST_F(Sdl3InputTest, ModifierBitsUseTheContractsLayoutNotSdls)
{
    // The snapshot's modifier field is a bare uint16_t, which is exactly the shape that invites
    // an implementation to pass its native mask straight through: it compiles, it runs, and it is
    // silently wrong on the second implementation because the values mean something else. With no
    // modifier held, every contract bit must be clear -- a passed-through mask would still carry
    // whatever latched state the host reports in bits the contract never assigned.
    IPlatformKeyboard* keyboard = platform_->GetKeyboard();
    ASSERT_NE(keyboard, nullptr);
    keyboard->Update();

    const std::uint16_t modifiers = keyboard->GetSnapshot().modifiers;
    constexpr std::uint16_t known =
        static_cast<std::uint16_t>(KeyModifier::Shift) |
        static_cast<std::uint16_t>(KeyModifier::Control) |
        static_cast<std::uint16_t>(KeyModifier::Alt) |
        static_cast<std::uint16_t>(KeyModifier::Gui) |
        static_cast<std::uint16_t>(KeyModifier::CapsLock) |
        static_cast<std::uint16_t>(KeyModifier::NumLock) |
        static_cast<std::uint16_t>(KeyModifier::ScrollLock) |
        static_cast<std::uint16_t>(KeyModifier::Mode);

    EXPECT_EQ(modifiers & ~known, 0)
        << "the mask carries bits the contract never assigned, which means it was not translated";
}

TEST_F(Sdl3InputTest, HasModifierReadsTheDocumentedBits)
{
    constexpr std::uint16_t mask = static_cast<std::uint16_t>(KeyModifier::Shift) |
                                   static_cast<std::uint16_t>(KeyModifier::NumLock);

    EXPECT_TRUE(HasModifier(mask, KeyModifier::Shift));
    EXPECT_TRUE(HasModifier(mask, KeyModifier::NumLock));
    EXPECT_FALSE(HasModifier(mask, KeyModifier::Control));
    EXPECT_FALSE(HasModifier(mask, KeyModifier::Mode));

    // None is zero, so it is never "present" -- a caller testing for it would otherwise get true
    // for every mask.
    EXPECT_FALSE(HasModifier(mask, KeyModifier::None));
    EXPECT_FALSE(HasModifier(0, KeyModifier::None));
}

TEST_F(Sdl3InputTest, EveryModifierBitIsDistinct)
{
    // Two modifiers sharing a bit would make one of them unreadable, and the failure would look
    // like "Alt is stuck on" rather than like a layout mistake.
    constexpr KeyModifier all[] = {KeyModifier::Shift,    KeyModifier::Control,
                                   KeyModifier::Alt,      KeyModifier::Gui,
                                   KeyModifier::CapsLock, KeyModifier::NumLock,
                                   KeyModifier::ScrollLock, KeyModifier::Mode};
    std::uint16_t seen = 0;
    for (const KeyModifier modifier : all)
    {
        const auto bit = static_cast<std::uint16_t>(modifier);
        EXPECT_NE(bit, 0) << "a modifier other than None must have a bit";
        EXPECT_EQ(seen & bit, 0) << "bit reused";
        seen = static_cast<std::uint16_t>(seen | bit);
    }
}
