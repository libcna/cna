#pragma once

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace CNA::Internal::Input
{
    /// Raw gamepad values read directly from the SDL layer, before dead-zone processing.
    struct RawGamePadState
    {
        bool isConnected = false;
        Microsoft::Xna::Framework::Input::Buttons buttons =
            static_cast<Microsoft::Xna::Framework::Input::Buttons>(0);
        float leftX       = 0.0f;
        float leftY       = 0.0f;
        float rightX      = 0.0f;
        float rightY      = 0.0f;
        float leftTrigger  = 0.0f;
        float rightTrigger = 0.0f;
        int packetNumber   = 0;
    };

    /**
     * @brief Internal identification of supported mouse buttons.
     *
     * @note Status: IMPLEMENTED
     */
    enum class MouseButton
    {
        Left,
        Right,
        Middle,
        XButton1,
        XButton2,
    };

    /**
     * @brief Internal identification of supported gamepad buttons.
     *
     * @note Status: PARTIAL
     */
    enum class GamePadButton
    {
        A,
        B,
        X,
        Y,
        Back,
        Start,
        LeftShoulder,
        RightShoulder,
        LeftStick,
        RightStick,
        DPadUp,
        DPadDown,
        DPadLeft,
        DPadRight,
        BigButton,
        Misc1EXT,
        Paddle1EXT,
        Paddle2EXT,
        Paddle3EXT,
        Paddle4EXT,
        TouchPadEXT,
    };

    /**
     * @brief Internal identification of supported gamepad axes.
     *
     * @note Status: PARTIAL
     */
    enum class GamePadAxis
    {
        LeftThumbstickX,
        LeftThumbstickY,
        RightThumbstickX,
        RightThumbstickY,
        LeftTrigger,
        RightTrigger,
    };

    /**
     * @brief Internal CNA input state manager.
     *
     * Keeps current input state and provides snapshots for the public
     * XNA-like API.
     *
     * Currently supports Mouse, basic Keyboard, basic TouchPanel and basic GamePad state.
     *
     * Architecturally, this is event-driven rather than poll-driven: FNA's platform layer
     * (e.g. SDL3_FNAPlatform) re-queries SDL fresh on every `Get*State()` call, while this
     * class only accumulates whatever `SdlInputBridge::ProcessEvent` has pushed in via
     * `Set*State()`. State returned by the `Get*State()` methods here is only as current as
     * the last `Game::Tick()` (which unconditionally pumps SDL events once per frame, before
     * `Update()`/`Draw()` run — see `Game::PollEvents()`).
     *
     * @note Status: PARTIAL
     */
    class InputManager
    {
    public:
        /**
         * @brief Updates mouse cursor position.
         */
        static void SetMousePosition(int x, int y);

        /**
         * @brief Updates selected mouse button state.
         */
        static void SetMouseButtonState(
            MouseButton button,
            Microsoft::Xna::Framework::Input::ButtonState state
        );

        /**
         * @brief Adds mouse wheel delta to internal state.
         */
        static void AddScrollWheelDelta(int delta);

        /**
         * @brief Toggles FNA extension relative-mouse-mode accumulation and flushes
         * any pending relative delta (matches SDL3_FNAPlatform's flush-on-enable).
         */
        static void SetMouseRelativeMode(bool enabled);

        /**
         * @brief Accumulates a relative mouse motion delta. Only has an effect while
         * relative mode is enabled (see SetMouseRelativeMode); fed from every mouse
         * motion event regardless of mode, matching the SDL event stream.
         */
        static void AddMouseRelativeDelta(float dx, float dy);

        /**
         * @brief Sets pressed/released state for one keyboard key.
         */
        static void SetKeyState(Microsoft::Xna::Framework::Input::Keys key, bool pressed);

        /**
         * @brief Updates one touch point in the internal touch state.
         */
        static void SetTouchState(
            int touchId,
            Microsoft::Xna::Framework::Input::Touch::TouchLocationState state,
            const Microsoft::Xna::Framework::Vector2& position
        );

        /**
         * @brief Marks one gamepad player slot as connected/disconnected.
         */
        static void SetGamePadConnection(Microsoft::Xna::Framework::PlayerIndex playerIndex, bool isConnected);

        /**
         * @brief Updates selected gamepad button state.
         */
        static void SetGamePadButtonState(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            GamePadButton button,
            Microsoft::Xna::Framework::Input::ButtonState state
        );

        /**
         * @brief Updates selected gamepad axis/trigger value.
         */
        static void SetGamePadAxisValue(
            Microsoft::Xna::Framework::PlayerIndex playerIndex,
            GamePadAxis axis,
            float value
        );

        /**
         * @brief Returns a snapshot of current mouse state.
         */
        static Microsoft::Xna::Framework::Input::MouseState GetMouseState();

        /**
         * @brief Returns a snapshot of current keyboard state.
         */
        static Microsoft::Xna::Framework::Input::KeyboardState GetKeyboardState();

        /**
         * @brief Returns a snapshot of current touch state.
         */
        static Microsoft::Xna::Framework::Input::Touch::TouchCollection GetTouchState();

        /**
         * @brief Returns a snapshot of current gamepad state for one player.
         */
        static Microsoft::Xna::Framework::Input::GamePadState GetGamePadState(
            Microsoft::Xna::Framework::PlayerIndex playerIndex
        );

        /**
         * @brief Returns raw gamepad values (no dead-zone applied) for one player.
         *
         * Used by GamePad::GetState() to apply dead-zone processing at the XNA layer.
         */
        static RawGamePadState GetRawGamePadState(
            Microsoft::Xna::Framework::PlayerIndex playerIndex
        );
    };
}
