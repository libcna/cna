// SPDX-License-Identifier: MS-PL
//
// INPUT-API-031 — public Input surface signature freeze (golden signature guard).
//
// This translation unit pins the EXACT signature of every public member of every public Input type.
// Each entry either takes the address of a member through a fully-spelled function-/member-pointer
// type, or asserts a constructor/operator/special-member property via a type trait. Consequences:
//   * removing or renaming a public member  -> compile error (the address-of has no target),
//   * changing a member's signature          -> compile error (the static_cast target no longer
//                                               matches the overload set / the trait flips),
//   * changing a constructor's parameters    -> the std::is_constructible_v static_assert fires.
//
// This is the enforced companion to the human-readable golden snapshot docs/input-public-api-frozen.md;
// the two are kept in lock-step. When a member is intentionally added/changed, update BOTH in the same
// commit. Strict-XNA, FNA EXT, and stable CNAEXT-convenience members are all frozen so the whole
// consumer-visible surface is drift-guarded (the doc marks which is which). Deliberately EXCLUDED are
// members that are internal plumbing rather than API: anything named INTERNAL_* or *ForTests, and
// private data members. Friend equality operators are frozen through an ADL expression (they are
// hidden friends, not visible to qualified lookup).
//
// Like PublicApiInputCompileTests, this TU includes ONLY public Input headers, so it doubles as a
// second standalone-compile / SDL-containment check of the public surface.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/MulticastAction.hpp"
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

// Defense-in-depth (INPUT-API-030 owns the primary check): no public Input header may leak SDL.
#if defined(SDL_MAJOR_VERSION) || defined(SDL_h_)
#error "A public Input header leaked SDL into the public API surface."
#endif

#include <gtest/gtest.h>

namespace
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Input;
    using namespace Microsoft::Xna::Framework::Input::Touch;

    // Freezes a hidden-friend equality pair (a == b / a != b), each yielding bool, via ADL.
    template <typename T>
    inline constexpr bool has_frozen_equality =
        std::is_same_v<decltype(std::declval<const T&>() == std::declval<const T&>()), bool> &&
        std::is_same_v<decltype(std::declval<const T&>() != std::declval<const T&>()), bool>;

    // ============================ GAMEPAD CLUSTER ============================

    // ----- GamePad (static class, XNA + EXT) -----
    static_assert(!std::is_default_constructible_v<GamePad>, "GamePad() = delete");
    [[maybe_unused]] constexpr auto z_GamePad_GetCapabilities_1 = static_cast<GamePadCapabilities(*)(PlayerIndex)>(&GamePad::GetCapabilities);
    [[maybe_unused]] constexpr auto z_GamePad_GetState_2 = static_cast<GamePadState(*)(PlayerIndex)>(&GamePad::GetState);
    [[maybe_unused]] constexpr auto z_GamePad_GetState_3 = static_cast<GamePadState(*)(PlayerIndex, GamePadDeadZone)>(&GamePad::GetState);
    [[maybe_unused]] constexpr auto z_GamePad_SetVibration_4 = static_cast<bool(*)(PlayerIndex, float, float)>(&GamePad::SetVibration);
    [[maybe_unused]] constexpr auto z_GamePad_GetGUIDEXT_5 = static_cast<std::string(*)(PlayerIndex)>(&GamePad::GetGUIDEXT);
    [[maybe_unused]] constexpr auto z_GamePad_SetLightBarEXT_6 = static_cast<void(*)(PlayerIndex, const Color&)>(&GamePad::SetLightBarEXT);
    [[maybe_unused]] constexpr auto z_GamePad_SetTriggerVibrationEXT_7 = static_cast<bool(*)(PlayerIndex, float, float)>(&GamePad::SetTriggerVibrationEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetGyroEXT_8 = static_cast<bool(*)(PlayerIndex, Vector3&)>(&GamePad::GetGyroEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetAccelerometerEXT_9 = static_cast<bool(*)(PlayerIndex, Vector3&)>(&GamePad::GetAccelerometerEXT);
    // CNAEXT/EXT (input_noxna.md N-009): SDL device player-index (LED) get/set.
    [[maybe_unused]] constexpr auto z_GamePad_GetPlayerIndexEXT = static_cast<int(*)(PlayerIndex)>(&GamePad::GetPlayerIndexEXT);
    [[maybe_unused]] constexpr auto z_GamePad_SetPlayerIndexEXT = static_cast<bool(*)(PlayerIndex, int)>(&GamePad::SetPlayerIndexEXT);
    // CNAEXT/EXT (input_noxna.md N-009b): SDL gamepad battery/charge state.
    [[maybe_unused]] constexpr auto z_GamePad_GetPowerInfoEXT = static_cast<CNA::Input::PowerStateEXT(*)(PlayerIndex, int&)>(&GamePad::GetPowerInfoEXT);
    // CNAEXT/EXT (input_noxna.md N-011): SDL gamepad face-button glyph label.
    [[maybe_unused]] constexpr auto z_GamePad_GetButtonLabelEXT = static_cast<CNA::Input::GamePadButtonLabelEXT(*)(PlayerIndex, Buttons)>(&GamePad::GetButtonLabelEXT);
    // CNAEXT/EXT (input_noxna.md N-010): SDL gamepad device metadata.
    [[maybe_unused]] constexpr auto z_GamePad_GetNameEXT = static_cast<std::string(*)(PlayerIndex)>(&GamePad::GetNameEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetPathEXT = static_cast<std::string(*)(PlayerIndex)>(&GamePad::GetPathEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetSerialEXT = static_cast<std::string(*)(PlayerIndex)>(&GamePad::GetSerialEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetFirmwareVersionEXT = static_cast<std::uint16_t(*)(PlayerIndex)>(&GamePad::GetFirmwareVersionEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetSteamHandleEXT = static_cast<std::uint64_t(*)(PlayerIndex)>(&GamePad::GetSteamHandleEXT);
    // CNAEXT/EXT (input_noxna.md N-010b): SDL gamepad wired/wireless connection state.
    [[maybe_unused]] constexpr auto z_GamePad_GetConnectionStateEXT = static_cast<CNA::Input::GamePadConnectionStateEXT(*)(PlayerIndex)>(&GamePad::GetConnectionStateEXT);
    // CNAEXT/EXT (input_noxna.md N-008): SDL gamepad touchpad fingers.
    [[maybe_unused]] constexpr auto z_GamePad_GetTouchpadCountEXT = static_cast<int(*)(PlayerIndex)>(&GamePad::GetTouchpadCountEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetTouchpadFingerCountEXT = static_cast<int(*)(PlayerIndex, int)>(&GamePad::GetTouchpadFingerCountEXT);
    [[maybe_unused]] constexpr auto z_GamePad_GetTouchpadFingerEXT = static_cast<bool(*)(PlayerIndex, int, int, bool&, float&, float&, float&)>(&GamePad::GetTouchpadFingerEXT);
    static_assert(std::is_same_v<decltype(GamePad::LeftDeadZone), const float>, "GamePad::LeftDeadZone : static constexpr float");
    static_assert(std::is_same_v<decltype(GamePad::RightDeadZone), const float>, "GamePad::RightDeadZone : static constexpr float");
    static_assert(std::is_same_v<decltype(GamePad::TriggerThreshold), const float>, "GamePad::TriggerThreshold : static constexpr float");
    [[maybe_unused]] constexpr auto z_GamePad_ExcludeAxisDeadZone_10 = static_cast<float(*)(float, float)>(&GamePad::ExcludeAxisDeadZone);

    // ----- GamePadState (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_GamePadState_getIsConnectedProperty_1 = static_cast<bool(GamePadState::*)() const>(&GamePadState::getIsConnectedProperty);
    [[maybe_unused]] constexpr auto z_GamePadState_getPacketNumberProperty_2 = static_cast<int(GamePadState::*)() const>(&GamePadState::getPacketNumberProperty);
    [[maybe_unused]] constexpr auto z_GamePadState_setPacketNumberProperty_3 = static_cast<void(GamePadState::*)(int)>(&GamePadState::setPacketNumberProperty);
    [[maybe_unused]] constexpr auto z_GamePadState_getButtonsProperty_4 = static_cast<const GamePadButtons&(GamePadState::*)() const>(&GamePadState::getButtonsProperty);
    [[maybe_unused]] constexpr auto z_GamePadState_getDPadProperty_5 = static_cast<const GamePadDPad&(GamePadState::*)() const>(&GamePadState::getDPadProperty);
    [[maybe_unused]] constexpr auto z_GamePadState_getThumbSticksProperty_6 = static_cast<const GamePadThumbSticks&(GamePadState::*)() const>(&GamePadState::getThumbSticksProperty);
    [[maybe_unused]] constexpr auto z_GamePadState_getTriggersProperty_7 = static_cast<const GamePadTriggers&(GamePadState::*)() const>(&GamePadState::getTriggersProperty);
    static_assert(std::is_default_constructible_v<GamePadState>, "GamePadState()");
    static_assert(std::is_constructible_v<GamePadState, const GamePadThumbSticks&, const GamePadTriggers&, const GamePadButtons&, const GamePadDPad&>, "GamePadState(thumbSticks, triggers, buttons, dPad)");
    static_assert(std::is_constructible_v<GamePadState, const Vector2&, const Vector2&, float, float, std::initializer_list<Buttons>>, "GamePadState(leftStick, rightStick, leftTrigger, rightTrigger, buttons)");
    [[maybe_unused]] constexpr auto z_GamePadState_IsButtonDown_8 = static_cast<bool(GamePadState::*)(Buttons) const>(&GamePadState::IsButtonDown);
    [[maybe_unused]] constexpr auto z_GamePadState_IsButtonUp_9 = static_cast<bool(GamePadState::*)(Buttons) const>(&GamePadState::IsButtonUp);
    [[maybe_unused]] constexpr auto z_GamePadState_Equals_10 = static_cast<bool(GamePadState::*)(const GamePadState&) const>(&GamePadState::Equals);
    [[maybe_unused]] constexpr auto z_GamePadState_GetHashCode_11 = static_cast<int(GamePadState::*)() const>(&GamePadState::GetHashCode);
    [[maybe_unused]] constexpr auto z_GamePadState_ToString_12 = static_cast<std::string(GamePadState::*)() const>(&GamePadState::ToString);
    static_assert(has_frozen_equality<GamePadState>, "GamePadState operator== / operator!= drift");

    // ----- GamePadButtons (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_GamePadButtons_getAProperty_1 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getAProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getBProperty_2 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getBProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getBackProperty_3 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getBackProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getXProperty_4 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getXProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getYProperty_5 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getYProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getStartProperty_6 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getStartProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getLeftShoulderProperty_7 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getLeftShoulderProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getLeftStickProperty_8 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getLeftStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getRightShoulderProperty_9 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getRightShoulderProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getRightStickProperty_10 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getRightStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadButtons_getBigButtonProperty_11 = static_cast<ButtonState(GamePadButtons::*)() const>(&GamePadButtons::getBigButtonProperty);
    static_assert(std::is_default_constructible_v<GamePadButtons>, "GamePadButtons()");
    static_assert(std::is_constructible_v<GamePadButtons, Buttons>, "explicit GamePadButtons(Buttons)");
    [[maybe_unused]] constexpr auto z_GamePadButtons_Equals_12 = static_cast<bool(GamePadButtons::*)(const GamePadButtons&) const>(&GamePadButtons::Equals);
    [[maybe_unused]] constexpr auto z_GamePadButtons_GetHashCode_13 = static_cast<int(GamePadButtons::*)() const>(&GamePadButtons::GetHashCode);
    static_assert(has_frozen_equality<GamePadButtons>, "GamePadButtons operator== / operator!= drift");
    [[maybe_unused]] constexpr auto z_GamePadButtons_FromButtonArray_14 = static_cast<GamePadButtons(*)(std::initializer_list<Buttons>)>(&GamePadButtons::FromButtonArray);

    // ----- GamePadDPad (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_GamePadDPad_getDownProperty_1 = static_cast<ButtonState(GamePadDPad::*)() const>(&GamePadDPad::getDownProperty);
    [[maybe_unused]] constexpr auto z_GamePadDPad_getLeftProperty_2 = static_cast<ButtonState(GamePadDPad::*)() const>(&GamePadDPad::getLeftProperty);
    [[maybe_unused]] constexpr auto z_GamePadDPad_getRightProperty_3 = static_cast<ButtonState(GamePadDPad::*)() const>(&GamePadDPad::getRightProperty);
    [[maybe_unused]] constexpr auto z_GamePadDPad_getUpProperty_4 = static_cast<ButtonState(GamePadDPad::*)() const>(&GamePadDPad::getUpProperty);
    static_assert(std::is_default_constructible_v<GamePadDPad>, "GamePadDPad()");
    static_assert(std::is_constructible_v<GamePadDPad, ButtonState, ButtonState, ButtonState, ButtonState>, "GamePadDPad(up, down, left, right)");
    [[maybe_unused]] constexpr auto z_GamePadDPad_FromButtonArray_5 = static_cast<GamePadDPad(*)(std::initializer_list<Buttons>)>(&GamePadDPad::FromButtonArray);
    [[maybe_unused]] constexpr auto z_GamePadDPad_Equals_6 = static_cast<bool(GamePadDPad::*)(const GamePadDPad&) const>(&GamePadDPad::Equals);
    [[maybe_unused]] constexpr auto z_GamePadDPad_GetHashCode_7 = static_cast<int(GamePadDPad::*)() const>(&GamePadDPad::GetHashCode);
    static_assert(has_frozen_equality<GamePadDPad>, "GamePadDPad operator== / operator!= drift");

    // ----- GamePadThumbSticks (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_GamePadThumbSticks_getLeftProperty_1 = static_cast<const Vector2&(GamePadThumbSticks::*)() const>(&GamePadThumbSticks::getLeftProperty);
    [[maybe_unused]] constexpr auto z_GamePadThumbSticks_getRightProperty_2 = static_cast<const Vector2&(GamePadThumbSticks::*)() const>(&GamePadThumbSticks::getRightProperty);
    static_assert(std::is_default_constructible_v<GamePadThumbSticks>, "GamePadThumbSticks()");
    static_assert(std::is_constructible_v<GamePadThumbSticks, const Vector2&, const Vector2&>, "GamePadThumbSticks(leftPosition, rightPosition)");
    [[maybe_unused]] constexpr auto z_GamePadThumbSticks_Equals_3 = static_cast<bool(GamePadThumbSticks::*)(const GamePadThumbSticks&) const>(&GamePadThumbSticks::Equals);
    [[maybe_unused]] constexpr auto z_GamePadThumbSticks_GetHashCode_4 = static_cast<int(GamePadThumbSticks::*)() const>(&GamePadThumbSticks::GetHashCode);
    static_assert(has_frozen_equality<GamePadThumbSticks>, "GamePadThumbSticks operator== / operator!= drift");

    // ----- GamePadTriggers (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_GamePadTriggers_getLeftProperty_1 = static_cast<float(GamePadTriggers::*)() const>(&GamePadTriggers::getLeftProperty);
    [[maybe_unused]] constexpr auto z_GamePadTriggers_getRightProperty_2 = static_cast<float(GamePadTriggers::*)() const>(&GamePadTriggers::getRightProperty);
    static_assert(std::is_default_constructible_v<GamePadTriggers>, "GamePadTriggers()");
    static_assert(std::is_constructible_v<GamePadTriggers, float, float>, "GamePadTriggers(leftTrigger, rightTrigger)");
    [[maybe_unused]] constexpr auto z_GamePadTriggers_Equals_3 = static_cast<bool(GamePadTriggers::*)(const GamePadTriggers&) const>(&GamePadTriggers::Equals);
    [[maybe_unused]] constexpr auto z_GamePadTriggers_GetHashCode_4 = static_cast<int(GamePadTriggers::*)() const>(&GamePadTriggers::GetHashCode);
    static_assert(has_frozen_equality<GamePadTriggers>, "GamePadTriggers operator== / operator!= drift");

    // ----- GamePadCapabilities (struct, XNA + EXT) -----
    static_assert(std::is_default_constructible_v<GamePadCapabilities>, "GamePadCapabilities() = default");
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getIsConnectedProperty_1 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getIsConnectedProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setIsConnectedProperty_2 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setIsConnectedProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasAButtonProperty_3 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasAButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasAButtonProperty_4 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasAButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasBackButtonProperty_5 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasBackButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasBackButtonProperty_6 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasBackButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasBButtonProperty_7 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasBButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasBButtonProperty_8 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasBButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasDPadDownButtonProperty_9 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasDPadDownButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasDPadDownButtonProperty_10 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasDPadDownButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasDPadLeftButtonProperty_11 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasDPadLeftButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasDPadLeftButtonProperty_12 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasDPadLeftButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasDPadRightButtonProperty_13 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasDPadRightButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasDPadRightButtonProperty_14 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasDPadRightButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasDPadUpButtonProperty_15 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasDPadUpButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasDPadUpButtonProperty_16 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasDPadUpButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLeftShoulderButtonProperty_17 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLeftShoulderButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLeftShoulderButtonProperty_18 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLeftShoulderButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLeftStickButtonProperty_19 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLeftStickButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLeftStickButtonProperty_20 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLeftStickButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasRightShoulderButtonProperty_21 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasRightShoulderButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasRightShoulderButtonProperty_22 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasRightShoulderButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasRightStickButtonProperty_23 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasRightStickButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasRightStickButtonProperty_24 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasRightStickButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasStartButtonProperty_25 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasStartButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasStartButtonProperty_26 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasStartButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasXButtonProperty_27 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasXButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasXButtonProperty_28 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasXButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasYButtonProperty_29 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasYButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasYButtonProperty_30 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasYButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasBigButtonProperty_31 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasBigButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasBigButtonProperty_32 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasBigButtonProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLeftXThumbStickProperty_33 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLeftXThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLeftXThumbStickProperty_34 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLeftXThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLeftYThumbStickProperty_35 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLeftYThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLeftYThumbStickProperty_36 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLeftYThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasRightXThumbStickProperty_37 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasRightXThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasRightXThumbStickProperty_38 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasRightXThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasRightYThumbStickProperty_39 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasRightYThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasRightYThumbStickProperty_40 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasRightYThumbStickProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLeftTriggerProperty_41 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLeftTriggerProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLeftTriggerProperty_42 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLeftTriggerProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasRightTriggerProperty_43 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasRightTriggerProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasRightTriggerProperty_44 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasRightTriggerProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLeftVibrationMotorProperty_45 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLeftVibrationMotorProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLeftVibrationMotorProperty_46 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLeftVibrationMotorProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasRightVibrationMotorProperty_47 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasRightVibrationMotorProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasRightVibrationMotorProperty_48 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasRightVibrationMotorProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasVoiceSupportProperty_49 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasVoiceSupportProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasVoiceSupportProperty_50 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasVoiceSupportProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getGamePadTypeProperty_51 = static_cast<GamePadType(GamePadCapabilities::*)() const>(&GamePadCapabilities::getGamePadTypeProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setGamePadTypeProperty_52 = static_cast<void(GamePadCapabilities::*)(GamePadType)>(&GamePadCapabilities::setGamePadTypeProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasLightBarEXTProperty_53 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasLightBarEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasLightBarEXTProperty_54 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasLightBarEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasTriggerVibrationMotorsEXTProperty_55 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasTriggerVibrationMotorsEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasTriggerVibrationMotorsEXTProperty_56 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasTriggerVibrationMotorsEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasMisc1EXTProperty_57 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasMisc1EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasMisc1EXTProperty_58 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasMisc1EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasPaddle1EXTProperty_59 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasPaddle1EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasPaddle1EXTProperty_60 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasPaddle1EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasPaddle2EXTProperty_61 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasPaddle2EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasPaddle2EXTProperty_62 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasPaddle2EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasPaddle3EXTProperty_63 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasPaddle3EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasPaddle3EXTProperty_64 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasPaddle3EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasPaddle4EXTProperty_65 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasPaddle4EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasPaddle4EXTProperty_66 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasPaddle4EXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasTouchPadEXTProperty_67 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasTouchPadEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasTouchPadEXTProperty_68 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasTouchPadEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasGyroEXTProperty_69 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasGyroEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasGyroEXTProperty_70 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasGyroEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_getHasAccelerometerEXTProperty_71 = static_cast<bool(GamePadCapabilities::*)() const>(&GamePadCapabilities::getHasAccelerometerEXTProperty);
    [[maybe_unused]] constexpr auto z_GamePadCapabilities_setHasAccelerometerEXTProperty_72 = static_cast<void(GamePadCapabilities::*)(bool)>(&GamePadCapabilities::setHasAccelerometerEXTProperty);

    // ======================= KEYBOARD / MOUSE CLUSTER =======================

    // ----- Keyboard (static class, XNA + EXT) -----
    static_assert(!std::is_default_constructible_v<Keyboard>, "Keyboard() = delete");
    [[maybe_unused]] constexpr auto z_Keyboard_GetState_1 = static_cast<KeyboardState(*)()>(&Keyboard::GetState);
    [[maybe_unused]] constexpr auto z_Keyboard_GetState_2 = static_cast<KeyboardState(*)(PlayerIndex)>(&Keyboard::GetState);
    [[maybe_unused]] constexpr auto z_Keyboard_GetKeyFromScancodeEXT_3 = static_cast<Keys(*)(Keys)>(&Keyboard::GetKeyFromScancodeEXT);
    // CNAEXT/EXT (input_noxna.md N-003): active keyboard modifier/lock state.
    [[maybe_unused]] constexpr auto z_Keyboard_GetModStateEXT = static_cast<CNA::Input::KeyModifiersEXT(*)()>(&Keyboard::GetModStateEXT);
    // CNAEXT/EXT (input_noxna.md N-002): physical key-name helpers.
    [[maybe_unused]] constexpr auto z_Keyboard_GetScancodeNameEXT = static_cast<std::string(*)(Keys)>(&Keyboard::GetScancodeNameEXT);
    [[maybe_unused]] constexpr auto z_Keyboard_GetScancodeFromNameEXT = static_cast<Keys(*)(const std::string&)>(&Keyboard::GetScancodeFromNameEXT);
    // CNAEXT/EXT (input_noxna.md N-002b): layout-dependent key-name helpers.
    [[maybe_unused]] constexpr auto z_Keyboard_GetKeyNameEXT = static_cast<std::string(*)(Keys)>(&Keyboard::GetKeyNameEXT);
    [[maybe_unused]] constexpr auto z_Keyboard_GetKeyFromNameEXT = static_cast<Keys(*)(const std::string&)>(&Keyboard::GetKeyFromNameEXT);

    // ----- KeyboardState (struct, XNA) -----
    static_assert(std::is_default_constructible_v<KeyboardState>, "KeyboardState()");
    static_assert(std::is_constructible_v<KeyboardState, std::initializer_list<Keys>>, "KeyboardState(std::initializer_list<Keys>)");
    static_assert(std::is_constructible_v<KeyboardState, const std::unordered_set<Keys>&>, "KeyboardState(const std::unordered_set<Keys>&)");
    [[maybe_unused]] constexpr auto z_KeyboardState_getItem_1 = static_cast<KeyState(KeyboardState::*)(Keys) const>(&KeyboardState::getItem);
    [[maybe_unused]] constexpr auto z_KeyboardState_operatorIndex_2 = static_cast<KeyState(KeyboardState::*)(Keys) const>(&KeyboardState::operator[]);
    [[maybe_unused]] constexpr auto z_KeyboardState_IsKeyDown_3 = static_cast<bool(KeyboardState::*)(Keys) const>(&KeyboardState::IsKeyDown);
    [[maybe_unused]] constexpr auto z_KeyboardState_IsKeyUp_4 = static_cast<bool(KeyboardState::*)(Keys) const>(&KeyboardState::IsKeyUp);
    [[maybe_unused]] constexpr auto z_KeyboardState_GetPressedKeys_5 = static_cast<std::vector<Keys>(KeyboardState::*)() const>(&KeyboardState::GetPressedKeys);
    [[maybe_unused]] constexpr auto z_KeyboardState_Equals_6 = static_cast<bool(KeyboardState::*)(const KeyboardState&) const>(&KeyboardState::Equals);
    [[maybe_unused]] constexpr auto z_KeyboardState_GetHashCode_7 = static_cast<int(KeyboardState::*)() const>(&KeyboardState::GetHashCode);
    [[maybe_unused]] constexpr auto z_KeyboardState_ToString_8 = static_cast<std::string(KeyboardState::*)() const>(&KeyboardState::ToString);
    static_assert(has_frozen_equality<KeyboardState>, "KeyboardState operator== / operator!= drift");

    // ----- Mouse (static class, XNA + EXT + CNAEXT) -----
    static_assert(!std::is_default_constructible_v<Mouse>, "Mouse() = delete");
    [[maybe_unused]] constexpr auto z_Mouse_getWindowHandleProperty_1 = static_cast<std::uintptr_t(*)()>(&Mouse::getWindowHandleProperty);
    [[maybe_unused]] constexpr auto z_Mouse_setWindowHandleProperty_2 = static_cast<void(*)(std::uintptr_t)>(&Mouse::setWindowHandleProperty);
    [[maybe_unused]] constexpr auto z_Mouse_GetState_3 = static_cast<MouseState(*)()>(&Mouse::GetState);
    [[maybe_unused]] constexpr auto z_Mouse_SetPosition_4 = static_cast<void(*)(int, int)>(&Mouse::SetPosition);
    [[maybe_unused]] constexpr auto z_Mouse_SetCursor_5 = static_cast<void(*)(MouseCursor&)>(&Mouse::SetCursor);
    [[maybe_unused]] constexpr auto z_Mouse_ClickedEXT = &Mouse::ClickedEXT; // static System::MulticastAction<int>
    [[maybe_unused]] constexpr auto z_Mouse_getIsRelativeMouseModeEXTProperty_6 = static_cast<bool(*)()>(&Mouse::getIsRelativeMouseModeEXTProperty);
    [[maybe_unused]] constexpr auto z_Mouse_setIsRelativeMouseModeEXTProperty_7 = static_cast<void(*)(bool)>(&Mouse::setIsRelativeMouseModeEXTProperty);
    // CNAEXT/EXT (input_noxna.md N-016): mouse capture + global position/warp.
    [[maybe_unused]] constexpr auto z_Mouse_SetCaptureEXT = static_cast<bool(*)(bool)>(&Mouse::SetCaptureEXT);
    [[maybe_unused]] constexpr auto z_Mouse_GetGlobalPositionEXT = static_cast<void(*)(int&, int&)>(&Mouse::GetGlobalPositionEXT);
    [[maybe_unused]] constexpr auto z_Mouse_WarpGlobalEXT = static_cast<bool(*)(int, int)>(&Mouse::WarpGlobalEXT);

    // ----- MouseState (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_MouseState_getXProperty_1 = static_cast<int(MouseState::*)() const>(&MouseState::getXProperty);
    [[maybe_unused]] constexpr auto z_MouseState_getYProperty_2 = static_cast<int(MouseState::*)() const>(&MouseState::getYProperty);
    [[maybe_unused]] constexpr auto z_MouseState_getLeftButtonProperty_3 = static_cast<ButtonState(MouseState::*)() const>(&MouseState::getLeftButtonProperty);
    [[maybe_unused]] constexpr auto z_MouseState_getRightButtonProperty_4 = static_cast<ButtonState(MouseState::*)() const>(&MouseState::getRightButtonProperty);
    [[maybe_unused]] constexpr auto z_MouseState_getMiddleButtonProperty_5 = static_cast<ButtonState(MouseState::*)() const>(&MouseState::getMiddleButtonProperty);
    [[maybe_unused]] constexpr auto z_MouseState_getXButton1Property_6 = static_cast<ButtonState(MouseState::*)() const>(&MouseState::getXButton1Property);
    [[maybe_unused]] constexpr auto z_MouseState_getXButton2Property_7 = static_cast<ButtonState(MouseState::*)() const>(&MouseState::getXButton2Property);
    [[maybe_unused]] constexpr auto z_MouseState_getScrollWheelValueProperty_8 = static_cast<int(MouseState::*)() const>(&MouseState::getScrollWheelValueProperty);
    // CNAEXT/EXT (input_noxna.md N-005): horizontal scroll wheel getter + the 9-arg ctor that populates it.
    [[maybe_unused]] constexpr auto z_MouseState_getHorizontalScrollWheelValueEXTProperty = static_cast<int(MouseState::*)() const>(&MouseState::getHorizontalScrollWheelValueEXTProperty);
    static_assert(std::is_default_constructible_v<MouseState>, "MouseState()");
    static_assert(std::is_constructible_v<MouseState, int, int, int, ButtonState, ButtonState, ButtonState, ButtonState, ButtonState>, "MouseState(x, y, scrollWheel, left, middle, right, x1, x2)");
    static_assert(std::is_constructible_v<MouseState, int, int, int, ButtonState, ButtonState, ButtonState, ButtonState, ButtonState, int>, "MouseState(x, y, scrollWheel, left, middle, right, x1, x2, horizontalScrollWheel) [CNAEXT/EXT]");
    [[maybe_unused]] constexpr auto z_MouseState_Equals_9 = static_cast<bool(MouseState::*)(const MouseState&) const>(&MouseState::Equals);
    [[maybe_unused]] constexpr auto z_MouseState_GetHashCode_10 = static_cast<int(MouseState::*)() const>(&MouseState::GetHashCode);
    [[maybe_unused]] constexpr auto z_MouseState_ToString_11 = static_cast<std::string(MouseState::*)() const>(&MouseState::ToString);
    static_assert(has_frozen_equality<MouseState>, "MouseState operator== / operator!= drift");

    // ----- MouseCursor (class, CNA/CNAEXT) -----
    static_assert(std::is_default_constructible_v<MouseCursor>, "MouseCursor()");
    [[maybe_unused]] constexpr auto z_MouseCursor_FromTexture2D_1 = static_cast<MouseCursor(*)(const Graphics::Texture2D&, int, int)>(&MouseCursor::FromTexture2D);
    static_assert(!std::is_copy_constructible_v<MouseCursor>, "MouseCursor(const MouseCursor&) = delete");
    static_assert(!std::is_copy_assignable_v<MouseCursor>, "MouseCursor& operator=(const MouseCursor&) = delete");
    static_assert(std::is_move_constructible_v<MouseCursor>, "MouseCursor(MouseCursor&&) noexcept");
    static_assert(std::is_move_assignable_v<MouseCursor>, "MouseCursor& operator=(MouseCursor&&) noexcept");
    static_assert(std::is_destructible_v<MouseCursor>, "~MouseCursor()");
    [[maybe_unused]] constexpr auto z_MouseCursor_Dispose_2 = static_cast<void(MouseCursor::*)()>(&MouseCursor::Dispose);
    [[maybe_unused]] constexpr auto z_MouseCursor_getArrowProperty_4 = static_cast<MouseCursor&(*)()>(&MouseCursor::getArrowProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getCrosshairProperty_5 = static_cast<MouseCursor&(*)()>(&MouseCursor::getCrosshairProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getHandProperty_6 = static_cast<MouseCursor&(*)()>(&MouseCursor::getHandProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getIBeamProperty_7 = static_cast<MouseCursor&(*)()>(&MouseCursor::getIBeamProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getNoProperty_8 = static_cast<MouseCursor&(*)()>(&MouseCursor::getNoProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getSizeAllProperty_9 = static_cast<MouseCursor&(*)()>(&MouseCursor::getSizeAllProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getSizeNESWProperty_10 = static_cast<MouseCursor&(*)()>(&MouseCursor::getSizeNESWProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getSizeNSProperty_11 = static_cast<MouseCursor&(*)()>(&MouseCursor::getSizeNSProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getSizeNWSEProperty_12 = static_cast<MouseCursor&(*)()>(&MouseCursor::getSizeNWSEProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getSizeWEProperty_13 = static_cast<MouseCursor&(*)()>(&MouseCursor::getSizeWEProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getWaitProperty_14 = static_cast<MouseCursor&(*)()>(&MouseCursor::getWaitProperty);
    [[maybe_unused]] constexpr auto z_MouseCursor_getWaitArrowProperty_15 = static_cast<MouseCursor&(*)()>(&MouseCursor::getWaitArrowProperty);

    // ----- TextInputEXT (static class, FNA EXT) -----
    static_assert(!std::is_default_constructible_v<TextInputEXT>, "TextInputEXT() = delete");
    [[maybe_unused]] constexpr auto z_TextInputEXT_TextInput = &TextInputEXT::TextInput;   // static System::MulticastAction<charcs>
    [[maybe_unused]] constexpr auto z_TextInputEXT_TextEditing = &TextInputEXT::TextEditing; // static System::MulticastAction<const std::string&, int, int>
    // CNAEXT/EXT (input_noxna.md N-014): IME candidate-list event.
    [[maybe_unused]] constexpr auto z_TextInputEXT_TextEditingCandidatesEXT = &TextInputEXT::TextEditingCandidatesEXT; // static MulticastAction<const std::vector<std::string>&, int, bool>
    [[maybe_unused]] constexpr auto z_TextInputEXT_getWindowHandleProperty_1 = static_cast<std::uintptr_t(*)()>(&TextInputEXT::getWindowHandleProperty);
    [[maybe_unused]] constexpr auto z_TextInputEXT_setWindowHandleProperty_2 = static_cast<void(*)(std::uintptr_t)>(&TextInputEXT::setWindowHandleProperty);
    [[maybe_unused]] constexpr auto z_TextInputEXT_IsTextInputActive_3 = static_cast<bool(*)()>(&TextInputEXT::IsTextInputActive);
    [[maybe_unused]] constexpr auto z_TextInputEXT_IsScreenKeyboardShown_4 = static_cast<bool(*)()>(&TextInputEXT::IsScreenKeyboardShown);
    [[maybe_unused]] constexpr auto z_TextInputEXT_IsScreenKeyboardShown_5 = static_cast<bool(*)(std::uintptr_t)>(&TextInputEXT::IsScreenKeyboardShown);
    [[maybe_unused]] constexpr auto z_TextInputEXT_StartTextInput_6 = static_cast<void(*)()>(&TextInputEXT::StartTextInput);
    [[maybe_unused]] constexpr auto z_TextInputEXT_StopTextInput_7 = static_cast<void(*)()>(&TextInputEXT::StopTextInput);
    [[maybe_unused]] constexpr auto z_TextInputEXT_SetInputRectangle_8 = static_cast<void(*)(const Rectangle&)>(&TextInputEXT::SetInputRectangle);
    // CNAEXT/EXT (input_noxna.md N-014b): input-type hint for the on-screen keyboard / IME.
    [[maybe_unused]] constexpr auto z_TextInputEXT_StartTextInputWithTypeEXT_9 = static_cast<void(*)(CNA::Input::TextInputTypeEXT)>(&TextInputEXT::StartTextInputWithTypeEXT);

    // ============================= TOUCH CLUSTER =============================

    // ----- TouchPanel (static class, XNA + CNAEXT) -----
    static_assert(!std::is_default_constructible_v<TouchPanel>, "TouchPanel() = delete");
    static_assert(std::is_same_v<std::remove_cv_t<decltype(TouchPanel::MAX_TOUCHES)>, intcs>, "TouchPanel::MAX_TOUCHES : static constexpr intcs");
    static_assert(std::is_same_v<std::remove_cv_t<decltype(TouchPanel::NO_FINGER)>, intcs>, "TouchPanel::NO_FINGER : static constexpr intcs");
    [[maybe_unused]] constexpr auto z_TouchPanel_getDisplayWidthProperty_1 = static_cast<intcs(*)()>(&TouchPanel::getDisplayWidthProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_setDisplayWidthProperty_2 = static_cast<void(*)(intcs)>(&TouchPanel::setDisplayWidthProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_getDisplayHeightProperty_3 = static_cast<intcs(*)()>(&TouchPanel::getDisplayHeightProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_setDisplayHeightProperty_4 = static_cast<void(*)(intcs)>(&TouchPanel::setDisplayHeightProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_getDisplayOrientationProperty_5 = static_cast<DisplayOrientation(*)()>(&TouchPanel::getDisplayOrientationProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_setDisplayOrientationProperty_6 = static_cast<void(*)(DisplayOrientation)>(&TouchPanel::setDisplayOrientationProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_getEnabledGesturesProperty_7 = static_cast<GestureType(*)()>(&TouchPanel::getEnabledGesturesProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_setEnabledGesturesProperty_8 = static_cast<void(*)(GestureType)>(&TouchPanel::setEnabledGesturesProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_getIsGestureAvailableProperty_9 = static_cast<bool(*)()>(&TouchPanel::getIsGestureAvailableProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_getWindowHandleProperty_10 = static_cast<std::uintptr_t(*)()>(&TouchPanel::getWindowHandleProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_setWindowHandleProperty_11 = static_cast<void(*)(std::uintptr_t)>(&TouchPanel::setWindowHandleProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_getTouchDeviceExistsProperty_12 = static_cast<bool(*)()>(&TouchPanel::getTouchDeviceExistsProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_setTouchDeviceExistsProperty_13 = static_cast<void(*)(bool)>(&TouchPanel::setTouchDeviceExistsProperty);
    [[maybe_unused]] constexpr auto z_TouchPanel_GetCapabilities_14 = static_cast<TouchPanelCapabilities(*)()>(&TouchPanel::GetCapabilities);
    [[maybe_unused]] constexpr auto z_TouchPanel_GetState_15 = static_cast<TouchCollection(*)()>(&TouchPanel::GetState);
    [[maybe_unused]] constexpr auto z_TouchPanel_ReadGesture_16 = static_cast<GestureSample(*)()>(&TouchPanel::ReadGesture);
    [[maybe_unused]] constexpr auto z_TouchPanel_EnqueueGesture_17 = static_cast<void(*)(const GestureSample&)>(&TouchPanel::EnqueueGesture);
    [[maybe_unused]] constexpr auto z_TouchPanel_SetFinger_18 = static_cast<void(*)(intcs, intcs, const Vector2&)>(&TouchPanel::SetFinger);
    [[maybe_unused]] constexpr auto z_TouchPanel_Update_19 = static_cast<void(*)()>(&TouchPanel::Update);

    // ----- TouchCollection (struct, XNA IList) -----
    [[maybe_unused]] constexpr auto z_TouchCollection_getCountProperty_1 = static_cast<int(TouchCollection::*)() const>(&TouchCollection::getCountProperty);
    [[maybe_unused]] constexpr auto z_TouchCollection_getIsConnectedProperty_2 = static_cast<bool(TouchCollection::*)() const>(&TouchCollection::getIsConnectedProperty);
    [[maybe_unused]] constexpr auto z_TouchCollection_getIsReadOnlyProperty_3 = static_cast<bool(TouchCollection::*)() const>(&TouchCollection::getIsReadOnlyProperty);
    static_assert(std::is_default_constructible_v<TouchCollection>, "TouchCollection()");
    static_assert(std::is_constructible_v<TouchCollection, const std::vector<TouchLocation>&>, "TouchCollection(const std::vector<TouchLocation>&)");
    static_assert(std::is_constructible_v<TouchCollection, std::vector<TouchLocation>&&>, "TouchCollection(std::vector<TouchLocation>&&)");
    [[maybe_unused]] constexpr auto z_TouchCollection_idx_mut = static_cast<TouchLocation&(TouchCollection::*)(std::size_t)>(&TouchCollection::operator[]);
    [[maybe_unused]] constexpr auto z_TouchCollection_idx = static_cast<const TouchLocation&(TouchCollection::*)(std::size_t) const>(&TouchCollection::operator[]);
    [[maybe_unused]] constexpr auto z_TouchCollection_empty_4 = static_cast<bool(TouchCollection::*)() const>(&TouchCollection::empty);
    [[maybe_unused]] constexpr auto z_TouchCollection_Contains_5 = static_cast<bool(TouchCollection::*)(const TouchLocation&) const>(&TouchCollection::Contains);
    [[maybe_unused]] constexpr auto z_TouchCollection_FindById_6 = static_cast<bool(TouchCollection::*)(int, TouchLocation&) const>(&TouchCollection::FindById);
    [[maybe_unused]] constexpr auto z_TouchCollection_CopyTo_7 = static_cast<void(TouchCollection::*)(std::vector<TouchLocation>&, int) const>(&TouchCollection::CopyTo);
    [[maybe_unused]] constexpr auto z_TouchCollection_IndexOf_8 = static_cast<int(TouchCollection::*)(const TouchLocation&) const>(&TouchCollection::IndexOf);
    [[maybe_unused]] constexpr auto z_TouchCollection_Add_9 = static_cast<void(TouchCollection::*)(const TouchLocation&)>(&TouchCollection::Add);
    [[maybe_unused]] constexpr auto z_TouchCollection_Clear_10 = static_cast<void(TouchCollection::*)()>(&TouchCollection::Clear);
    [[maybe_unused]] constexpr auto z_TouchCollection_Remove_11 = static_cast<bool(TouchCollection::*)(const TouchLocation&)>(&TouchCollection::Remove);
    [[maybe_unused]] constexpr auto z_TouchCollection_RemoveAt_12 = static_cast<void(TouchCollection::*)(int)>(&TouchCollection::RemoveAt);
    [[maybe_unused]] constexpr auto z_TouchCollection_Insert_13 = static_cast<void(TouchCollection::*)(int, const TouchLocation&)>(&TouchCollection::Insert);
    [[maybe_unused]] constexpr auto z_TouchCollection_begin_14 = static_cast<std::vector<TouchLocation>::iterator(TouchCollection::*)()>(&TouchCollection::begin);
    [[maybe_unused]] constexpr auto z_TouchCollection_end_15 = static_cast<std::vector<TouchLocation>::iterator(TouchCollection::*)()>(&TouchCollection::end);
    [[maybe_unused]] constexpr auto z_TouchCollection_begin_16 = static_cast<std::vector<TouchLocation>::const_iterator(TouchCollection::*)() const>(&TouchCollection::begin);
    [[maybe_unused]] constexpr auto z_TouchCollection_end_17 = static_cast<std::vector<TouchLocation>::const_iterator(TouchCollection::*)() const>(&TouchCollection::end);

    // ----- TouchLocation (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_TouchLocation_getIdProperty_1 = static_cast<int(TouchLocation::*)() const>(&TouchLocation::getIdProperty);
    [[maybe_unused]] constexpr auto z_TouchLocation_getStateProperty_2 = static_cast<TouchLocationState(TouchLocation::*)() const>(&TouchLocation::getStateProperty);
    [[maybe_unused]] constexpr auto z_TouchLocation_getPositionProperty_3 = static_cast<const Vector2&(TouchLocation::*)() const>(&TouchLocation::getPositionProperty);
    // CNAEXT/EXT (input_noxna.md N-006): SDL finger pressure getter + pressure-carrying constructors.
    [[maybe_unused]] constexpr auto z_TouchLocation_getPressureEXT = static_cast<float(TouchLocation::*)() const>(&TouchLocation::getPressureEXT);
    static_assert(std::is_constructible_v<TouchLocation, int, TouchLocationState, const Vector2&, float>, "TouchLocation(id, state, position, pressure)");
    static_assert(std::is_constructible_v<TouchLocation, int, TouchLocationState, const Vector2&, TouchLocationState, const Vector2&, float>, "TouchLocation(id, state, position, previousState, previousPosition, pressure)");
    static_assert(std::is_default_constructible_v<TouchLocation>, "TouchLocation()");
    static_assert(std::is_constructible_v<TouchLocation, int, TouchLocationState, const Vector2&>, "TouchLocation(id, state, position)");
    static_assert(std::is_constructible_v<TouchLocation, int, TouchLocationState, const Vector2&, TouchLocationState, const Vector2&>, "TouchLocation(id, state, position, previousState, previousPosition)");
    [[maybe_unused]] constexpr auto z_TouchLocation_TryGetPreviousLocation_4 = static_cast<bool(TouchLocation::*)(TouchLocation&) const>(&TouchLocation::TryGetPreviousLocation);
    [[maybe_unused]] constexpr auto z_TouchLocation_Equals_5 = static_cast<bool(TouchLocation::*)(const TouchLocation&) const>(&TouchLocation::Equals);
    [[maybe_unused]] constexpr auto z_TouchLocation_GetHashCode_6 = static_cast<int(TouchLocation::*)() const>(&TouchLocation::GetHashCode);
    [[maybe_unused]] constexpr auto z_TouchLocation_ToString_7 = static_cast<std::string(TouchLocation::*)() const>(&TouchLocation::ToString);
    static_assert(has_frozen_equality<TouchLocation>, "TouchLocation operator== / operator!= drift");

    // ----- TouchPanelCapabilities (struct, XNA) -----
    [[maybe_unused]] constexpr auto z_TouchPanelCapabilities_getIsConnectedProperty_1 = static_cast<bool(TouchPanelCapabilities::*)() const>(&TouchPanelCapabilities::getIsConnectedProperty);
    [[maybe_unused]] constexpr auto z_TouchPanelCapabilities_getMaximumTouchCountProperty_2 = static_cast<int(TouchPanelCapabilities::*)() const>(&TouchPanelCapabilities::getMaximumTouchCountProperty);
    static_assert(std::is_default_constructible_v<TouchPanelCapabilities>, "TouchPanelCapabilities()");
    static_assert(std::is_constructible_v<TouchPanelCapabilities, bool, int>, "TouchPanelCapabilities(bool, int)");

    // ----- GestureSample (struct, XNA + EXT) -----
    [[maybe_unused]] constexpr auto z_GestureSample_getGestureTypeProperty_1 = static_cast<GestureType(GestureSample::*)() const>(&GestureSample::getGestureTypeProperty);
    [[maybe_unused]] constexpr auto z_GestureSample_getTimestampProperty_2 = static_cast<System::TimeSpan(GestureSample::*)() const>(&GestureSample::getTimestampProperty);
    [[maybe_unused]] constexpr auto z_GestureSample_getPositionProperty_3 = static_cast<const Vector2&(GestureSample::*)() const>(&GestureSample::getPositionProperty);
    [[maybe_unused]] constexpr auto z_GestureSample_getPosition2Property_4 = static_cast<const Vector2&(GestureSample::*)() const>(&GestureSample::getPosition2Property);
    [[maybe_unused]] constexpr auto z_GestureSample_getDeltaProperty_5 = static_cast<const Vector2&(GestureSample::*)() const>(&GestureSample::getDeltaProperty);
    [[maybe_unused]] constexpr auto z_GestureSample_getDelta2Property_6 = static_cast<const Vector2&(GestureSample::*)() const>(&GestureSample::getDelta2Property);
    [[maybe_unused]] constexpr auto z_GestureSample_getFingerIdEXTProperty_7 = static_cast<int(GestureSample::*)() const>(&GestureSample::getFingerIdEXTProperty);
    [[maybe_unused]] constexpr auto z_GestureSample_getFingerId2EXTProperty_8 = static_cast<int(GestureSample::*)() const>(&GestureSample::getFingerId2EXTProperty);
    static_assert(std::is_default_constructible_v<GestureSample>, "GestureSample()");
    static_assert(std::is_constructible_v<GestureSample, GestureType, System::TimeSpan, Vector2, Vector2, Vector2, Vector2>, "GestureSample(type, timestamp, position, position2, delta, delta2)");
    static_assert(std::is_constructible_v<GestureSample, GestureType, System::TimeSpan, Vector2, Vector2, Vector2, Vector2, int, int>, "GestureSample(type, timestamp, position, position2, delta, delta2, fingerId, fingerId2)");
}

// The freeze is entirely compile-time (the entries above). This trivial runtime test exists only so
// the TU is linked into CnaTests and its compilation is exercised by every build / CI run.
TEST(PublicApiInputSignatureFreezeTest, PublicSignaturesAreFrozen)
{
    SUCCEED();
}
