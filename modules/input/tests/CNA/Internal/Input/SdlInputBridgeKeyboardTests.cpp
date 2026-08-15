// SPDX-License-Identifier: MS-PL
//
// PLAT-90: the shared input state machine is driven exclusively by PlatformEvent. Native SDL
// keycode/scancode translation belongs to Sdl3EventMapperTests in modules/platform; this file pins
// the platform-independent state, repeat and scancode-mode semantics plus the public name helpers.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <array>

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using CNA::Internal::Input::SdlInputBridge;
using namespace CNA::Platform;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    struct Keyboard
    {
        static auto GetState() { return InputManager::GetKeyboardState(); }
        static Keys GetKeyFromScancodeEXT(const Keys key)
        {
            return Microsoft::Xna::Framework::Input::Keyboard::GetKeyFromScancodeEXT(key);
        }
    };

    class PlatformInputBridgeKeyboardTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Reset(); }
        void TearDown() override { Reset(); }

        static void Reset()
        {
            SdlInputBridge::ClearScancodeModeForTests();
            InputManager::ResetForTests();
        }
    };

    PlatformEvent keyEvent(const Keys key, const bool pressed = true,
                           const bool repeat = false,
                           const Scancode scancode = Scancode::Unknown)
    {
        return KeyEvent{0, scancode,
                        static_cast<KeyCode>(static_cast<std::uint16_t>(key)),
                        0, pressed, repeat};
    }
}

TEST_F(PlatformInputBridgeKeyboardTest, WindowFocusLostDoesNotClearHeldKeysMatchingFna)
{
    PlatformInputBridge::ProcessEvent(keyEvent(Keys::A));
    ASSERT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));

    PlatformInputBridge::ProcessEvent(WindowEvent{0, WindowEventKind::FocusLost});

    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));
}

TEST_F(PlatformInputBridgeKeyboardTest, WindowLifecycleAndQuitEventsDoNotCorruptKeyboardState)
{
    PlatformInputBridge::ProcessEvent(keyEvent(Keys::A));
    PlatformInputBridge::ProcessEvent(keyEvent(Keys::B));

    for (const WindowEventKind kind : {
             WindowEventKind::Minimized, WindowEventKind::Restored,
             WindowEventKind::Maximized, WindowEventKind::FocusGained,
             WindowEventKind::CloseRequested, WindowEventKind::PixelSizeChanged,
             WindowEventKind::DisplayChanged})
    {
        PlatformInputBridge::ProcessEvent(WindowEvent{0, kind, 1920, 1080});
    }
    PlatformInputBridge::ProcessEvent(QuitEvent{});

    const auto state = Keyboard::GetState();
    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_TRUE(state.IsKeyDown(Keys::B));
    EXPECT_EQ(state.GetPressedKeys().size(), 2u);

    PlatformInputBridge::ProcessEvent(keyEvent(Keys::A, false));
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::A));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::B));
}

TEST_F(PlatformInputBridgeKeyboardTest, KeyCodeContractCoversRepresentativeKeyFamilies)
{
    constexpr Keys keys[] = {
        Keys::A, Keys::D, Keys::D0, Keys::D1, Keys::NumPad1,
        Keys::OemSemicolon, Keys::OemComma, Keys::OemPeriod,
        Keys::LeftControl, Keys::LeftShift, Keys::LeftAlt,
        Keys::F1, Keys::F13, Keys::F24, Keys::Up, Keys::Space,
        Keys::Enter, Keys::Escape, Keys::VolumeUp, Keys::Apps, Keys::Sleep,
    };

    for (const Keys key : keys)
    {
        InputManager::ResetForTests();
        PlatformInputBridge::ProcessEvent(keyEvent(key));
        EXPECT_TRUE(Keyboard::GetState().IsKeyDown(key)) << static_cast<int>(key);
    }
}

TEST_F(PlatformInputBridgeKeyboardTest, ModifierAndLockKeysStayDistinct)
{
    constexpr Keys allKeys[] = {
        Keys::LeftShift, Keys::RightShift, Keys::LeftControl, Keys::RightControl,
        Keys::LeftAlt, Keys::RightAlt, Keys::CapsLock, Keys::NumLock, Keys::Scroll,
    };

    for (const Keys key : allKeys)
        PlatformInputBridge::ProcessEvent(keyEvent(key));

    const auto state = Keyboard::GetState();
    for (const Keys key : allKeys)
        EXPECT_TRUE(state.IsKeyDown(key)) << static_cast<int>(key);
    EXPECT_EQ(state.GetPressedKeys().size(), std::size(allKeys));

    PlatformInputBridge::ProcessEvent(keyEvent(Keys::LeftShift, false));
    const auto afterRelease = Keyboard::GetState();
    EXPECT_FALSE(afterRelease.IsKeyDown(Keys::LeftShift));
    EXPECT_TRUE(afterRelease.IsKeyDown(Keys::RightShift));
    EXPECT_EQ(afterRelease.GetPressedKeys().size(), std::size(allKeys) - 1);
}

TEST_F(PlatformInputBridgeKeyboardTest, NoneKeyCodeIsDropped)
{
    PlatformInputBridge::ProcessEvent(KeyEvent{});
    EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::None));
    EXPECT_TRUE(Keyboard::GetState().GetPressedKeys().empty());
}

TEST_F(PlatformInputBridgeKeyboardTest, ScancodeMapUsedWhenScancodeModeForced)
{
    SdlInputBridge::SetScancodeModeForTests(true);
    const struct Case { Scancode scancode; Keys expected; } cases[] = {
        {Scancode::A, Keys::A}, {Scancode::D1, Keys::D1},
        {Scancode::Keypad1, Keys::NumPad1}, {Scancode::KeypadPlus, Keys::Add},
        {Scancode::LeftShift, Keys::LeftShift}, {Scancode::LeftControl, Keys::LeftControl},
        {Scancode::LeftAlt, Keys::LeftAlt}, {Scancode::F1, Keys::F1},
        {Scancode::F13, Keys::F13}, {Scancode::Semicolon, Keys::OemSemicolon},
        {Scancode::Comma, Keys::OemComma}, {Scancode::Period, Keys::OemPeriod},
        {Scancode::Grave, Keys::OemTilde}, {Scancode::Slash, Keys::OemQuestion},
        {Scancode::Up, Keys::Up}, {Scancode::Space, Keys::Space},
        {Scancode::Enter, Keys::Enter}, {Scancode::Escape, Keys::Escape},
        {Scancode::VolumeUp, Keys::VolumeUp},
    };

    for (const Case& testCase : cases)
    {
        InputManager::ResetForTests();
        PlatformInputBridge::ProcessEvent(
            keyEvent(Keys::Z, true, false, testCase.scancode));
        EXPECT_TRUE(Keyboard::GetState().IsKeyDown(testCase.expected))
            << static_cast<int>(testCase.scancode);
        if (testCase.expected != Keys::Z)
            EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::Z));
    }
}

TEST_F(PlatformInputBridgeKeyboardTest, UnknownAndIsoExtraScancodesAreDropped)
{
    SdlInputBridge::SetScancodeModeForTests(true);
    for (const Scancode scancode : {
             Scancode::Unknown, Scancode::NonUsHash, Scancode::NonUsBackslash})
    {
        InputManager::ResetForTests();
        PlatformInputBridge::ProcessEvent(keyEvent(Keys::A, true, false, scancode));
        EXPECT_TRUE(Keyboard::GetState().GetPressedKeys().empty())
            << static_cast<int>(scancode);
    }
}

TEST_F(PlatformInputBridgeKeyboardTest, KeyRepeatKeepsOneKeyDownUntilRelease)
{
    PlatformInputBridge::ProcessEvent(keyEvent(Keys::A));
    for (int i = 0; i < 5; ++i)
        PlatformInputBridge::ProcessEvent(keyEvent(Keys::A, true, true));

    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));
    EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 1u);

    PlatformInputBridge::ProcessEvent(keyEvent(Keys::A, false));
    EXPECT_TRUE(Keyboard::GetState().GetPressedKeys().empty());
}

TEST_F(PlatformInputBridgeKeyboardTest, ImeAndChatPadKeysRemainPartOfThePublicEnum)
{
    static_assert(static_cast<int>(Keys::Kana) == 21 && static_cast<int>(Keys::Kanji) == 25);
    static_assert(static_cast<int>(Keys::ImeConvert) == 28 && static_cast<int>(Keys::ImeNoConvert) == 29);
    static_assert(static_cast<int>(Keys::ProcessKey) == 229);
    static_assert(static_cast<int>(Keys::ChatPadGreen) == 202 &&
                  static_cast<int>(Keys::ChatPadOrange) == 203);
    SUCCEED();
}

TEST_F(PlatformInputBridgeKeyboardTest, GetKeyFromScancodeEXTIsIdentityInScancodeMode)
{
    SdlInputBridge::SetScancodeModeForTests(true);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::A), Keys::A);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::F13), Keys::F13);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::OemComma), Keys::OemComma);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::None), Keys::None);
}

TEST_F(PlatformInputBridgeKeyboardTest, GetKeyFromScancodeEXTTranslatesInNormalMode)
{
    SdlInputBridge::SetScancodeModeForTests(false);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::F13), Keys::F13);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Kana), Keys::None);
}
