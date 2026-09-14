// SPDX-License-Identifier: MS-PL
//
// `interactive`: a guided session in which a PERSON presses real keys and moves the real mouse.
//
// Everything else in this harness injects input synthetically (XTest, or a uinput device), and
// says so. This is the one scenario whose evidence is a human hand on physical hardware: the
// window's title bar tells the person what to do, the background turns green or red as each
// step passes or fails, and every observation is logged in full -- scancode, key code,
// modifiers, repeat flag, the exact UTF-8 bytes of committed text, buttons, wheel notches and
// raw relative deltas -- so a surprise can be analysed afterwards rather than only counted.
//
// Escape skips the current step. The session never needs the terminal.

#include "Harness.hpp"

#include "CNA/Platform/PlatformException.hpp"

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <thread>
#include <variant>

namespace CnaX11Validation {

    namespace {

        enum class Outcome { Pending, Passed, Failed, Skipped };

        std::string Hex(const std::string& bytes)
        {
            std::ostringstream out;
            for (const unsigned char byte : bytes)
            {
                out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte)
                    << ' ';
            }
            return out.str();
        }

        std::string ModifierText(const std::uint16_t modifiers)
        {
            std::string text;
            const auto add = [&text, modifiers](const KeyModifier bit, const char* name) {
                if ((modifiers & static_cast<std::uint16_t>(bit)) != 0)
                {
                    text += text.empty() ? name : std::string("+") + name;
                }
            };
            add(KeyModifier::Shift, "Shift");
            add(KeyModifier::Control, "Ctrl");
            add(KeyModifier::Alt, "Alt");
            add(KeyModifier::Gui, "Gui");
            add(KeyModifier::CapsLock, "CapsLock");
            add(KeyModifier::NumLock, "NumLock");
            add(KeyModifier::ScrollLock, "ScrollLock");
            add(KeyModifier::Mode, "Mode");
            return text.empty() ? "none" : text;
        }

        std::string DescribeKey(const KeyEvent& key)
        {
            return std::string(key.pressed ? "press " : "release ") + ToString(key.scancode) +
                   " / " + ToString(key.keycode) + " mods=" + ModifierText(key.modifiers) +
                   (key.repeat ? " REPEAT" : "");
        }

        /// What one step has observed so far.
        struct Observed
        {
            std::vector<KeyEvent> keys;
            std::string text;
            std::vector<MouseButtonEvent> buttons;
            float wheelX = 0.0f;
            float wheelY = 0.0f;
            int motions = 0;
            MouseDelta relative{0, 0};
            bool focusLost = false;
            bool focusGained = false;
            bool escape = false;

            [[nodiscard]] int Presses(const Scancode scancode) const
            {
                int count = 0;
                for (const KeyEvent& key : keys)
                {
                    count += key.pressed && !key.repeat && key.scancode == scancode ? 1 : 0;
                }
                return count;
            }
            [[nodiscard]] int Repeats(const Scancode scancode) const
            {
                int count = 0;
                for (const KeyEvent& key : keys)
                {
                    count += key.pressed && key.repeat && key.scancode == scancode ? 1 : 0;
                }
                return count;
            }
            [[nodiscard]] int Releases(const Scancode scancode) const
            {
                int count = 0;
                for (const KeyEvent& key : keys)
                {
                    count += !key.pressed && key.scancode == scancode ? 1 : 0;
                }
                return count;
            }
            [[nodiscard]] const KeyEvent* FirstPress(const Scancode scancode) const
            {
                for (const KeyEvent& key : keys)
                {
                    if (key.pressed && key.scancode == scancode) { return &key; }
                }
                return nullptr;
            }
            [[nodiscard]] int ButtonPresses(const std::uint8_t button) const
            {
                int count = 0;
                for (const MouseButtonEvent& event : buttons)
                {
                    count += event.pressed && event.button == button ? 1 : 0;
                }
                return count;
            }
        };

        struct Context
        {
            Session& session;
            Driver& driver;
            IPlatformWindow& window;
            IPlatformKeyboard& keyboard;
            IPlatformMouse& mouse;
            IPlatformTextInput* text;
        };

        struct Step
        {
            std::string title;
            std::string instruction;
            int timeoutSeconds = 40;
            std::function<void(Context&)> begin;
            /// Decides the outcome from what has been observed; Pending keeps waiting.
            std::function<Outcome(Context&, const Observed&, std::string&)> judge;
            std::function<void(Context&)> end;
        };

        void Paint(IPlatformSurfacePresenter* presenter, const std::uint8_t red,
                   const std::uint8_t green, const std::uint8_t blue)
        {
            if (presenter == nullptr) { return; }
            static std::vector<std::uint8_t> pixels(16 * 16 * 4);
            for (std::size_t index = 0; index < pixels.size(); index += 4)
            {
                pixels[index + 0] = red;
                pixels[index + 1] = green;
                pixels[index + 2] = blue;
                pixels[index + 3] = 0xFF;
            }
            SurfaceFrame frame;
            frame.pixels = pixels.data();
            frame.width = 16;
            frame.height = 16;
            try { presenter->Present(frame); } catch (...) {}
        }

        Outcome KeyTap(const Observed& seen, const Scancode scancode, std::string& detail)
        {
            const KeyEvent* press = seen.FirstPress(scancode);
            if (press == nullptr || seen.Releases(scancode) == 0)
            {
                return Outcome::Pending;
            }
            detail = DescribeKey(*press);
            return Outcome::Passed;
        }

    } // namespace

    int RunInteractive(const std::vector<std::string>& arguments)
    {
        const int onlyStep = static_cast<int>(OptionInt(arguments, "step", 0));
        Driver driver;
        Session session;
        if (!session.Ok() || !driver.Ok())
        {
            Fail("interactive.session", session.Ok() ? "driver connection failed" : session.Error());
            return 1;
        }
        IPlatform& platform = session.Platform();
        IPlatformKeyboard* keyboard = platform.GetKeyboard();
        IPlatformMouse* mouse = platform.GetMouse();
        if (keyboard == nullptr || mouse == nullptr)
        {
            Fail("interactive.services");
            return 1;
        }
        auto window = session.Make("CNA PHYSICAL TEST - starting", 1100, 700, true,
                                   WindowRenderIntent::None, 120, 120);
        const WindowId id = window->GetId();
        session.WaitFor(id, WindowEventKind::Exposed);
        std::unique_ptr<IPlatformSurfacePresenter> presenter;
        try { presenter = platform.CreateSurfacePresenter(*window); } catch (...) {}
        Context context{session, driver, *window, *keyboard, *mouse, platform.GetTextInput()};

        std::vector<Step> steps;
        const auto keyStep = [&steps](const std::string& title, const Scancode scancode) {
            steps.push_back({title, title, 40, nullptr,
                             [scancode](Context&, const Observed& seen, std::string& detail) {
                                 return KeyTap(seen, scancode, detail);
                             },
                             nullptr});
        };

        keyStep("Click inside this window, then press and release A", Scancode::A);
        keyStep("Press and release Z", Scancode::Z);
        keyStep("Press and release the 1 key (top row, NOT the keypad)", Scancode::D1);
        keyStep("Press and release F5", Scancode::F5);
        keyStep("Press and release ESC is reserved - press BACKSPACE", Scancode::Backspace);
        steps.push_back({"Press the four ARROW keys (up, down, left, right)", "", 40, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             const bool all = seen.Presses(Scancode::Up) > 0 &&
                                              seen.Presses(Scancode::Down) > 0 &&
                                              seen.Presses(Scancode::Left) > 0 &&
                                              seen.Presses(Scancode::Right) > 0;
                             if (!all) { return Outcome::Pending; }
                             detail = "up/down/left/right all pressed";
                             return Outcome::Passed;
                         },
                         nullptr});
        steps.push_back({"Press HOME, END, PAGE UP, PAGE DOWN, INSERT, DELETE", "", 50, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             int count = 0;
                             for (const Scancode key : {Scancode::Home, Scancode::End,
                                                        Scancode::PageUp, Scancode::PageDown,
                                                        Scancode::Insert, Scancode::Delete})
                             {
                                 count += seen.Presses(key) > 0 ? 1 : 0;
                             }
                             detail = std::to_string(count) + "/6 navigation keys";
                             return count == 6 ? Outcome::Passed : Outcome::Pending;
                         },
                         nullptr});
        keyStep("Press LEFT SHIFT alone", Scancode::LeftShift);
        keyStep("Press RIGHT SHIFT alone", Scancode::RightShift);
        keyStep("Press LEFT CTRL alone", Scancode::LeftControl);
        keyStep("Press RIGHT CTRL alone", Scancode::RightControl);
        keyStep("Press LEFT ALT alone", Scancode::LeftAlt);
        keyStep("Press RIGHT ALT (AltGr) alone", Scancode::RightAlt);
        steps.push_back({"Toggle CAPS LOCK on, then press A (then Caps Lock off again later)", "",
                         40, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             const KeyEvent* press = seen.FirstPress(Scancode::A);
                             if (seen.Presses(Scancode::CapsLock) == 0 || press == nullptr)
                             {
                                 return Outcome::Pending;
                             }
                             detail = DescribeKey(*press);
                             return (press->modifiers &
                                     static_cast<std::uint16_t>(KeyModifier::CapsLock)) != 0
                                        ? Outcome::Passed
                                        : Outcome::Failed;
                         },
                         nullptr});
        keyStep("Turn CAPS LOCK off again (press it once)", Scancode::CapsLock);
        steps.push_back({"With NUM LOCK ON press KEYPAD 5; then NUM LOCK OFF and KEYPAD 5 again",
                         "", 50, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             std::vector<const KeyEvent*> fives;
                             for (const KeyEvent& key : seen.keys)
                             {
                                 if (key.pressed && key.scancode == Scancode::Keypad5)
                                 {
                                     fives.push_back(&key);
                                 }
                             }
                             if (fives.size() < 2) { return Outcome::Pending; }
                             detail = DescribeKey(*fives[0]) + " | " + DescribeKey(*fives[1]);
                             return Outcome::Passed;
                         },
                         nullptr});
        steps.push_back({"HOLD the D key down for about 2 seconds, then release it", "", 40,
                         nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             if (seen.Releases(Scancode::D) == 0) { return Outcome::Pending; }
                             const int presses = seen.Presses(Scancode::D);
                             const int repeats = seen.Repeats(Scancode::D);
                             const int releases = seen.Releases(Scancode::D);
                             detail = std::to_string(presses) + " press, " +
                                      std::to_string(repeats) + " auto-repeats, " +
                                      std::to_string(releases) + " release";
                             return presses == 1 && releases == 1 && repeats > 3
                                        ? Outcome::Passed
                                        : Outcome::Failed;
                         },
                         nullptr});
        steps.push_back(
            {"HOLD J, click another window WHILE holding, release J there, then click back here",
             "", 60, nullptr,
             [](Context& context, const Observed& seen, std::string& detail) {
                 if (!(seen.Presses(Scancode::J) > 0 && seen.focusLost && seen.focusGained &&
                       context.window.HasFocus()))
                 {
                     return Outcome::Pending;
                 }
                 context.keyboard.Update();
                 const KeyboardSnapshot& snapshot = context.keyboard.GetSnapshot();
                 bool stuck = false;
                 for (const KeyCode held : snapshot.pressedKeys)
                 {
                     stuck = stuck || held == KeyCode::J;
                 }
                 detail = std::string("released on focus loss: ") +
                          (seen.Releases(Scancode::J) > 0 ? "yes" : "NO") +
                          ", J held after return: " + (stuck ? "YES (stuck)" : "no");
                 return !stuck && seen.Releases(Scancode::J) > 0 ? Outcome::Passed
                                                                 : Outcome::Failed;
             },
             nullptr});
        steps.push_back({"Switch keyboard layout to CZECH (Super+Space), press the 1 key, switch back",
                         "", 60, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             std::vector<const KeyEvent*> ones;
                             for (const KeyEvent& key : seen.keys)
                             {
                                 if (key.pressed && key.scancode == Scancode::D1)
                                 {
                                     ones.push_back(&key);
                                 }
                             }
                             if (ones.empty()) { return Outcome::Pending; }
                             detail = "physical D1 reported as " + DescribeKey(*ones.front()) +
                                      " (US layout reports D1 there)";
                             return Outcome::Passed;
                         },
                         nullptr});
        steps.push_back({"Text: switch to CZECH, type  prilis  with accents (p r-hacek i-carka l i s-hacek), then Enter",
                         "", 90,
                         [](Context& context) {
                             if (context.text != nullptr)
                             {
                                 context.text->Start(context.window.GetId(), TextInputType::Text);
                             }
                         },
                         [](Context&, const Observed& seen, std::string& detail) {
                             if (seen.Presses(Scancode::Enter) == 0) { return Outcome::Pending; }
                             detail = "committed \"" + seen.text + "\" bytes: " + Hex(seen.text);
                             return seen.text == "p\xC5\x99\xC3\xADli\xC5\xA1" ? Outcome::Passed
                                                                              : Outcome::Failed;
                         },
                         [](Context& context) {
                             if (context.text != nullptr)
                             {
                                 context.text->Stop(context.window.GetId());
                             }
                         }});
        steps.push_back({"Text: in US layout type  Hello, World!  then Enter", "", 60,
                         [](Context& context) {
                             if (context.text != nullptr)
                             {
                                 context.text->Start(context.window.GetId(), TextInputType::Text);
                             }
                         },
                         [](Context&, const Observed& seen, std::string& detail) {
                             if (seen.Presses(Scancode::Enter) == 0) { return Outcome::Pending; }
                             detail = "committed \"" + seen.text + "\"";
                             return seen.text == "Hello, World!" ? Outcome::Passed : Outcome::Failed;
                         },
                         [](Context& context) {
                             if (context.text != nullptr)
                             {
                                 context.text->Stop(context.window.GetId());
                             }
                         }});
        const auto clickStep = [&steps](const std::string& title, const std::uint8_t button) {
            steps.push_back({title, "", 40, nullptr,
                             [button](Context&, const Observed& seen, std::string& detail) {
                                 if (seen.ButtonPresses(button) == 0) { return Outcome::Pending; }
                                 detail = "CNA button " + std::to_string(button);
                                 return Outcome::Passed;
                             },
                             nullptr});
        };
        clickStep("LEFT-click inside this window", 1);
        clickStep("MIDDLE-click (press the wheel) inside this window", 2);
        clickStep("RIGHT-click inside this window", 3);
        clickStep("Press the BACK side button (thumb) inside this window", 4);
        clickStep("Press the FORWARD side button (thumb) inside this window", 5);
        steps.push_back({"DOUBLE-click (left) inside this window", "", 40, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             for (const MouseButtonEvent& event : seen.buttons)
                             {
                                 if (event.pressed && event.button == 1 && event.clicks >= 2)
                                 {
                                     detail = "clicks=" + std::to_string(event.clicks);
                                     return Outcome::Passed;
                                 }
                             }
                             return Outcome::Pending;
                         },
                         nullptr});
        steps.push_back({"Scroll the wheel UP 3 notches, slowly (pointer inside the window)", "", 40,
                         nullptr,
                         [](Context& context, const Observed& seen, std::string& detail) {
                             if (seen.wheelY < 3.0f) { return Outcome::Pending; }
                             context.mouse.Update();
                             detail = "wheel y total " + std::to_string(seen.wheelY) +
                                      " notches, snapshot scrollY " +
                                      std::to_string(context.mouse.GetSnapshot().scrollY);
                             return Outcome::Passed;
                         },
                         nullptr});
        steps.push_back({"Scroll the wheel DOWN 3 notches", "", 40, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             if (seen.wheelY > -3.0f) { return Outcome::Pending; }
                             detail = "wheel y total " + std::to_string(seen.wheelY);
                             return Outcome::Passed;
                         },
                         nullptr});
        steps.push_back({"TILT the wheel LEFT, then RIGHT (skip with Esc if your wheel cannot)", "",
                         40, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             (void) seen;
                             static float left = 0.0f;
                             static float right = 0.0f;
                             left = std::min(left, seen.wheelX);
                             right = std::max(right, seen.wheelX);
                             detail = "horizontal wheel seen: " + std::to_string(seen.wheelX);
                             return seen.wheelX != 0.0f ? Outcome::Passed : Outcome::Pending;
                         },
                         nullptr});
        steps.push_back({"RELATIVE MODE ON (pointer locked): move the mouse to the RIGHT ~5 cm", "",
                         40,
                         [](Context& context) {
                             try
                             {
                                 context.mouse.SetRelativeMode(context.window.GetId(), true);
                             }
                             catch (const std::exception& error)
                             {
                                 Info(std::string("SetRelativeMode refused: ") + error.what());
                             }
                         },
                         [](Context&, const Observed& seen, std::string& detail) {
                             detail = "relative delta (" + Signed(seen.relative.x) + ", " +
                                      Signed(seen.relative.y) + ")";
                             return seen.relative.x > 300 ? Outcome::Passed : Outcome::Pending;
                         },
                         nullptr});
        steps.push_back({"Still locked: move the mouse UP ~5 cm", "", 40, nullptr,
                         [](Context&, const Observed& seen, std::string& detail) {
                             detail = "relative delta (" + Signed(seen.relative.x) + ", " +
                                      Signed(seen.relative.y) + ")";
                             return seen.relative.y < -300 ? Outcome::Passed : Outcome::Pending;
                         },
                         nullptr});
        steps.push_back({"Still locked: press ALT+TAB to another window, then click back into this one",
                         "", 60, nullptr,
                         [](Context& context, const Observed& seen, std::string& detail) {
                             static bool sampled = false;
                             static bool grabbedWhileAway = false;
                             if (seen.focusLost && !sampled)
                             {
                                 std::this_thread::sleep_for(std::chrono::milliseconds(300));
                                 grabbedWhileAway = context.driver.PointerIsGrabbedElsewhere();
                                 sampled = true;
                             }
                             if (!(seen.focusLost && seen.focusGained && context.window.HasFocus()))
                             {
                                 return Outcome::Pending;
                             }
                             detail = std::string("pointer grab while another window was active: ") +
                                      (grabbedWhileAway ? "STILL HELD" : "released");
                             return grabbedWhileAway ? Outcome::Failed : Outcome::Passed;
                         },
                         [](Context& context) {
                             context.mouse.SetRelativeMode(context.window.GetId(), false);
                         }});

        int number = 0;
        for (Step& step : steps)
        {
            ++number;
            if (onlyStep != 0 && number != onlyStep)
            {
                continue;
            }
            const std::string label = "interactive." + std::to_string(number);
            window->SetTitle("CNA PHYSICAL TEST " + std::to_string(number) + "/" +
                             std::to_string(steps.size()) + ": " + step.title +
                             "   [Esc = skip]");
            Info("step " + std::to_string(number) + ": " + step.title);
            Paint(presenter.get(), 0x50, 0x50, 0x50);
            session.Focus(*window, driver, std::chrono::milliseconds(1500));
            session.Clear();
            if (step.begin) { step.begin(context); }

            Observed seen;
            Outcome outcome = Outcome::Pending;
            std::string detail;
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::seconds(step.timeoutSeconds);
            while (outcome == Outcome::Pending && std::chrono::steady_clock::now() < deadline)
            {
                for (const PlatformEvent& event : session.Poll())
                {
                    if (const auto* key = std::get_if<KeyEvent>(&event))
                    {
                        Info("  key " + DescribeKey(*key));
                        if (key->scancode == Scancode::Escape && key->pressed)
                        {
                            seen.escape = true;
                        }
                        else
                        {
                            seen.keys.push_back(*key);
                        }
                    }
                    else if (const auto* text = std::get_if<TextInputEvent>(&event))
                    {
                        Info("  text \"" + text->text + "\" bytes " + Hex(text->text));
                        seen.text += text->text;
                    }
                    else if (const auto* button = std::get_if<MouseButtonEvent>(&event))
                    {
                        Info("  button " + std::to_string(button->button) +
                             (button->pressed ? " press" : " release") +
                             " clicks=" + std::to_string(button->clicks) + " at (" +
                             std::to_string(static_cast<int>(button->x)) + "," +
                             std::to_string(static_cast<int>(button->y)) + ")");
                        seen.buttons.push_back(*button);
                    }
                    else if (const auto* wheel = std::get_if<MouseWheelEvent>(&event))
                    {
                        Info("  wheel x=" + std::to_string(wheel->x) +
                             " y=" + std::to_string(wheel->y));
                        seen.wheelX += wheel->x;
                        seen.wheelY += wheel->y;
                    }
                    else if (std::holds_alternative<MouseMotionEvent>(event))
                    {
                        ++seen.motions;
                    }
                    else if (const auto* change = std::get_if<WindowEvent>(&event))
                    {
                        if (change->window == id && change->kind == WindowEventKind::FocusLost)
                        {
                            seen.focusLost = true;
                            Info("  focus lost");
                        }
                        if (change->window == id && change->kind == WindowEventKind::FocusGained)
                        {
                            seen.focusGained = seen.focusLost;
                            Info("  focus gained");
                        }
                    }
                }
                mouse->Update();
                const MouseDelta delta = mouse->ConsumeRelativeDelta();
                seen.relative.x += delta.x;
                seen.relative.y += delta.y;
                if (seen.escape)
                {
                    outcome = Outcome::Skipped;
                    break;
                }
                outcome = step.judge(context, seen, detail);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (step.end) { step.end(context); }

            switch (outcome)
            {
                case Outcome::Passed:
                    Pass(label, step.title + " -- " + detail);
                    Paint(presenter.get(), 0x20, 0xB0, 0x40);
                    break;
                case Outcome::Failed:
                    Fail(label, step.title + " -- " + detail);
                    Paint(presenter.get(), 0xC0, 0x20, 0x20);
                    break;
                case Outcome::Skipped:
                    Skip(label, step.title + " -- skipped by the person with Escape");
                    break;
                case Outcome::Pending:
                    Fail(label, step.title + " -- timed out" +
                                    (detail.empty() ? std::string() : " (" + detail + ")"));
                    Paint(presenter.get(), 0xC0, 0x20, 0x20);
                    break;
            }
            session.PumpFor(std::chrono::milliseconds(500));
        }
        window->SetTitle("CNA PHYSICAL TEST - finished, thank you. You can close this window.");
        session.PumpFor(std::chrono::milliseconds(1500));
        return Results().failed == 0 ? 0 : 1;
    }

} // namespace CnaX11Validation
