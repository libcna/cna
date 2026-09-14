// SPDX-License-Identifier: MS-PL
//
// cna_x11_desktop_validation -- plans/plan_native_platform_validation.md.
//
// Drives CNA's native X11 backend against whatever X server $DISPLAY names -- normally a real
// desktop session, which is the point -- and prints one PASS/FAIL/SKIP/INFO line per check.
//
//     cna_x11_desktop_validation <scenario> [options]
//
// Exit status: 0 when no check failed, 1 when one did, 2 for a usage error. See
// docs/testing-x11-desktop.md for what each scenario covers and what it needs.
//
// WARNING: several scenarios act on the desktop they run on. They open windows, move focus,
// toggle fullscreen, lock the pointer for moments at a time, inject input through a virtual
// keyboard or mouse, and take over the clipboard. Run them on a session you are prepared to have
// used that way.

#include "Harness.hpp"

#include <cstring>
#include <iostream>
#include <map>

namespace CnaX11Validation::Detail {
    void InstallDriverErrorHandler();
}

namespace {

    using Scenario = int (*)(const std::vector<std::string>&);

    const std::map<std::string, std::pair<Scenario, const char*>>& Scenarios()
    {
        using namespace CnaX11Validation;
        static const std::map<std::string, std::pair<Scenario, const char*>> scenarios = {
            {"info", {RunInfo, "server, session, GPU and capability report"}},
            {"lifecycle", {RunLifecycle, "create/show/hide/move/resize/minimize/maximize/focus/close/destroy"}},
            {"stress", {RunStress, "thousands of window operations with leak monitoring (--ops N)"}},
            {"keyboard", {RunKeyboard, "virtual-keyboard keys, modifiers, layouts, auto-repeat, focus loss"}},
            {"text", {RunText, "committed UTF-8 text through the input method path"}},
            {"mouse", {RunMouse, "buttons, wheel, double click, enter/leave, capture"}},
            {"relative", {RunRelative, "XInput2 raw relative mouse, grabs and focus (--cycles N)"}},
            {"clipboard", {RunClipboard, "clipboard against external X and Wayland clients"}},
            {"wm", {RunWindowManager, "the running window manager: states and fullscreen (--toggles N)"}},
            {"displays", {RunDisplays, "XRandR monitors, window-to-display mapping, DPI policy"}},
            {"glx", {RunGlx, "hardware GLX context, shaders, rendering, resize (--frames N)"}},
            {"vulkan", {RunVulkan, "VK_KHR_xlib_surface swapchain, present, resize (--frames N)"}},
            {"presenter", {RunPresenter, "software presenter: XImage and MIT-SHM paths"}},
            {"lifetime", {RunLifetime, "repeated window + GL/Vulkan resource lifetimes (--iterations N)"}},
            {"interactive", {RunInteractive, "guided session for a PERSON pressing real keys and moving the real mouse (--step N)"}},
            {"rawprobe", {RunRawProbe, "diagnostic: every XI_RawMotion event a relative-mode client receives"}},
            {"soak", {RunSoak, "long hardware-rendered session with input and state changes (--seconds N)"}},
        };
        return scenarios;
    }

    int Usage()
    {
        std::cerr << "usage: cna_x11_desktop_validation <scenario> [options]\n\nscenarios:\n";
        for (const auto& [name, entry] : Scenarios())
        {
            std::cerr << "  " << name << std::string(12 - name.size(), ' ') << entry.second << '\n';
        }
        return 2;
    }

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        return Usage();
    }
    const auto found = Scenarios().find(argv[1]);
    if (found == Scenarios().end())
    {
        return Usage();
    }
    // Before any CNA platform exists, so CNA's own error policy chains to it for the driver's
    // connection rather than being replaced by it.
    CnaX11Validation::Detail::InstallDriverErrorHandler();

    std::vector<std::string> arguments(argv + 2, argv + argc);
    const int status = found->second.first(arguments);
    const CnaX11Validation::Tally& tally = CnaX11Validation::Results();
    std::cout << "SUMMARY " << argv[1] << ": " << tally.passed << " passed, " << tally.failed
              << " failed, " << tally.skipped << " skipped" << std::endl;
    return tally.failed == 0 && status == 0 ? 0 : 1;
}
