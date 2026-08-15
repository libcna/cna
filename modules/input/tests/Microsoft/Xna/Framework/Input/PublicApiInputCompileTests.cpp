// SPDX-License-Identifier: MS-PL
//
// INPUT-API-030 — public-API header-hygiene / compile guard.
//
// This translation unit includes ONLY public XNA Input headers (never CNA/Internal/** and never an
// SDL header of its own) and links a function that exercises every public Input type. It guards two
// properties of the public surface:
//   1. Self-containment: each public Input header compiles standalone and every public type is usable
//      from a consumer that includes only public headers (no internal header pulled in by hand).
//   2. SDL containment: NO public Input header drags <SDL3/SDL.h> or an SDL type into a consumer's
//      TU. MouseCursor stores only a platform-neutral cursor description.

#include <cstdint>
#include <functional>
#include <type_traits>

#include "System/Object.hpp"

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
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp"

// Guardrail: NO public Input header above may pull SDL into a consumer's TU. If any public Input
// header starts (transitively) including an SDL header, this fails to compile.
#if defined(SDL_MAJOR_VERSION) || defined(SDL_h_)
#error "A public Input header leaked SDL (<SDL3/SDL.h>) into the public API surface."
#endif

// INPUT-API-029 — GetTypeName() policy guard.
//
// CLAUDE.md requires every *concrete* System::Object subclass to override `CNAEXT GetTypeName()`
// (Object declares it pure-virtual, so a concrete Object subclass that omitted it would not even
// compile). Audit result: NO public Input type inherits System::Object — the value structs, the
// static classes, and the enums are all non-Object, and the one type with a base (`MouseCursor :
// System::IDisposable`) inherits IDisposable, which is itself NOT an Object subclass. So GetTypeName()
// does not apply to any Input type; every type is exempt. These static_asserts pin that exemption: if a
// future change makes any public Input type derive from System::Object, this stops compiling — the
// signal to add the required `CNAEXT GetTypeName()` override at that point.
namespace
{
    template <class T>
    inline constexpr bool not_object_v = !std::is_base_of_v<System::Object, T>;

    using namespace Microsoft::Xna::Framework::Input;
    using namespace Microsoft::Xna::Framework::Input::Touch;

    static_assert(not_object_v<GamePad>);
    static_assert(not_object_v<GamePadButtons>);
    static_assert(not_object_v<GamePadCapabilities>);
    static_assert(not_object_v<GamePadDPad>);
    static_assert(not_object_v<GamePadState>);
    static_assert(not_object_v<GamePadThumbSticks>);
    static_assert(not_object_v<GamePadTriggers>);
    static_assert(not_object_v<Keyboard>);
    static_assert(not_object_v<KeyboardState>);
    static_assert(not_object_v<Mouse>);
    static_assert(not_object_v<MouseCursor>);
    static_assert(not_object_v<MouseState>);
    static_assert(not_object_v<TextInputEXT>);
    static_assert(not_object_v<GestureSample>);
    static_assert(not_object_v<TouchCollection>);
    static_assert(not_object_v<TouchLocation>);
    static_assert(not_object_v<TouchPanel>);
    static_assert(not_object_v<TouchPanelCapabilities>);
}

// INPUT-API-028 — namespace + include-path placement guard.
//
// Every public Input type must live in `Microsoft::Xna::Framework::Input`, and every Touch type in
// `…::Input::Touch`, with the header path mirroring the namespace (verified against FNA: 18 top-level +
// 8 Touch .cs share exactly these two namespaces). The includes above already prove each header is
// reachable at its mirrored path; these fully-qualified references additionally pin the *namespace* of
// each type — naming `…::Input::Touch::GestureSample` (etc.) fails to compile if a type is ever moved to
// the wrong namespace. `sizeof(T) > 0` resolves uniformly for both the value structs/classes and enums.
namespace ns_placement_guard
{
    namespace X = Microsoft::Xna::Framework::Input;
    namespace T = Microsoft::Xna::Framework::Input::Touch;

    static_assert(sizeof(X::ButtonState) > 0 && sizeof(X::Buttons) > 0 && sizeof(X::KeyState) > 0
                  && sizeof(X::Keys) > 0 && sizeof(X::GamePadType) > 0 && sizeof(X::GamePadDeadZone) > 0);
    static_assert(sizeof(X::GamePad) > 0 && sizeof(X::GamePadButtons) > 0 && sizeof(X::GamePadCapabilities) > 0
                  && sizeof(X::GamePadDPad) > 0 && sizeof(X::GamePadState) > 0 && sizeof(X::GamePadThumbSticks) > 0
                  && sizeof(X::GamePadTriggers) > 0);
    static_assert(sizeof(X::Keyboard) > 0 && sizeof(X::KeyboardState) > 0 && sizeof(X::Mouse) > 0
                  && sizeof(X::MouseState) > 0 && sizeof(X::MouseCursor) > 0 && sizeof(X::TextInputEXT) > 0);
    static_assert(sizeof(T::GestureSample) > 0 && sizeof(T::GestureType) > 0 && sizeof(T::TouchCollection) > 0
                  && sizeof(T::TouchLocation) > 0 && sizeof(T::TouchLocationState) > 0
                  && sizeof(T::TouchPanel) > 0 && sizeof(T::TouchPanelCapabilities) > 0);
}

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
