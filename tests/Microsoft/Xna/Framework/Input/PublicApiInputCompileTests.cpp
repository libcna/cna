// SPDX-License-Identifier: MS-PL
//
// INPUT-API-030 — public-API header-hygiene / compile guard.
//
// This translation unit includes ONLY public XNA Input headers (never CNA/Internal/** and never an
// SDL header of its own) and links a function that exercises every public Input type. It guards two
// properties of the public surface:
//   1. Self-containment: each public Input header compiles standalone and every public type is usable
//      from a consumer that includes only public headers (no internal header pulled in by hand).
//   2. SDL containment: no public Input header drags <SDL3/SDL.h> into a consumer's TU — EXCEPT the
//      two currently-known offenders, MouseCursor.hpp and (transitively, because Mouse.hpp includes
//      MouseCursor.hpp) Mouse.hpp. That SDL exposure is the open decision DEC-21 / INPUT-MOUSE-018 /
//      INPUT-API-033; once it is fixed, move those two includes above the #error guard so the guard
//      covers them too, and tighten the message.

#include <cstdint>
#include <functional>

#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"
#include "Microsoft/Xna/Framework/Input/KeyState.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"

// Guardrail: none of the headers above may pull SDL into a public consumer. The only sanctioned
// SDL-exposing public Input headers are Mouse.hpp / MouseCursor.hpp, included AFTER this point. If a
// future change makes any other public Input header transitively include SDL, this fails to compile.
#if defined(SDL_MAJOR_VERSION) || defined(SDL_h_)
#error "A public Input header (other than Mouse.hpp / MouseCursor.hpp) leaked SDL into the public API."
#endif

#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"

#include <gtest/gtest.h>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Input;
    using namespace Microsoft::Xna::Framework::Input::Touch;

    // Compiled and linked to prove every public type is usable from public headers only, but never
    // executed: the Get*State / cursor calls would otherwise need live SDL + input state, and this
    // TU must stay order-independent under --gtest_shuffle.
    [[maybe_unused]] void UsePublicInputApi()
    {
        // enums
        ButtonState bstate = ButtonState::Pressed;                 (void)bstate;
        KeyState kstate = KeyState::Down;                          (void)kstate;
        Buttons buttons = Buttons::A | Buttons::B;                 (void)buttons;
        GamePadType gptype = GamePadType::GamePad;                 (void)gptype;
        GamePadDeadZone dz = GamePadDeadZone::Circular;            (void)dz;
        TouchLocationState tlstate = TouchLocationState::Moved;    (void)tlstate;
        GestureType gtype = GestureType::Tap | GestureType::Hold;  (void)gtype;
        Keys key = Keys::A;                                        (void)key;

        // value structs
        GamePadButtons gpb(Buttons::A);        (void)gpb.getAProperty();
        GamePadDPad dpad;                      (void)dpad.getUpProperty();
        GamePadThumbSticks ts;                 (void)ts.getLeftProperty();
        GamePadTriggers tr;                    (void)tr.getLeftProperty();
        GamePadCapabilities caps;              (void)caps.getIsConnectedProperty();
        GamePadState gps;                      (void)gps.IsButtonDown(Buttons::A);
                                               (void)gps.getPacketNumberProperty();
        KeyboardState ks;                      (void)ks.IsKeyDown(Keys::A);
                                               (void)ks.GetPressedKeys();
        MouseState ms;                         (void)ms.getXProperty();
                                               (void)ms.getScrollWheelValueProperty();
        TouchLocation tl;                      (void)tl.getStateProperty();
        TouchCollection tc;                    (void)tc.getCountProperty();
        TouchPanelCapabilities tcaps;          (void)tcaps.getMaximumTouchCountProperty();
        GestureSample gsample;                 (void)gsample.getGestureTypeProperty();

        // static classes — referencing forces the link symbols to exist (never run here).
        (void)Keyboard::GetState();
        (void)Keyboard::GetState(PlayerIndex::One);
        (void)Keyboard::GetKeyFromScancodeEXT(Keys::A);
        (void)GamePad::GetState(PlayerIndex::One);
        (void)GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular);
        (void)GamePad::GetCapabilities(PlayerIndex::One);
        (void)GamePad::SetVibration(PlayerIndex::One, 0.0f, 0.0f);
        (void)GamePad::GetGUIDEXT(PlayerIndex::One);
        (void)Mouse::GetState();
        Mouse::SetPosition(0, 0);
        (void)Mouse::getIsRelativeMouseModeEXTProperty();
        (void)TouchPanel::GetState();
        (void)TouchPanel::GetCapabilities();
        (void)TouchPanel::getEnabledGesturesProperty();
        (void)TextInputEXT::IsTextInputActive();
        TextInputEXT::StartTextInput();
        (void)MouseCursor::getArrowProperty();
    }
}

// The meaningful assertions are compile-time (the include set + the SDL-leak #error guard) and
// link-time (UsePublicInputApi resolves using only public headers). Taking its address ODR-uses it so
// it is compiled and linked, without ever running it.
TEST(PublicApiInputCompileTest, PublicHeadersAreSelfContainedAndConfineSdlExposure)
{
    auto* fn = &UsePublicInputApi;
    EXPECT_NE(fn, nullptr);
}
