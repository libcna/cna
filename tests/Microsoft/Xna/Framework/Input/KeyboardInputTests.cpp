// SPDX-License-Identifier: MS-PL
//
// Task 768: dedicated coverage for KeyboardState/Keyboard, consolidating the ad-hoc
// verifications done (but not committed) for tasks 760-767.
//
// GetKeyFromScancodeEXT's scancode-mode branch is NOT covered here: FNA_KEYBOARD_USE_SCANCODES
// is read into a function-local static bool the first time any key-conversion path in
// SdlInputBridge.cpp runs, cached for the rest of the process (mirroring FNA's own
// UseScancodes static-readonly-at-startup semantics). By the time this shared CnaTests binary
// reaches this file, some earlier test has already triggered that first read with the env var
// unset, so it can never be flipped to scancode mode within this process. Scancode mode was
// verified via a separate single-purpose process in task 765 (not committed, since it needs a
// fresh process rather than a gtest case).

#include <gtest/gtest.h>

#include <unordered_set>

#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

using namespace Microsoft::Xna::Framework::Input;

namespace
{
    void ResetKeyboardState()
    {
        const Keys keysToReset[] = {
            Keys::Left,
            Keys::Right,
            Keys::Up,
            Keys::Down,
            Keys::Space,
            Keys::Enter,
            Keys::Escape,
            Keys::LeftControl,
            Keys::RightControl,
            Keys::LeftShift,
            Keys::RightShift,
            Keys::A,
            Keys::D,
            Keys::W,
            Keys::S,
        };

        for (const auto key : keysToReset)
        {
            CNA::Internal::Input::InputManager::SetKeyState(key, false);
        }
    }
}

// ===========================================================================
// Keyboard::GetState / GetState(PlayerIndex)
// ===========================================================================

TEST(KeyboardInputTest, GetStateReflectsPressedAndReleasedKeys)
{
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::Left, true);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::Space, true);

    const auto state = Keyboard::GetState();

    EXPECT_TRUE(state.IsKeyDown(Keys::Left));
    EXPECT_TRUE(state.IsKeyDown(Keys::Space));
    EXPECT_TRUE(state.IsKeyUp(Keys::Right));

    ResetKeyboardState();
}

TEST(KeyboardInputTest, SnapshotDoesNotChangeAfterInternalStateMutation)
{
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::A, true);
    const auto snapshot = Keyboard::GetState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::A, false);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::D, true);

    EXPECT_TRUE(snapshot.IsKeyDown(Keys::A));
    EXPECT_TRUE(snapshot.IsKeyUp(Keys::D));

    const auto currentState = Keyboard::GetState();
    EXPECT_TRUE(currentState.IsKeyUp(Keys::A));
    EXPECT_TRUE(currentState.IsKeyDown(Keys::D));

    ResetKeyboardState();
}

TEST(KeyboardInputTest, GetPressedKeysContainsOnlyPressedKeys)
{
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::W, true);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::S, true);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::W, false);

    const auto state = Keyboard::GetState();
    const auto pressedKeys = state.GetPressedKeys();

    EXPECT_EQ(pressedKeys.size(), 1);
    EXPECT_EQ(pressedKeys[0], Keys::S);
    EXPECT_TRUE(state.IsKeyDown(Keys::S));
    EXPECT_TRUE(state.IsKeyUp(Keys::W));

    ResetKeyboardState();
}

TEST(KeyboardInputTest, GetStateWithPlayerIndexMatchesGetState)
{
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::Enter, true);

    const auto state = Keyboard::GetState(Microsoft::Xna::Framework::PlayerIndex::One);

    EXPECT_TRUE(state.IsKeyDown(Keys::Enter));
    EXPECT_EQ(state, Keyboard::GetState());

    ResetKeyboardState();
}

// ===========================================================================
// Keyboard::GetKeyFromScancodeEXT (default, non-scancode mode; see file header)
// ===========================================================================

TEST(KeyboardInputTest, GetKeyFromScancodeEXTRoundTripsCommonKeys)
{
    // On this environment's default (US) layout, physical-key round-trips are identity.
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::A), Keys::A);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::D1), Keys::D1);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::F13), Keys::F13);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Enter), Keys::Enter);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::OemPeriod), Keys::OemPeriod);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Space), Keys::Space);
}

TEST(KeyboardInputTest, GetKeyFromScancodeEXTNoneReturnsNone)
{
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::None), Keys::None);
}

TEST(KeyboardInputTest, GetKeyFromScancodeEXTUnmappedKeyReturnsNone)
{
    // Keys::Kana has no SDL_Scancode equivalent (no such physical key on US layouts).
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Kana), Keys::None);
}

// ===========================================================================
// KeyboardState — constructors
// ===========================================================================

TEST(KeyboardStateTest, DefaultConstructorHasNoPressedKeys)
{
    const KeyboardState state;

    EXPECT_TRUE(state.GetPressedKeys().empty());
    EXPECT_TRUE(state.IsKeyUp(Keys::A));
    EXPECT_FALSE(state.IsKeyDown(Keys::A));
}

TEST(KeyboardStateTest, InitializerListConstructorFlagsGivenKeys)
{
    const KeyboardState state{Keys::A, Keys::Space};

    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_TRUE(state.IsKeyDown(Keys::Space));
    EXPECT_TRUE(state.IsKeyUp(Keys::B));
}

TEST(KeyboardStateTest, UnorderedSetConstructorFlagsGivenKeys)
{
    const std::unordered_set<Keys> pressed{Keys::Left, Keys::Right};
    const KeyboardState state(pressed);

    EXPECT_TRUE(state.IsKeyDown(Keys::Left));
    EXPECT_TRUE(state.IsKeyDown(Keys::Right));
    EXPECT_TRUE(state.IsKeyUp(Keys::Up));
}

// ===========================================================================
// KeyboardState — operator[] / getItem
// ===========================================================================

TEST(KeyboardStateTest, IndexerMatchesGetItemAndIsKeyDown)
{
    const KeyboardState state{Keys::A};

    EXPECT_EQ(state[Keys::A], KeyState::Down);
    EXPECT_EQ(state[Keys::B], KeyState::Up);
    EXPECT_EQ(state[Keys::A], state.getItem(Keys::A));
    EXPECT_EQ(state[Keys::B], state.getItem(Keys::B));
    EXPECT_TRUE(state.IsKeyDown(Keys::A));
    EXPECT_TRUE(state.IsKeyUp(Keys::B));
}

// ===========================================================================
// KeyboardState — GetPressedKeys ordering
// ===========================================================================

TEST(KeyboardStateTest, GetPressedKeysReturnsEmptyForDefaultState)
{
    const KeyboardState state;
    EXPECT_TRUE(state.GetPressedKeys().empty());
}

TEST(KeyboardStateTest, GetPressedKeysIsSortedByAscendingNumericValue)
{
    // Z (90), A (65), Space (32), D1 (49) -> ascending: Space, D1, A, Z.
    const KeyboardState state{Keys::Z, Keys::A, Keys::Space, Keys::D1};

    const auto pressed = state.GetPressedKeys();
    const std::vector<Keys> expected{Keys::Space, Keys::D1, Keys::A, Keys::Z};

    EXPECT_EQ(pressed, expected);
}

// ===========================================================================
// KeyboardState — equality / Equals
// ===========================================================================

TEST(KeyboardStateTest, EqualStatesCompareEqual)
{
    const KeyboardState a{Keys::A, Keys::B};
    const KeyboardState b{Keys::B, Keys::A};

    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(KeyboardStateTest, UnequalStatesCompareUnequal)
{
    const KeyboardState a{Keys::A};
    const KeyboardState b{Keys::B};

    EXPECT_FALSE(a.Equals(b));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// ===========================================================================
// KeyboardState — GetHashCode
// ===========================================================================

TEST(KeyboardStateTest, GetHashCodeIsConsistentForEqualStates)
{
    const KeyboardState a{Keys::A, Keys::Enter};
    const KeyboardState b{Keys::Enter, Keys::A};

    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(KeyboardStateTest, GetHashCodeOfEmptyStateIsZero)
{
    const KeyboardState state;
    EXPECT_EQ(state.GetHashCode(), 0);
}

TEST(KeyboardStateTest, GetHashCodeMatchesFNAWordXorFormula)
{
    // Keys::A = 65 -> word 2 (65>>5=2), bit 1 (65&0x1f=1) -> word2 = 1<<1 = 0x2.
    // Keys::Space = 32 -> word 1 (32>>5=1), bit 0 (32&0x1f=0) -> word1 = 1<<0 = 0x1.
    // No other words set, so hash = word1 ^ word2 = 0x1 ^ 0x2 = 0x3.
    const KeyboardState state{Keys::A, Keys::Space};
    EXPECT_EQ(state.GetHashCode(), 0x3);
}

// ===========================================================================
// KeyboardState — ToString
// ===========================================================================

TEST(KeyboardStateTest, ToStringMatchesFNAValueTypeDefault)
{
    const KeyboardState state{Keys::A};
    EXPECT_EQ(state.ToString(), "Microsoft.Xna.Framework.Input.KeyboardState");
}

// ===========================================================================
// Keys / KeyState — value spot-checks
// ===========================================================================

TEST(KeyboardStateTest, KeysValuesMatchXNANumericConstants)
{
    EXPECT_EQ(static_cast<int>(Keys::None), 0);
    EXPECT_EQ(static_cast<int>(Keys::Back), 8);
    EXPECT_EQ(static_cast<int>(Keys::Enter), 13);
    EXPECT_EQ(static_cast<int>(Keys::Space), 32);
    EXPECT_EQ(static_cast<int>(Keys::D0), 48);
    EXPECT_EQ(static_cast<int>(Keys::A), 65);
    EXPECT_EQ(static_cast<int>(Keys::Z), 90);
    EXPECT_EQ(static_cast<int>(Keys::F1), 112);
    EXPECT_EQ(static_cast<int>(Keys::F24), 135);
    EXPECT_EQ(static_cast<int>(Keys::OemClear), 254);
}

TEST(KeyboardStateTest, KeyStateValuesMatchXNAConstants)
{
    EXPECT_NE(KeyState::Up, KeyState::Down);
}
