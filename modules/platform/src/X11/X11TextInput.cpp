// SPDX-License-Identifier: MS-PL

#include "X11TextInput.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Window.hpp"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
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

        /// One window's composition: what the input method's callbacks point at. Owned by the
        /// window together with its input context (X11Window::SetInputContext) and released only
        /// after the context is destroyed, so it outlives every callback.
        struct PreeditState
        {
            X11TextInput* owner = nullptr;
            WindowId window = 0;
            std::u32string text;
            std::vector<XIMFeedback> feedback;
            int caret = 0;
            XIMCallback start{};
            XIMCallback done{};
            XIMCallback draw{};
            XIMCallback caretMove{};
        };

        std::string ToUtf8(const std::u32string& text)
        {
            std::string utf8;
            for (const char32_t code : text)
            {
                if (code < 0x80)
                {
                    utf8.push_back(static_cast<char>(code));
                }
                else if (code < 0x800)
                {
                    utf8.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else if (code < 0x10000)
                {
                    utf8.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else
                {
                    utf8.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    utf8.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
            return utf8;
        }

        /// The characters of an XIMText. Its `length` is not trusted: input methods report it
        /// wrongly (SDL counts the string itself for the same reason), so the characters are
        /// counted off the string.
        std::u32string DecodeXimText(const XIMText& text)
        {
            std::u32string decoded;
            if (text.encoding_is_wchar)
            {
                if (text.string.wide_char != nullptr)
                {
                    for (const wchar_t* character = text.string.wide_char; *character != L'\0'; ++character)
                    {
                        decoded.push_back(static_cast<char32_t>(*character));
                    }
                }
                return decoded;
            }
            if (text.string.multi_byte == nullptr)
            {
                return decoded;
            }
            // Multibyte text is in the locale's encoding -- UTF-8 wherever an IME is in use --
            // and glibc's wchar_t is the Unicode code point.
            std::mbstate_t state{};
            const char* cursor = text.string.multi_byte;
            const char* end = cursor + std::strlen(cursor);
            while (cursor < end)
            {
                wchar_t character = 0;
                const std::size_t used = std::mbrtowc(&character, cursor, static_cast<std::size_t>(end - cursor), &state);
                if (used == 0 || used == static_cast<std::size_t>(-1) || used == static_cast<std::size_t>(-2))
                {
                    break;
                }
                decoded.push_back(static_cast<char32_t>(character));
                cursor += used;
            }
            return decoded;
        }

        void EmitComposition(PreeditState& state)
        {
            TextEditingEvent event;
            event.window = state.window;
            event.text = ToUtf8(state.text);
            // The selection is the run of reversed or highlighted characters -- the segment the
            // input method is converting -- and otherwise there is none, at the caret; the same
            // reading SDL3 gives XIM's feedback.
            const int length = static_cast<int>(state.text.size());
            int start = -1;
            int run = 0;
            for (int index = 0; index < length && index < static_cast<int>(state.feedback.size()); ++index)
            {
                if ((state.feedback[static_cast<std::size_t>(index)] & (XIMReverse | XIMHighlight)) != 0)
                {
                    if (start < 0)
                    {
                        start = index;
                    }
                    ++run;
                }
                else if (start >= 0)
                {
                    break;
                }
            }
            event.cursor = start >= 0 ? start : std::clamp(state.caret, 0, length);
            event.selectionLength = start >= 0 ? run : 0;
            state.owner->QueueEditingEvent(std::move(event));
        }

        // The input method's callbacks. Declared with XIMProc's own parameters (an XIC is passed
        // where XIMProc says XIM: both are pointers the callback does not use) and the call data
        // cast inside, rather than with the typed signatures cast into XIMProc.
        void PreeditStart(XIM, XPointer clientData, XPointer)
        {
            auto& state = *reinterpret_cast<PreeditState*>(clientData);
            state.text.clear();
            state.feedback.clear();
            state.caret = 0;
        }

        void PreeditDone(XIM, XPointer clientData, XPointer)
        {
            auto& state = *reinterpret_cast<PreeditState*>(clientData);
            const bool hadText = !state.text.empty();
            state.text.clear();
            state.feedback.clear();
            state.caret = 0;
            if (hadText)
            {
                EmitComposition(state);
            }
        }

        void PreeditDraw(XIM, XPointer clientData, XPointer callData)
        {
            auto& state = *reinterpret_cast<PreeditState*>(clientData);
            const auto& draw = *reinterpret_cast<const XIMPreeditDrawCallbackStruct*>(callData);
            const int size = static_cast<int>(state.text.size());
            const int first = std::clamp(draw.chg_first, 0, size);
            const int changed = std::clamp(draw.chg_length, 0, size - first);

            std::u32string inserted;
            std::vector<XIMFeedback> insertedFeedback;
            if (draw.text != nullptr)
            {
                inserted = DecodeXimText(*draw.text);
                insertedFeedback.assign(inserted.size(), 0);
                if (draw.text->feedback != nullptr)
                {
                    const std::size_t reported = std::min<std::size_t>(draw.text->length, inserted.size());
                    std::copy_n(draw.text->feedback, reported, insertedFeedback.begin());
                }
            }
            state.text.replace(static_cast<std::size_t>(first), static_cast<std::size_t>(changed), inserted);
            state.feedback.resize(state.text.size() - inserted.size() + static_cast<std::size_t>(changed), 0);
            state.feedback.erase(state.feedback.begin() + first, state.feedback.begin() + first + changed);
            state.feedback.insert(state.feedback.begin() + first, insertedFeedback.begin(), insertedFeedback.end());
            state.caret = std::clamp(draw.caret, 0, static_cast<int>(state.text.size()));
            EmitComposition(state);
        }

        void PreeditCaret(XIM, XPointer clientData, XPointer callData)
        {
            auto& state = *reinterpret_cast<PreeditState*>(clientData);
            auto& caret = *reinterpret_cast<XIMPreeditCaretCallbackStruct*>(callData);
            const int size = static_cast<int>(state.text.size());
            int position = state.caret;
            switch (caret.direction)
            {
                case XIMAbsolutePosition: position = caret.position; break;
                case XIMForwardChar: ++position; break;
                case XIMBackwardChar: --position; break;
                case XIMLineStart: position = 0; break;
                case XIMLineEnd: position = size; break;
                default: break;
            }
            position = std::clamp(position, 0, size);
            caret.position = position;  // The callback reports where the caret ended up.
            if (position != state.caret)
            {
                state.caret = position;
                EmitComposition(state);
            }
        }

        XIMCallback MakeCallback(PreeditState* state, void (*function)(XIM, XPointer, XPointer))
        {
            XIMCallback callback{};
            callback.client_data = reinterpret_cast<XPointer>(state);
            callback.callback = function;
            return callback;
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
        if (inputMethod_ == nullptr)
        {
            return;
        }

        // The style: the input method drawing its own composition, unless the application said
        // it draws it (see the class comment) and the input method can hand it over.
        const char* ui = std::getenv("CNA_IME_IMPLEMENTED_UI");
        const bool applicationDrawsComposition = ui != nullptr && std::strstr(ui, "composition") != nullptr;
        XIMStyles* styles = nullptr;
        if (XGetIMValues(inputMethod_, XNQueryInputStyle, &styles, nullptr) == nullptr && styles != nullptr)
        {
            const auto supports = [styles](const XIMStyle style) {
                for (unsigned short index = 0; index < styles->count_styles; ++index)
                {
                    if (styles->supported_styles[index] == style)
                    {
                        return true;
                    }
                }
                return false;
            };
            const XIMStyle composition[] = {XIMPreeditCallbacks | XIMStatusNothing,
                                            XIMPreeditCallbacks | XIMStatusNone};
            const XIMStyle drawnByTheInputMethod[] = {XIMPreeditNothing | XIMStatusNothing,
                                                     XIMPreeditNothing | XIMStatusNone,
                                                     XIMPreeditNone | XIMStatusNone};
            if (applicationDrawsComposition)
            {
                for (const XIMStyle style : composition)
                {
                    if (style_ == 0 && supports(style))
                    {
                        style_ = style;
                    }
                }
            }
            for (const XIMStyle style : drawnByTheInputMethod)
            {
                if (style_ == 0 && supports(style))
                {
                    style_ = style;
                }
            }
            XFree(styles);
        }
        if (style_ == 0)
        {
            // The style this backend has always asked for; an input method that lists nothing
            // better still accepts it or refuses the context, and a refused context is handled.
            style_ = XIMPreeditNothing | XIMStatusNothing;
        }
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
        XIC context = nullptr;
        std::shared_ptr<PreeditState> state;
        if (HasCompositionEvents())
        {
            state = std::make_shared<PreeditState>();
            state->owner = this;
            state->window = window.GetId();
            state->start = MakeCallback(state.get(), &PreeditStart);
            state->done = MakeCallback(state.get(), &PreeditDone);
            state->draw = MakeCallback(state.get(), &PreeditDraw);
            state->caretMove = MakeCallback(state.get(), &PreeditCaret);
            XVaNestedList preedit = XVaCreateNestedList(
                0, XNPreeditStartCallback, &state->start, XNPreeditDoneCallback, &state->done,
                XNPreeditDrawCallback, &state->draw, XNPreeditCaretCallback, &state->caretMove, nullptr);
            if (preedit != nullptr)
            {
                context = XCreateIC(inputMethod_, XNInputStyle, style_, XNClientWindow,
                                    window.GetXWindow(), XNFocusWindow, window.GetXWindow(),
                                    XNPreeditAttributes, preedit, nullptr);
                XFree(preedit);
            }
        }
        else
        {
            // XIMPreeditNothing is the "root window" style: the input method draws its own
            // preedit and status windows.
            context = XCreateIC(inputMethod_, XNInputStyle, style_, XNClientWindow,
                                window.GetXWindow(), XNFocusWindow, window.GetXWindow(), nullptr);
        }
        if (context != nullptr)
        {
            window.SetInputContext(context, std::move(state));
        }
    }

    void X11TextInput::QueueEditingEvent(TextEditingEvent event)
    {
        editing_.emplace_back(std::move(event));
    }

    void X11TextInput::TakeEditingEvents(std::vector<PlatformEvent>& destination)
    {
        for (PlatformEvent& event : editing_)
        {
            destination.push_back(std::move(event));
        }
        editing_.clear();
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
            if (found != windows_.end() && found->second == contextFocused_)
            {
                contextFocused_ = nullptr;
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
        focused_ = window;
        UpdateContextFocus();
    }

    void X11TextInput::UpdateContextFocus()
    {
        // Focus the input context only while its window has focus and text input is started for
        // it -- see the class comment for the keys a focused context would otherwise swallow.
        X11Window* wanted = focused_ != nullptr && focused_->GetInputContext() != nullptr &&
                                    IsActive(focused_->GetId())
                                ? focused_
                                : nullptr;
        if (wanted == contextFocused_)
        {
            return;
        }
        if (contextFocused_ != nullptr && contextFocused_->GetInputContext() != nullptr)
        {
            XUnsetICFocus(contextFocused_->GetInputContext());
        }
        contextFocused_ = wanted;
        if (contextFocused_ != nullptr)
        {
            XSetICFocus(contextFocused_->GetInputContext());
        }
    }

    void X11TextInput::EndComposition(X11Window& window)
    {
        const XIC context = window.GetInputContext();
        if (context == nullptr)
        {
            return;
        }
        // Resetting discards what was being composed; what it hands back is not committed.
        if (char* discarded = XmbResetIC(context); discarded != nullptr)
        {
            XFree(discarded);
        }
        if (HasCompositionEvents())
        {
            auto* state = static_cast<PreeditState*>(window.GetInputContextData());
            if (state != nullptr && !state->text.empty())
            {
                state->text.clear();
                state->feedback.clear();
                state->caret = 0;
                EmitComposition(*state);
            }
        }
    }

    void X11TextInput::Start(const WindowId window, const TextInputType type)
    {
        if (FindWindow(window) == nullptr)
        {
            throw PlatformException("X11TextInput::Start", "unknown window id");
        }
        active_[window] = type;
        UpdateContextFocus();
    }

    void X11TextInput::Stop(const WindowId window)
    {
        if (IsActive(window))
        {
            if (X11Window* target = FindWindow(window); target != nullptr)
            {
                // A composition in progress ends with text input: an application that stopped
                // listening must not be left with half a word the next time it starts.
                EndComposition(*target);
            }
        }
        active_.erase(window);
        UpdateContextFocus();
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
