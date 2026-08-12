// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Internal/Input/GestureDetector.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef __ANDROID__
#include <SDL3/SDL.h>
#endif

namespace CNA::Internal::Input
{
    namespace
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        using Microsoft::Xna::Framework::Input::GamePadState;
        using Microsoft::Xna::Framework::PlayerIndex;
        using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

        struct InternalMouseState
        {
            int X = 0;
            int Y = 0;
            int ScrollWheelValue = 0;
            int HorizontalScrollWheelValue = 0; // CNAEXT/EXT — SDL wheel.x, surfaced via MouseState EXT
            ButtonState LeftButton = ButtonState::Released;
            ButtonState RightButton = ButtonState::Released;
            ButtonState MiddleButton = ButtonState::Released;
            ButtonState XButton1 = ButtonState::Released;
            ButtonState XButton2 = ButtonState::Released;

            // Legacy raw-bridge compatibility accumulator. Public Mouse uses
            // IPlatformMouse::ConsumeRelativeDelta after PLAT-80, but the SDL-shaped bridge tests
            // keep this state until PLAT-90 retires that adapter.
            bool RelativeMode = false;
            float RelativeDeltaX = 0.0f;
            float RelativeDeltaY = 0.0f;
        };

        using Microsoft::Xna::Framework::Input::Buttons;

        struct InternalGamePadState
        {
            bool IsConnected = false;
            Buttons Buttons_ = static_cast<Buttons>(0);

            float LeftThumbstickX  = 0.0f;
            float LeftThumbstickY  = 0.0f;
            float RightThumbstickX = 0.0f;
            float RightThumbstickY = 0.0f;
            float LeftTrigger      = 0.0f;
            float RightTrigger     = 0.0f;

            // FNA increments PacketNumber whenever a poll of the device's raw state
            // differs from the previous poll. CNA is event-driven rather than
            // poll-driven, so the equivalent is tracked here: bumped whenever a Set*
            // call below actually changes a stored value (connection, button, or axis).
            int PacketNumber = 0;
        };

        struct InternalTouchLocationState
        {
            int Id = 0;
            TouchLocationState State = TouchLocationState::Invalid;
            Microsoft::Xna::Framework::Vector2 Position = Microsoft::Xna::Framework::Vector2();
            bool RemoveAfterSnapshot = false;
            // Previous-frame location (what the last GetTouchState() reported), so a Moved/Released
            // touch exposes TryGetPreviousLocation() (task 868–870). Invalid = no previous yet.
            TouchLocationState PreviousState = TouchLocationState::Invalid;
            Microsoft::Xna::Framework::Vector2 PreviousPosition = Microsoft::Xna::Framework::Vector2();
            // CNAEXT/EXT: SDL finger pressure (0..1), surfaced via TouchLocation::getPressureEXT.
            float Pressure = 0.0f;
        };

        struct InternalInputState
        {
            InternalMouseState Mouse;
            std::array<InternalGamePadState, 4> GamePads;
            std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys;
            std::unordered_map<int, InternalTouchLocationState> TouchLocations;
        };

        std::optional<std::size_t> try_get_player_slot(const PlayerIndex playerIndex)
        {
            const int index = static_cast<int>(playerIndex);
            if (index < 0 || index >= 4)
            {
                return std::nullopt;
            }
            return static_cast<std::size_t>(index);
        }

        float clamp_signed_unit(const float value)
        {
            return std::clamp(value, -1.0f, 1.0f);
        }

        float clamp_positive_unit(const float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        InternalInputState& getInternalInputState()
        {
            static InternalInputState state{};
            return state;
        }
    }

    void InputManager::ResetForTests()
    {
        getInternalInputState() = InternalInputState{};
    }

    void InputManager::ResetAllForTests()
    {
        // Deterministic order: clear the bridge's file-static event bookkeeping first, then the
        // accumulated input singleton, then the higher-level panels/handlers. All entries are
        // independent process-wide statics, so ordering is for reproducibility, not correctness.
        SdlInputBridge::ResetForTests();
        ResetForTests();
        Microsoft::Xna::Framework::Input::Touch::TouchPanel::ResetForTests();
        GestureDetector::ResetForTests();
        Microsoft::Xna::Framework::Input::Mouse::ResetForTests();
        Microsoft::Xna::Framework::Input::TextInputEXT::ResetForTests();
    }

    void InputManager::SetMousePosition(const int x, const int y)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.X = x;
        mouseState.Y = y;
    }

    void InputManager::SetMouseButtonState(
        const MouseButton button,
        const Microsoft::Xna::Framework::Input::ButtonState state
    )
    {
        auto& mouseState = getInternalInputState().Mouse;
        switch (button)
        {
        case MouseButton::Left:
            mouseState.LeftButton = state;
            break;
        case MouseButton::Right:
            mouseState.RightButton = state;
            break;
        case MouseButton::Middle:
            mouseState.MiddleButton = state;
            break;
        case MouseButton::XButton1:
            mouseState.XButton1 = state;
            break;
        case MouseButton::XButton2:
            mouseState.XButton2 = state;
            break;
        }
    }

    void InputManager::AddScrollWheelDelta(const int delta)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.ScrollWheelValue += delta;
    }

    void InputManager::AddHorizontalScrollWheelDelta(const int delta)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.HorizontalScrollWheelValue += delta;
    }

    void InputManager::SetMouseRelativeMode(const bool enabled)
    {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.RelativeMode = enabled;
        // Flush stale accumulated motion on toggle, matching SDL3_FNAPlatform's
        // throwaway SDL_GetRelativeMouseState() call on enable.
        mouseState.RelativeDeltaX = 0.0f;
        mouseState.RelativeDeltaY = 0.0f;
    }

    void InputManager::AddMouseRelativeDelta(const float dx, const float dy)
    {
        auto& mouseState = getInternalInputState().Mouse;
        if (!mouseState.RelativeMode)
        {
            return;
        }
        mouseState.RelativeDeltaX += dx;
        mouseState.RelativeDeltaY += dy;
    }

    void InputManager::SetKeyState(
        const Microsoft::Xna::Framework::Input::Keys key,
        const bool pressed
    )
    {
        auto& pressedKeys = getInternalInputState().PressedKeys;
        if (pressed)
        {
            pressedKeys.insert(key);
            return;
        }
        pressedKeys.erase(key);
    }

    void InputManager::SetTouchState(
        const int touchId,
        const TouchLocationState state,
        const Microsoft::Xna::Framework::Vector2& position,
        const float pressure
    )
    {
        auto& touchLocations = getInternalInputState().TouchLocations;
        auto& touchLocation = touchLocations[touchId];
        touchLocation.Id = touchId;
        touchLocation.State = state;
        touchLocation.Position = position;
        touchLocation.Pressure = pressure;
        touchLocation.RemoveAfterSnapshot = state == TouchLocationState::Released;
    }

    void InputManager::SetGamePadConnection(const PlayerIndex playerIndex, const bool isConnected)
    {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value())
        {
            return;
        }

        auto& gamePadState = getInternalInputState().GamePads[slot.value()];
        if (isConnected)
        {
            if (!gamePadState.IsConnected)
            {
                gamePadState.PacketNumber += 1;
            }
            gamePadState.IsConnected = true;
            return;
        }

        gamePadState = InternalGamePadState();
    }

    void InputManager::SetGamePadButtonState(
        const PlayerIndex playerIndex,
        const GamePadButton button,
        const ButtonState state
    )
    {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value())
        {
            return;
        }

        auto& gamePadState = getInternalInputState().GamePads[slot.value()];
        const bool pressed = (state == Microsoft::Xna::Framework::Input::ButtonState::Pressed);

        auto setFlag = [&](Buttons flag) {
            const Buttons before = gamePadState.Buttons_;
            if (pressed)
                gamePadState.Buttons_ |= flag;
            else
                gamePadState.Buttons_ &= ~flag;
            if (gamePadState.Buttons_ != before)
                gamePadState.PacketNumber += 1;
        };

        switch (button)
        {
        case GamePadButton::A:            setFlag(Buttons::A);            break;
        case GamePadButton::B:            setFlag(Buttons::B);            break;
        case GamePadButton::X:            setFlag(Buttons::X);            break;
        case GamePadButton::Y:            setFlag(Buttons::Y);            break;
        case GamePadButton::Back:         setFlag(Buttons::Back);         break;
        case GamePadButton::Start:        setFlag(Buttons::Start);        break;
        case GamePadButton::LeftShoulder: setFlag(Buttons::LeftShoulder); break;
        case GamePadButton::RightShoulder:setFlag(Buttons::RightShoulder);break;
        case GamePadButton::LeftStick:    setFlag(Buttons::LeftStick);    break;
        case GamePadButton::RightStick:   setFlag(Buttons::RightStick);   break;
        case GamePadButton::DPadUp:       setFlag(Buttons::DPadUp);       break;
        case GamePadButton::DPadDown:     setFlag(Buttons::DPadDown);     break;
        case GamePadButton::DPadLeft:     setFlag(Buttons::DPadLeft);     break;
        case GamePadButton::DPadRight:    setFlag(Buttons::DPadRight);    break;
        case GamePadButton::BigButton:    setFlag(Buttons::BigButton);    break;
        case GamePadButton::Misc1EXT:     setFlag(Buttons::Misc1EXT);     break;
        case GamePadButton::Paddle1EXT:   setFlag(Buttons::Paddle1EXT);   break;
        case GamePadButton::Paddle2EXT:   setFlag(Buttons::Paddle2EXT);   break;
        case GamePadButton::Paddle3EXT:   setFlag(Buttons::Paddle3EXT);   break;
        case GamePadButton::Paddle4EXT:   setFlag(Buttons::Paddle4EXT);   break;
        case GamePadButton::TouchPadEXT:  setFlag(Buttons::TouchPadEXT);  break;
        }
    }

    void InputManager::SetGamePadAxisValue(
        const PlayerIndex playerIndex,
        const GamePadAxis axis,
        const float value
    )
    {
        const auto slot = try_get_player_slot(playerIndex);
        if (!slot.has_value())
        {
            return;
        }

        auto& gamePadState = getInternalInputState().GamePads[slot.value()];

        auto setAxis = [&](float& field, const float newValue) {
            if (newValue != field)
                gamePadState.PacketNumber += 1;
            field = newValue;
        };

        switch (axis)
        {
        case GamePadAxis::LeftThumbstickX:
            setAxis(gamePadState.LeftThumbstickX, clamp_signed_unit(value));
            break;
        case GamePadAxis::LeftThumbstickY:
            setAxis(gamePadState.LeftThumbstickY, clamp_signed_unit(value));
            break;
        case GamePadAxis::RightThumbstickX:
            setAxis(gamePadState.RightThumbstickX, clamp_signed_unit(value));
            break;
        case GamePadAxis::RightThumbstickY:
            setAxis(gamePadState.RightThumbstickY, clamp_signed_unit(value));
            break;
        case GamePadAxis::LeftTrigger:
            setAxis(gamePadState.LeftTrigger, clamp_positive_unit(value));
            break;
        case GamePadAxis::RightTrigger:
            setAxis(gamePadState.RightTrigger, clamp_positive_unit(value));
            break;
        }
    }

    Microsoft::Xna::Framework::Input::MouseState InputManager::GetMouseState()
    {
        using Microsoft::Xna::Framework::Input::ButtonState;
        auto& mouseState = getInternalInputState().Mouse;

        int x = mouseState.X;
        int y = mouseState.Y;
        if (mouseState.RelativeMode)
        {
            x = static_cast<int>(mouseState.RelativeDeltaX);
            y = static_cast<int>(mouseState.RelativeDeltaY);
            mouseState.RelativeDeltaX = 0.0f;
            mouseState.RelativeDeltaY = 0.0f;
        }

        return Microsoft::Xna::Framework::Input::MouseState(
            x,
            y,
            mouseState.ScrollWheelValue,
            mouseState.LeftButton,
            mouseState.MiddleButton,
            mouseState.RightButton,
            mouseState.XButton1,
            mouseState.XButton2,
            mouseState.HorizontalScrollWheelValue // CNAEXT/EXT 9th arg
        );
    }

    Microsoft::Xna::Framework::Input::KeyboardState InputManager::GetKeyboardState()
    {
        const auto& pressedKeys = getInternalInputState().PressedKeys;
#ifdef __ANDROID__
        if (!pressedKeys.empty())
        {
            std::string keyList;
            for (const auto k : pressedKeys)
            {
                keyList += std::to_string(static_cast<int>(k));
                keyList += ' ';
            }
            SDL_Log("[Keyboard] GetKeyboardState: pressed=%zu [%s]",
                    pressedKeys.size(), keyList.c_str());
        }
#endif
        return Microsoft::Xna::Framework::Input::KeyboardState(pressedKeys);
    }

    bool InputManager::HasAnyTouch()
    {
        return !getInternalInputState().TouchLocations.empty();
    }

    Microsoft::Xna::Framework::Input::Touch::TouchCollection InputManager::GetTouchState()
    {
        auto& touchLocations = getInternalInputState().TouchLocations;

        std::vector<int> sortedTouchIds;
        sortedTouchIds.reserve(touchLocations.size());
        for (const auto& [touchId, _] : touchLocations)
        {
            sortedTouchIds.push_back(touchId);
        }
        std::sort(sortedTouchIds.begin(), sortedTouchIds.end());

        std::vector<Microsoft::Xna::Framework::Input::Touch::TouchLocation> snapshot;
        snapshot.reserve(sortedTouchIds.size());

        for (const int touchId : sortedTouchIds)
        {
            const auto touchLocationIterator = touchLocations.find(touchId);
            if (touchLocationIterator == touchLocations.end())
            {
                continue;
            }

            const auto& touchLocation = touchLocationIterator->second;

            // Expose the previous-frame location for Moved/Released touches (Pressed/new touches
            // have no previous, matching FNA). Previous is "what AdvanceTouchFrame() last recorded".
            if (touchLocation.PreviousState != TouchLocationState::Invalid)
            {
                snapshot.emplace_back(touchLocation.Id, touchLocation.State, touchLocation.Position,
                                      touchLocation.PreviousState, touchLocation.PreviousPosition,
                                      touchLocation.Pressure);
            }
            else
            {
                snapshot.emplace_back(touchLocation.Id, touchLocation.State, touchLocation.Position,
                                      touchLocation.Pressure);
            }
        }

        return Microsoft::Xna::Framework::Input::Touch::TouchCollection(std::move(snapshot));
    }

    void InputManager::AdvanceTouchFrame()
    {
        auto& touchLocations = getInternalInputState().TouchLocations;

        std::vector<int> touchIdsToRemove;
        touchIdsToRemove.reserve(touchLocations.size());

        for (auto& [touchId, touchLocation] : touchLocations)
        {
            // Record the location just reported as "previous" for the next snapshot — done before
            // the Pressed→Moved promotion below, so a promoted touch's previous is the Pressed
            // location the game actually saw, not the promoted Moved state.
            touchLocation.PreviousState    = touchLocation.State;
            touchLocation.PreviousPosition = touchLocation.Position;

            if (touchLocation.RemoveAfterSnapshot)
            {
                touchIdsToRemove.push_back(touchId);
                continue;
            }

            if (touchLocation.State == TouchLocationState::Pressed)
            {
                touchLocation.State = TouchLocationState::Moved;
            }
        }

        for (const int touchId : touchIdsToRemove)
        {
            touchLocations.erase(touchId);
        }
    }


    RawGamePadState InputManager::GetRawGamePadState(const PlayerIndex playerIndex)
    {
        const auto slot = try_get_player_slot(playerIndex);
        RawGamePadState raw{};
        if (!slot.has_value())
            return raw;

        const auto& g = getInternalInputState().GamePads[slot.value()];
        raw.isConnected  = g.IsConnected;
        raw.buttons      = g.Buttons_;
        raw.leftX        = g.LeftThumbstickX;
        raw.leftY        = g.LeftThumbstickY;
        raw.rightX       = g.RightThumbstickX;
        raw.rightY       = g.RightThumbstickY;
        raw.leftTrigger  = g.LeftTrigger;
        raw.rightTrigger = g.RightTrigger;
        raw.packetNumber = g.PacketNumber;
        return raw;
    }
}
