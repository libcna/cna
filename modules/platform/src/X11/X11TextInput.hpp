// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "X11Headers.hpp"

#include <map>
#include <string>

namespace CNA::Platform::X11 {

    class X11Connection;
    class X11Window;

    /**
     * @brief Committed Unicode text input through X input methods.
     *
     * ### Committed text, not composition
     *
     * `Xutf8LookupString` on a per-window `XIC` is what turns a key press into the characters it
     * actually produced — dead keys resolved, Compose sequences applied, an IME's selected
     * candidate delivered. That is CNA's `TextInput` capability and it is fully implemented here.
     *
     * CNA's `Ime` capability promises something further: `TextEditingEvent` and
     * `TextEditingCandidatesEvent`, the in-progress composition string and the candidate list.
     * Those need XIM preedit and status callbacks in `XIMPreeditCallbacks` style, which this
     * backend does not implement — so `ime` is reported **false** while `textInput` is true. The
     * capability set is a promise, and claiming IME because XIM appears in the implementation
     * would be exactly the sort of unearned checkbox the contract forbids.
     *
     * ### What happens when there is no input method
     *
     * A bare `Xvfb`, a minimal container or a session with no `XMODIFIERS` has no input-method
     * server to open. `XOpenIM` then fails, and the backend falls back to `XLookupString`, which
     * still produces Latin-1 text from ordinary keys. `textInput` stays true because text input
     * genuinely works; what is lost is dead-key composition, not the capability.
     */
    class X11TextInput final : public IPlatformTextInput
    {
    public:
        /**
         * @brief Opens the input method for a connection.
         *
         * @param connection The connection to open the input method on.
         */
        explicit X11TextInput(X11Connection& connection);

        /** @brief Closes the input method. Window contexts are destroyed by their windows. */
        ~X11TextInput() override;

        X11TextInput(const X11TextInput&) = delete;
        X11TextInput& operator=(const X11TextInput&) = delete;

        /**
         * @brief Starts delivering text events for a window.
         * @param window The window to start text input for.
         * @param type The kind of text expected; X11 has no on-screen keyboard to hint, so this
         *        is recorded and otherwise unused.
         */
        void Start(WindowId window, TextInputType type) override;

        /** @brief Stops delivering text events for a window. @param window The window. */
        void Stop(WindowId window) override;

        /**
         * @brief Gets whether text input is active for a window.
         * @param window The window.
         * @return True when active.
         */
        [[nodiscard]] bool IsActive(WindowId window) const override;

        /**
         * @brief Gets whether an on-screen keyboard is shown.
         * @param window The window.
         * @return False; a desktop X session has no on-screen keyboard under the application's control.
         */
        [[nodiscard]] bool IsScreenKeyboardShown(WindowId window) const override;

        /**
         * @brief Tells the input method where the text being edited is.
         * @param window The window.
         * @param area The caret rectangle in client coordinates.
         */
        void SetInputArea(WindowId window, const TextInputArea& area) override;

        // --- used by the platform and the event mapper -----------------------------------------

        /**
         * @brief Creates and attaches an input context to a window.
         *
         * @param window The window to attach a context to.
         */
        void AttachWindow(X11Window& window);

        /**
         * @brief Registers or unregisters a window so ids can be resolved.
         *
         * @param id The CNA window id.
         * @param window The window, or null to unregister.
         */
        void RegisterWindow(WindowId id, X11Window* window);

        /**
         * @brief Tells the input method which window has focus.
         *
         * @param window The focused window, or null when focus was lost.
         */
        void SetFocusedWindow(X11Window* window);

        /**
         * @brief Converts a key press into committed text.
         *
         * @param window The window the key press was delivered to.
         * @param event The key press event; not const because `Xutf8LookupString` takes a
         *        non-const pointer.
         * @param text Receives the committed UTF-8 text; cleared first.
         * @return True when @p text is non-empty.
         */
        bool LookupText(X11Window* window, XKeyEvent& event, std::string& text);

        /** @brief Gets whether a real input method was opened. @return True when XIM is in use. */
        [[nodiscard]] bool HasInputMethod() const { return inputMethod_ != nullptr; }

    private:
        [[nodiscard]] X11Window* FindWindow(WindowId id) const;

        X11Connection& connection_;
        XIM inputMethod_ = nullptr;
        std::map<WindowId, X11Window*> windows_;
        std::map<WindowId, TextInputType> active_;
        X11Window* focused_ = nullptr;
    };

} // namespace CNA::Platform::X11
