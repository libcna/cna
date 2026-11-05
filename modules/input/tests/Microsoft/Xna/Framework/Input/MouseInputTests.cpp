// SPDX-License-Identifier: MS-PL
//
// Task 755: unit tests for MouseState, Mouse, and MouseCursor.
//
// MouseCursor uses Texture2D's CPU-only fixture, so its full system/custom mapping is deterministic
// and does not need an SDL video driver.

#include <gtest/gtest.h>
#include "System/ArgumentException.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "CNA/Internal/Input/CoordinateTransformTestRenderer.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SystemInput.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/CannedMouse.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/WindowDescription.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    void ResetMouseState()
    {
        CNA::Internal::Input::InputManager::SetMousePosition(0, 0);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::Left, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::Right, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::Middle, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::XButton1, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::XButton2, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseRelativeMode(false);
        Mouse::setWindowHandleProperty(0);
        Mouse::ClickedEXT   = nullptr;
    }

    class MousePlatformInputTest : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedMousePlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            ResetMouseState();
            installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }

        void TearDown() override
        {
            Mouse::ResetForTests();
            installed.reset();
        }
    };
}

// ===========================================================================
// MouseState
// ===========================================================================

TEST(MouseStateTest, DefaultConstructorAllValuesAtRest)
{
    const MouseState state;

    EXPECT_EQ(state.getXProperty(), 0);
    EXPECT_EQ(state.getYProperty(), 0);
    EXPECT_EQ(state.getScrollWheelValueProperty(), 0);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getXButton1Property(), ButtonState::Released);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Released);

    // P1-018: the CNAEXT/EXT horizontal wheel also defaults to 0 through the true (parameterless)
    // default constructor, not just through the 8-arg XNA ctor (already covered separately below).
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 0);
}

TEST(MouseStateTest, EightArgConstructorSetsEveryFieldInTheRightSlot)
{
    // Ctor order is (x, y, scrollWheel, leftButton, middleButton, rightButton, xButton1,
    // xButton2) — alternate Pressed/Released so an accidental parameter swap fails this test.
    const MouseState state(10, 20, 30,
                            ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                            ButtonState::Released, ButtonState::Pressed);

    EXPECT_EQ(state.getXProperty(), 10);
    EXPECT_EQ(state.getYProperty(), 20);
    EXPECT_EQ(state.getScrollWheelValueProperty(), 30);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton1Property(), ButtonState::Released);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);

    // N-005: the 8-arg XNA ctor leaves the CNAEXT/EXT horizontal wheel at 0.
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 0);
}

// N-005: the 9-arg CNAEXT/EXT ctor sets the horizontal wheel while keeping every XNA field in the same slot.
TEST(MouseStateTest, NineArgConstructorAlsoSetsHorizontalScrollWheelEXT)
{
    const MouseState state(10, 20, 30,
                            ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                            ButtonState::Released, ButtonState::Pressed,
                            240); // horizontal wheel

    EXPECT_EQ(state.getXProperty(), 10);
    EXPECT_EQ(state.getScrollWheelValueProperty(), 30);
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 240);
}

// N-005: the horizontal wheel is a CNAEXT/EXT extra field deliberately EXCLUDED from Equals and GetHashCode,
// so those stay byte-identical to FNA (which has no horizontal wheel). Two states differing only in the
// horizontal wheel are equal and hash equal.
TEST(MouseStateTest, HorizontalScrollWheelEXTIsExcludedFromEqualityAndHash)
{
    const MouseState a(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                       ButtonState::Released, ButtonState::Released, 120);
    const MouseState b(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                       ButtonState::Released, ButtonState::Released, 999);

    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(MouseStateTest, EqualsAndOperatorsReturnTrueForIdenticalStates)
{
    const MouseState a(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                        ButtonState::Released, ButtonState::Pressed);
    const MouseState b(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                        ButtonState::Released, ButtonState::Pressed);

    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(MouseStateTest, EqualsAndOperatorsReturnFalseWhenPositionDiffers)
{
    const MouseState a(1, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(9, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_FALSE(a.Equals(b));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(MouseStateTest, EqualsAndOperatorsReturnFalseWhenScrollWheelDiffers)
{
    const MouseState a(1, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(1, 2, 99, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_FALSE(a.Equals(b));
    EXPECT_TRUE(a != b);
}

TEST(MouseStateTest, EqualsAndOperatorsReturnFalseWhenAButtonDiffers)
{
    const MouseState a(1, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_FALSE(a.Equals(b));
    EXPECT_TRUE(a != b);

    // FNA MouseState.operator== compares each of the five buttons independently. Checking
    // only Left would miss a comparison that accidentally omits either X button.
    const auto up = ButtonState::Released, down = ButtonState::Pressed;
    for (const MouseState changed : {
             MouseState(1, 2, 3, down, up, up, up, up),
             MouseState(1, 2, 3, up, down, up, up, up),
             MouseState(1, 2, 3, up, up, down, up, up),
             MouseState(1, 2, 3, up, up, up, down, up),
             MouseState(1, 2, 3, up, up, up, up, down)})
    {
        EXPECT_FALSE(a.Equals(changed));
        EXPECT_FALSE(changed.Equals(a));
        EXPECT_FALSE(a == changed);
        EXPECT_TRUE(a != changed);
    }
}

TEST(MouseStateTest, Tq027GetHashCodeXorsEveryXnaField)
{
    // XNA MouseState.GetHashCode IL_0000..0078: x ^ y ^ wheel ^ all five button values.
    const MouseState state(3, 5, 7, ButtonState::Released, ButtonState::Released,
                           ButtonState::Released, ButtonState::Released, ButtonState::Released);
    EXPECT_EQ(state.GetHashCode(), 1); // 3 ^ 5 ^ 7 = 1.
    EXPECT_EQ(MouseState().GetHashCode(), 0); // no SmartGetHashCode zero substitution here.
    for (int field = 0; field < 5; ++field)
    {
        const auto pressed = [field](int index) {
            return field == index ? ButtonState::Pressed : ButtonState::Released;
        };
        EXPECT_EQ(MouseState(0, 0, 0, pressed(0), pressed(1), pressed(2), pressed(3), pressed(4)).GetHashCode(), 1);
    }
    EXPECT_EQ(MouseState(-1, 0, 0, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released, ButtonState::Released).GetHashCode(), -1);
}

TEST(MouseStateTest, GetHashCodeIsConsistentForEqualStates)
{
    const MouseState a(3, 5, 7, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(3, 5, 7, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(MouseStateTest, ToStringFormatsNoneWhenNoButtonsPressed)
{
    const MouseState state(1, 2, 3, ButtonState::Released, ButtonState::Released,
                            ButtonState::Released, ButtonState::Released, ButtonState::Released);

    EXPECT_EQ(state.ToString(), "{X:1 Y:2 Buttons:None Wheel:3}");
}

TEST(MouseStateTest, ToStringFormatsMultiplePressedButtonsInLeftRightMiddleXButton1XButton2Order)
{
    // leftButton=Pressed, xButton2=Pressed; middle/right/xButton1 stay Released.
    const MouseState state(0, 0, 0, ButtonState::Pressed, ButtonState::Released,
                            ButtonState::Released, ButtonState::Released, ButtonState::Pressed);

    EXPECT_EQ(state.ToString(), "{X:0 Y:0 Buttons:Left XButton2 Wheel:0}");
}

TEST(MouseStateTest, ToStringUsesXnaFormatWithNegativeValuesAndEveryButton)
{
    // XNA 4.0's IL: "{{X:{0} Y:{1} Buttons:{2} Wheel:{3}}}", buttons in Left, Right, Middle,
    // XButton1, XButton2 order.
    const MouseState state(-12, -34, -240, ButtonState::Pressed, ButtonState::Pressed,
                            ButtonState::Pressed, ButtonState::Pressed, ButtonState::Pressed);

    EXPECT_EQ(state.ToString(), "{X:-12 Y:-34 Buttons:Left Right Middle XButton1 XButton2 Wheel:-240}");
}

// TQ-077: one-hot arguments distinguish every pair of constructor button slots.
TEST(MouseStateTest, Tq077BothConstructorsMapEachButtonAndItsTextIndependently)
{
    // Refresh MouseState.ctor IL_0015..0038 stores each enum argument verbatim;
    // ToString IL_0006..00d2 lists Left, Right, Middle, XButton1, XButton2.
    const char* expectedText[] = {
        "{X:17 Y:-19 Buttons:Left Wheel:-121}",
        "{X:17 Y:-19 Buttons:Middle Wheel:-121}",
        "{X:17 Y:-19 Buttons:Right Wheel:-121}",
        "{X:17 Y:-19 Buttons:XButton1 Wheel:-121}",
        "{X:17 Y:-19 Buttons:XButton2 Wheel:-121}"};
    for (int field = 0; field < 5; ++field)
    {
        SCOPED_TRACE(field);
        const auto button = [field](int index) {
            return field == index ? ButtonState::Pressed : ButtonState::Released;
        };
        const MouseState xna(17, -19, -121, button(0), button(1), button(2), button(3), button(4));
        const MouseState extended(17, -19, -121, button(0), button(1), button(2), button(3), button(4), 997);
        for (const auto& state : {xna, extended})
        {
            EXPECT_EQ(state.getXProperty(), 17);
            EXPECT_EQ(state.getYProperty(), -19);
            EXPECT_EQ(state.getScrollWheelValueProperty(), -121);
            const std::array<ButtonState, 5> actual{
                state.getLeftButtonProperty(), state.getMiddleButtonProperty(),
                state.getRightButtonProperty(), state.getXButton1Property(), state.getXButton2Property()};
            for (int index = 0; index < 5; ++index)
                EXPECT_EQ(actual[index], index == field ? ButtonState::Pressed : ButtonState::Released);
            EXPECT_EQ(state.ToString(), expectedText[field]);
        }
        EXPECT_EQ(xna.getHorizontalScrollWheelValueEXTProperty(), 0);
        EXPECT_EQ(extended.getHorizontalScrollWheelValueEXTProperty(), 997);
    }
}

TEST(MouseStateTest, Tq077UndefinedButtonValuesAreStoredComparedAndHashedVerbatim)
{
    // Refresh ctor IL_0015..0038 has no enum validation; getters IL_0000..0006
    // return the fields. ToString tests ==1 (IL_000c..00b1), not nonzero.
    const MouseState state(3, 5, 7, static_cast<ButtonState>(2), static_cast<ButtonState>(-1),
                           static_cast<ButtonState>(3), static_cast<ButtonState>(7), static_cast<ButtonState>(-8));
    EXPECT_EQ(static_cast<int>(state.getLeftButtonProperty()), 2);
    EXPECT_EQ(static_cast<int>(state.getMiddleButtonProperty()), -1);
    EXPECT_EQ(static_cast<int>(state.getRightButtonProperty()), 3);
    EXPECT_EQ(static_cast<int>(state.getXButton1Property()), 7);
    EXPECT_EQ(static_cast<int>(state.getXButton2Property()), -8);
    EXPECT_EQ(state.ToString(), "{X:3 Y:5 Buttons:None Wheel:7}");
    // Refresh GetHashCode IL_0000..0078: 3^5^7^2^(-1)^3^7^(-8) = 0.
    EXPECT_EQ(state.GetHashCode(), 0);
    const MouseState different(3, 5, 7, ButtonState::Released, static_cast<ButtonState>(-1),
                               static_cast<ButtonState>(3), static_cast<ButtonState>(7), static_cast<ButtonState>(-8));
    EXPECT_FALSE(state.Equals(different));
    EXPECT_FALSE(state == different);
    EXPECT_TRUE(state != different);
    // Refresh Equals(object) IL_0000..001b checks the runtime type before comparing fields.
    EXPECT_TRUE(state.Equals(std::any(MouseState(state))));
    EXPECT_FALSE(state.Equals(std::any(different)));
}

TEST(MouseStateTest, Tq077SignedIntegerEndpointsHaveExactPropertiesHashesAndText)
{
    // Refresh ctor IL_0000..0010 stores Int32 without clamping. GetHashCode
    // IL_0000..0078 XORs the same bits; ToString IL_00e1..0126 formats Int32.
    struct Case { int x, y, wheel, hash; const char* text; };
    const Case cases[] = {
        {-2147483647 - 1, 2147483647, -2147483647 - 1, 2147483647,
         "{X:-2147483648 Y:2147483647 Buttons:None Wheel:-2147483648}"},
        {2147483647, -2147483647 - 1, 2147483647, -2147483647 - 1,
         "{X:2147483647 Y:-2147483648 Buttons:None Wheel:2147483647}"},
        {-16777217, 16777217, -1, 1,
         "{X:-16777217 Y:16777217 Buttons:None Wheel:-1}"}};
    const auto off = ButtonState::Released;
    for (const auto& item : cases)
    {
        SCOPED_TRACE(item.text);
        const MouseState state(item.x, item.y, item.wheel, off, off, off, off, off);
        EXPECT_EQ(state.getXProperty(), item.x);
        EXPECT_EQ(state.getYProperty(), item.y);
        EXPECT_EQ(state.getScrollWheelValueProperty(), item.wheel);
        // First two hashes cancel x with wheel; third: (-n)^n == -2, (-2)^(-1)==1.
        EXPECT_EQ(state.GetHashCode(), item.hash);
        EXPECT_EQ(state.ToString(), item.text);
    }
}

// ===========================================================================
// Mouse
// ===========================================================================

TEST_F(MousePlatformInputTest, GetStateReflectsPublishedPositionAndAllFiveButtons)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.x = 15;
    snapshot.y = 25;
    snapshot.buttons = 0x01 | 0x02 | 0x04 | 0x08 | 0x10;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    const auto state = Mouse::GetState();

    EXPECT_EQ(state.getXProperty(), 15);
    EXPECT_EQ(state.getYProperty(), 25);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton1Property(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);
}

TEST_F(MousePlatformInputTest, GetStateReflectsBothCumulativeWheelAxes)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.scrollX = -240;
    snapshot.scrollY = 360;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getScrollWheelValueProperty(), 360);
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), -240);
}

TEST_F(MousePlatformInputTest, SetPositionUsesTheSnapshotWindowAndUpdatesGetState)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 27;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    Mouse::SetPosition(42, 84);
    const auto state = Mouse::GetState();

    EXPECT_EQ(platform.Canned().PositionCalls(), 1);
    EXPECT_EQ(platform.Canned().LastPositionWindow(), 27u);
    EXPECT_EQ(state.getXProperty(), 42);
    EXPECT_EQ(state.getYProperty(), 84);
}

TEST_F(MousePlatformInputTest, GetStateDoesNotAdvanceThePlatformFrame)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.x = 3;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    ASSERT_EQ(platform.Canned().UpdateCount(), 1);

    EXPECT_EQ(Mouse::GetState().getXProperty(), 3);
    EXPECT_EQ(Mouse::GetState().getXProperty(), 3);
    EXPECT_EQ(platform.Canned().UpdateCount(), 1);
}

TEST_F(MousePlatformInputTest, MissingMouseServiceReturnsRestState)
{
    platform.SetMouseAvailable(false);

    const auto state = Mouse::GetState();
    EXPECT_EQ(state, MouseState());
}

TEST(MouseTest, InternalOnClickedFiresClickedEXTWithButtonIndex)
{
    ResetMouseState();

    int firedButton = -1;
    Mouse::ClickedEXT = [&firedButton](const int button) { firedButton = button; };

    Mouse::INTERNAL_onClicked(2);

    EXPECT_EQ(firedButton, 2);

    ResetMouseState();
}

// DEC-06: ClickedEXT is a multicast System::MulticastAction<int> (matches FNA's Action<int>).
TEST(MouseTest, ClickedEXTIsMulticastAndInvokesAllSubscribersInOrder)
{
    ResetMouseState();
    Mouse::ClickedEXT = nullptr;

    std::vector<int> order;
    Mouse::ClickedEXT += [&order](int) { order.push_back(1); };
    Mouse::ClickedEXT += [&order](const int button) { order.push_back(100 + button); };

    Mouse::INTERNAL_onClicked(3);

    ASSERT_EQ(order.size(), 2u) << "both subscribers must fire (multicast, matching FNA)";
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 103);

    ResetMouseState();
}

// DEC-06: `=` replaces the whole invocation list (C# `field = handler;`), while `+=` adds.
TEST(MouseTest, ClickedEXTAssignmentReplacesAllSubscribers)
{
    ResetMouseState();
    Mouse::ClickedEXT = nullptr;

    int viaAdded = 0;
    int viaAssigned = 0;
    Mouse::ClickedEXT += [&viaAdded](int) { ++viaAdded; };
    Mouse::ClickedEXT = [&viaAssigned](int) { ++viaAssigned; }; // replaces the += subscriber above

    Mouse::INTERNAL_onClicked(0);

    EXPECT_EQ(viaAdded, 0) << "assignment must replace, not append";
    EXPECT_EQ(viaAssigned, 1);

    ResetMouseState();
}

TEST(MouseTest, InternalOnClickedIsSafeWithNoSubscriber)
{
    ResetMouseState();

    EXPECT_NO_THROW(Mouse::INTERNAL_onClicked(0));

    ResetMouseState();
}

TEST_F(MousePlatformInputTest, GetIsRelativeMouseModeEXTDefaultsToFalse)
{
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());
}

TEST_F(MousePlatformInputTest, RelativeModeAccumulatesDeltaAndDrainsOnRead)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 9;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    Mouse::setIsRelativeMouseModeEXTProperty(true);

    platform.Canned().AddPendingRelativeDelta(3.0f, -4.0f);
    platform.Canned().AddPendingRelativeDelta(1.0f, 1.0f);
    platform.Canned().Update();

    const auto first = Mouse::GetState();
    EXPECT_EQ(first.getXProperty(), 4);
    EXPECT_EQ(first.getYProperty(), -3);

    // Draining semantics (mirrors FNA's native relative-state read): a second read with no new
    // motion in between returns 0,0.
    const auto second = Mouse::GetState();
    EXPECT_EQ(second.getXProperty(), 0);
    EXPECT_EQ(second.getYProperty(), 0);
}

TEST(MouseTest, IsRelativeMouseModeEXTRoundTripsThroughRealWindow)
{
#if !defined(CNA_PLATFORM_SDL3)
    GTEST_SKIP() << "requires the SDL3 platform mouse service";
#endif
    ResetMouseState();

    CNA::Platform::IPlatform& current = CNA::Platform::GetCurrentPlatform();
    try
    {
        current.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
    }
    catch (const CNA::Platform::PlatformException& error)
    {
        GTEST_SKIP() << "video subsystem unavailable: " << error.what();
    }

    CNA::Platform::WindowDescription description;
    description.title = "MouseInputTests";
    description.width = 64;
    description.height = 64;
    description.visible = false;
    std::unique_ptr<CNA::Platform::IPlatformWindow> window;
    try
    {
        window = current.CreateWindow(description);
    }
    catch (const CNA::Platform::PlatformException& error)
    {
        current.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
        GTEST_SKIP() << "window creation unavailable: " << error.what();
    }

    Mouse::setWindowHandleProperty(window->GetWindowHandle());

    Mouse::setIsRelativeMouseModeEXTProperty(true);
    EXPECT_TRUE(Mouse::getIsRelativeMouseModeEXTProperty());

    Mouse::setIsRelativeMouseModeEXTProperty(false);
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());

    window.reset();
    current.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
    ResetMouseState();
}

TEST_F(MousePlatformInputTest, SetIsRelativeMouseModeEXTUsesTheAssociatedPlatformWindow)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 11;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    Mouse::setIsRelativeMouseModeEXTProperty(true);
    EXPECT_TRUE(Mouse::getIsRelativeMouseModeEXTProperty());
    EXPECT_EQ(platform.Canned().RelativeModeCalls(), 1);
    EXPECT_EQ(platform.Canned().LastRelativeWindow(), 11u);
    Mouse::setIsRelativeMouseModeEXTProperty(false);
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());
    EXPECT_EQ(platform.Canned().RelativeModeCalls(), 2);
}

TEST_F(MousePlatformInputTest, SetPositionIsNoOpWhenRelativeModeEnabled)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 13;
    snapshot.x = 7;
    snapshot.y = 7;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    Mouse::setIsRelativeMouseModeEXTProperty(true);
    Mouse::SetPosition(99, 99); // must be a no-op while relative mode is on (Mouse.cs:106-110)

    EXPECT_EQ(platform.Canned().PositionCalls(), 0);
    // FNA Mouse.cs:106-110: a refused relative-mode warp must preserve the backing position.
    EXPECT_EQ(platform.Canned().GetSnapshot().x, 7);
    EXPECT_EQ(platform.Canned().GetSnapshot().y, 7);
    EXPECT_TRUE(Mouse::getIsRelativeMouseModeEXTProperty());
}

TEST_F(MousePlatformInputTest, SetRelativeMouseModeIsSafeNoOpWithNoWindow)
{
    EXPECT_NO_THROW(Mouse::setIsRelativeMouseModeEXTProperty(true));
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());
    EXPECT_EQ(platform.Canned().RelativeModeCalls(), 0);
}

TEST_F(MousePlatformInputTest, SetPositionIsSafeAndUpdatesSnapshotWithNoWindow)
{
    ASSERT_EQ(Mouse::getWindowHandleProperty(), 0u);

    EXPECT_NO_THROW(Mouse::SetPosition(123, 456));

    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), 123);
    EXPECT_EQ(state.getYProperty(), 456);

    // Negative and large coordinates must be equally safe with no window (no crash, state tracks).
    EXPECT_NO_THROW(Mouse::SetPosition(-100, -100));
    EXPECT_NO_THROW(Mouse::SetPosition(1 << 20, 1 << 20));
    EXPECT_EQ(Mouse::GetState().getXProperty(), 1 << 20);
    EXPECT_EQ(Mouse::GetState().getYProperty(), 1 << 20);
}

TEST_F(MousePlatformInputTest, SetCursorIsSafeNoOpForDisposedCursor)
{
    MouseCursor cursor;
    cursor.Dispose();

    EXPECT_NO_THROW(Mouse::SetCursor(cursor));
    EXPECT_EQ(platform.Canned().CursorCalls(), 0);
}

TEST_F(MousePlatformInputTest, SetCursorIsSafeNoOpWithoutMouseService)
{
    platform.SetMouseAvailable(false);
    MouseCursor cursor;
    EXPECT_NO_THROW(Mouse::SetCursor(cursor));
    EXPECT_EQ(platform.Canned().CursorCalls(), 0);
}

TEST_F(MousePlatformInputTest, CoordinateTransformPreservesLetterboxOffsetAndInverse)
{
    constexpr CNA::Platform::WindowId Window = 0x7ffe1002u;
    constexpr std::uintptr_t LegacyToken = 0xfedcba98u;
    CNA::Internal::Input::Testing::CoordinateTransformTestRenderer renderer(
        100.0f, 100.0f, 50.0f, 0.0f, 100.0f, 100.0f);
    CNA::Internal::Renderers::IGraphicsRenderer::RegisterForWindow(Window, &renderer);
    Mouse::INTERNAL_setWindow(LegacyToken, Window);

    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = Window;
    snapshot.x = 100;
    snapshot.y = 50;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    EXPECT_EQ(Mouse::getWindowHandleProperty(), LegacyToken);
    const auto initial = Mouse::GetState();
    EXPECT_EQ(initial.getXProperty(), 50);
    EXPECT_EQ(initial.getYProperty(), 50);

    Mouse::SetPosition(25, 75);
    EXPECT_EQ(platform.Canned().GetSnapshot().window, Window);
    EXPECT_EQ(platform.Canned().GetSnapshot().x, 75) << "50px letterbox offset must be applied";
    EXPECT_EQ(platform.Canned().GetSnapshot().y, 75);

    const auto roundTrip = Mouse::GetState();
    EXPECT_EQ(roundTrip.getXProperty(), 25);
    EXPECT_EQ(roundTrip.getYProperty(), 75);
    EXPECT_EQ(renderer.LogicalToWindowCalls(), 1);
    EXPECT_EQ(renderer.WindowToLogicalCalls(), 2);

    CNA::Internal::Renderers::IGraphicsRenderer::UnregisterForWindow(Window);
}

// ===========================================================================
// MouseCursor
// ===========================================================================

TEST(MouseCursorTest, StockCursorGetterReturnsTheSameInstanceOnRepeatedCalls)
{
    EXPECT_EQ(&MouseCursor::getArrowProperty(), &MouseCursor::getArrowProperty());
}

TEST_F(MousePlatformInputTest, EveryStockCursorMapsToThePlatformContract)
{
    using Getter = MouseCursor& (*)();
    struct Case
    {
        Getter get;
        CNA::Platform::SystemCursor expected;
    };
    const std::array<Case, 12> cases{{
        {&MouseCursor::getArrowProperty, CNA::Platform::SystemCursor::Arrow},
        {&MouseCursor::getCrosshairProperty, CNA::Platform::SystemCursor::Crosshair},
        {&MouseCursor::getHandProperty, CNA::Platform::SystemCursor::Pointer},
        {&MouseCursor::getIBeamProperty, CNA::Platform::SystemCursor::IBeam},
        {&MouseCursor::getNoProperty, CNA::Platform::SystemCursor::NotAllowed},
        {&MouseCursor::getSizeAllProperty, CNA::Platform::SystemCursor::Move},
        {&MouseCursor::getSizeNESWProperty, CNA::Platform::SystemCursor::NeswResize},
        {&MouseCursor::getSizeNSProperty, CNA::Platform::SystemCursor::NsResize},
        {&MouseCursor::getSizeNWSEProperty, CNA::Platform::SystemCursor::NwseResize},
        {&MouseCursor::getSizeWEProperty, CNA::Platform::SystemCursor::EwResize},
        {&MouseCursor::getWaitProperty, CNA::Platform::SystemCursor::Wait},
        {&MouseCursor::getWaitArrowProperty, CNA::Platform::SystemCursor::Progress},
    }};

    int expectedCalls = 0;
    for (const auto& item : cases)
    {
        Mouse::SetCursor(item.get());
        EXPECT_EQ(platform.Canned().CursorCalls(), ++expectedCalls);
        EXPECT_FALSE(platform.Canned().LastCursorWasCustom());
        EXPECT_EQ(platform.Canned().LastSystemCursor(), item.expected);
    }
}

TEST_F(MousePlatformInputTest, DefaultConstructorRepresentsArrow)
{
    MouseCursor cursor;
    Mouse::SetCursor(cursor);
    EXPECT_EQ(platform.Canned().CursorCalls(), 1);
    EXPECT_EQ(platform.Canned().LastSystemCursor(), CNA::Platform::SystemCursor::Arrow);
}

TEST_F(MousePlatformInputTest, DisposingAStockSingletonIsANoOpAndKeepsItUsable)
{
    MouseCursor& crosshair = MouseCursor::getCrosshairProperty();
    crosshair.Dispose();
    crosshair.Dispose();
    Mouse::SetCursor(crosshair);

    EXPECT_EQ(platform.Canned().CursorCalls(), 1);
    EXPECT_EQ(platform.Canned().LastSystemCursor(), CNA::Platform::SystemCursor::Crosshair);
    EXPECT_EQ(&crosshair, &MouseCursor::getCrosshairProperty());
}

TEST_F(MousePlatformInputTest, DisposeIsIdempotentAndMakesOwnedCursorUnusable)
{
    MouseCursor cursor;
    cursor.Dispose();
    EXPECT_NO_THROW(cursor.Dispose());
    Mouse::SetCursor(cursor);
    EXPECT_EQ(platform.Canned().CursorCalls(), 0);
}

TEST_F(MousePlatformInputTest, MoveConstructorTransfersDescriptionAndDisposesSource)
{
    MouseCursor original;
    MouseCursor moved(std::move(original));

    Mouse::SetCursor(original);
    EXPECT_EQ(platform.Canned().CursorCalls(), 0);
    Mouse::SetCursor(moved);
    EXPECT_EQ(platform.Canned().CursorCalls(), 1);
    EXPECT_EQ(platform.Canned().LastSystemCursor(), CNA::Platform::SystemCursor::Arrow);
}

TEST_F(MousePlatformInputTest, MoveAssignmentTransfersCustomDescriptionAndDisposesSource)
{
    const std::vector<Color> pixels(4, Color(1, 2, 3, 4));
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(2, 2, SurfaceFormat::Color, pixels);
    MouseCursor target;
    MouseCursor source = MouseCursor::FromTexture2D(tex, 1, 1);

    target = std::move(source);

    Mouse::SetCursor(source);
    EXPECT_EQ(platform.Canned().CursorCalls(), 0);
    Mouse::SetCursor(target);
    EXPECT_EQ(platform.Canned().CursorCalls(), 1);
    EXPECT_TRUE(platform.Canned().LastCursorWasCustom());
    EXPECT_EQ(platform.Canned().LastCustomCursorHotSpotX(), 1);
    EXPECT_EQ(platform.Canned().LastCustomCursorHotSpotY(), 1);
}

TEST_F(MousePlatformInputTest, SelfMoveAssignmentLeavesCursorIntact)
{
    MouseCursor cursor;
    MouseCursor* self = &cursor;
    *self = std::move(*self);

    Mouse::SetCursor(cursor);
    EXPECT_EQ(platform.Canned().CursorCalls(), 1);
    EXPECT_EQ(platform.Canned().LastSystemCursor(), CNA::Platform::SystemCursor::Arrow);
}

TEST_F(MousePlatformInputTest, FromTexture2DCopiesRgbaPixelsAndHotSpot)
{
    std::vector<Color> pixels{
        Color(1, 2, 3, 4), Color(5, 6, 7, 8),
        Color(9, 10, 11, 12), Color(13, 14, 15, 16)};
    Texture2D tex = Texture2D::CreateCpuOnlyForTests(2, 2, SurfaceFormat::Color, pixels);
    MouseCursor cursor = MouseCursor::FromTexture2D(tex, 1, 0);

    const std::vector<std::uint32_t> expected{
        pixels[0].getPackedValueProperty(), pixels[1].getPackedValueProperty(),
        pixels[2].getPackedValueProperty(), pixels[3].getPackedValueProperty()};
    std::fill(pixels.begin(), pixels.end(), Color::Transparent);
    tex.SetData(pixels.data(), static_cast<int>(pixels.size()));

    Mouse::SetCursor(cursor);
    EXPECT_TRUE(platform.Canned().LastCursorWasCustom());
    EXPECT_EQ(platform.Canned().LastCustomCursorWidth(), 2);
    EXPECT_EQ(platform.Canned().LastCustomCursorHeight(), 2);
    EXPECT_EQ(platform.Canned().LastCustomCursorHotSpotX(), 1);
    EXPECT_EQ(platform.Canned().LastCustomCursorHotSpotY(), 0);
    EXPECT_EQ(platform.Canned().LastCustomCursorPixels(), expected);
}

TEST_F(MousePlatformInputTest, FromTexture2DAcceptsColorSrgbTexture)
{
    const std::vector<Color> pixels(16, Color::White);
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::ColorSrgbEXT, pixels);

    MouseCursor cursor = MouseCursor::FromTexture2D(tex, 1, 2);
    Mouse::SetCursor(cursor);
    EXPECT_TRUE(platform.Canned().LastCursorWasCustom());
    EXPECT_EQ(platform.Canned().LastCustomCursorPixels().size(), 16u);
    EXPECT_EQ(platform.Canned().LastCustomCursorHotSpotX(), 1);
    EXPECT_EQ(platform.Canned().LastCustomCursorHotSpotY(), 2);
}

TEST(MouseCursorTest, FromTexture2DRejectsNonColorSurfaceFormat)
{
    const std::vector<Color> pixels(16, Color::White);
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::Bgr565, pixels);

    EXPECT_THROW((void)MouseCursor::FromTexture2D(tex, 0, 0), System::ArgumentException);
}

TEST(MouseCursorTest, FromTexture2DThrowsWhenOriginIsOutsideTheTexture)
{
    const std::vector<Color> pixels(16, Color::White);
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::Color, pixels);

    EXPECT_THROW((void)MouseCursor::FromTexture2D(tex, 100, 100), System::ArgumentException);
    EXPECT_THROW((void)MouseCursor::FromTexture2D(tex, -1, 0), System::ArgumentException);
}


TEST(MouseStateTest, Tq025EachButtonAndCoordinateIndependentlyAffectsEquality)
{
    // XNA MouseState.op_Equality IL_0000..0082 compares all eight XNA fields.
    // Existing cases combine button changes; isolate each to detect an omitted comparison.
    const auto off = ButtonState::Released;
    const auto on = ButtonState::Pressed;
    const MouseState rest;
    const MouseState changed[] = {
        {1, 0, 0, off, off, off, off, off}, {0, 1, 0, off, off, off, off, off},
        {0, 0, 1, off, off, off, off, off}, {0, 0, 0, on, off, off, off, off},
        {0, 0, 0, off, on, off, off, off}, {0, 0, 0, off, off, on, off, off},
        {0, 0, 0, off, off, off, on, off}, {0, 0, 0, off, off, off, off, on}};
    for (const auto& value : changed)
    {
        EXPECT_FALSE(rest.Equals(value));
        EXPECT_FALSE(rest == value);
        EXPECT_TRUE(rest != value);
        EXPECT_TRUE(value.Equals(value));
        EXPECT_EQ(value.GetHashCode(), MouseState(value).GetHashCode());
    }
}

TEST_F(MousePlatformInputTest, Tq025ClearingWindowHandleRestoresSnapshotRouting)
{
    // XNA Mouse.WindowHandle getter/setter retain the token. CNA's platform contract
    // routes warps by stable window identity and falls back to the snapshot after clearing.
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 27;
    snapshot.scrollY = -240;
    snapshot.buttons = 0x15;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    Mouse::INTERNAL_setWindow(1234, 41);
    EXPECT_EQ(Mouse::getWindowHandleProperty(), 1234u);
    Mouse::SetPosition(-19, 23);
    EXPECT_EQ(platform.Canned().LastPositionWindow(), 41u);
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    Mouse::setWindowHandleProperty(0);
    EXPECT_EQ(Mouse::getWindowHandleProperty(), 0u);
    Mouse::SetPosition(-37, -51);
    EXPECT_EQ(platform.Canned().LastPositionWindow(), 27u);
    EXPECT_EQ(platform.Canned().PositionCalls(), 2);
    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), -37);
    EXPECT_EQ(state.getYProperty(), -51);
    EXPECT_EQ(state.getScrollWheelValueProperty(), -240);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);
}

TEST_F(MousePlatformInputTest, Tq077GetStatePreservesIntegerCoordinatesWithoutRenderer)
{
    // Refresh Mouse.GetState IL_0024..0035 stores POINT's Int32 coordinates directly.
    // 2^24+1 is the first positive integer lost by an unnecessary binary32 round trip.
    const std::array<std::pair<int, int>, 3> positions{{
        {16777217, -16777217}, {-2147483647 - 1, 2147483647},
        {2147483647, -2147483647 - 1}}};
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 0x7ffe1077u; // no renderer registered for this canned identity
    snapshot.scrollY = -2147483647 - 1;
    snapshot.scrollX = 2147483647;
    snapshot.buttons = 0x12; // Middle and XButton2, not adjacent constructor slots
    for (const auto& [x, y] : positions)
    {
        SCOPED_TRACE(x);
        snapshot.x = x;
        snapshot.y = y;
        platform.Canned().SetPending(snapshot);
        platform.Canned().Update();
        const auto state = Mouse::GetState();
        EXPECT_EQ(state.getXProperty(), x);
        EXPECT_EQ(state.getYProperty(), y);
        // The Guide's parallel snapshot route shares the same pass-through contract.
        const auto system = CNA::Internal::Input::systemMouseState();
        EXPECT_EQ(system.getXProperty(), x);
        EXPECT_EQ(system.getYProperty(), y);
        EXPECT_EQ(state.getScrollWheelValueProperty(), -2147483647 - 1);
        EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 2147483647);
        EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Released);
        EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Pressed);
        EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Released);
        EXPECT_EQ(state.getXButton1Property(), ButtonState::Released);
        EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);
    }
    EXPECT_EQ(platform.Canned().UpdateCount(), 3);
}

TEST_F(MousePlatformInputTest, Tq077SetPositionPreservesIntegerCoordinatesWithoutRenderer)
{
    // Refresh SetPosition IL_0000..0009/001e..002c keeps Int32 all the way to SetCursorPos.
    // Inspect the canned service directly so GetState cannot hide a bad warp conversion.
    const std::array<std::pair<int, int>, 3> positions{{
        {16777217, -16777217}, {-2147483647 - 1, 2147483647},
        {2147483647, -2147483647 - 1}}};
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 0x7ffe1077u;
    snapshot.scrollY = -120;
    snapshot.buttons = 0x09;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    for (const auto& [x, y] : positions)
    {
        SCOPED_TRACE(x);
        Mouse::SetPosition(x, y);
        EXPECT_EQ(platform.Canned().GetSnapshot().x, x);
        EXPECT_EQ(platform.Canned().GetSnapshot().y, y);
        EXPECT_EQ(platform.Canned().GetSnapshot().scrollY, -120);
        EXPECT_EQ(platform.Canned().GetSnapshot().buttons, 0x09);
        EXPECT_EQ(platform.Canned().LastPositionWindow(), snapshot.window);
    }
    EXPECT_EQ(platform.Canned().PositionCalls(), 3);
}

TEST_F(MousePlatformInputTest, Tq077DeclinedRendererTransformsPreserveIntegerCoordinates)
{
    // A renderer returning false promises pass-through (the CNA coordinate contract).
    // XNA's integer route above remains the oracle for the coordinates themselves.
    constexpr CNA::Platform::WindowId window = 0x7ffe1077u;
    CNA::Internal::Input::Testing::CoordinateTransformTestRenderer renderer(0, 0, 0, 0, 0, 0);
    CNA::Internal::Renderers::IGraphicsRenderer::RegisterForWindow(window, &renderer);
    struct Unregister
    {
        CNA::Platform::WindowId id;
        ~Unregister() { CNA::Internal::Renderers::IGraphicsRenderer::UnregisterForWindow(id); }
    } unregister{window};
    Mouse::INTERNAL_setWindow(0x7700, window);
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = window;
    snapshot.x = 16777217;
    snapshot.y = -16777217;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    const auto first = Mouse::GetState();
    EXPECT_EQ(first.getXProperty(), 16777217);
    EXPECT_EQ(first.getYProperty(), -16777217);
    Mouse::SetPosition(-2147483647 - 1, 2147483647);
    EXPECT_EQ(platform.Canned().GetSnapshot().x, -2147483647 - 1);
    EXPECT_EQ(platform.Canned().GetSnapshot().y, 2147483647);
    EXPECT_EQ(platform.Canned().LastPositionWindow(), window);
    const auto second = Mouse::GetState();
    EXPECT_EQ(second.getXProperty(), -2147483647 - 1);
    EXPECT_EQ(second.getYProperty(), 2147483647);
    EXPECT_EQ(first.getXProperty(), 16777217); // earlier value remains a snapshot
    EXPECT_EQ(first.getYProperty(), -16777217);
    const auto system = CNA::Internal::Input::systemMouseState();
    EXPECT_EQ(system.getXProperty(), -2147483647 - 1);
    EXPECT_EQ(system.getYProperty(), 2147483647);
    EXPECT_EQ(renderer.WindowToLogicalCalls(), 3);
    EXPECT_EQ(renderer.LogicalToWindowCalls(), 1);
}

TEST_F(MousePlatformInputTest, Tq077MissingMouseServiceRefusesWarpWithoutChangingRetainedState)
{
    // CNA's missing-service adaptation returns rest and makes SetPosition a no-op.
    // No pointer may fall back to native hardware when a service is unavailable.
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.x = 17;
    snapshot.y = -19;
    snapshot.scrollY = -120;
    snapshot.buttons = 0x15;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    Mouse::INTERNAL_setWindow(0x7700, 77);
    platform.SetMouseAvailable(false);
    Mouse::SetPosition(-2147483647 - 1, 2147483647);
    const auto rest = Mouse::GetState();
    EXPECT_EQ(rest, MouseState());
    EXPECT_EQ(rest.getXProperty(), 0);
    EXPECT_EQ(rest.getYProperty(), 0);
    EXPECT_EQ(rest.getScrollWheelValueProperty(), 0);
    for (const auto button : {rest.getLeftButtonProperty(), rest.getMiddleButtonProperty(),
                              rest.getRightButtonProperty(), rest.getXButton1Property(), rest.getXButton2Property()})
        EXPECT_EQ(button, ButtonState::Released);
    EXPECT_EQ(rest.getHorizontalScrollWheelValueEXTProperty(), 0);
    EXPECT_EQ(Mouse::getWindowHandleProperty(), 0x7700u);
    EXPECT_EQ(platform.Canned().PositionCalls(), 0);
    EXPECT_EQ(platform.Canned().GetSnapshot().x, 17);
    EXPECT_EQ(platform.Canned().GetSnapshot().y, -19);
    EXPECT_EQ(platform.Canned().GetSnapshot().scrollY, -120);
    EXPECT_EQ(platform.Canned().GetSnapshot().buttons, 0x15);
}

TEST_F(MousePlatformInputTest, Tq077RejectedWindowAdoptionRetainsTokenAndDropsStaleRouting)
{
    // Refresh Mouse.set_WindowHandle IL_0000..0007 retains the opaque token.
    // CNA's platform adaptation tolerates null/PlatformException adoption and must
    // discard the old stable id; all adoption here is canned, with no native call.
    class RefusingPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        CNA::Platform::Testing::CannedMouse mouse;
        bool throws = false;
        int adoptions = 0;
        std::uintptr_t lastToken = 0;
        CNA::Platform::IPlatformMouse* GetMouse() override { return &mouse; }
        std::unique_ptr<CNA::Platform::IPlatformWindow> AdoptWindowHandle(std::uintptr_t token) override
        {
            ++adoptions;
            lastToken = token;
            if (throws) throw CNA::Platform::PlatformException("AdoptWindowHandle", "canned refusal");
            return nullptr;
        }
    } refusing;
    CNA::Platform::Testing::ScopedCurrentPlatform scoped(refusing);
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 27;
    snapshot.scrollY = -240;
    snapshot.buttons = 0x12;
    for (const bool throws : {false, true})
    {
        SCOPED_TRACE(throws);
        refusing.throws = throws;
        refusing.mouse.SetPending(snapshot);
        refusing.mouse.Update();
        Mouse::INTERNAL_setWindow(0x1111, 41);
        Mouse::setWindowHandleProperty(0xfedcba98u);
        EXPECT_EQ(Mouse::getWindowHandleProperty(), 0xfedcba98u);
        EXPECT_EQ(refusing.lastToken, 0xfedcba98u);
        Mouse::SetPosition(-17, 23);
        EXPECT_EQ(refusing.mouse.LastPositionWindow(), 27u);
        const auto state = Mouse::GetState();
        EXPECT_EQ(state.getXProperty(), -17);
        EXPECT_EQ(state.getYProperty(), 23);
        EXPECT_EQ(state.getScrollWheelValueProperty(), -240);
        EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Pressed);
        EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);
        const int calls = refusing.adoptions;
        Mouse::setWindowHandleProperty(0);
        EXPECT_EQ(Mouse::getWindowHandleProperty(), 0u);
        EXPECT_EQ(refusing.adoptions, calls);
    }
    EXPECT_EQ(refusing.adoptions, 2);
}
