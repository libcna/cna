// SPDX-License-Identifier: MS-PL

#include "X11Keyboard.hpp"

#include "X11Display.hpp"
#include "../Xkb/XkbKeyMapping.hpp"

#include <X11/keysym.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace CNA::Platform::X11 {

    // The XKB tables are shared with the Wayland backend (plans/plan_wayland.md WAYLAND-0010):
    // key names and keysyms mean the same thing to an X server and to xkbcommon, so one table
    // gives a key the same Scancode and KeyCode under both.
    Scancode ScancodeFromXkbKeyName(const char name[4])
    {
        return Xkb::ScancodeFromKeyNameField(name);
    }

    KeyCode KeyCodeFromKeysym(const KeySym keysym)
    {
        return Xkb::KeyCodeFromKeysym(static_cast<Xkb::Keysym>(keysym));
    }

    KeyCode UsLayoutKeyCode(const Scancode scancode)
    {
        return Xkb::UsLayoutKeyCode(scancode);
    }

    std::array<KeyCode, kX11KeycodeCount> BuildKeyCodeTable(
        const std::array<Scancode, kX11KeycodeCount>& scancodes,
        const std::array<X11KeySymbols, kX11KeycodeCount>& symbols)
    {
        std::array<Xkb::KeySymbols, kX11KeycodeCount> shared{};
        for (std::size_t keycode = 0; keycode < kX11KeycodeCount; ++keycode)
        {
            shared[keycode].unshifted = static_cast<Xkb::Keysym>(symbols[keycode].unshifted);
            shared[keycode].shifted = static_cast<Xkb::Keysym>(symbols[keycode].shifted);
        }
        const std::vector<KeyCode> built = Xkb::BuildKeyCodeTable(scancodes, shared);
        std::array<KeyCode, kX11KeycodeCount> table{};
        std::copy(built.begin(), built.end(), table.begin());
        return table;
    }

    std::uint16_t ModifiersFromXState(const unsigned int state, const unsigned int modeSwitchMask)
    {
        std::uint16_t result = 0;
        const auto add = [&result](const KeyModifier modifier) {
            result |= static_cast<std::uint16_t>(modifier);
        };
        if ((state & ShiftMask) != 0) { add(KeyModifier::Shift); }
        if ((state & ControlMask) != 0) { add(KeyModifier::Control); }
        if ((state & Mod1Mask) != 0) { add(KeyModifier::Alt); }
        if ((state & Mod4Mask) != 0) { add(KeyModifier::Gui); }
        if ((state & LockMask) != 0) { add(KeyModifier::CapsLock); }
        // Num Lock is conventionally Mod2 and Scroll Lock Mod5 or Mod3, but the assignment is a
        // layout's choice rather than a protocol constant. Mod2 is universal enough to hardcode;
        // AltGr's bit is looked up from the keyboard mapping instead, which is what
        // modeSwitchMask carries.
        if ((state & Mod2Mask) != 0) { add(KeyModifier::NumLock); }
        if (modeSwitchMask != 0 && (state & modeSwitchMask) != 0) { add(KeyModifier::Mode); }
        return result;
    }

    X11Keyboard::X11Keyboard(X11Connection& connection) : connection_(connection)
    {
        scancodes_.fill(Scancode::Unknown);
        keycodes_.fill(KeyCode::None);
        held_.fill(false);
        RefreshKeyboardMapping();
    }

    KeySym X11Keyboard::KeysymAt(const XkbDescPtr description, const unsigned int keycode,
                                 const int level) const
    {
        Display* display = connection_.GetDisplay();
        if (description != nullptr)
        {
            // A key can define fewer groups than the keymap has -- F1 has one, a letter has as
            // many as there are layouts -- and XKB says per key what an out-of-range group means:
            // wrap, clamp or redirect. XkbKeycodeToKeysym does not apply that rule itself.
            int group = group_;
            if (keycode >= description->min_key_code && keycode <= description->max_key_code)
            {
                const int groups = XkbKeyNumGroups(description, keycode);
                const unsigned char info = XkbKeyGroupInfo(description, keycode);
                if (groups > 0 && group >= groups)
                {
                    switch (XkbOutOfRangeGroupAction(info))
                    {
                        case XkbRedirectIntoRange:
                            group = XkbOutOfRangeGroupNumber(info);
                            if (group >= groups) { group = 0; }
                            break;
                        case XkbClampIntoRange:
                            group = groups - 1;
                            break;
                        default:
                            group %= groups;
                            break;
                    }
                }
            }
            const KeySym keysym = XkbKeycodeToKeysym(display, static_cast<::KeyCode>(keycode),
                                                     group, level);
            if (keysym != NoSymbol)
            {
                return keysym;
            }
        }
        // The pre-XKB path, still correct and still needed for a server with no XKB: the core
        // mapping's first two columns are group 1's unshifted and shifted symbols.
        XKeyEvent probe{};
        probe.display = display;
        probe.keycode = keycode;
        probe.state = 0;
        return XLookupKeysym(&probe, level);
    }

    void X11Keyboard::RefreshKeyboardMapping()
    {
        Display* display = connection_.GetDisplay();

        scancodes_.fill(Scancode::Unknown);
        keycodes_.fill(KeyCode::None);

        // --- physical: XKB key names ---------------------------------------------------------
        if (connection_.HasXkb())
        {
            XkbDescPtr description = XkbGetMap(display, 0, XkbUseCoreKbd);
            if (description != nullptr)
            {
                if (XkbGetNames(display, XkbKeyNamesMask, description) == Success &&
                    description->names != nullptr && description->names->keys != nullptr)
                {
                    const int first = std::max<int>(description->min_key_code, kMinKeycode);
                    const int last = std::min<int>(description->max_key_code, kMaxKeycode);
                    for (int keycode = first; keycode <= last; ++keycode)
                    {
                        scancodes_[static_cast<std::size_t>(keycode)] =
                            ScancodeFromXkbKeyName(description->names->keys[keycode].name);
                    }
                }
                XkbFreeKeyboard(description, 0, True);
            }
        }

        // --- logical: what each key means on the ACTIVE layout ----------------------------------
        //
        // The active layout is the effective XKB group. The first version always read group 0,
        // so switching from "us" to "cz" (group 1) rebuilt the same US table: the layout switch
        // this refresh exists for never reached a single key code
        // (plans/plan_native_platform_validation.md NPV-0117). AltGr does not change the group
        // in XKB -- it is a shift *level* -- so following the group cannot make keys move while
        // AltGr is held.
        group_ = 0;
        if (connection_.HasXkb())
        {
            XkbStateRec state{};
            if (XkbGetState(display, XkbUseCoreKbd, &state) == Success)
            {
                group_ = state.group;
            }
        }
        // One request for every key's group layout, rather than one per key.
        XkbDescPtr description =
            connection_.HasXkb() ? XkbGetMap(display, XkbKeySymsMask, XkbUseCoreKbd) : nullptr;
        std::array<X11KeySymbols, kKeycodeCount> symbols{};
        for (int keycode = kMinKeycode; keycode <= kMaxKeycode; ++keycode)
        {
            symbols[static_cast<std::size_t>(keycode)].unshifted =
                KeysymAt(description, static_cast<unsigned int>(keycode), 0);
            symbols[static_cast<std::size_t>(keycode)].shifted =
                KeysymAt(description, static_cast<unsigned int>(keycode), 1);
        }
        keycodes_ = BuildKeyCodeTable(scancodes_, symbols);
        if (description != nullptr)
        {
            XkbFreeKeyboard(description, 0, True);
        }

        // --- which modifier bit is AltGr ------------------------------------------------------
        //
        // Hardcoding Mod5 would be wrong on layouts that put Mode_switch elsewhere, and there is
        // no constant for it: the mapping is data the server owns. Reading it costs one round
        // trip, here rather than per event.
        modeSwitchMask_ = 0;
        XModifierKeymap* modifiers = XGetModifierMapping(display);
        if (modifiers != nullptr)
        {
            for (int modifier = 0; modifier < 8; ++modifier)
            {
                for (int slot = 0; slot < modifiers->max_keypermod; ++slot)
                {
                    const int index = modifier * modifiers->max_keypermod + slot;
                    const unsigned int keycode = modifiers->modifiermap[index];
                    if (keycode == 0)
                    {
                        continue;
                    }
                    const KeySym keysym =
                        XkbKeycodeToKeysym(display, static_cast<::KeyCode>(keycode), 0, 0);
                    if (keysym == XK_Mode_switch || keysym == XK_ISO_Level3_Shift)
                    {
                        modeSwitchMask_ = 1u << modifier;
                    }
                }
            }
            XFreeModifiermap(modifiers);
        }
    }

    void X11Keyboard::Update()
    {
        Display* display = connection_.GetDisplay();

        // XQueryKeymap is the server's own answer to "which keys are physically down right now",
        // which is exactly what a level query needs and what accumulated press/release events
        // cannot give after a missed event. The bit vector is 32 bytes, one bit per keycode.
        char keys[32] = {};
        XQueryKeymap(display, keys);

        snapshot_.pressedKeys.clear();
        held_.fill(false);
        for (int keycode = kMinKeycode; keycode <= kMaxKeycode; ++keycode)
        {
            const bool down =
                (keys[keycode / 8] & (1 << (keycode % 8))) != 0;
            if (!down)
            {
                continue;
            }
            held_[static_cast<std::size_t>(keycode)] = true;
            const KeyCode key = keycodes_[static_cast<std::size_t>(keycode)];
            if (key != KeyCode::None &&
                std::find(snapshot_.pressedKeys.begin(), snapshot_.pressedKeys.end(), key) ==
                    snapshot_.pressedKeys.end())
            {
                snapshot_.pressedKeys.push_back(key);
            }
        }

        unsigned int state = 0;
        if (connection_.HasXkb())
        {
            XkbStateRec xkbState{};
            if (XkbGetState(display, XkbUseCoreKbd, &xkbState) == Success)
            {
                // `mods` is the effective modifier set: held plus latched plus locked, which is
                // what a level query means by "Caps Lock is on".
                state = xkbState.mods | xkbState.locked_mods;
            }
        }
        if (state == 0)
        {
            ::Window root = kNone;
            ::Window child = kNone;
            int rootX = 0;
            int rootY = 0;
            int windowX = 0;
            int windowY = 0;
            unsigned int mask = 0;
            if (XQueryPointer(display, connection_.GetRoot(), &root, &child, &rootX, &rootY,
                              &windowX, &windowY, &mask) == True)
            {
                state = mask;
            }
        }
        snapshot_.modifiers = ModifiersFromXState(state, modeSwitchMask_);
    }

    Scancode X11Keyboard::GetScancode(const unsigned int keycode) const
    {
        if (keycode >= kKeycodeCount)
        {
            return Scancode::Unknown;
        }
        return scancodes_[keycode];
    }

    KeyCode X11Keyboard::GetKeyCode(const unsigned int keycode) const
    {
        if (keycode >= kKeycodeCount)
        {
            return KeyCode::None;
        }
        return keycodes_[keycode];
    }

    KeyCode X11Keyboard::GetKeyFromScancode(const Scancode scancode) const
    {
        if (scancode == Scancode::Unknown)
        {
            return KeyCode::None;
        }
        for (std::size_t keycode = kMinKeycode; keycode < kKeycodeCount; ++keycode)
        {
            if (scancodes_[keycode] == scancode)
            {
                return keycodes_[keycode];
            }
        }
        return KeyCode::None;
    }

    std::string X11Keyboard::GetScancodeName(const Scancode scancode) const
    {
        // The contract's own stable name, not an X one. A scancode name must not change with the
        // layout -- that is the whole distinction between a scancode and a key code -- and X has
        // no layout-independent key label to offer.
        return ToString(scancode);
    }

    Scancode X11Keyboard::GetScancodeFromName(const std::string& name) const
    {
        return ScancodeFromString(name);
    }

    std::string X11Keyboard::GetKeyName(const Scancode scancode) const
    {
        if (scancode == Scancode::Unknown)
        {
            return {};
        }
        Display* display = connection_.GetDisplay();
        for (std::size_t keycode = kMinKeycode; keycode < kKeycodeCount; ++keycode)
        {
            if (scancodes_[keycode] != scancode)
            {
                continue;
            }
            // The label on the ACTIVE layout, as the contract says, so a user who switched to
            // "cz" is shown the key they have now.
            XkbDescPtr description =
                connection_.HasXkb() ? XkbGetMap(display, XkbKeySymsMask, XkbUseCoreKbd) : nullptr;
            const KeySym keysym = KeysymAt(description, static_cast<unsigned int>(keycode), 0);
            if (description != nullptr)
            {
                XkbFreeKeyboard(description, 0, True);
            }
            if (keysym == NoSymbol)
            {
                return {};
            }
            const char* name = XKeysymToString(keysym);
            if (name == nullptr)
            {
                return {};
            }

            // X's keysym names are the protocol's own spelling, not a label: the A key is `a` and
            // the space bar is `space`. This method's contract is the name a key-binding UI shows
            // the user, which is what is printed on the keycap -- `A` and `Space`. The existing
            // cross-implementation suite pins exactly that (KeyboardKeyNameTests asserts `"A"`,
            // `"Z"` and `"Space"`), and it caught this: the first implementation returned X's
            // spelling unchanged and the whole backend disagreed with every other one.
            std::string label(name);
            if (!label.empty())
            {
                label[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(label[0])));
            }
            return label;
        }
        return {};
    }

    KeyCode X11Keyboard::GetKeyFromName(const std::string& name) const
    {
        if (name.empty())
        {
            return KeyCode::None;
        }

        // Tried as given first, because a caller may legitimately pass X's own spelling.
        KeySym keysym = XStringToKeysym(name.c_str());

        // Then as X spells it. GetKeyName capitalises for display, so `Space` must resolve even
        // though X calls that keysym `space` -- otherwise the name this very class produces does
        // not round-trip through it, which is what KeyboardKeyNameTests.NameToKeyReversesKeyToName
        // asserts across every implementation.
        if (keysym == NoSymbol)
        {
            std::string lowered = name;
            lowered[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[0])));
            keysym = XStringToKeysym(lowered.c_str());
        }
        if (keysym == NoSymbol)
        {
            // A single letter is the remaining case: `A` is the label, `a` is the keysym whose
            // virtual key it names, and both must answer KeyCode::A.
            std::string allLower = name;
            std::transform(allLower.begin(), allLower.end(), allLower.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            keysym = XStringToKeysym(allLower.c_str());
        }
        if (keysym == NoSymbol)
        {
            return KeyCode::None;
        }
        return KeyCodeFromKeysym(keysym);
    }

    bool X11Keyboard::TrackKeyState(const unsigned int keycode, const bool pressed)
    {
        if (keycode >= kKeycodeCount)
        {
            return false;
        }
        const bool wasHeld = held_[keycode];
        held_[keycode] = pressed;
        // A press of a key that was already held is auto-repeat. With detectable auto-repeat this
        // is the only signal there is, because the server sends no intervening release -- and it
        // is a better signal than a timeout would be, because it is the server's own state rather
        // than a guess about how fast a human types.
        return pressed && wasHeld;
    }

    void X11Keyboard::ReleaseAllKeys()
    {
        held_.fill(false);
        snapshot_.pressedKeys.clear();
    }

    bool X11Keyboard::IsKeyHeld(const unsigned int keycode) const
    {
        return keycode < kKeycodeCount && held_[keycode];
    }

} // namespace CNA::Platform::X11
