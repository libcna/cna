// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include "Win32Utf.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Platform::Win32 {

    /** @brief Receives the events a translated window message produced. */
    class Win32EventSink
    {
    public:
        /** @brief Destroys the sink. */
        virtual ~Win32EventSink() = default;

        /**
         * @brief Accepts one translated event.
         *
         * @param event The event to queue for the next `PollEvents`.
         */
        virtual void Push(PlatformEvent event) = 0;
    };

    /** @brief How a window is currently shown, as far as size messages are concerned. */
    enum class Win32ShowState
    {
        /** @brief An ordinary restored window. */
        Normal,
        /** @brief Minimised to the taskbar. */
        Minimized,
        /** @brief Maximised to fill its monitor's work area. */
        Maximized
    };

    /**
     * @brief Translates one window's Win32 messages into CNA platform events.
     *
     * ### Why this is a separate object from the window
     *
     * Everything here is a pure function of the message plus the translation state it carries
     * (last pointer position, show state, half of a surrogate pair, which keys it has reported as
     * held). It never touches an `HWND`, never calls a Win32 function that needs a live window,
     * and never performs a side effect. That makes the whole translation testable from synthetic
     * `(message, wParam, lParam)` triples with no window and no message loop — which is what
     * `Win32EventMapperTests` does, and is the only way the interesting cases (surrogate pairs,
     * sided modifiers, the maximize/restore ordering) can be exercised deterministically.
     *
     * `Win32Window` owns one of these and is responsible for the side effects the translation
     * implies: mouse capture, `TrackMouseEvent`, honouring `WM_DPICHANGED`'s suggested rectangle.
     */
    class Win32EventMapper
    {
    public:
        /** @brief Reads the modifier keys currently held, in CNA's bit layout. */
        using ModifierProvider = std::uint16_t (*)();

        /**
         * @brief Creates a mapper for one window.
         *
         * @param window The stable id every event this mapper produces is attributed to.
         * @param sink Receives the produced events. Must outlive the mapper.
         */
        Win32EventMapper(WindowId window, Win32EventSink& sink);

        /**
         * @brief Translates one window message.
         *
         * @param message The message identifier.
         * @param wParam The message's first parameter.
         * @param lParam The message's second parameter.
         * @return True when the message was fully handled and must not reach `DefWindowProcW`.
         *         `WM_CLOSE` is the significant case: the contract says a close is a *request*,
         *         so the default destroy has to be suppressed.
         */
        bool Translate(std::uint32_t message, std::uint64_t wParam, std::int64_t lParam);

        /**
         * @brief Stops delivering events, because the sink is going away.
         *
         * Translation state (held keys, pointer position, wheel totals) keeps being maintained,
         * so a window whose platform has been destroyed still answers the pollable queries
         * correctly -- it simply has nobody to send events to.
         */
        void DetachSink();

        /**
         * @brief Releases every key this mapper still believes is held.
         *
         * Called on focus loss. Without it, Alt+Tab leaves Alt held forever: the window receives
         * the `WM_SYSKEYDOWN` and never the matching release, because the release goes to whatever
         * took focus. Flushing here is what lets `exactKeyboardState` be reported as true.
         */
        void ReleaseHeldKeys();

        /**
         * @brief Gets the keys currently reported as held.
         *
         * @return The virtual keys, in the order they were pressed.
         */
        [[nodiscard]] const std::vector<KeyCode>& GetHeldKeys() const { return heldKeys_; }

        /**
         * @brief Gets how the window is currently shown.
         *
         * @return The show state as the last size message left it.
         */
        [[nodiscard]] Win32ShowState GetShowState() const { return showState_; }

        /**
         * @brief Gets the last pointer position reported in client coordinates.
         *
         * @param x Receives the x position.
         * @param y Receives the y position.
         * @return True when a position has been observed.
         */
        [[nodiscard]] bool TryGetPointerPosition(int& x, int& y) const;

        /**
         * @brief Gets the buttons currently held, as `MouseSnapshot::buttons` bits.
         *
         * @return Bits 0..4 for left, middle, right, X1 and X2.
         */
        [[nodiscard]] std::uint8_t GetHeldButtons() const { return heldButtons_; }

        /**
         * @brief Gets accumulated wheel movement in XNA units and clears nothing.
         *
         * @param horizontal Receives the accumulated horizontal total.
         * @param vertical Receives the accumulated vertical total.
         */
        void GetWheelTotals(int& horizontal, int& vertical) const;

        /**
         * @brief Replaces the modifier source, for tests that need a deterministic answer.
         *
         * @param provider The replacement; null restores the live `GetKeyState` source.
         */
        void SetModifierProvider(ModifierProvider provider);

        /**
         * @brief Enables or disables committed-text delivery for this window.
         *
         * Text input is a *mode*, exactly as `IPlatformTextInput` describes it. Windows delivers
         * `WM_CHAR` whether or not anything asked for it, so the gate lives here: with the mode
         * off, character messages are consumed and produce no `TextInputEvent`, matching what a
         * game that never called `TextInputEXT::StartTextInput()` sees on every other backend.
         *
         * @param active True to deliver committed text.
         */
        void SetTextInputActive(bool active);

        /**
         * @brief Gets whether committed text is being delivered for this window.
         *
         * @return True while text input is active.
         */
        [[nodiscard]] bool IsTextInputActive() const { return textInputActive_; }

        /**
         * @brief Gets whether a pointer button press left the pointer captured.
         *
         * `Win32Window` uses this to decide between `SetCapture` and `ReleaseCapture`, which is
         * the one piece of mouse behaviour that cannot live in a pure translator.
         *
         * @return True while at least one button is held.
         */
        [[nodiscard]] bool WantsPointerCapture() const { return heldButtons_ != 0; }

    private:
        void Emit(PlatformEvent event);
        void PushKey(KeyCode keycode, Scancode scancode, bool pressed, bool repeat);
        void PushWindow(WindowEventKind kind, int data1 = 0, int data2 = 0);
        bool TranslateKey(std::uint32_t message, std::uint64_t wParam, std::int64_t lParam);
        bool TranslateChar(std::uint64_t wParam);
        bool TranslateSize(std::uint64_t wParam, std::int64_t lParam);
        bool TranslateMouseButton(std::uint32_t message, std::int64_t lParam);
        bool TranslateMouseWheel(std::uint32_t message, std::uint64_t wParam);
        bool TranslateMouseMove(std::int64_t lParam);

        WindowId window_;
        Win32EventSink* sink_;
        ModifierProvider modifiers_;

        Win32ShowState showState_ = Win32ShowState::Normal;
        std::vector<KeyCode> heldKeys_;
        std::vector<Scancode> heldScancodes_;
        SurrogateAssembler surrogates_;
        bool textInputActive_ = false;

        bool pointerPositionKnown_ = false;
        int pointerX_ = 0;
        int pointerY_ = 0;
        std::uint8_t heldButtons_ = 0;
        int wheelHorizontal_ = 0;
        int wheelVertical_ = 0;
    };

} // namespace CNA::Platform::Win32
