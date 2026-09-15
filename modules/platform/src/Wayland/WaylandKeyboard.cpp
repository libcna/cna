// SPDX-License-Identifier: MS-PL

#include "WaylandKeyboard.hpp"

#include "../Xkb/XkbKeyMapping.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include <sys/mman.h>
#include <unistd.h>

namespace CNA::Platform::Wayland {

    namespace {

        /// The protocol's own "keycode + 8": Wayland sends evdev codes and an xkb_v1 keymap
        /// numbers keys from 8, as X did.
        constexpr std::uint32_t kEvdevOffset = 8;

        /// The repeat a keyboard has before the compositor says otherwise: wl_keyboard before v4
        /// sends no repeat_info at all, and these are the values its specification suggests.
        constexpr std::int32_t kDefaultRepeatRate = 25;
        constexpr std::int32_t kDefaultRepeatDelay = 600;

        /// The locale compose sequences are looked up for: the environment's, as xkbcommon's own
        /// documentation says, without calling setlocale (a library must not change the host
        /// process's locale).
        const char* ComposeLocale()
        {
            for (const char* variable : {"LC_ALL", "LC_CTYPE", "LANG"})
            {
                const char* value = std::getenv(variable);
                if (value != nullptr && *value != '\0')
                {
                    return value;
                }
            }
            return "C";
        }

        bool ModActive(xkb_state* state, const char* name)
        {
            return xkb_state_mod_name_is_active(state, name, XKB_STATE_MODS_EFFECTIVE) > 0;
        }

    } // namespace

    struct WaylandKeyboard::SeatKeyboard
    {
        wl_keyboard* proxy = nullptr;
        wl_seat* seat = nullptr;
        WaylandKeyboard* owner = nullptr;
        xkb_keymap* keymap = nullptr;
        xkb_state* state = nullptr;
        xkb_compose_state* compose = nullptr;
        std::vector<Scancode> scancodes;
        std::vector<KeyCode> keycodes;
        std::vector<std::uint8_t> held;
        xkb_layout_index_t layout = 0;
        WindowId focus = 0;
        std::int32_t repeatRate = kDefaultRepeatRate;
        std::int32_t repeatDelay = kDefaultRepeatDelay;
        std::uint32_t repeatKey = 0;
        std::chrono::steady_clock::time_point repeatNext{};
        std::uint16_t modifiers = 0;

        ~SeatKeyboard()
        {
            if (compose != nullptr) { xkb_compose_state_unref(compose); }
            if (state != nullptr) { xkb_state_unref(state); }
            if (keymap != nullptr) { xkb_keymap_unref(keymap); }
        }
    };

    std::uint16_t ModifiersFromXkbState(xkb_state* state)
    {
        if (state == nullptr)
        {
            return 0;
        }
        std::uint16_t result = 0;
        const auto add = [&result](const KeyModifier modifier) { result |= static_cast<std::uint16_t>(modifier); };
        if (ModActive(state, XKB_MOD_NAME_SHIFT)) { add(KeyModifier::Shift); }
        if (ModActive(state, XKB_MOD_NAME_CTRL)) { add(KeyModifier::Control); }
        if (ModActive(state, XKB_MOD_NAME_ALT)) { add(KeyModifier::Alt); }
        if (ModActive(state, XKB_MOD_NAME_LOGO)) { add(KeyModifier::Gui); }
        if (ModActive(state, XKB_MOD_NAME_CAPS)) { add(KeyModifier::CapsLock); }
        if (ModActive(state, XKB_MOD_NAME_NUM)) { add(KeyModifier::NumLock); }
        if (xkb_state_led_name_is_active(state, XKB_LED_NAME_SCROLL) > 0) { add(KeyModifier::ScrollLock); }
        // AltGr: ISO_Level3_Shift sets the virtual modifier "LevelThree", which every
        // xkeyboard-config layout maps onto the real modifier Mod5, as X conventionally did.
        // xkbcommon before 1.8 never reports a virtual modifier as active -- only real ones -- so
        // Mod5 is what is asked; the virtual name is asked too for a keymap that maps level three
        // elsewhere, which xkbcommon 1.8 and later answer.
        xkb_keymap* keymap = xkb_state_get_keymap(state);
        const bool levelThree = xkb_keymap_mod_get_index(keymap, "LevelThree") != XKB_MOD_INVALID &&
                                ModActive(state, "LevelThree");
        if (levelThree || ModActive(state, "Mod5"))
        {
            add(KeyModifier::Mode);
        }
        return result;
    }

    bool IsCommittableText(const std::string& utf8)
    {
        if (utf8.empty())
        {
            return false;
        }
        if (utf8.size() == 1)
        {
            const auto byte = static_cast<unsigned char>(utf8[0]);
            return byte >= 0x20u && byte != 0x7fu;
        }
        return true;
    }

    const wl_keyboard_listener WaylandKeyboard::kListener = {
        .keymap = [](void* data, wl_keyboard* proxy, const std::uint32_t format, const std::int32_t fd,
                     const std::uint32_t size) {
            auto* self = static_cast<WaylandKeyboard*>(data);
            if (SeatKeyboard* keyboard = self->Find(proxy))
            {
                self->OnKeymap(*keyboard, format, fd, size);
            }
            else
            {
                ::close(fd);
            }
        },
        .enter = [](void* data, wl_keyboard* proxy, const std::uint32_t serial, wl_surface* surface, wl_array* keys) {
            auto* self = static_cast<WaylandKeyboard*>(data);
            if (SeatKeyboard* keyboard = self->Find(proxy))
            {
                self->OnEnter(*keyboard, serial, surface, keys);
            }
        },
        .leave = [](void* data, wl_keyboard* proxy, const std::uint32_t serial, wl_surface*) {
            auto* self = static_cast<WaylandKeyboard*>(data);
            if (SeatKeyboard* keyboard = self->Find(proxy))
            {
                self->OnLeave(*keyboard, serial);
            }
        },
        .key = [](void* data, wl_keyboard* proxy, const std::uint32_t serial, std::uint32_t, const std::uint32_t key,
                  const std::uint32_t state) {
            auto* self = static_cast<WaylandKeyboard*>(data);
            if (SeatKeyboard* keyboard = self->Find(proxy))
            {
                self->OnKey(*keyboard, serial, key, state);
            }
        },
        .modifiers = [](void* data, wl_keyboard* proxy, const std::uint32_t serial, const std::uint32_t depressed,
                        const std::uint32_t latched, const std::uint32_t locked, const std::uint32_t group) {
            auto* self = static_cast<WaylandKeyboard*>(data);
            if (SeatKeyboard* keyboard = self->Find(proxy))
            {
                self->OnModifiers(*keyboard, serial, depressed, latched, locked, group);
            }
        },
        .repeat_info = [](void* data, wl_keyboard* proxy, const std::int32_t rate, const std::int32_t delay) {
            auto* self = static_cast<WaylandKeyboard*>(data);
            if (SeatKeyboard* keyboard = self->Find(proxy))
            {
                self->OnRepeatInfo(*keyboard, rate, delay);
            }
        },
    };

    WaylandKeyboard::WaylandKeyboard(Host host) : host_(std::move(host))
    {
        context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (context_ != nullptr)
        {
            // Compose sequences for the user's locale. A locale with no compose file (C, or a
            // container without the data) simply has no dead keys, as under X.
            composeTable_ = xkb_compose_table_new_from_locale(context_, ComposeLocale(), XKB_COMPOSE_COMPILE_NO_FLAGS);
        }
    }

    WaylandKeyboard::~WaylandKeyboard()
    {
        while (!keyboards_.empty())
        {
            Detach(keyboards_.back()->proxy);
        }
        if (composeTable_ != nullptr)
        {
            xkb_compose_table_unref(composeTable_);
        }
        if (context_ != nullptr)
        {
            xkb_context_unref(context_);
        }
    }

    WaylandKeyboard::SeatKeyboard* WaylandKeyboard::Find(wl_keyboard* keyboard) const
    {
        for (const auto& candidate : keyboards_)
        {
            if (candidate->proxy == keyboard)
            {
                return candidate.get();
            }
        }
        return nullptr;
    }

    const WaylandKeyboard::SeatKeyboard* WaylandKeyboard::GetPrimary() const
    {
        return keyboards_.empty() ? nullptr : keyboards_.front().get();
    }

    void WaylandKeyboard::Attach(wl_keyboard* keyboard, wl_seat* seat)
    {
        auto seatKeyboard = std::make_unique<SeatKeyboard>();
        seatKeyboard->proxy = keyboard;
        seatKeyboard->seat = seat;
        seatKeyboard->owner = this;
        wl_keyboard_add_listener(keyboard, &kListener, this);
        keyboards_.push_back(std::move(seatKeyboard));
    }

    void WaylandKeyboard::Detach(wl_keyboard* keyboard)
    {
        const auto found = std::find_if(keyboards_.begin(), keyboards_.end(),
                                        [keyboard](const auto& candidate) { return candidate->proxy == keyboard; });
        if (found == keyboards_.end())
        {
            return;
        }
        const WindowId focus = (*found)->focus;
        if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
        {
            wl_keyboard_release(keyboard);
        }
        else
        {
            wl_keyboard_destroy(keyboard);
        }
        keyboards_.erase(found);
        Update();
        // A keyboard that goes away while focused takes its focus with it -- told after it is
        // gone, so the window sees the seat as it now is (with no keyboard left, the compositor's
        // activated window keeps the focus, and nothing is lost).
        if (focus != 0 && host_.focusChanged)
        {
            host_.focusChanged(focus, false);
        }
    }

    void WaylandKeyboard::ForgetWindow(const WindowId window)
    {
        for (const auto& keyboard : keyboards_)
        {
            if (keyboard->focus == window)
            {
                keyboard->focus = 0;
                keyboard->repeatKey = 0;
                std::fill(keyboard->held.begin(), keyboard->held.end(), 0);
            }
        }
        Update();
    }

    void WaylandKeyboard::OnKeymap(SeatKeyboard& keyboard, const std::uint32_t format, const int fd,
                                   const std::uint32_t size)
    {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || context_ == nullptr || size == 0)
        {
            ::close(fd);
            return;
        }
        // MAP_PRIVATE: from wl_keyboard v7 the compositor may hand the same read-only file to
        // every client, and a shared writable mapping of it is refused.
        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        ::close(fd);
        if (mapped == MAP_FAILED)
        {
            return;
        }
        const char* text = static_cast<const char*>(mapped);
        // The string is NUL-terminated by the protocol; bounded anyway, so a compositor that
        // forgot the NUL cannot make xkbcommon read past the mapping.
        xkb_keymap* keymap = xkb_keymap_new_from_buffer(context_, text, strnlen(text, size),
                                                        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(mapped, size);
        if (keymap == nullptr)
        {
            return;
        }
        xkb_state* state = xkb_state_new(keymap);
        if (state == nullptr)
        {
            xkb_keymap_unref(keymap);
            return;
        }
        if (keyboard.state != nullptr) { xkb_state_unref(keyboard.state); }
        if (keyboard.keymap != nullptr) { xkb_keymap_unref(keyboard.keymap); }
        keyboard.keymap = keymap;
        keyboard.state = state;
        if (keyboard.compose != nullptr)
        {
            xkb_compose_state_unref(keyboard.compose);
            keyboard.compose = nullptr;
        }
        if (composeTable_ != nullptr)
        {
            keyboard.compose = xkb_compose_state_new(composeTable_, XKB_COMPOSE_STATE_NO_FLAGS);
        }

        // --- physical: key names --------------------------------------------------------------
        const xkb_keycode_t last = xkb_keymap_max_keycode(keymap);
        keyboard.scancodes.assign(static_cast<std::size_t>(last) + 1, Scancode::Unknown);
        keyboard.held.resize(static_cast<std::size_t>(last) + 1, 0);
        for (xkb_keycode_t keycode = xkb_keymap_min_keycode(keymap); keycode <= last; ++keycode)
        {
            if (const char* name = xkb_keymap_key_get_name(keymap, keycode))
            {
                keyboard.scancodes[keycode] = Xkb::ScancodeFromKeyName(name);
            }
        }
        keyboard.layout = 0;
        RebuildKeyCodes(keyboard);
        Update();
    }

    void WaylandKeyboard::RebuildKeyCodes(SeatKeyboard& keyboard)
    {
        if (keyboard.keymap == nullptr)
        {
            return;
        }
        // --- logical: what each key means on the ACTIVE layout ---------------------------------
        //
        // The layout of a key is the effective group clamped/wrapped by that key's own rules,
        // which xkb_state_key_get_layout applies; AltGr is a level, not a layout, so following the
        // layout cannot make keys move while AltGr is held.
        std::vector<Xkb::KeySymbols> symbols(keyboard.scancodes.size());
        const xkb_keycode_t last = xkb_keymap_max_keycode(keyboard.keymap);
        for (xkb_keycode_t keycode = xkb_keymap_min_keycode(keyboard.keymap); keycode <= last; ++keycode)
        {
            const xkb_layout_index_t layout = xkb_state_key_get_layout(keyboard.state, keycode);
            if (layout == XKB_LAYOUT_INVALID)
            {
                continue;
            }
            const xkb_keysym_t* syms = nullptr;
            if (xkb_keymap_key_get_syms_by_level(keyboard.keymap, keycode, layout, 0, &syms) > 0)
            {
                symbols[keycode].unshifted = syms[0];
            }
            if (xkb_keymap_key_get_syms_by_level(keyboard.keymap, keycode, layout, 1, &syms) > 0)
            {
                symbols[keycode].shifted = syms[0];
            }
        }
        keyboard.keycodes = Xkb::BuildKeyCodeTable(keyboard.scancodes, symbols);
    }

    void WaylandKeyboard::OnEnter(SeatKeyboard& keyboard, const std::uint32_t serial, wl_surface* surface,
                                  wl_array* keys)
    {
        if (host_.recordSerial) { host_.recordSerial(keyboard.seat, serial); }
        const WindowId window = resolveSurface_ && surface != nullptr ? resolveSurface_(surface) : 0;
        if (keyboard.focus != 0 && keyboard.focus != window && host_.focusChanged)
        {
            host_.focusChanged(keyboard.focus, false);
        }
        keyboard.focus = window;
        std::fill(keyboard.held.begin(), keyboard.held.end(), 0);
        keyboard.repeatKey = 0;
        // The keys already down when focus arrives are held: in the snapshot, since
        // Keyboard.GetState() is a level query, but with no press events -- nothing was pressed
        // in this window, and X11 reports them the same way.
        const auto* values = static_cast<const std::uint32_t*>(keys->data);
        for (std::size_t index = 0; index < keys->size / sizeof(std::uint32_t); ++index)
        {
            const std::uint32_t keycode = values[index] + kEvdevOffset;
            if (keycode < keyboard.held.size())
            {
                keyboard.held[keycode] = 1;
            }
        }
        if (window != 0 && host_.focusChanged)
        {
            host_.focusChanged(window, true);
        }
        Update();
    }

    void WaylandKeyboard::OnLeave(SeatKeyboard& keyboard, const std::uint32_t serial)
    {
        (void) serial;
        const WindowId window = keyboard.focus;
        keyboard.focus = 0;
        // The compositor sends no release for a key let go after focus left, so everything this
        // keyboard held is released now -- as X11 does on FocusOut (X11-0034), and without events,
        // as there. The alternative is a key stuck down forever.
        std::fill(keyboard.held.begin(), keyboard.held.end(), 0);
        keyboard.repeatKey = 0;
        if (keyboard.compose != nullptr)
        {
            xkb_compose_state_reset(keyboard.compose);
        }
        if (window != 0 && host_.focusChanged)
        {
            host_.focusChanged(window, false);
        }
        Update();
    }

    void WaylandKeyboard::OnKey(SeatKeyboard& keyboard, const std::uint32_t serial, const std::uint32_t key,
                                const std::uint32_t state)
    {
        if (host_.recordSerial) { host_.recordSerial(keyboard.seat, serial); }
        const std::uint32_t keycode = key + kEvdevOffset;
        const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
        if (keycode >= keyboard.held.size())
        {
            keyboard.held.resize(static_cast<std::size_t>(keycode) + 1, 0);
        }
        // A press of a key already held would be a compositor repeating it; none should, but if
        // one does it is reported as what it is.
        const bool repeat = pressed && keyboard.held[keycode] != 0;
        keyboard.held[keycode] = pressed ? 1 : 0;
        EmitKey(keyboard, keycode, pressed, repeat);
        if (pressed)
        {
            EmitText(keyboard, keycode);
            if (keyboard.keymap != nullptr && keyboard.repeatRate > 0 &&
                xkb_keymap_key_repeats(keyboard.keymap, keycode) != 0)
            {
                keyboard.repeatKey = keycode;
                keyboard.repeatNext = std::chrono::steady_clock::now() + std::chrono::milliseconds(keyboard.repeatDelay);
            }
        }
        else if (keyboard.repeatKey == keycode)
        {
            keyboard.repeatKey = 0;
        }
        Update();
    }

    void WaylandKeyboard::EmitKey(SeatKeyboard& keyboard, const std::uint32_t keycode, const bool pressed,
                                  const bool repeat)
    {
        if (!host_.post)
        {
            return;
        }
        KeyEvent event;
        event.window = keyboard.focus;
        event.scancode = keycode < keyboard.scancodes.size() ? keyboard.scancodes[keycode] : Scancode::Unknown;
        event.keycode = keycode < keyboard.keycodes.size() ? keyboard.keycodes[keycode] : KeyCode::None;
        event.modifiers = keyboard.modifiers;
        event.pressed = pressed;
        event.repeat = repeat;
        host_.post(event);
    }

    void WaylandKeyboard::EmitText(SeatKeyboard& keyboard, const std::uint32_t keycode)
    {
        if (keyboard.state == nullptr || keyboard.focus == 0 || !host_.post || composeSuppressed_ ||
            !host_.textInputActive || !host_.textInputActive(keyboard.focus))
        {
            return;
        }
        std::string text;
        const xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboard.state, keycode);
        if (keyboard.compose != nullptr && keysym != XKB_KEY_NoSymbol &&
            xkb_compose_state_feed(keyboard.compose, keysym) == XKB_COMPOSE_FEED_ACCEPTED)
        {
            switch (xkb_compose_state_get_status(keyboard.compose))
            {
                case XKB_COMPOSE_COMPOSING:
                    // A dead key or the middle of a Multi_key sequence: no text yet.
                    return;
                case XKB_COMPOSE_COMPOSED:
                {
                    char buffer[64] = {};
                    const int length = xkb_compose_state_get_utf8(keyboard.compose, buffer, sizeof(buffer));
                    xkb_compose_state_reset(keyboard.compose);
                    if (length > 0)
                    {
                        text.assign(buffer, static_cast<std::size_t>(std::min<int>(length, sizeof(buffer) - 1)));
                    }
                    break;
                }
                case XKB_COMPOSE_CANCELLED:
                    // A sequence that matches nothing ends without text, as GTK and Qt end it.
                    xkb_compose_state_reset(keyboard.compose);
                    return;
                case XKB_COMPOSE_NOTHING:
                    break;
            }
        }
        if (text.empty())
        {
            const int length = xkb_state_key_get_utf8(keyboard.state, keycode, nullptr, 0);
            if (length > 0)
            {
                text.resize(static_cast<std::size_t>(length) + 1);
                xkb_state_key_get_utf8(keyboard.state, keycode, text.data(), text.size());
                text.resize(static_cast<std::size_t>(length));
            }
        }
        if (!IsCommittableText(text))
        {
            return;
        }
        TextInputEvent input;
        input.window = keyboard.focus;
        input.text = std::move(text);
        host_.post(std::move(input));
    }

    void WaylandKeyboard::OnModifiers(SeatKeyboard& keyboard, const std::uint32_t serial, const std::uint32_t depressed,
                                      const std::uint32_t latched, const std::uint32_t locked,
                                      const std::uint32_t group)
    {
        (void) serial;
        if (keyboard.state == nullptr)
        {
            return;
        }
        // The compositor's own serialisation of the state: clients apply it with update_mask and
        // never with update_key, or modifiers held on another client's keyboard would be counted
        // twice.
        xkb_state_update_mask(keyboard.state, depressed, latched, locked, 0, 0, group);
        keyboard.modifiers = ModifiersFromXkbState(keyboard.state);
        const xkb_layout_index_t layout = xkb_state_serialize_layout(keyboard.state, XKB_STATE_LAYOUT_EFFECTIVE);
        if (layout != keyboard.layout)
        {
            // A layout switch: the logical table follows the layout, the physical one does not.
            keyboard.layout = layout;
            RebuildKeyCodes(keyboard);
        }
        Update();
    }

    void WaylandKeyboard::OnRepeatInfo(SeatKeyboard& keyboard, const std::int32_t rate, const std::int32_t delay)
    {
        // A rate of 0 turns repeating off (the protocol's own meaning).
        keyboard.repeatRate = std::max(0, rate);
        keyboard.repeatDelay = std::max(0, delay);
        if (keyboard.repeatRate == 0)
        {
            keyboard.repeatKey = 0;
        }
    }

    void WaylandKeyboard::GenerateRepeats(const std::chrono::steady_clock::time_point now)
    {
        for (const auto& keyboard : keyboards_)
        {
            if (keyboard->repeatKey == 0 || keyboard->focus == 0 || keyboard->repeatRate <= 0)
            {
                continue;
            }
            const auto interval = std::chrono::microseconds(1000000 / keyboard->repeatRate);
            // Every repeat due since the last pump, but not an unbounded burst after a long stall
            // (a frame that took a second must not type thirty characters at once).
            int produced = 0;
            while (keyboard->repeatNext <= now && produced < 4)
            {
                EmitKey(*keyboard, keyboard->repeatKey, true, true);
                EmitText(*keyboard, keyboard->repeatKey);
                keyboard->repeatNext += interval;
                ++produced;
            }
            if (keyboard->repeatNext <= now)
            {
                keyboard->repeatNext = now + interval;
            }
        }
    }

    void WaylandKeyboard::Update()
    {
        snapshot_.pressedKeys.clear();
        snapshot_.modifiers = 0;
        for (const auto& keyboard : keyboards_)
        {
            for (std::size_t keycode = 0; keycode < keyboard->held.size(); ++keycode)
            {
                if (keyboard->held[keycode] == 0 || keycode >= keyboard->keycodes.size())
                {
                    continue;
                }
                const KeyCode key = keyboard->keycodes[keycode];
                if (key != KeyCode::None &&
                    std::find(snapshot_.pressedKeys.begin(), snapshot_.pressedKeys.end(), key) ==
                        snapshot_.pressedKeys.end())
                {
                    snapshot_.pressedKeys.push_back(key);
                }
            }
            snapshot_.modifiers |= keyboard->modifiers;
        }
    }

    KeyCode WaylandKeyboard::GetKeyFromScancode(const Scancode scancode) const
    {
        const SeatKeyboard* keyboard = GetPrimary();
        if (keyboard == nullptr || scancode == Scancode::Unknown)
        {
            return KeyCode::None;
        }
        for (std::size_t keycode = 0; keycode < keyboard->scancodes.size() && keycode < keyboard->keycodes.size();
             ++keycode)
        {
            if (keyboard->scancodes[keycode] == scancode)
            {
                return keyboard->keycodes[keycode];
            }
        }
        return KeyCode::None;
    }

    std::string WaylandKeyboard::GetScancodeName(const Scancode scancode) const
    {
        // The contract's own stable name, as the X11 backend returns: a scancode's name must not
        // change with the layout.
        return ToString(scancode);
    }

    Scancode WaylandKeyboard::GetScancodeFromName(const std::string& name) const
    {
        return ScancodeFromString(name);
    }

    std::string WaylandKeyboard::GetKeyName(const Scancode scancode) const
    {
        const SeatKeyboard* keyboard = GetPrimary();
        if (keyboard == nullptr || keyboard->keymap == nullptr || scancode == Scancode::Unknown)
        {
            return {};
        }
        for (std::size_t keycode = 0; keycode < keyboard->scancodes.size(); ++keycode)
        {
            if (keyboard->scancodes[keycode] != scancode)
            {
                continue;
            }
            const auto xkbKeycode = static_cast<xkb_keycode_t>(keycode);
            const xkb_layout_index_t layout = xkb_state_key_get_layout(keyboard->state, xkbKeycode);
            const xkb_keysym_t* syms = nullptr;
            if (layout == XKB_LAYOUT_INVALID ||
                xkb_keymap_key_get_syms_by_level(keyboard->keymap, xkbKeycode, layout, 0, &syms) <= 0)
            {
                return {};
            }
            char name[64] = {};
            if (xkb_keysym_get_name(syms[0], name, sizeof(name)) <= 0)
            {
                return {};
            }
            // The keycap's label, as the X11 backend and every other backend give it: `A`, `Space`
            // -- XKB's own spelling is `a`, `space` (KeyboardKeyNameTests pins this).
            std::string label(name);
            label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
            return label;
        }
        return {};
    }

    KeyCode WaylandKeyboard::GetKeyFromName(const std::string& name) const
    {
        if (name.empty())
        {
            return KeyCode::None;
        }
        // As given, then in XKB's spelling (GetKeyName capitalises for display, and the two must
        // round-trip), then all lower case for a single letter's label.
        xkb_keysym_t keysym = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_NO_FLAGS);
        if (keysym == XKB_KEY_NoSymbol)
        {
            std::string lowered = name;
            lowered[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[0])));
            keysym = xkb_keysym_from_name(lowered.c_str(), XKB_KEYSYM_NO_FLAGS);
        }
        if (keysym == XKB_KEY_NoSymbol)
        {
            keysym = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
        }
        if (keysym == XKB_KEY_NoSymbol)
        {
            return KeyCode::None;
        }
        return Xkb::KeyCodeFromKeysym(static_cast<Xkb::Keysym>(keysym));
    }

} // namespace CNA::Platform::Wayland
