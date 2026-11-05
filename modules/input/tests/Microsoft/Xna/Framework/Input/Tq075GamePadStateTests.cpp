// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

TEST(Tq075GamePadStateTest, MixedPhysicalDPadAndVirtualQueriesRequireEveryBit)
{
    // Refresh IsButtonDown IL_0000..0016 / IL_0017..02bd builds the native + analog mask;
    // IL_02bd..02c3 compares (requested & mask) == requested. Up IL_0000..000a negates it.
    const GamePadState state(Vector2(-1, 1), Vector2(1, -1), 1, 0,
                             {Buttons::A, Buttons::DPadLeft});
    const Buttons down = Buttons::A | Buttons::DPadLeft | Buttons::LeftTrigger |
        Buttons::LeftThumbstickLeft | Buttons::LeftThumbstickUp |
        Buttons::RightThumbstickRight | Buttons::RightThumbstickDown;
    for (const auto request : {down, Buttons::A | Buttons::LeftTrigger,
                               Buttons::DPadLeft | Buttons::RightThumbstickDown}) {
        EXPECT_TRUE(state.IsButtonDown(request));
        EXPECT_FALSE(state.IsButtonUp(request));
    }
    for (const auto missing : {Buttons::B, Buttons::DPadRight, Buttons::RightTrigger,
                               Buttons::LeftThumbstickDown, Buttons::RightThumbstickUp}) {
        EXPECT_FALSE(state.IsButtonDown(down | missing));
        EXPECT_TRUE(state.IsButtonUp(down | missing));
    }
}

TEST(Tq075GamePadStateTest, UndefinedAndZeroQueriesKeepTheFullRequestedMask)
{
    // Same Refresh IsButtonDown IL_02bd..02c3: 0&mask==0, but unknown bits cannot be met.
    // 0x80000000 is outside XNA and CNA's named Buttons; do not confuse it with CNAEXT bits.
    const auto unknown = static_cast<Buttons>(0x80000000u);
    const auto zero = static_cast<Buttons>(0);
    for (const auto& state : {GamePadState(),
             GamePadState(Vector2::Zero, Vector2::Zero, 0, 0, {Buttons::A, unknown})}) {
        EXPECT_TRUE(state.IsButtonDown(zero));
        EXPECT_FALSE(state.IsButtonUp(zero));
        EXPECT_FALSE(state.IsButtonDown(unknown));
        EXPECT_TRUE(state.IsButtonUp(unknown));
        EXPECT_FALSE(state.IsButtonDown(Buttons::A | unknown));
        EXPECT_TRUE(state.IsButtonUp(Buttons::A | unknown));
        EXPECT_FALSE(state.IsButtonDown(static_cast<Buttons>(0xffffffffu)));
        EXPECT_TRUE(state.IsButtonUp(static_cast<Buttons>(0xffffffffu)));
    }
}

TEST(Tq075GamePadStateTest, EmptyListProjectsNullArrayAndConstructorsKeepClampedValues)
{
    // Refresh five-arg ctor IL_002b..0045 treats null and empty arrays identically.
    // C++ initializer_list has no nullable object; {} is its no-buttons projection.
    // Public ctors set connected=true/packet=0 (both IL_0000..000d); clamp only, no dead zone.
    const GamePadState empty(Vector2::Zero, Vector2::Zero, 0, 0, {});
    EXPECT_TRUE(empty.getIsConnectedProperty());
    EXPECT_EQ(empty.getPacketNumberProperty(), 0);
    EXPECT_EQ(empty.getButtonsProperty(), GamePadButtons());
    EXPECT_EQ(empty.getDPadProperty(), GamePadDPad());
    EXPECT_TRUE(empty.IsButtonUp(Buttons::A | Buttons::DPadUp | Buttons::LeftTrigger));
    const GamePadState components(GamePadThumbSticks(Vector2(2, -2), Vector2(-2, 2)),
        GamePadTriggers(2, -2), GamePadButtons(Buttons::B), GamePadDPad());
    const GamePadState raw(Vector2(2, -2), Vector2(-2, 2), 2, -2, {Buttons::B});
    for (const auto& value : {components, raw}) {
        EXPECT_TRUE(value.getIsConnectedProperty());
        EXPECT_EQ(value.getPacketNumberProperty(), 0);
        EXPECT_EQ(value.getThumbSticksProperty().getLeftProperty(), Vector2(1, -1));
        EXPECT_EQ(value.getThumbSticksProperty().getRightProperty(), Vector2(-1, 1));
        EXPECT_FLOAT_EQ(value.getTriggersProperty().getLeftProperty(), 1);
        EXPECT_FLOAT_EQ(value.getTriggersProperty().getRightProperty(), 0);
        EXPECT_EQ(value.getButtonsProperty(), GamePadButtons(Buttons::B));
    }
}
