// SPDX-License-Identifier: MS-PL
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
     * Keeps event-accumulated compatibility state for the legacy raw bridge plus the gamepad and
     * touch paths that have not yet completed their platform-service migrations. Public keyboard
     * and mouse state read `IPlatformKeyboard`/`IPlatformMouse` instead.
     *
     * Currently stores Mouse, basic Keyboard, basic TouchPanel and basic GamePad state.
     *
     * Architecturally, this is event-driven rather than poll-driven: FNA's platform layer
     * (e.g. SDL3_FNAPlatform) re-queries SDL fresh on every `Get*State()` call, while this
     * class only accumulates whatever `PlatformInputBridge::ProcessEvent` has pushed in via
     * `Set*State()`. State returned by the `Get*State()` methods here is only as current as
     * the last `Game::Tick()` (which unconditionally pumps SDL events once per frame, before
     * `Update()`/`Draw()` run — see `Game::PollEvents()`).
     *
     * @note Thread safety: this state is unsynchronized on purpose. Input is a single-threaded
     *       (game-loop-thread) API — writes come from `PlatformInputBridge::ProcessEvent` during
     *       `Game::PollEvents()`, reads from game `Update()`/`Draw()`, all on the same thread
     *       (matching XNA/FNA and required by SDL's event model). See `docs/input-backend.md` §6.
     *       Do not call `Set*`/`Get*` from a background thread. No locking is added.
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
         * @brief CNAEXT/EXT: adds a horizontal mouse wheel delta (SDL wheel.x) to internal state.
         */
        static void AddHorizontalScrollWheelDelta(int delta);

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
         * @param touchId The touch id.
         * @param state The touch location state.
         * @param position The touch position in pixels.
         * @param pressure CNAEXT/EXT: SDL finger pressure (0..1); surfaced via TouchLocation::getPressureEXT.
         */
        static void SetTouchState(
            int touchId,
            Microsoft::Xna::Framework::Input::Touch::TouchLocationState state,
            const Microsoft::Xna::Framework::Vector2& position,
            float pressure = 0.0f
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
         *
         * This is a pure read: it does not advance previous-location tracking, promote a
         * Pressed touch to Moved, or retire a Released touch. Calling it any number of times
         * within the same frame returns identical results. See AdvanceTouchFrame() for the
         * operation that performs those mutations once per frame.
         */
        static Microsoft::Xna::Framework::Input::Touch::TouchCollection GetTouchState();

        /**
         * @brief Advances event-driven touch state by exactly one frame.
         *
         * For every tracked touch: records the state/position last reported by GetTouchState()
         * as its previous location, promotes a still-Pressed touch to Moved, then removes any
         * touch that was Released as of the last snapshot. Must be called exactly once per game
         * frame (from TouchPanel::Update(), which FrameworkDispatcher::Update() drives once per
         * Game::Update() tick) — never from a getter, and never more than once per frame.
         */
        static void AdvanceTouchFrame();

        /**
         * @brief Returns whether any touch point is currently tracked, without mutating state.
         *
         * Unlike GetTouchState(), this does not advance previous-location tracking, consume
         * Released touches, or promote Pressed to Moved. Safe to call from capability queries.
         *
         * @return true if at least one touch location is currently tracked.
         */
        static bool HasAnyTouch();

        /**
         * @brief Returns raw gamepad values (no dead-zone applied) for one player.
         *
         * Used by GamePad::GetState() to apply dead-zone processing at the XNA layer.
         */
        static RawGamePadState GetRawGamePadState(
            Microsoft::Xna::Framework::PlayerIndex playerIndex
        );

        /**
         * @brief Test-only: resets all accumulated input state (mouse, keyboard, all gamepad
         *        slots, touch) to defaults.
         *
         * The input state is a process-wide singleton shared across the whole test binary, so
         * tests that mutate it (connect a gamepad, press keys, etc.) must reset it to avoid
         * leaking state into later tests. Not part of the runtime input path — for tests only.
         */
        static void ResetForTests();

        /**
         * @brief Test-only: resets ALL input subsystems' process-wide state in a deterministic order.
         *
         * Central entry point that fans out to every input subsystem so a test does not have to
         * know (and remember) the full list of individual reset helpers: SdlInputBridge file-static
         * state, this InputManager singleton, TouchPanel statics (incl. display metrics + window
         * handle), GestureDetector statics, Mouse statics, and TextInputEXT callbacks/handle.
         * Call this in a fixture SetUp()/TearDown() to guarantee input tests are order-independent.
         */
        static void ResetAllForTests();
    };
}
