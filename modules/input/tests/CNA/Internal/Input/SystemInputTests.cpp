// SPDX-License-Identifier: MS-PL
// The system UI (the Guide) owning input: the game reads nothing meanwhile, and what the system used
// stays hidden from the game until it is released.
#include <gtest/gtest.h>

#include "CNA/Internal/Input/SystemInput.hpp"
#include "CNA/Platform/CannedGamepad.hpp"
#include "CNA/Platform/CannedKeyboard.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Xna::Framework::PlayerIndex;
namespace SysIn = CNA::Internal::Input;

namespace
{
    struct Released
    {
        ~Released() { SysIn::setSystemOwnsInput(false); }
    };
}

TEST(SystemInputTest, TheGameReadsNoKeysWhileTheSystemOwnsThemAndNotTheOneThatClosedIt)
{
    CNA::Platform::Testing::CannedKeyboardPlatform platform;
    CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    Released released;
    auto press = [&](std::initializer_list<CNA::Platform::KeyCode> keys) {
        platform.Canned().SetPending(keys);
        platform.Canned().Update();
    };
    press({CNA::Platform::KeyCode::Left});
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::Left));

    SysIn::setSystemOwnsInput(true);
    press({CNA::Platform::KeyCode::Escape});
    EXPECT_TRUE(SysIn::systemOwnsInput());
    EXPECT_TRUE(Keyboard::GetState().GetPressedKeys().empty());
    EXPECT_TRUE(SysIn::systemKeyboardState().IsKeyDown(Keys::Escape));

    // Escape closed the Guide and is still held: the game does not see it, but sees a new key.
    SysIn::setSystemOwnsInput(false);
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::Escape));
    press({CNA::Platform::KeyCode::Escape, CNA::Platform::KeyCode::Space});
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::Escape));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::Space));
    // Released, then pressed again: the game's own press.
    press({});
    EXPECT_TRUE(Keyboard::GetState().GetPressedKeys().empty());
    press({CNA::Platform::KeyCode::Escape});
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::Escape));
}

TEST(SystemInputTest, APadStaysConnectedAndNeutralWhileTheSystemOwnsItAndHidesTheButtonItUsed)
{
    CNA::Platform::Testing::CannedGamepadPlatform platform;
    CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    Released released;
    auto pad = [&](std::uint32_t buttons, float leftX) {
        CNA::Platform::GamepadSnapshot snapshot;
        snapshot.connected = true;
        snapshot.buttons = buttons;
        snapshot.axes[static_cast<std::size_t>(CNA::Platform::GamepadAxis::LeftThumbstickX)] = leftX;
        platform.Canned().SetPendingSnapshot(0, snapshot);
        platform.Canned().Update();
    };
    const auto b = static_cast<std::uint32_t>(Buttons::B), a = static_cast<std::uint32_t>(Buttons::A);
    SysIn::setSystemOwnsInput(true);
    pad(b, 0.9f);
    auto state = GamePad::GetState(PlayerIndex::One);
    EXPECT_TRUE(state.getIsConnectedProperty());
    EXPECT_TRUE(state.IsButtonUp(Buttons::B));
    EXPECT_FLOAT_EQ(0.0f, state.getThumbSticksProperty().getLeftProperty().X);
    EXPECT_TRUE(SysIn::systemGamePadState(PlayerIndex::One).IsButtonDown(Buttons::B));

    // B closed the Guide and is still held; the stick is the game's again at once.
    SysIn::setSystemOwnsInput(false);
    pad(b | a, 0.9f);
    state = GamePad::GetState(PlayerIndex::One);
    EXPECT_TRUE(state.IsButtonUp(Buttons::B));
    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
    EXPECT_GT(state.getThumbSticksProperty().getLeftProperty().X, 0.5f);
    pad(0, 0.0f);
    (void)GamePad::GetState(PlayerIndex::One);
    pad(b, 0.0f);
    EXPECT_TRUE(GamePad::GetState(PlayerIndex::One).IsButtonDown(Buttons::B));
}
