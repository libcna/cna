// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

namespace CNA::Internal::Input
{
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
     * @brief Internal CNA input state manager.
     *
     * Keeps event-accumulated compatibility state for the legacy raw bridge.
     * Public keyboard, mouse and gamepad state read their corresponding platform services instead.
     *
     * Currently stores basic mouse and keyboard state.
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
         * @brief Returns a snapshot of current mouse state.
         */
        static Microsoft::Xna::Framework::Input::MouseState GetMouseState();

        /**
         * @brief Returns a snapshot of current keyboard state.
         */
        static Microsoft::Xna::Framework::Input::KeyboardState GetKeyboardState();

        /**
         * @brief Test-only: resets all accumulated mouse and keyboard state to defaults.
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
