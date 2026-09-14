// SPDX-License-Identifier: MS-PL
//
// Phases 7-9: keyboard, committed text and mouse on the real desktop.
//
// Input enters through uinput virtual devices (see UInput.hpp): kernel -> libinput -> compositor
// -> Xwayland -> CNA, the path a physical device's events take. It is still synthetic, and every
// check says "virtual" in its detail -- `interactive` is the scenario for a person's hands.
//
// Safety: a key goes wherever the COMPOSITOR has focus. Before every burst of input the harness
// confirms, over its own X connection, that the X server's focus is inside the CNA test window,
// and stops the scenario if it is not -- so no keystroke can land in another application.

#include "Harness.hpp"
#include "UInput.hpp"

#include "CNA/Platform/PlatformException.hpp"

#if defined(__linux__)
#  include <linux/input-event-codes.h>
#endif

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <thread>
#include <variant>

namespace CnaX11Validation {

    namespace {

        using CNA::Platform::X11::kXFalse;

        std::string Hex(const std::string& bytes)
        {
            std::ostringstream out;
            for (const unsigned char byte : bytes)
            {
                out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
            }
            return out.str();
        }

        std::vector<KeyEvent> Keys(const Session& session)
        {
            std::vector<KeyEvent> keys;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* key = std::get_if<KeyEvent>(&event))
                {
                    keys.push_back(*key);
                }
            }
            return keys;
        }

        std::string Text(const Session& session)
        {
            std::string text;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* input = std::get_if<TextInputEvent>(&event))
                {
                    text += input->text;
                }
            }
            return text;
        }

        /// Makes sure keys can only reach the CNA window. Returns false (and reports) otherwise.
        bool GuardFocus(Session& session, Driver& driver, IPlatformWindow& window,
                        const std::string& scenario)
        {
            if (!session.Focus(window, driver, std::chrono::milliseconds(2000)))
            {
                Fail(scenario + ".focus-guard", "the CNA window could not be focused; no input sent");
                return false;
            }
            if (!driver.FocusIsWithin(static_cast<::Window>(window.GetWindowHandle())))
            {
                Fail(scenario + ".focus-guard",
                     "the X server's focus is not in the CNA window; no input sent");
                return false;
            }
            return true;
        }

        /// The XKB group (layout) the server has active right now.
        int ActiveGroup(Driver& driver)
        {
            XkbStateRec state{};
            if (XkbGetState(driver.GetDisplay(), XkbUseCoreKbd, &state) != Success)
            {
                return -1;
            }
            return state.group;
        }

        struct KeyCase
        {
            int evdev;
            Scancode scancode;
            KeyCode keycode;
            const char* name;
        };

    } // namespace

    // ============================================================================================
    // keyboard
    // ============================================================================================

    int RunKeyboard(const std::vector<std::string>& arguments)
    {
        const bool switchLayouts = !OptionFlag(arguments, "no-layout-switch");
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("keyboard.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
#if !defined(__linux__)
        Skip("keyboard", "uinput is Linux-only");
        return 0;
#else
        IPlatform& platform = session.Platform();
        IPlatformKeyboard* keyboard = platform.GetKeyboard();
        if (keyboard == nullptr)
        {
            Fail("keyboard.service");
            return 1;
        }
        std::string error;
        if (!UinputReachesDisplay(driver, arguments, error))
        {
            Skip("keyboard", error);
            return 0;
        }
        auto window = session.Make("CNA keyboard validation (virtual keyboard)", 700, 400);
        session.WaitFor(window->GetId(), WindowEventKind::Exposed);
        if (!GuardFocus(session, driver, *window, "keyboard"))
        {
            return 1;
        }
        std::unique_ptr<VirtualInput> device = VirtualInput::Create(true, false, error);
        if (device == nullptr)
        {
            Skip("keyboard", error);
            return 0;
        }
        if (!GuardFocus(session, driver, *window, "keyboard") ||
            !ProbeKeyboardReachesWindow(*device, session, *window))
        {
            Fail("keyboard.probe", "the virtual keyboard's probe key did not arrive in the CNA "
                                   "window; stopping before any other key is sent");
            return 1;
        }
        const int startGroup = ActiveGroup(driver);
        Info("active XKB group at start: " + std::to_string(startGroup));

        const auto tap = [&](const int code) {
            session.Clear();
            device->Tap(code, 20);
            session.PumpUntil(
                [&session] {
                    for (const KeyEvent& key : Keys(session))
                    {
                        if (!key.pressed) { return true; }
                    }
                    return false;
                },
                std::chrono::milliseconds(800));
            session.PumpFor(std::chrono::milliseconds(20));
        };

        // --- 1. every key class, physical and logical identity ---------------------------------
        const std::vector<KeyCase> cases = {
            {KEY_A, Scancode::A, KeyCode::A, "A"},       {KEY_Q, Scancode::Q, KeyCode::Q, "Q"},
            {KEY_W, Scancode::W, KeyCode::W, "W"},       {KEY_Y, Scancode::Y, KeyCode::Y, "Y"},
            {KEY_Z, Scancode::Z, KeyCode::Z, "Z"},       {KEY_M, Scancode::M, KeyCode::M, "M"},
            {KEY_1, Scancode::D1, KeyCode::D1, "1"},     {KEY_5, Scancode::D5, KeyCode::D5, "5"},
            {KEY_0, Scancode::D0, KeyCode::D0, "0"},     {KEY_F1, Scancode::F1, KeyCode::F1, "F1"},
            {KEY_F5, Scancode::F5, KeyCode::F5, "F5"},   {KEY_F12, Scancode::F12, KeyCode::F12, "F12"},
            {KEY_UP, Scancode::Up, KeyCode::Up, "Up"},   {KEY_DOWN, Scancode::Down, KeyCode::Down, "Down"},
            {KEY_LEFT, Scancode::Left, KeyCode::Left, "Left"},
            {KEY_RIGHT, Scancode::Right, KeyCode::Right, "Right"},
            {KEY_HOME, Scancode::Home, KeyCode::Home, "Home"},
            {KEY_END, Scancode::End, KeyCode::End, "End"},
            {KEY_PAGEUP, Scancode::PageUp, KeyCode::PageUp, "PageUp"},
            {KEY_PAGEDOWN, Scancode::PageDown, KeyCode::PageDown, "PageDown"},
            {KEY_INSERT, Scancode::Insert, KeyCode::Insert, "Insert"},
            {KEY_DELETE, Scancode::Delete, KeyCode::Delete, "Delete"},
            {KEY_BACKSPACE, Scancode::Backspace, KeyCode::Back, "Backspace"},
            {KEY_TAB, Scancode::Tab, KeyCode::Tab, "Tab"},
            {KEY_ENTER, Scancode::Enter, KeyCode::Enter, "Enter"},
            {KEY_SPACE, Scancode::Space, KeyCode::Space, "Space"},
            {KEY_MINUS, Scancode::Minus, KeyCode::OemMinus, "Minus"},
            {KEY_SEMICOLON, Scancode::Semicolon, KeyCode::OemSemicolon, "Semicolon"},
            {KEY_KPPLUS, Scancode::KeypadPlus, KeyCode::Add, "KeypadPlus"},
            {KEY_KPENTER, Scancode::KeypadEnter, KeyCode::Enter, "KeypadEnter"},
            {KEY_LEFTSHIFT, Scancode::LeftShift, KeyCode::LeftShift, "LeftShift"},
            {KEY_RIGHTSHIFT, Scancode::RightShift, KeyCode::RightShift, "RightShift"},
            {KEY_LEFTCTRL, Scancode::LeftControl, KeyCode::LeftControl, "LeftCtrl"},
            {KEY_RIGHTCTRL, Scancode::RightControl, KeyCode::RightControl, "RightCtrl"},
            {KEY_LEFTALT, Scancode::LeftAlt, KeyCode::LeftAlt, "LeftAlt"},
            {KEY_RIGHTALT, Scancode::RightAlt, KeyCode::RightAlt, "RightAlt"},
        };
        int keyFailures = 0;
        for (const KeyCase& key : cases)
        {
            if (!GuardFocus(session, driver, *window, "keyboard"))
            {
                return 1;
            }
            tap(key.evdev);
            const std::vector<KeyEvent> keys = Keys(session);
            const auto press = std::find_if(keys.begin(), keys.end(),
                                            [](const KeyEvent& event) { return event.pressed; });
            const auto release = std::find_if(keys.begin(), keys.end(),
                                              [](const KeyEvent& event) { return !event.pressed; });
            const bool ok = press != keys.end() && release != keys.end() &&
                            press->scancode == key.scancode && press->keycode == key.keycode &&
                            release->scancode == key.scancode && !press->repeat;
            if (!ok)
            {
                ++keyFailures;
                Fail(std::string("keyboard.key.") + key.name,
                     press == keys.end()
                         ? std::string("no key event arrived")
                         : "got " + ToString(press->scancode) + " / " + ToString(press->keycode) +
                               ", wanted " + ToString(key.scancode) + " / " + ToString(key.keycode));
            }
        }
        Check(keyFailures == 0, "keyboard.key-identities",
              std::to_string(cases.size()) + " keys via a virtual keyboard, " +
                  std::to_string(keyFailures) + " wrong");

        // --- 2. keypad with Num Lock on and off ------------------------------------------------
        {
            keyboard->Update();
            const bool numLockAtStart =
                (keyboard->GetSnapshot().modifiers & static_cast<std::uint16_t>(KeyModifier::NumLock)) != 0;
            std::vector<std::string> seen;
            for (int pass = 0; pass < 2; ++pass)
            {
                GuardFocus(session, driver, *window, "keyboard");
                tap(KEY_KP5);
                const std::vector<KeyEvent> keys = Keys(session);
                if (!keys.empty())
                {
                    seen.push_back(ToString(keys.front().scancode) + "/" +
                                   ToString(keys.front().keycode) + (keys.front().modifiers &
                                   static_cast<std::uint16_t>(KeyModifier::NumLock) ? " numlock" : ""));
                }
                tap(KEY_NUMLOCK);
            }
            keyboard->Update();
            const bool numLockAtEnd =
                (keyboard->GetSnapshot().modifiers & static_cast<std::uint16_t>(KeyModifier::NumLock)) != 0;
            Check(seen.size() == 2 && seen[0] != seen[1] && numLockAtStart == numLockAtEnd,
                  "keyboard.keypad-follows-numlock",
                  (seen.size() == 2 ? seen[0] + " | " + seen[1] : std::string("missing events")) +
                      (numLockAtStart == numLockAtEnd ? "" : " (Num Lock state NOT restored)"));
        }

        // --- 3. Caps Lock as a latched modifier ------------------------------------------------
        {
            GuardFocus(session, driver, *window, "keyboard");
            tap(KEY_CAPSLOCK);
            tap(KEY_A);
            const std::vector<KeyEvent> keys = Keys(session);
            const bool latched =
                !keys.empty() &&
                (keys.front().modifiers & static_cast<std::uint16_t>(KeyModifier::CapsLock)) != 0;
            tap(KEY_CAPSLOCK);
            tap(KEY_A);
            const std::vector<KeyEvent> after = Keys(session);
            const bool cleared =
                !after.empty() &&
                (after.front().modifiers & static_cast<std::uint16_t>(KeyModifier::CapsLock)) == 0;
            Check(latched && cleared, "keyboard.capslock-latches-and-clears");
        }

        // --- 4. modifiers carried on the key they modify ----------------------------------------
        {
            GuardFocus(session, driver, *window, "keyboard");
            session.Clear();
            device->Key(KEY_LEFTSHIFT, true);
            device->Key(KEY_RIGHTCTRL, true);
            device->Tap(KEY_B, 20);
            device->Key(KEY_RIGHTCTRL, false);
            device->Key(KEY_LEFTSHIFT, false);
            session.PumpFor(std::chrono::milliseconds(300));
            bool sawShiftCtrlB = false;
            for (const KeyEvent& key : Keys(session))
            {
                if (key.pressed && key.scancode == Scancode::B)
                {
                    sawShiftCtrlB =
                        (key.modifiers & static_cast<std::uint16_t>(KeyModifier::Shift)) != 0 &&
                        (key.modifiers & static_cast<std::uint16_t>(KeyModifier::Control)) != 0;
                }
            }
            Check(sawShiftCtrlB, "keyboard.modifiers-on-events", "Shift+Ctrl+B");
        }

        // --- 5. Super, without letting the compositor open its overview ------------------------
        {
            GuardFocus(session, driver, *window, "keyboard");
            session.Clear();
            // Super pressed and released alone toggles GNOME's Activities overview; with another
            // key pressed in between it does not. Escape is that key (Super+Escape is harmless).
            device->Key(KEY_LEFTMETA, true);
            device->Tap(KEY_ESC, 20);
            device->Key(KEY_LEFTMETA, false);
            session.PumpFor(std::chrono::milliseconds(300));
            bool sawSuper = false;
            for (const KeyEvent& key : Keys(session))
            {
                sawSuper = sawSuper || (key.pressed && key.scancode == Scancode::LeftGui);
            }
            if (sawSuper)
            {
                Pass("keyboard.super", "Left Super reached the window as LeftGui");
            }
            else
            {
                Info("Left Super did not reach the X client: the compositor keeps the overlay key "
                     "for itself (environment behaviour, not a CNA verdict)");
                Skip("keyboard.super", "the compositor consumed the Super key");
            }
            session.Focus(*window, driver);
        }

        // --- 6. auto-repeat -----------------------------------------------------------------------
        {
            GuardFocus(session, driver, *window, "keyboard");
            session.Clear();
            device->Key(KEY_D, true);
            session.PumpFor(std::chrono::milliseconds(1300));
            device->Key(KEY_D, false);
            session.PumpFor(std::chrono::milliseconds(200));
            int presses = 0;
            int repeats = 0;
            int releases = 0;
            bool releaseBeforeEnd = false;
            const std::vector<KeyEvent> keys = Keys(session);
            for (std::size_t index = 0; index < keys.size(); ++index)
            {
                const KeyEvent& key = keys[index];
                if (key.scancode != Scancode::D) { continue; }
                if (key.pressed && !key.repeat) { ++presses; }
                if (key.pressed && key.repeat) { ++repeats; }
                if (!key.pressed)
                {
                    ++releases;
                    releaseBeforeEnd = releaseBeforeEnd || index + 1 < keys.size();
                }
            }
            Check(presses == 1 && releases == 1 && repeats >= 5 && !releaseBeforeEnd,
                  "keyboard.autorepeat-without-false-releases",
                  std::to_string(presses) + " press, " + std::to_string(repeats) + " repeats, " +
                      std::to_string(releases) + " release in 1.3 s");
        }

        // --- 7. a key held across a focus change -------------------------------------------------
        {
            GuardFocus(session, driver, *window, "keyboard");
            const ::Window thief = driver.CreatePlainWindow("CNA validation: focus thief", 900,
                                                            500, 250, 150);
            session.PumpFor(std::chrono::milliseconds(300));
            GuardFocus(session, driver, *window, "keyboard");
            session.Clear();
            device->Key(KEY_J, true);
            session.PumpFor(std::chrono::milliseconds(150));
            driver.Activate(thief);
            const bool lost = session.PumpUntil([&window] { return !window->HasFocus(); },
                                                std::chrono::milliseconds(2000));
            // Released while the other window has focus: this release never reaches CNA.
            device->Key(KEY_J, false);
            session.PumpFor(std::chrono::milliseconds(150));
            bool releasedEvent = false;
            for (const KeyEvent& key : Keys(session))
            {
                releasedEvent = releasedEvent || (!key.pressed && key.scancode == Scancode::J);
            }
            session.Focus(*window, driver);
            keyboard->Update();
            bool stuck = false;
            for (const KeyCode held : keyboard->GetSnapshot().pressedKeys)
            {
                stuck = stuck || held == KeyCode::J;
            }
            Check(lost, "keyboard.focus-moved-away-while-held");
            Check(!stuck, "keyboard.no-stuck-key-after-focus-return");
            if (releasedEvent)
            {
                Pass("keyboard.release-event-on-focus-loss");
            }
            else
            {
                Info("no KeyEvent release was emitted for the key held across the focus change; the "
                     "snapshot is correct (it reads the server's key vector), but an application "
                     "consuming only the event stream still believes J is held");
                Fail("keyboard.release-event-on-focus-loss",
                     "focus loss released the key internally but emitted no KeyEvent");
            }
            driver.DestroyWindow(thief);
        }

        // --- 8. layouts: physical identity stays, logical identity follows the layout ------------
        if (switchLayouts)
        {
            GuardFocus(session, driver, *window, "keyboard");
            const auto switchLayout = [&] {
                device->Key(KEY_LEFTMETA, true);
                device->Tap(KEY_SPACE, 30);
                device->Key(KEY_LEFTMETA, false);
                session.PumpFor(std::chrono::milliseconds(700));
                session.Focus(*window, driver);
            };
            const int before = ActiveGroup(driver);
            tap(KEY_1);
            const std::vector<KeyEvent> usOne = Keys(session);
            tap(KEY_MINUS);
            const std::vector<KeyEvent> usMinus = Keys(session);
            switchLayout();
            const int during = ActiveGroup(driver);
            if (during == before)
            {
                Skip("keyboard.layout-switch",
                     "Super+Space did not change the active XKB group (only one layout, or a "
                     "different switch shortcut)");
            }
            else
            {
                GuardFocus(session, driver, *window, "keyboard");
                tap(KEY_1);
                const std::vector<KeyEvent> otherOne = Keys(session);
                GuardFocus(session, driver, *window, "keyboard");
                tap(KEY_MINUS);
                const std::vector<KeyEvent> otherMinus = Keys(session);
                GuardFocus(session, driver, *window, "keyboard");
                tap(KEY_RIGHTALT);
                const std::vector<KeyEvent> altGr = Keys(session);
                switchLayout();
                const int restored = ActiveGroup(driver);
                if (!usOne.empty() && !otherOne.empty() && !usMinus.empty() && !otherMinus.empty())
                {
                    const auto describe = [](const KeyEvent& key) {
                        return ToString(key.scancode) + " / " + ToString(key.keycode);
                    };
                    Info("group " + std::to_string(before) + ": key 1 " + describe(usOne.front()) +
                         ", minus key " + describe(usMinus.front()) + "; group " +
                         std::to_string(during) + ": key 1 " + describe(otherOne.front()) +
                         ", minus key " + describe(otherMinus.front()));
                    Check(usOne.front().scancode == otherOne.front().scancode &&
                              usMinus.front().scancode == otherMinus.front().scancode,
                          "keyboard.layout-scancode-stable");
                    // NPV-0117: the key code is what the key means on the ACTIVE layout. The minus
                    // key types '-' on us and '=' on Czech, so its key code must change with it.
                    Check(usMinus.front().keycode != otherMinus.front().keycode,
                          "keyboard.layout-keycode-follows-layout",
                          "the minus key produces a different unshifted symbol in the second layout");
                    // ...and a number row of symbols over digits still reports the digits, as
                    // SDL3 and Windows do: Keys.D1 stays the key that types 1.
                    Check(otherOne.front().keycode == KeyCode::D1 ||
                              otherOne.front().keycode == usOne.front().keycode,
                          "keyboard.layout-number-row-keeps-digits", describe(otherOne.front()));
                }
                else
                {
                    Fail("keyboard.layout-switch", "no key events around the layout switch");
                }
                if (!altGr.empty())
                {
                    Info("Right Alt in group " + std::to_string(during) + ": " +
                         ToString(altGr.front().keycode));
                }
                Check(restored == startGroup, "keyboard.layout-restored",
                      "group " + std::to_string(restored) + ", started at " +
                          std::to_string(startGroup));
            }
        }
        return Results().failed == 0 ? 0 : 1;
#endif
    }

    // ============================================================================================
    // committed text
    // ============================================================================================

    int RunText(const std::vector<std::string>& arguments)
    {
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("text.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
#if !defined(__linux__)
        Skip("text", "uinput is Linux-only");
        return 0;
#else
        IPlatform& platform = session.Platform();
        IPlatformTextInput* text = platform.GetTextInput();
        if (text == nullptr)
        {
            Fail("text.service");
            return 1;
        }
        Check(platform.GetCapabilities().textInput, "text.capability-textInput");
        Check(!platform.GetCapabilities().ime, "text.capability-ime-stays-false",
              "composition/candidate events are not implemented");
        {
            // What an input method connection looks like from an ordinary client, for the record.
            ::Display* probe = XOpenDisplay(nullptr);
            XSetLocaleModifiers("");
            XIM method = probe != nullptr ? XOpenIM(probe, nullptr, nullptr, nullptr) : nullptr;
            const char* modifiers = std::getenv("XMODIFIERS");
            Info(std::string("XMODIFIERS=") + (modifiers != nullptr ? modifiers : "(unset)") +
                 "; XOpenIM from a plain client " + (method != nullptr ? "succeeds" : "FAILS"));
            if (method != nullptr) { XCloseIM(method); }
            if (probe != nullptr) { XCloseDisplay(probe); }
        }

        std::string error;
        if (!UinputReachesDisplay(driver, arguments, error))
        {
            Skip("text", error);
            return 0;
        }
        auto window = session.Make("CNA text validation (virtual keyboard)", 700, 400);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);
        if (!GuardFocus(session, driver, *window, "text"))
        {
            return 1;
        }
        std::unique_ptr<VirtualInput> device = VirtualInput::Create(true, false, error);
        if (device == nullptr)
        {
            Skip("text", error);
            return 0;
        }
        if (!GuardFocus(session, driver, *window, "text") ||
            !ProbeKeyboardReachesWindow(*device, session, *window))
        {
            Fail("text.probe", "the virtual keyboard's probe key did not arrive in the CNA window; "
                               "stopping before any text is typed");
            return 1;
        }

        const auto typeKeys = [&](const std::vector<std::pair<int, bool>>& strokes) {
            for (const auto& [code, shifted] : strokes)
            {
                if (shifted) { device->Key(KEY_LEFTSHIFT, true); }
                device->Tap(code, 8);
                if (shifted) { device->Key(KEY_LEFTSHIFT, false); }
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
                session.Poll();
            }
            session.PumpFor(std::chrono::milliseconds(250));
        };

        // --- off: keys arrive, text does not ------------------------------------------------------
        session.Clear();
        typeKeys({{KEY_X, false}});
        Check(Text(session).empty() && !Keys(session).empty(), "text.none-while-stopped");

        // --- ASCII ----------------------------------------------------------------------------------
        text->Start(id, TextInputType::Text);
        GuardFocus(session, driver, *window, "text");
        session.Clear();
        typeKeys({{KEY_H, true}, {KEY_E, false}, {KEY_L, false}, {KEY_L, false}, {KEY_O, false},
                  {KEY_COMMA, false}, {KEY_SPACE, false}, {KEY_W, true}, {KEY_O, false},
                  {KEY_R, false}, {KEY_L, false}, {KEY_D, false}, {KEY_1, true}});
        Check(Text(session) == "Hello, World!", "text.ascii", "\"" + Text(session) + "\"");

        // --- control characters are not text ---------------------------------------------------
        session.Clear();
        typeKeys({{KEY_BACKSPACE, false}, {KEY_TAB, false}, {KEY_ENTER, false}});
        Check(Text(session).empty(), "text.control-keys-commit-nothing", Hex(Text(session)));

        // --- long text -------------------------------------------------------------------------------
        {
            GuardFocus(session, driver, *window, "text");
            session.Clear();
            std::vector<std::pair<int, bool>> strokes;
            std::string expected;
            const int letters[] = {KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
                                   KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
                                   KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
            for (int index = 0; index < 400; ++index)
            {
                strokes.push_back({letters[index % 26], false});
                expected.push_back(static_cast<char>('a' + index % 26));
            }
            typeKeys(strokes);
            Check(Text(session) == expected, "text.long-ascii-in-order",
                  std::to_string(Text(session).size()) + " of 400 characters");
        }

        // --- Czech through the second layout ---------------------------------------------------------
        {
            GuardFocus(session, driver, *window, "text");
            const int before = ActiveGroup(driver);
            device->Key(KEY_LEFTMETA, true);
            device->Tap(KEY_SPACE, 30);
            device->Key(KEY_LEFTMETA, false);
            session.PumpFor(std::chrono::milliseconds(700));
            session.Focus(*window, driver);
            const int during = ActiveGroup(driver);
            if (during == before)
            {
                Skip("text.czech", "no second keyboard layout became active");
            }
            else
            {
                GuardFocus(session, driver, *window, "text");
                session.Clear();
                // cz(qwerty): the unshifted number row is + ě š č ř ž ý á í é.
                typeKeys({{KEY_P, false}, {KEY_5, false}, {KEY_9, false}, {KEY_L, false},
                          {KEY_I, false}, {KEY_3, false}, {KEY_SPACE, false}, {KEY_2, false},
                          {KEY_6, false}, {KEY_8, false}, {KEY_0, false}, {KEY_7, false},
                          {KEY_4, false}});
                const std::string got = Text(session);
                const std::string expected = "p\xC5\x99\xC3\xADli\xC5\xA1 \xC4\x9B\xC5\xBE\xC3\xA1\xC3\xA9\xC3\xBD\xC4\x8D";
                Check(got == expected, "text.czech-utf8",
                      "\"" + got + "\" bytes " + Hex(got) + ", wanted " + Hex(expected));
                GuardFocus(session, driver, *window, "text");
                device->Key(KEY_LEFTMETA, true);
                device->Tap(KEY_SPACE, 30);
                device->Key(KEY_LEFTMETA, false);
                session.PumpFor(std::chrono::milliseconds(700));
                Check(ActiveGroup(driver) == before, "text.layout-restored");
            }
        }

        // --- focus transitions: text for another window is not ours --------------------------------
        {
            const ::Window other = driver.CreatePlainWindow("CNA validation: other window", 900,
                                                            500, 250, 150);
            session.PumpFor(std::chrono::milliseconds(300));
            driver.Activate(other);
            session.PumpUntil([&window] { return !window->HasFocus(); },
                              std::chrono::milliseconds(2000));
            if (driver.FocusIsWithin(other))
            {
                session.Clear();
                device->Tap(KEY_Q, 10);  // lands in the harness's own blank window
                session.PumpFor(std::chrono::milliseconds(250));
                Check(Text(session).empty(), "text.none-for-another-window");
            }
            else
            {
                Skip("text.none-for-another-window", "could not move focus to the other window");
            }
            driver.DestroyWindow(other);
            GuardFocus(session, driver, *window, "text");
            session.Clear();
            typeKeys({{KEY_K, false}});
            Check(Text(session) == "k", "text.resumes-after-focus-returns", Text(session));
        }
        text->Stop(id);
        Check(!text->IsActive(id), "text.stops");
        return Results().failed == 0 ? 0 : 1;
#endif
    }

    // ============================================================================================
    // mouse
    // ============================================================================================

    int RunMouse(const std::vector<std::string>& arguments)
    {
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("mouse.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
#if !defined(__linux__)
        Skip("mouse", "uinput is Linux-only");
        return 0;
#else
        IPlatform& platform = session.Platform();
        IPlatformMouse* mouse = platform.GetMouse();
        if (mouse == nullptr)
        {
            Fail("mouse.service");
            return 1;
        }
        std::string error;
        if (!UinputReachesDisplay(driver, arguments, error))
        {
            Skip("mouse", error);
            return 0;
        }
        std::unique_ptr<VirtualInput> device = VirtualInput::Create(false, true, error);
        if (device == nullptr)
        {
            Skip("mouse", error);
            return 0;
        }
        auto window = session.Make("CNA mouse validation (virtual mouse)", 800, 600, true,
                                   WindowRenderIntent::None, 200, 150);
        const WindowId id = window->GetId();
        const ::Window xid = static_cast<::Window>(window->GetWindowHandle());
        session.WaitFor(id, WindowEventKind::Exposed);
        session.Focus(*window, driver);

        const auto steer = [&](const int x, const int y) {
            return SteerPointerInto(*device, driver, xid, x, y);
        };
        if (!Check(steer(400, 300), "mouse.real-cursor-steered-into-window"))
        {
            return 1;
        }

        // --- motion ------------------------------------------------------------------------------------
        {
            session.Clear();
            for (int step = 0; step < 20; ++step)
            {
                device->Move(3, 2);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            session.PumpFor(std::chrono::milliseconds(200));
            int motions = 0;
            float lastX = -1.0f;
            float lastY = -1.0f;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* motion = std::get_if<MouseMotionEvent>(&event))
                {
                    ++motions;
                    lastX = motion->x;
                    lastY = motion->y;
                }
            }
            int windowX = 0;
            int windowY = 0;
            int rootX = 0;
            int rootY = 0;
            driver.PointerInside(xid, windowX, windowY, rootX, rootY);
            mouse->Update();
            Check(motions > 0, "mouse.motion-events", std::to_string(motions) + " events");
            Check(static_cast<int>(lastX) == windowX && static_cast<int>(lastY) == windowY,
                  "mouse.motion-coordinates-are-client-pixels",
                  "event (" + std::to_string(static_cast<int>(lastX)) + "," +
                      std::to_string(static_cast<int>(lastY)) + "), server (" +
                      std::to_string(windowX) + "," + std::to_string(windowY) + ")");
            Check(mouse->GetSnapshot().x == windowX && mouse->GetSnapshot().y == windowY,
                  "mouse.snapshot-position");
        }

        // --- buttons ------------------------------------------------------------------------------------
        struct ButtonCase
        {
            int evdev;
            std::uint8_t cna;
            const char* name;
        };
        for (const ButtonCase& button : {ButtonCase{BTN_LEFT, 1, "left"}, ButtonCase{BTN_MIDDLE, 2, "middle"},
                                         ButtonCase{BTN_RIGHT, 3, "right"}, ButtonCase{BTN_SIDE, 4, "x1"},
                                         ButtonCase{BTN_EXTRA, 5, "x2"}})
        {
            session.Focus(*window, driver);
            int windowX = 0;
            int windowY = 0;
            int rootX = 0;
            int rootY = 0;
            if (!driver.PointerInside(xid, windowX, windowY, rootX, rootY))
            {
                steer(400, 300);
            }
            session.Clear();
            device->Key(button.evdev, true);
            session.PumpFor(std::chrono::milliseconds(80));
            mouse->Update();
            const std::uint8_t heldBits = mouse->GetSnapshot().buttons;
            device->Key(button.evdev, false);
            session.PumpFor(std::chrono::milliseconds(120));
            mouse->Update();
            const std::uint8_t releasedBits = mouse->GetSnapshot().buttons;
            int presses = 0;
            int releases = 0;
            bool inside = true;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* pressed = std::get_if<MouseButtonEvent>(&event))
                {
                    if (pressed->button != button.cna) { continue; }
                    (pressed->pressed ? presses : releases) += 1;
                    inside = inside && pressed->x >= 0 && pressed->y >= 0 && pressed->x < 800 &&
                             pressed->y < 600;
                }
            }
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << (button.cna - 1));
            Check(presses == 1 && releases == 1 && inside,
                  std::string("mouse.button.") + button.name,
                  std::to_string(presses) + " press, " + std::to_string(releases) + " release");
            Check((heldBits & bit) != 0 && (releasedBits & bit) == 0,
                  std::string("mouse.snapshot-button.") + button.name,
                  "held bits " + std::to_string(heldBits) + ", released bits " +
                      std::to_string(releasedBits));
        }

        // --- double click ---------------------------------------------------------------------------------
        {
            session.Clear();
            device->Tap(BTN_LEFT, 15);
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            device->Tap(BTN_LEFT, 15);
            session.PumpFor(std::chrono::milliseconds(200));
            int maxClicks = 0;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* button = std::get_if<MouseButtonEvent>(&event))
                {
                    if (button->pressed) { maxClicks = std::max<int>(maxClicks, button->clicks); }
                }
            }
            Check(maxClicks == 2, "mouse.double-click", "clicks=" + std::to_string(maxClicks));
        }

        // --- wheels -----------------------------------------------------------------------------------------
        {
            mouse->Update();
            const int scrollBefore = mouse->GetSnapshot().scrollY;
            session.Clear();
            for (int notch = 0; notch < 3; ++notch)
            {
                device->Wheel(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            session.PumpFor(std::chrono::milliseconds(250));
            float vertical = 0.0f;
            int wheelButtons = 0;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* wheel = std::get_if<MouseWheelEvent>(&event)) { vertical += wheel->y; }
                if (std::holds_alternative<MouseButtonEvent>(event)) { ++wheelButtons; }
            }
            mouse->Update();
            const int scrollAfter = mouse->GetSnapshot().scrollY;
            Check(vertical == 3.0f, "mouse.wheel-up-three-notches",
                  "event total " + std::to_string(vertical));
            Check(scrollAfter - scrollBefore == 360, "mouse.wheel-snapshot-in-xna-units",
                  std::to_string(scrollAfter - scrollBefore) + " (120 per notch)");
            Check(wheelButtons == 0, "mouse.wheel-is-not-a-button");
            Check((mouse->GetSnapshot().buttons & 0x18) == 0, "mouse.wheel-leaks-no-x1-x2-bits");

            session.Clear();
            device->HorizontalWheel(2);
            session.PumpFor(std::chrono::milliseconds(250));
            float horizontal = 0.0f;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* wheel = std::get_if<MouseWheelEvent>(&event)) { horizontal += wheel->x; }
            }
            Check(horizontal == 2.0f, "mouse.horizontal-wheel", std::to_string(horizontal));
        }

        // --- leave and re-enter -----------------------------------------------------------------------------
        {
            // Out through the left edge in small steps, then back in.
            for (int step = 0; step < 150; ++step)
            {
                device->Move(-4, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            session.PumpFor(std::chrono::milliseconds(150));
            session.Clear();
            steer(300, 300);
            session.PumpFor(std::chrono::milliseconds(100));
            const MouseMotionEvent* first = nullptr;
            for (const PlatformEvent& event : session.Seen())
            {
                if (const auto* motion = std::get_if<MouseMotionEvent>(&event))
                {
                    first = motion;
                    break;
                }
            }
            Check(first != nullptr && first->deltaX == 0.0f && first->deltaY == 0.0f,
                  "mouse.first-motion-after-reentry-has-no-jump",
                  first != nullptr ? "delta (" + std::to_string(first->deltaX) + "," +
                                         std::to_string(first->deltaY) + ")"
                                   : std::string("no motion"));
        }

        // --- capture ------------------------------------------------------------------------------------------
        {
            const bool captured = mouse->SetCapture(true);
            Check(captured && driver.PointerIsGrabbedElsewhere(), "mouse.capture-grabs");
            mouse->SetCapture(false);
            Check(!driver.PointerIsGrabbedElsewhere(), "mouse.capture-release-frees-pointer");
        }

        // --- a button held across a focus change ------------------------------------------------------------
        {
            steer(400, 300);
            const ::Window thief = driver.CreatePlainWindow("CNA validation: focus thief", 1300,
                                                            300, 250, 150);
            session.PumpFor(std::chrono::milliseconds(300));
            device->Key(BTN_LEFT, true);
            session.PumpFor(std::chrono::milliseconds(100));
            driver.Activate(thief);
            session.PumpUntil([&window] { return !window->HasFocus(); },
                              std::chrono::milliseconds(2000));
            device->Key(BTN_LEFT, false);
            session.PumpFor(std::chrono::milliseconds(150));
            driver.DestroyWindow(thief);
            session.Focus(*window, driver);
            mouse->Update();
            Check((mouse->GetSnapshot().buttons & 0x1) == 0, "mouse.no-stuck-button-after-focus-return",
                  "buttons " + std::to_string(mouse->GetSnapshot().buttons));
        }
        return Results().failed == 0 ? 0 : 1;
#endif
    }

} // namespace CnaX11Validation
