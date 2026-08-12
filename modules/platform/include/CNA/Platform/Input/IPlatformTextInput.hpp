// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Platform {

    class IPlatformWindow;

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
         * @throws PlatformNotSupportedException If the platform reports no `TextInput` capability.
         */
        virtual void Start(IPlatformWindow& window) = 0;

        /**
         * @brief Ends text input for a window.
         *
         * @param window The window to stop text input for.
         */
        virtual void Stop(IPlatformWindow& window) = 0;

        /**
         * @brief Gets whether text input is currently active.
         *
         * @return True while text input is active.
         */
        [[nodiscard]] virtual bool IsActive() const = 0;

        /**
         * @brief Tells the input method where the text being edited is.
         *
         * @param window The window being edited in.
         * @param area The area to avoid covering.
         */
        virtual void SetInputArea(IPlatformWindow& window, const TextInputArea& area) = 0;
    };

} // namespace CNA::Platform
