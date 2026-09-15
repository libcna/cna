// SPDX-License-Identifier: MS-PL

#include "WaylandTextInput.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <algorithm>

namespace CNA::Platform::Wayland {

    int CodePointsBefore(const std::string_view text, const std::int32_t bytes)
    {
        const std::size_t limit = bytes < 0 ? 0 : std::min<std::size_t>(static_cast<std::size_t>(bytes), text.size());
        int count = 0;
        for (std::size_t index = 0; index < limit; ++index)
        {
            // Every byte that is not a UTF-8 continuation byte starts a code point.
            if ((static_cast<unsigned char>(text[index]) & 0xC0u) != 0x80u)
            {
                ++count;
            }
        }
        return count;
    }

    struct WaylandTextInput::SeatInput
    {
        WaylandTextInput* owner = nullptr;
        wl_seat* seat = nullptr;
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        zwp_text_input_v3* proxy = nullptr;
#endif
        WindowId focus = 0;
        bool enabled = false;
        // The state of the event group in progress, applied by `done` (text-input-v3 is
        // double-buffered in both directions).
        std::string pendingPreedit;
        std::int32_t pendingCursorBegin = 0;
        std::int32_t pendingCursorEnd = 0;
        std::string pendingCommit;
        bool hasPreedit = false;
        bool hasCommit = false;
        bool composing = false;
    };

#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
    namespace {

        void ContentTypeOf(const TextInputType type, std::uint32_t& hint, std::uint32_t& purpose)
        {
            hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE;
            purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
            switch (type)
            {
                case TextInputType::Default:
                case TextInputType::Text:
                    break;
                case TextInputType::TextName:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NAME;
                    break;
                case TextInputType::TextEmail:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL;
                    break;
                case TextInputType::TextUsername:
                    hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_LATIN;
                    break;
                case TextInputType::TextPasswordHidden:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
                    hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
                    break;
                case TextInputType::TextPasswordVisible:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD;
                    hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
                    break;
                case TextInputType::Number:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER;
                    break;
                case TextInputType::NumberPasswordHidden:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN;
                    hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
                    break;
                case TextInputType::NumberPasswordVisible:
                    purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN;
                    hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
                    break;
            }
        }

        WaylandTextInput::SeatInput* SeatOf(void* data)
        {
            return static_cast<WaylandTextInput::SeatInput*>(data);
        }

    } // namespace
#endif

    WaylandTextInput::WaylandTextInput(Host host) : host_(std::move(host)) {}

    WaylandTextInput::~WaylandTextInput()
    {
        while (!seats_.empty())
        {
            DetachSeat(seats_.back()->seat);
        }
    }

    void WaylandTextInput::AttachSeat(void* manager, wl_seat* seat)
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        if (manager == nullptr || seat == nullptr)
        {
            return;
        }
        static const zwp_text_input_v3_listener listener = {
            .enter = [](void* data, zwp_text_input_v3*, wl_surface* surface) {
                SeatInput* input = SeatOf(data);
                WaylandTextInput* self = input->owner;
                input->focus = self->host_.resolveSurface && surface != nullptr ? self->host_.resolveSurface(surface) : 0;
                if (input->focus != 0 && self->IsActive(input->focus))
                {
                    self->Enable(*input);
                }
            },
            .leave = [](void* data, zwp_text_input_v3*, wl_surface*) {
                SeatInput* input = SeatOf(data);
                WaylandTextInput* self = input->owner;
                // The protocol disables the object on leave; saying so keeps the next enable's
                // state machine exact. A composition in progress ends without text.
                if (input->composing && self->host_.post)
                {
                    TextEditingEvent ended;
                    ended.window = input->focus;
                    self->host_.post(ended);
                }
                input->composing = false;
                if (input->enabled)
                {
                    self->Disable(*input);
                }
                input->focus = 0;
            },
            .preedit_string = [](void* data, zwp_text_input_v3*, const char* text, const std::int32_t cursorBegin,
                                 const std::int32_t cursorEnd) {
                SeatInput* input = SeatOf(data);
                input->pendingPreedit = text != nullptr ? text : "";
                input->pendingCursorBegin = cursorBegin;
                input->pendingCursorEnd = cursorEnd;
                input->hasPreedit = true;
            },
            .commit_string = [](void* data, zwp_text_input_v3*, const char* text) {
                SeatInput* input = SeatOf(data);
                input->pendingCommit = text != nullptr ? text : "";
                input->hasCommit = true;
            },
            .delete_surrounding_text = [](void*, zwp_text_input_v3*, std::uint32_t, std::uint32_t) {
                // The platform does not hold the application's text, so there is nothing to
                // delete from; the contract has no event for it (see the class comment).
            },
            .done = [](void* data, zwp_text_input_v3*, std::uint32_t) {
                SeatInput* input = SeatOf(data);
                WaylandTextInput* self = input->owner;
                if (!self->host_.post || input->focus == 0)
                {
                    input->hasPreedit = input->hasCommit = false;
                    return;
                }
                // The order the protocol prescribes: the old preedit goes, text is committed, the
                // new preedit is shown.
                if (input->hasCommit && !input->pendingCommit.empty())
                {
                    if (input->composing)
                    {
                        TextEditingEvent cleared;
                        cleared.window = input->focus;
                        self->host_.post(cleared);
                        input->composing = false;
                    }
                    TextInputEvent committed;
                    committed.window = input->focus;
                    committed.text = input->pendingCommit;
                    self->host_.post(std::move(committed));
                }
                const std::string preedit = input->hasPreedit ? input->pendingPreedit : std::string();
                if (!preedit.empty() || input->composing)
                {
                    TextEditingEvent editing;
                    editing.window = input->focus;
                    editing.text = preedit;
                    if (!preedit.empty() && input->pendingCursorBegin >= 0)
                    {
                        editing.cursor = CodePointsBefore(preedit, input->pendingCursorBegin);
                        editing.selectionLength =
                            std::max(0, CodePointsBefore(preedit, input->pendingCursorEnd) - editing.cursor);
                    }
                    else
                    {
                        // -1: the input method wants no cursor shown; the end is where one would be.
                        editing.cursor = CodePointsBefore(preedit, static_cast<std::int32_t>(preedit.size()));
                    }
                    self->host_.post(std::move(editing));
                    input->composing = !preedit.empty();
                }
                input->pendingPreedit.clear();
                input->pendingCommit.clear();
                input->hasPreedit = input->hasCommit = false;
            },
        };
        auto input = std::make_unique<SeatInput>();
        input->owner = this;
        input->seat = seat;
        input->proxy = zwp_text_input_manager_v3_get_text_input(static_cast<zwp_text_input_manager_v3*>(manager), seat);
        zwp_text_input_v3_add_listener(input->proxy, &listener, input.get());
        seats_.push_back(std::move(input));
#else
        (void) manager;
        (void) seat;
#endif
    }

    void WaylandTextInput::DetachSeat(wl_seat* seat)
    {
        const auto found = std::find_if(seats_.begin(), seats_.end(),
                                        [seat](const auto& input) { return input->seat == seat; });
        if (found == seats_.end())
        {
            return;
        }
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        if ((*found)->proxy != nullptr)
        {
            zwp_text_input_v3_destroy((*found)->proxy);
            (*found)->proxy = nullptr;
        }
#endif
        seats_.erase(found);
    }

    void WaylandTextInput::ForgetWindow(const WindowId window)
    {
        active_.erase(window);
        areas_.erase(window);
        for (const auto& input : seats_)
        {
            if (input->focus == window)
            {
                if (input->enabled)
                {
                    Disable(*input);
                }
                input->focus = 0;
                input->composing = false;
            }
        }
    }

    void WaylandTextInput::Enable(SeatInput& input)
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        if (input.proxy == nullptr)
        {
            return;
        }
        // enable resets the object's state, so everything is stated again after it, and the whole
        // group takes effect on commit.
        zwp_text_input_v3_enable(input.proxy);
        std::uint32_t hint = 0;
        std::uint32_t purpose = 0;
        const auto type = active_.find(input.focus);
        ContentTypeOf(type != active_.end() ? type->second : TextInputType::Default, hint, purpose);
        zwp_text_input_v3_set_content_type(input.proxy, hint, purpose);
        const auto area = areas_.find(input.focus);
        if (area != areas_.end())
        {
            zwp_text_input_v3_set_cursor_rectangle(input.proxy, area->second.x + area->second.cursorOffset, area->second.y,
                                                   1, std::max(1, area->second.height));
        }
        zwp_text_input_v3_commit(input.proxy);
        input.enabled = true;
        if (host_.flush) { host_.flush(); }
#else
        (void) input;
#endif
    }

    void WaylandTextInput::Disable(SeatInput& input)
    {
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        if (input.proxy == nullptr)
        {
            return;
        }
        zwp_text_input_v3_disable(input.proxy);
        zwp_text_input_v3_commit(input.proxy);
        input.enabled = false;
        if (host_.flush) { host_.flush(); }
#else
        (void) input;
#endif
    }

    void WaylandTextInput::Start(const WindowId window, const TextInputType type)
    {
        if (!host_.windowExists || !host_.windowExists(window))
        {
            throw PlatformException("WaylandTextInput::Start", "no window of this platform has that id");
        }
        active_[window] = type;
        for (const auto& input : seats_)
        {
            if (input->focus == window)
            {
                Enable(*input);
            }
        }
    }

    void WaylandTextInput::Stop(const WindowId window)
    {
        if (active_.erase(window) == 0)
        {
            return;
        }
        for (const auto& input : seats_)
        {
            if (input->focus == window && input->enabled)
            {
                Disable(*input);
                if (input->composing && host_.post)
                {
                    TextEditingEvent ended;
                    ended.window = window;
                    host_.post(ended);
                }
                input->composing = false;
            }
        }
    }

    bool WaylandTextInput::IsActive(const WindowId window) const
    {
        return active_.find(window) != active_.end();
    }

    bool WaylandTextInput::IsScreenKeyboardShown(const WindowId window) const
    {
        (void) window;
        return false;
    }

    void WaylandTextInput::SetInputArea(const WindowId window, const TextInputArea& area)
    {
        areas_[window] = area;
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
        for (const auto& input : seats_)
        {
            if (input->focus == window && input->enabled && input->proxy != nullptr)
            {
                zwp_text_input_v3_set_cursor_rectangle(input->proxy, area.x + area.cursorOffset, area.y, 1,
                                                       std::max(1, area.height));
                zwp_text_input_v3_commit(input->proxy);
                if (host_.flush) { host_.flush(); }
            }
        }
#endif
    }

} // namespace CNA::Platform::Wayland
