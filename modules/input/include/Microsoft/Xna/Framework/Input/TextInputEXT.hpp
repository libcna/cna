// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/MulticastAction.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Forward-declared rather than including CNA/Input/TextInputType.hpp: this is a strict-XNA header
// (P1-027/P1-028) and must not require a consumer to pull in a CNA::Input extension header just to
// use TextInputEXT's non-EXT surface. StartTextInputWithTypeEXT() below only needs the type name to
// declare its parameter type; TextInputEXT.cpp includes the real header for the definition.
namespace CNA::Input
{
    enum class TextInputTypeEXT;
}

namespace Microsoft::Xna::Framework::Input
{
    using SharpRuntime::charcs;
    /**
     * @brief Provides text input events and on-screen keyboard control.
     *
     * @note CNAEXT — not part of the XNA 4.0 API. FNA extension (the `EXT` suffix
     *       marks an FNA addition beyond XNA 4.0). XNA 4.0 had no portable text-input
     *       event; FNA exposes the equivalent static extension class.
     *
     * @note Threading (INPUT-TEXT-016): like all CNA input, `TextInput`/`TextEditing` are dispatched on
     *       the event-pump (game-loop) thread during `Game::PollEvents()`; do not subscribe/raise from a
     *       background thread. The `TextEditing` composition is a **UTF-8** `std::string` (multi-byte), and
     *       its `start`/`length` are passed straight through from the platform IME model — index
     *       the string carefully, since a byte offset is not a character/code-point count for
     *       multi-byte text.
     */
    CNAEXT class TextInputEXT
    {
    public:
        /** @brief TextInputEXT is a static class (FNA `public static class TextInputEXT`) and cannot be instantiated. */
        TextInputEXT() = delete;

        /**
         * @brief Raised for each UTF-16 code unit produced by the keyboard.
         *
         * The argument is a single UTF-16 code unit (`charcs` / `char16_t`), matching FNA's
         * `event Action<char>` (C# `char` is a UTF-16 code unit). A code point above U+FFFF
         * (e.g. an emoji) is delivered as two calls — a high surrogate then a low surrogate —
         * exactly as FNA's `Encoding.UTF8.GetChars` decode does. This event supports key repeat
         * and is not raised by non-character keys.
         *
         * Multicast (matches FNA's `event Action<char>`): use `+=` to add subscribers, `=` to set a
         * single handler or `= nullptr` to clear.
         */
        CNAEXT static System::MulticastAction<charcs> TextInput;

        /**
         * @brief Raised during IME composition with draft text, start offset, and length.
         *
         * Allows displaying draft text before it has been committed as input.
         *
         * Multicast (matches FNA's `event Action<string, int, int>`): use `+=` to add subscribers,
         * `=` to set a single handler or `= nullptr` to clear.
         */
        CNAEXT static System::MulticastAction<const std::string&, int, int> TextEditing;

        /**
         * @brief CNAEXT/EXT: raised with the current IME candidate list during composition.
         *
         * XNA and older FNA implementations had no candidate-list event. Lets a game render the
         * CJK/IME candidate popup itself. The arguments are the candidate strings (UTF-8), the
         * index of the pre-selected candidate (or -1 if none), and whether the list is laid out
         * horizontally (otherwise vertically).
         *
         * Multicast: use `+=` to add subscribers, `=` to set a single handler or `= nullptr` to clear.
         */
        CNAEXT static System::MulticastAction<const std::vector<std::string>&, int, bool> TextEditingCandidatesEXT;

        /**
         * @brief Returns the native window handle used by the text input APIs.
         * @return The implementation's native window token stored as an integer, or 0 if unset.
         */
        CNAEXT static std::uintptr_t getWindowHandleProperty();

        /**
         * @brief Sets the native window handle used by the text input APIs.
         * @param value The implementation's native window token stored as an integer.
         */
        CNAEXT static void setWindowHandleProperty(std::uintptr_t value);

        /**
         * @brief Internal: associates the published native handle with its platform window id.
         *
         * GraphicsDevice calls this immediately after publishing a live window. Keeping the id
         * separate preserves the FNA-compatible native-handle property while lifecycle calls use
         * CNA's platform-neutral text-input contract.
         *
         * @param value The platform window id, or zero when no window is published.
         */
        CNAEXT static void INTERNAL_setWindowId(std::uint32_t value);

        /**
         * @brief Returns true if text input mode is currently active.
         *
         * For the on-screen keyboard, this may remain true on some platforms if an external event
         * closed the keyboard; in that case, check `IsScreenKeyboardShown` instead.
         *
         * @return True if text input is active; false otherwise.
         */
        CNAEXT static bool IsTextInputActive();

        /**
         * @brief Returns true if the on-screen keyboard is currently visible.
         * @return True if the keyboard is shown; false otherwise.
         */
        CNAEXT static bool IsScreenKeyboardShown();

        /**
         * @brief Returns true if the on-screen keyboard is visible for the given native window.
         * @param window The native window handle to query.
         * @return True if the keyboard is shown for that window; false otherwise.
         */
        CNAEXT static bool IsScreenKeyboardShown(std::uintptr_t window);

        /**
         * @brief Activates text input mode and shows the on-screen keyboard if applicable.
         */
        CNAEXT static void StartTextInput();

        /**
         * @brief Deactivates text input mode.
         */
        CNAEXT static void StopTextInput();

        /**
         * @brief CNAEXT/EXT: activates text input mode with an input-type hint for the on-screen
         *        keyboard / IME (e.g. a numeric pad or a hidden-password field).
         *
         * XNA 4.0 and older FNA implementations had no input-type hint. Falls back to the plain
         * `StartTextInput` no-op behavior when no window handle is set.
         *
         * @param type The kind of text being entered.
         */
        CNAEXT static void StartTextInputWithTypeEXT(CNA::Input::TextInputTypeEXT type);

        /**
         * @brief Hints to the platform where text is being entered (for IME popup placement).
         * @param rectangle Text input location relative to GameWindow.ClientBounds.
         */
        CNAEXT static void SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle);

        /**
         * @brief Internal: dispatches a text input code unit to subscribers.
         * @param c The UTF-16 code unit that was entered.
         */
        CNAEXT static void INTERNAL_OnTextInput(charcs c);

        /**
         * @brief Internal: dispatches a text editing event to subscribers.
         * @param text The current draft composition text.
         * @param start The starting position of the active editing region.
         * @param length The length of the active editing region.
         */
        CNAEXT static void INTERNAL_OnTextEditing(const std::string& text, int start, int length);

        /**
         * @brief Internal: dispatches an IME candidate-list event to subscribers.
         * @param candidates The candidate strings (UTF-8).
         * @param selected The index of the selected candidate, or -1 if none.
         * @param horizontal True if the candidate list is horizontal; false if vertical.
         */
        CNAEXT static void INTERNAL_OnTextEditingCandidates(
            const std::vector<std::string>& candidates, int selected, bool horizontal);

        /**
         * @brief Test-only: resets TextInputEXT's static state (callbacks and window identity).
         */
        CNAEXT static void ResetForTests();

    private:
        /** @brief Backing store for the window handle property. */
        static std::uintptr_t windowHandle_;
        /** @brief Platform-neutral identity paired with the published native handle. */
        static std::uint32_t windowId_;
    };
}
