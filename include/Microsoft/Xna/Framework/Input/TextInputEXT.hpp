// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace Microsoft::Xna::Framework::Input
{
    using SharpRuntime::charcs;
    /**
     * @brief Provides text input events and on-screen keyboard control.
     *
     * @note NOXNA — not part of the XNA 4.0 API. FNA extension (the `EXT` suffix
     *       marks an FNA addition beyond XNA 4.0). XNA 4.0 had no portable text-input
     *       event; FNA exposes this static class backed by SDL.
     */
    NOXNA class TextInputEXT
    {
    public:
        TextInputEXT() = delete;

        /**
         * @brief Raised for each UTF-16 code unit produced by the keyboard.
         *
         * The argument is a single UTF-16 code unit (`charcs` / `char16_t`), matching FNA's
         * `event Action<char>` (C# `char` is a UTF-16 code unit). A code point above U+FFFF
         * (e.g. an emoji) is delivered as two calls — a high surrogate then a low surrogate —
         * exactly as FNA's `Encoding.UTF8.GetChars` decode does. This event supports key repeat
         * and is not raised by non-character keys.
         */
        NOXNA static std::function<void(charcs)> TextInput;

        /**
         * @brief Raised during IME composition with draft text, start offset, and length.
         *
         * Allows displaying draft text before it has been committed as input.
         */
        NOXNA static std::function<void(const std::string&, int, int)> TextEditing;

        /**
         * @brief Returns the native window handle used by the text input APIs.
         * @return The native window handle (an SDL_Window* stored as an integer), or 0 if unset.
         */
        NOXNA static std::uintptr_t getWindowHandleProperty();

        /**
         * @brief Sets the native window handle used by the text input APIs.
         * @param value The native window handle (an SDL_Window* stored as an integer).
         */
        NOXNA static void setWindowHandleProperty(std::uintptr_t value);

        /**
         * @brief Returns true if text input mode is currently active.
         * @return True if text input is active; false otherwise.
         */
        NOXNA static bool IsTextInputActive();

        /**
         * @brief Returns true if the on-screen keyboard is currently visible.
         * @return True if the keyboard is shown; false otherwise.
         */
        NOXNA static bool IsScreenKeyboardShown();

        /**
         * @brief Returns true if the on-screen keyboard is visible for the given native window.
         * @param window The native window handle to query.
         * @return True if the keyboard is shown for that window; false otherwise.
         */
        NOXNA static bool IsScreenKeyboardShown(std::uintptr_t window);

        /**
         * @brief Activates text input mode and shows the on-screen keyboard if applicable.
         */
        NOXNA static void StartTextInput();

        /**
         * @brief Deactivates text input mode.
         */
        NOXNA static void StopTextInput();

        /**
         * @brief Hints to the platform where text is being entered (for IME popup placement).
         * @param rectangle Text input location relative to GameWindow.ClientBounds.
         */
        NOXNA static void SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle);

        /**
         * @brief Internal: dispatches a text input code unit to subscribers.
         * @param c The UTF-16 code unit that was entered.
         */
        NOXNA static void INTERNAL_OnTextInput(charcs c);

        /**
         * @brief Internal: dispatches a text editing event to subscribers.
         * @param text The current draft composition text.
         * @param start The starting position of the active editing region.
         * @param length The length of the active editing region.
         */
        NOXNA static void INTERNAL_OnTextEditing(const std::string& text, int start, int length);

        /**
         * @brief Test-only: resets TextInputEXT's static state (callbacks, window handle).
         */
        NOXNA static void ResetForTests();

    private:
        /** @brief Backing store for the window handle property. */
        static std::uintptr_t windowHandle_;
    };
}
