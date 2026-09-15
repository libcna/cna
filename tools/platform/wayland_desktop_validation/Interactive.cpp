// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114: what needs a person -- real keys, a real mouse, a real input
// method -- and the one scenario that touches the user's clipboard, which must be asked for.
//
// Nothing here injects input. The interactive scenario tells the person what to do and prints
// what the backend delivered, so a validation log shows real hardware through real GNOME.

#include "Harness.hpp"

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"

#include <iostream>
#include <sstream>

namespace CnaWaylandValidation {

    int RunClipboard(const std::vector<std::string>& arguments)
    {
        if (!OptionFlag(arguments, "replace-my-clipboard"))
        {
            Skip("clipboard", "replaces the user's clipboard; run with --replace-my-clipboard to allow it");
            return 0;
        }
        Session session;
        if (!Check(session.Ok(), "clipboard.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        IPlatformClipboard* clipboard = platform.GetClipboard();
        if (clipboard == nullptr)
        {
            Skip("clipboard", "no clipboard (no seat, or no wl_data_device_manager)");
            return 0;
        }
        auto window = session.Make("CNA Wayland clipboard -- click here", 640, 200);
        auto presenter = platform.CreateSurfacePresenter(*window);
        PresentSolid(*presenter, *window, 0x305070);
        Info("the clipboard can only be set by a focused window: click the window if it does not get focus");
        if (!session.PumpUntil([&] { return window->HasFocus(); }, std::chrono::milliseconds(15000)))
        {
            Skip("clipboard", "the window never got keyboard focus");
            return 0;
        }
        const std::string before = clipboard->GetText();
        Info("the clipboard held " + std::to_string(before.size()) + " bytes of text");
        const std::string text = "CNA Wayland clipboard: P\xc5\x99\xc3\xadli\xc5\xa1 \xc5\xbelu\xc5\xa5ou\xc4\x8dk\xc3\xbd k\xc5\xaf\xc5\x88";
        clipboard->SetText(text);
        session.PumpFor(std::chrono::milliseconds(200));
        Check(clipboard->GetText() == text, "clipboard.own-text-reads-back");
        const std::string large(4u << 20, 'L');
        clipboard->SetText(large);
        session.PumpFor(std::chrono::milliseconds(200));
        Check(clipboard->GetText().size() == large.size(), "clipboard.large-text", "4 MiB");
        Info("paste into another program now to see CNA serve it; the window stays open for 10 s");
        session.PumpFor(std::chrono::milliseconds(10000));
        clipboard->SetText(before);
        session.PumpFor(std::chrono::milliseconds(200));
        Check(session.Alive(), "clipboard.no-protocol-error", session.ConnectionError());
        return 0;
    }

    int RunInteractive(const std::vector<std::string>& arguments)
    {
        const int seconds = static_cast<int>(OptionInt(arguments, "seconds", 20));
        Session session;
        if (!Check(session.Ok(), "interactive.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        auto window = session.Make("CNA Wayland interactive -- type, click, scroll; R toggles relative mode", 900, 500,
                                   true, WindowRenderIntent::None, true);
        auto presenter = platform.CreateSurfacePresenter(*window);
        PresentSolid(*presenter, *window, 0x203040);
        platform.GetTextInput()->Start(window->GetId(), TextInputType::Text);
        TextInputArea area;
        area.x = 40;
        area.y = 40;
        area.width = 400;
        area.height = 30;
        platform.GetTextInput()->SetInputArea(window->GetId(), area);
        std::cout << "\nFor the next " << seconds << " s, in the CNA window:\n"
                  << "  * type text, including accented letters, dead keys and your input method;\n"
                  << "  * click every mouse button, double-click, scroll the wheel and a touchpad;\n"
                  << "  * press R to lock the pointer (relative mode) and move the mouse, R again to release;\n"
                  << "  * resize, maximize and move the window with its title bar.\n\n";
        IPlatformMouse* mouse = platform.GetMouse();
        int keys = 0;
        int texts = 0;
        int buttons = 0;
        int wheels = 0;
        int motion = 0;
        long relativeX = 0;
        long relativeY = 0;
        std::string typed;
        const double started = NowMs();
        while (NowMs() - started < seconds * 1000.0 && session.Alive())
        {
            for (const PlatformEvent& event : session.Poll())
            {
                if (const auto* key = std::get_if<KeyEvent>(&event))
                {
                    ++keys;
                    std::cout << "  key " << (key->pressed ? "down" : "up  ") << " scancode "
                              << static_cast<int>(key->scancode) << " keycode " << static_cast<int>(key->keycode)
                              << " '" << platform.GetKeyboard()->GetKeyName(key->scancode) << "' modifiers 0x" << std::hex
                              << key->modifiers << std::dec << (key->repeat ? " (repeat)" : "") << '\n';
                    if (key->pressed && !key->repeat && key->scancode == Scancode::R)
                    {
                        const bool enable = !mouse->IsRelativeMode();
                        mouse->SetRelativeMode(window->GetId(), enable);
                        std::cout << "  relative mode " << (enable ? "ON" : "off") << '\n';
                    }
                }
                else if (const auto* text = std::get_if<TextInputEvent>(&event))
                {
                    ++texts;
                    typed += text->text;
                    std::cout << "  text \"" << text->text << "\"\n";
                }
                else if (const auto* editing = std::get_if<TextEditingEvent>(&event))
                {
                    std::cout << "  composing \"" << editing->text << "\" cursor " << editing->cursor << '\n';
                }
                else if (const auto* button = std::get_if<MouseButtonEvent>(&event))
                {
                    ++buttons;
                    std::cout << "  button " << static_cast<int>(button->button) << (button->pressed ? " down" : " up")
                              << " clicks " << static_cast<int>(button->clicks) << " at " << button->x << "," << button->y << '\n';
                }
                else if (const auto* wheel = std::get_if<MouseWheelEvent>(&event))
                {
                    ++wheels;
                    std::cout << "  wheel " << wheel->x << "," << wheel->y << " notches\n";
                }
                else if (std::get_if<MouseMotionEvent>(&event) != nullptr)
                {
                    ++motion;
                }
                else if (const auto* windowEvent = std::get_if<WindowEvent>(&event))
                {
                    std::cout << "  window event " << static_cast<int>(windowEvent->kind) << " " << windowEvent->data1 << "x"
                              << windowEvent->data2 << '\n';
                }
            }
            if (mouse->IsRelativeMode())
            {
                const MouseDelta delta = mouse->ConsumeRelativeDelta();
                relativeX += delta.x;
                relativeY += delta.y;
            }
            PresentSolid(*presenter, *window, 0x203040);
            session.Clear();
        }
        if (mouse->IsRelativeMode())
        {
            mouse->SetRelativeMode(window->GetId(), false);
        }
        std::ostringstream summary;
        summary << keys << " key events, " << texts << " text events (\"" << typed << "\"), " << buttons << " button events, "
                << wheels << " wheel events, " << motion << " motion events, relative motion " << relativeX << ","
                << relativeY;
        Info(summary.str());
        Check(session.Alive(), "interactive.no-protocol-error", session.ConnectionError());
        return 0;
    }

} // namespace CnaWaylandValidation
