// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "X11Headers.hpp"

#include <map>
#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;
    class X11Window;

    /**
     * @brief Unicode text input, and input-method composition, through X input methods.
     *
     * ### Committed text
     *
     * `Xutf8LookupString` on a per-window `XIC` is what turns a key press into the characters it
     * actually produced — dead keys resolved, Compose sequences applied, an IME's selected
     * candidate delivered. That is CNA's `TextInput` capability.
     *
     * ### Composition (plans/plan_x11.md X11-0152)
     *
     * By default the input method draws its own composition, in its own window — the only
     * behaviour an XNA game can live with, because XNA had no IME API and so no game draws a
     * composition. `ime` is then false: no composition reaches the application.
     *
     * An application that does draw it says so with `CNA_IME_IMPLEMENTED_UI=composition` in its
     * environment, read once when the platform is created (the SDL3 backend's equivalent is
     * SDL's own IME-UI hint). If the input method offers the *on-the-spot* style
     * (`XIMPreeditCallbacks`) — ibus and fcitx do — the backend then asks for it, and the input
     * method hands the composition over through four callbacks instead of drawing it: start,
     * draw (a range of the composition replaced), caret, done. Each change becomes a
     * `TextEditingEvent`, delivered from `PollEvents` in order with the key and text events around
     * it; the end of a composition is an editing event with empty text. `ime` is reported true
     * exactly when that style was negotiated.
     *
     * The candidate list is not delivered: XIM has no protocol for handing it to the client, and
     * the input method draws its own candidate window at the spot `SetInputArea` gives it — as it
     * does for every other X application, and as the SDL3 backend leaves it by default. So
     * `TextEditingCandidatesEvent` never occurs here.
     *
     * The input context has focus only while its window has focus **and** text input is started
     * for it. Outside text entry an IME would otherwise go on swallowing keys a game is using as
     * keys — a Hangul or Japanese input mode turning WASD into a composition.
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
         * @return True when @p text is non-empty. A commit also ends the window's composition,
         *         queueing its empty `TextEditingEvent` -- drain those before delivering the text.
         */
        bool LookupText(X11Window* window, XKeyEvent& event, std::string& text);

        /** @brief Gets whether a real input method was opened. @return True when XIM is in use. */
        [[nodiscard]] bool HasInputMethod() const { return inputMethod_ != nullptr; }

        /**
         * @brief Gets whether the input method hands its composition to the application.
         * @return True when the on-the-spot style was negotiated; what `ime` reports.
         */
        [[nodiscard]] bool HasCompositionEvents() const
        {
            return (style_ & XIMPreeditCallbacks) != 0;
        }

        /**
         * @brief Moves the composition events produced since the last call to @p destination.
         * @param destination Receives `TextEditingEvent` values, in order.
         */
        void TakeEditingEvents(std::vector<PlatformEvent>& destination);

        /**
         * @brief Records a composition change; called by the input method's callbacks.
         * @param event The event.
         */
        void QueueEditingEvent(TextEditingEvent event);

    private:
        [[nodiscard]] X11Window* FindWindow(WindowId id) const;
        void UpdateContextFocus();
        void EndComposition(X11Window& window);
        void ClearComposition(X11Window& window);

        X11Connection& connection_;
        XIM inputMethod_ = nullptr;
        XIMStyle style_ = 0;
        std::map<WindowId, X11Window*> windows_;
        std::map<WindowId, TextInputType> active_;
        X11Window* focused_ = nullptr;
        X11Window* contextFocused_ = nullptr;
        std::vector<PlatformEvent> editing_;
    };

} // namespace CNA::Platform::X11
