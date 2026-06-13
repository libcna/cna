// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Provides text input events and on-screen keyboard control (FNA extension).
     */
    class TextInputEXT
    {
    public:
        TextInputEXT() = delete;

        /**
         * @brief Raised for each printable character produced by the keyboard.
         *
         * This event supports key repeat and is not raised by non-character keys.
         */
        static std::function<void(char)> TextInput;

        /**
         * @brief Raised during IME composition with draft text, start offset, and length.
         *
         * Allows displaying draft text before it has been committed as input.
         */
        static std::function<void(const std::string&, int, int)> TextEditing;

        /** @brief Native window handle used by text input APIs. */
        static std::uintptr_t WindowHandle;

        /**
         * @brief Returns true if text input mode is currently active.
         * @return True if text input is active; false otherwise.
         */
        static bool IsTextInputActive();

        /**
         * @brief Returns true if the on-screen keyboard is currently visible.
         * @return True if the keyboard is shown; false otherwise.
         */
        static bool IsScreenKeyboardShown();

        /**
         * @brief Returns true if the on-screen keyboard is visible for the given native window.
         * @param window The native window handle to query.
         * @return True if the keyboard is shown for that window; false otherwise.
         */
        static bool IsScreenKeyboardShown(std::uintptr_t window);

        /**
         * @brief Activates text input mode and shows the on-screen keyboard if applicable.
         */
        static void StartTextInput();

        /**
         * @brief Deactivates text input mode.
         */
        static void StopTextInput();

        /**
         * @brief Hints to the platform where text is being entered (for IME popup placement).
         * @param rectangle Text input location relative to GameWindow.ClientBounds.
         */
        static void SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle);

        /**
         * @brief Internal: dispatches a text input character to subscribers.
         * @param c The character that was entered.
         */
        static void INTERNAL_OnTextInput(char c);

        /**
         * @brief Internal: dispatches a text editing event to subscribers.
         * @param text The current draft composition text.
         * @param start The starting position of the active editing region.
         * @param length The length of the active editing region.
         */
        static void INTERNAL_OnTextEditing(const std::string& text, int start, int length);
    };
}
