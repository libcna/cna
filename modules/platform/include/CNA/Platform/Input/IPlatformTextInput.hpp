// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

namespace CNA::Platform {

    /** @brief Describes the kind of text being entered, so a platform can choose an input UI. */
    enum class TextInputType
    {
        /** @brief Use the platform's ordinary text-input mode without an explicit purpose hint. */
        Default,
        /** @brief Ordinary text input with no more specific hint. */
        Text,
        /** @brief A person's name. */
        TextName,
        /** @brief An e-mail address. */
        TextEmail,
        /** @brief A username. */
        TextUsername,
        /** @brief A password whose characters should be hidden. */
        TextPasswordHidden,
        /** @brief A password whose characters may remain visible. */
        TextPasswordVisible,
        /** @brief An ordinary number. */
        Number,
        /** @brief A numeric password or PIN whose digits should be hidden. */
        NumberPasswordHidden,
        /** @brief A numeric password or PIN whose digits may remain visible. */
        NumberPasswordVisible
    };

    /** @brief A rectangle the input method should avoid covering. */
    struct TextInputArea
    {
        /** @brief Left edge in client coordinates. */
        int x = 0;
        /** @brief Top edge in client coordinates. */
        int y = 0;
        /** @brief Width. */
        int width = 0;
        /** @brief Height. */
        int height = 0;
        /** @brief Cursor offset from the left edge, so an IME can position its candidate window. */
        int cursorOffset = 0;
    };

    /**
     * @brief Controls text entry and input-method composition.
     *
     * Text input is a mode, not a passive query: while it is active the platform may show an
     * on-screen keyboard or IME candidate window, and key events may be consumed by composition
     * instead of reaching the game. Backs `Microsoft::Xna::Framework::Input::TextInputEXT`.
     */
    class IPlatformTextInput
    {
    public:
        /** @brief Destroys the service. */
        virtual ~IPlatformTextInput() = default;

        /**
         * @brief Begins text input for a window.
         *
         * @param window The window to receive text input events.
         * @param type The kind of text being entered.
         * @throws PlatformNotSupportedException If the platform reports no `TextInput` capability.
         * @throws PlatformException If @p window does not identify a live platform window.
         */
        virtual void Start(WindowId window, TextInputType type) = 0;

        /**
         * @brief Ends text input for a window.
         *
         * @param window The window to stop text input for.
         */
        virtual void Stop(WindowId window) = 0;

        /**
         * @brief Gets whether text input is currently active.
         *
         * @param window The window to inspect.
         * @return True while text input is active for that window.
         */
        [[nodiscard]] virtual bool IsActive(WindowId window) const = 0;

        /**
         * @brief Gets whether the platform's on-screen keyboard is visible for a window.
         *
         * This is deliberately separate from `IsActive`: a user or the operating system can
         * dismiss an on-screen keyboard while the application's text-input mode remains active.
         *
         * @param window The window to inspect.
         * @return True while an on-screen keyboard is shown for that window.
         */
        [[nodiscard]] virtual bool IsScreenKeyboardShown(WindowId window) const = 0;

        /**
         * @brief Tells the input method where the text being edited is.
         *
         * @param window The window being edited in.
         * @param area The area to avoid covering.
         */
        virtual void SetInputArea(WindowId window, const TextInputArea& area) = 0;
    };

} // namespace CNA::Platform
