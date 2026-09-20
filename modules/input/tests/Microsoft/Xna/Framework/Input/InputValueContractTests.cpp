// SPDX-License-Identifier: MS-PL
//
// The documented XNA 4.0 value contracts of the Input and Touch snapshot types: Equals(object)
// on all eight, and ToString() on the four GamePad component structs that override it. Expected
// strings come from the Microsoft implementations
// (xna4-decomp/.../Microsoft.Xna.Framework.Input/GamePad{Buttons,DPad,ThumbSticks,Triggers}.cs).

#include <gtest/gtest.h>

#include <any>
#include <string>

#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Xna::Framework::Input::Touch::TouchLocation;
using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

namespace
{
    /// The documented `Equals(object)` contract for one snapshot type.
    template <typename T>
    void ExpectObjectEquality(const T& value, const T& equalToValue, const T& different)
    {
        ASSERT_TRUE(value.Equals(equalToValue));
        ASSERT_FALSE(value.Equals(different));

        EXPECT_TRUE(value.Equals(std::any(equalToValue)));
        EXPECT_FALSE(value.Equals(std::any(different)));
        EXPECT_FALSE(value.Equals(std::any(7)));
        EXPECT_FALSE(value.Equals(std::any(std::string("state"))));
        EXPECT_FALSE(value.Equals(std::any()));

        EXPECT_EQ(value.GetHashCode(), equalToValue.GetHashCode());
    }
}

// =============================================================================
// Equals(System.Object)
// =============================================================================

TEST(InputValueContractTest, GamePadButtonsObjectEquality)
{
    ExpectObjectEquality(GamePadButtons(Buttons::A), GamePadButtons(Buttons::A),
                         GamePadButtons(Buttons::B));
}

TEST(InputValueContractTest, GamePadDPadObjectEquality)
{
    ExpectObjectEquality(
        GamePadDPad(ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                    ButtonState::Released),
        GamePadDPad(ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                    ButtonState::Released),
        GamePadDPad(ButtonState::Released, ButtonState::Pressed, ButtonState::Released,
                    ButtonState::Released));
}

TEST(InputValueContractTest, GamePadThumbSticksObjectEquality)
{
    ExpectObjectEquality(GamePadThumbSticks(Vector2(1.0f, 0.0f), Vector2(0.0f, 1.0f)),
                         GamePadThumbSticks(Vector2(1.0f, 0.0f), Vector2(0.0f, 1.0f)),
                         GamePadThumbSticks(Vector2(0.0f, 1.0f), Vector2(1.0f, 0.0f)));
}

TEST(InputValueContractTest, GamePadTriggersObjectEquality)
{
    ExpectObjectEquality(GamePadTriggers(0.25f, 0.75f), GamePadTriggers(0.25f, 0.75f),
                         GamePadTriggers(0.75f, 0.25f));
}

TEST(InputValueContractTest, GamePadStateObjectEquality)
{
    ExpectObjectEquality(
        GamePadState(Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f), 0.0f, 0.0f, {Buttons::A}),
        GamePadState(Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f), 0.0f, 0.0f, {Buttons::A}),
        GamePadState(Vector2(1.0f, 0.0f), Vector2(0.0f, 0.0f), 0.0f, 0.0f, {Buttons::B}));
}

TEST(InputValueContractTest, KeyboardStateObjectEquality)
{
    ExpectObjectEquality(KeyboardState({Keys::A, Keys::B}), KeyboardState({Keys::A, Keys::B}),
                         KeyboardState({Keys::A, Keys::C}));
}

TEST(InputValueContractTest, MouseStateObjectEquality)
{
    ExpectObjectEquality(
        MouseState(10, 20, 0, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                   ButtonState::Released, ButtonState::Released),
        MouseState(10, 20, 0, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                   ButtonState::Released, ButtonState::Released),
        MouseState(10, 21, 0, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                   ButtonState::Released, ButtonState::Released));
}

TEST(InputValueContractTest, TouchLocationObjectEquality)
{
    ExpectObjectEquality(
        TouchLocation(1, TouchLocationState::Pressed, Vector2(3.0f, 4.0f)),
        TouchLocation(1, TouchLocationState::Pressed, Vector2(3.0f, 4.0f)),
        TouchLocation(2, TouchLocationState::Pressed, Vector2(3.0f, 4.0f)));
}

TEST(InputValueContractTest, BoxedEqualityRejectsAnotherInputSnapshotType)
{
    const GamePadButtons buttons(Buttons::A);
    const GamePadDPad dpad(ButtonState::Released, ButtonState::Released, ButtonState::Released,
                           ButtonState::Released);
    EXPECT_FALSE(buttons.Equals(std::any(dpad)));
    EXPECT_FALSE(dpad.Equals(std::any(buttons)));

    const GamePadTriggers triggers(0.0f, 0.0f);
    const GamePadThumbSticks sticks;
    EXPECT_FALSE(triggers.Equals(std::any(sticks)));
    EXPECT_FALSE(sticks.Equals(std::any(triggers)));
}

// =============================================================================
// ToString
// =============================================================================

TEST(InputValueContractTest, GamePadButtonsToStringListsPressedNamesInOrder)
{
    EXPECT_EQ(GamePadButtons().ToString(), "{Buttons:None}");
    EXPECT_EQ(GamePadButtons(Buttons::A).ToString(), "{Buttons:A}");
    EXPECT_EQ(GamePadButtons(Buttons::BigButton).ToString(), "{Buttons:BigButton}");

    // Declared order is A, B, X, Y, LeftShoulder, RightShoulder, LeftStick, RightStick, Start,
    // Back, BigButton -- independent of the order the flags are combined in.
    EXPECT_EQ(GamePadButtons(Buttons::Back | Buttons::A).ToString(), "{Buttons:A Back}");
    EXPECT_EQ(GamePadButtons(Buttons::RightStick | Buttons::LeftShoulder).ToString(),
              "{Buttons:LeftShoulder RightStick}");

    const Buttons all = Buttons::A | Buttons::B | Buttons::X | Buttons::Y | Buttons::LeftShoulder |
                        Buttons::RightShoulder | Buttons::LeftStick | Buttons::RightStick |
                        Buttons::Start | Buttons::Back | Buttons::BigButton;
    EXPECT_EQ(GamePadButtons(all).ToString(),
              "{Buttons:A B X Y LeftShoulder RightShoulder LeftStick RightStick Start Back "
              "BigButton}");
}

TEST(InputValueContractTest, GamePadDPadToStringListsPressedDirectionsInOrder)
{
    const ButtonState up = ButtonState::Pressed;
    const ButtonState off = ButtonState::Released;

    EXPECT_EQ(GamePadDPad(off, off, off, off).ToString(), "{DPad:None}");
    EXPECT_EQ(GamePadDPad(up, off, off, off).ToString(), "{DPad:Up}");
    EXPECT_EQ(GamePadDPad(off, off, off, up).ToString(), "{DPad:Right}");
    EXPECT_EQ(GamePadDPad(off, up, up, off).ToString(), "{DPad:Down Left}");
    EXPECT_EQ(GamePadDPad(up, up, up, up).ToString(), "{DPad:Up Down Left Right}");
}

TEST(InputValueContractTest, GamePadThumbSticksToStringNestsTheVectorForm)
{
    const GamePadThumbSticks sticks(Vector2(1.0f, 0.0f), Vector2(0.0f, -1.0f));
    EXPECT_EQ(sticks.ToString(),
              "{Left:" + sticks.getLeftProperty().ToString() + " Right:" +
                  sticks.getRightProperty().ToString() + "}");
    EXPECT_EQ(sticks.ToString(), "{Left:{X:1 Y:0} Right:{X:0 Y:-1}}");
    EXPECT_EQ(GamePadThumbSticks().ToString(), "{Left:{X:0 Y:0} Right:{X:0 Y:0}}");
}

TEST(InputValueContractTest, GamePadTriggersToStringUsesTheFloatValues)
{
    EXPECT_EQ(GamePadTriggers(0.0f, 0.0f).ToString(), "{Left:0 Right:0}");
    EXPECT_EQ(GamePadTriggers(1.0f, 0.5f).ToString(), "{Left:1 Right:0.5}");
}

TEST(InputValueContractTest, EqualValuesShareTheirStringForm)
{
    EXPECT_EQ(GamePadButtons(Buttons::A).ToString(), GamePadButtons(Buttons::A).ToString());
    EXPECT_NE(GamePadButtons(Buttons::A).ToString(), GamePadButtons(Buttons::B).ToString());
    EXPECT_EQ(GamePadTriggers(0.25f, 0.5f).ToString(), GamePadTriggers(0.25f, 0.5f).ToString());
}
