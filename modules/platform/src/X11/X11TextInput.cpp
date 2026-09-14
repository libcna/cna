// SPDX-License-Identifier: MS-PL

#include "X11TextInput.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Window.hpp"

#include <clocale>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace CNA::Platform::X11 {

    namespace {

        /**
         * Prepares the process locale for X input methods, without taking it from the host.
         *
         * plans/plan_x11.md design decision 9. `XOpenIM` needs a locale Xlib supports, and an
         * application that has not called `setlocale` is in the `"C"` locale, where
         * `XSupportsLocale()` is true but multi-byte input is not. The temptation is to call
         * `setlocale(LC_ALL, "")` and move on. CNA is a library: that would change the host
         * process's number and date formatting as a side effect of opening a window, and a
         * program that prints `3,14` instead of `3.14` after adding CNA would have no way to
         * connect the two.
         *
         * So: only `LC_CTYPE`, only when it is still the startup default, and only from the
         * environment the user already set. Everything else is left exactly as the host had it.
         */
        void PrepareLocaleForInputMethod()
        {
            const char* current = std::setlocale(LC_CTYPE, nullptr);
            if (current != nullptr && std::strcmp(current, "C") != 0 &&
                std::strcmp(current, "POSIX") != 0)
            {
                return;
            }
            std::setlocale(LC_CTYPE, "");
        }

    } // namespace

    X11TextInput::X11TextInput(X11Connection& connection) : connection_(connection)
    {
        PrepareLocaleForInputMethod();

        // XSetLocaleModifiers("") reads XMODIFIERS from the environment, which is how a session
        // says which input method to use (@im=ibus, @im=fcitx). Passing it explicitly rather than
        // relying on Xlib's default is what makes an IME actually reachable.
        if (XSupportsLocale() == True)
        {
            XSetLocaleModifiers("");
            inputMethod_ = XOpenIM(connection_.GetDisplay(), nullptr, nullptr, nullptr);
        }
        // A null inputMethod_ is not an error. There may be no input-method server on this
        // display at all, which is the normal state of a test server -- LookupText falls back to
        // XLookupString and text input keeps working for ordinary keys.
    }

    X11TextInput::~X11TextInput()
    {
        // The contexts are owned by their windows and destroyed there, which is the order Xlib
        // requires: an XIC must not outlive its XIM, and a window must not outlive its XIC.
        // Platform teardown destroys windows before services for exactly this reason.
        if (inputMethod_ != nullptr)
        {
            XCloseIM(inputMethod_);
            inputMethod_ = nullptr;
        }
    }

    void X11TextInput::AttachWindow(X11Window& window)
    {
        if (inputMethod_ == nullptr)
        {
            return;
        }
        // XIMPreeditNothing|XIMStatusNothing is the "root window" style: the input method draws
        // its own preedit and status windows. The alternatives (callbacks, or over-the-spot) are
        // what an Ime capability would need, and this backend reports that capability false --
        // see the class comment.
        XIC context = XCreateIC(inputMethod_, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                                XNClientWindow, window.GetXWindow(), XNFocusWindow,
                                window.GetXWindow(), nullptr);
        if (context != nullptr)
        {
            window.SetInputContext(context);
        }
    }

    void X11TextInput::RegisterWindow(const WindowId id, X11Window* window)
    {
        if (window == nullptr)
        {
            const auto found = windows_.find(id);
            if (found != windows_.end() && found->second == focused_)
            {
                // The focused window is going away. Unregistration happens while the window is
                // still whole (or after the server destroyed it, when its context is already
                // gone), so releasing the input method's focus here acts on a live context -- and
                // forgetting the pointer is what keeps the next focus change from reaching into a
                // freed window.
                SetFocusedWindow(nullptr);
            }
            windows_.erase(id);
            active_.erase(id);
            return;
        }
        windows_[id] = window;
    }

    X11Window* X11TextInput::FindWindow(const WindowId id) const
    {
        const auto found = windows_.find(id);
        return found != windows_.end() ? found->second : nullptr;
    }

    void X11TextInput::SetFocusedWindow(X11Window* window)
    {
        if (focused_ == window)
        {
            return;
        }
        if (focused_ != nullptr && focused_->GetInputContext() != nullptr)
        {
            XUnsetICFocus(focused_->GetInputContext());
        }
        focused_ = window;
        if (focused_ != nullptr && focused_->GetInputContext() != nullptr)
        {
            XSetICFocus(focused_->GetInputContext());
        }
    }

    void X11TextInput::Start(const WindowId window, const TextInputType type)
    {
        if (FindWindow(window) == nullptr)
        {
            throw PlatformException("X11TextInput::Start", "unknown window id");
        }
        active_[window] = type;
    }

    void X11TextInput::Stop(const WindowId window)
    {
        active_.erase(window);
    }

    bool X11TextInput::IsActive(const WindowId window) const
    {
        return active_.find(window) != active_.end();
    }

    bool X11TextInput::IsScreenKeyboardShown(const WindowId window) const
    {
        (void) window;
        // A desktop X session's on-screen keyboard, where one exists, is an independent
        // accessibility application. The application neither summons it nor can observe it, so
        // reporting false is the accurate answer rather than a missing feature.
        return false;
    }

    void X11TextInput::SetInputArea(const WindowId window, const TextInputArea& area)
    {
        X11Window* target = FindWindow(window);
        if (target == nullptr || target->GetInputContext() == nullptr)
        {
            return;
        }
        // The spot is where the input method draws its preedit. Setting it under
        // XIMPreeditNothing is harmless and becomes meaningful the moment the style changes, so
        // the caller's intent is recorded with the input method rather than dropped.
        XPoint spot{};
        spot.x = static_cast<short>(area.x + area.cursorOffset);
        spot.y = static_cast<short>(area.y + area.height);
        XVaNestedList list = XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
        if (list != nullptr)
        {
            XSetICValues(target->GetInputContext(), XNPreeditAttributes, list, nullptr);
            XFree(list);
        }
    }

    bool X11TextInput::LookupText(X11Window* window, XKeyEvent& event, std::string& text)
    {
        text.clear();

        KeySym keysym = NoSymbol;
        XStatus status = 0;
        // 32 bytes covers every single committed character; a longer commit comes from an input
        // method delivering a whole phrase, which the regrowth path below handles.
        char stack[32] = {};
        int length = 0;

        const XIC context = window != nullptr ? window->GetInputContext() : nullptr;
        if (context != nullptr)
        {
            length = Xutf8LookupString(context, &event, stack, static_cast<int>(sizeof(stack) - 1),
                                       &keysym, &status);
            if (status == XBufferOverflow)
            {
                // XBufferOverflow does not fill the buffer: it reports the required size and
                // commits nothing. Retrying with that size is mandatory, not an optimisation --
                // treating the first call's return as a length here is how a phrase-committing
                // IME produces truncated mojibake.
                std::vector<char> heap(static_cast<std::size_t>(length) + 1, '\0');
                length = Xutf8LookupString(context, &event, heap.data(), length, &keysym, &status);
                if (status == XLookupChars || status == XLookupBoth)
                {
                    text.assign(heap.data(), static_cast<std::size_t>(length));
                }
            }
            else if (status == XLookupChars || status == XLookupBoth)
            {
                text.assign(stack, static_cast<std::size_t>(length));
            }
        }
        else
        {
            // No input context: either no input method on this display, or the window has not
            // been attached. XLookupString still resolves the keysym to Latin-1 text, which is
            // correct for ordinary keys and is what makes text input work on a bare test server.
            length = XLookupString(&event, stack, static_cast<int>(sizeof(stack) - 1), &keysym,
                                   nullptr);
            for (int index = 0; index < length; ++index)
            {
                const auto byte = static_cast<unsigned char>(stack[index]);
                if (byte < 0x80u)
                {
                    text.push_back(static_cast<char>(byte));
                }
                else
                {
                    // XLookupString returns Latin-1, and the contract's text is UTF-8. A byte
                    // above 0x7F is a two-byte UTF-8 sequence, not a character to pass through:
                    // copying it raw is how an accented character becomes an invalid string.
                    text.push_back(static_cast<char>(0xC0u | (byte >> 6)));
                    text.push_back(static_cast<char>(0x80u | (byte & 0x3Fu)));
                }
            }
        }

        // Control characters are key events, not text. Backspace, Tab, Return and Escape all
        // produce a character from XLookupString, and delivering them as committed text is how a
        // text field ends up with a literal 0x08 in its buffer.
        if (text.size() == 1 && static_cast<unsigned char>(text[0]) < 0x20u)
        {
            text.clear();
        }
        if (text.size() == 1 && static_cast<unsigned char>(text[0]) == 0x7Fu)
        {
            text.clear();
        }
        return !text.empty();
    }

} // namespace CNA::Platform::X11
