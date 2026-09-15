// SPDX-License-Identifier: MS-PL
//
// cna_wayland_desktop_validation -- plans/plan_wayland.md WAYLAND-0114.
//
// Drives CNA's native Wayland backend against whatever compositor WAYLAND_DISPLAY names --
// normally a real desktop session, which is the point -- and prints one PASS/FAIL/SKIP/INFO line
// per check.
//
//     cna_wayland_desktop_validation <scenario> [options]
//
// Exit status: 0 when no check failed, 1 when one did, 2 for a usage error. docs/platform-wayland.md
// describes every scenario.
//
// Several scenarios act on the desktop they run on: they open windows, maximize them and make them
// fullscreen for moments at a time. None injects input -- the `interactive` scenario asks a person
// instead -- and only `clipboard`, which must be asked for with --replace-my-clipboard, touches the
// user's clipboard.

#include "Harness.hpp"

#include <iostream>
#include <map>

namespace {

    using Scenario = int (*)(const std::vector<std::string>&);

    const std::map<std::string, std::pair<Scenario, const char*>>& Scenarios()
    {
        using namespace CnaWaylandValidation;
        static const std::map<std::string, std::pair<Scenario, const char*>> scenarios = {
            {"info", {RunInfo, "compositor, globals, outputs, seats and the capability set"}},
            {"lifecycle", {RunLifecycle, "create/show/hide/resize/maximize/fullscreen/minimize/destroy (--windows N)"}},
            {"scale", {RunScale, "high-DPI windows at the desktop's (fractional) scale, per output"}},
            {"presenter", {RunPresenter, "wl_shm presenter: pacing, throughput, scale modes (--frames N)"}},
            {"gl", {RunGl, "EGL/OpenGL: hardware context, versions, readback, resize, swap interval (--frames N)"}},
            {"vulkan", {RunVulkan, "VK_KHR_wayland_surface swapchain, present, resize (--frames N, --validation)"}},
            {"lifetime", {RunLifetime, "repeated window + shm/GL/Vulkan lifetimes with leak monitoring (--iterations N)"}},
            {"stress", {RunStress, "thousands of window operations with leak monitoring (--ops N)"}},
            {"soak", {RunSoak, "long rendered session with state changes (--seconds N)"}},
            {"clipboard", {RunClipboard, "clipboard round trips; REPLACES the user's clipboard (--replace-my-clipboard)"}},
            {"interactive", {RunInteractive, "a PERSON types, clicks, scrolls; reports what arrived (--seconds N)"}},
        };
        return scenarios;
    }

    int Usage()
    {
        std::cerr << "usage: cna_wayland_desktop_validation <scenario> [options]\n\nscenarios:\n";
        for (const auto& [name, entry] : Scenarios())
        {
            std::cerr << "  " << name << std::string(14 - name.size(), ' ') << entry.second << '\n';
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
    std::vector<std::string> arguments(argv + 2, argv + argc);
    const int status = found->second.first(arguments);
    const CnaWaylandValidation::Tally& tally = CnaWaylandValidation::Results();
    std::cout << "SUMMARY " << argv[1] << ": " << tally.passed << " passed, " << tally.failed << " failed, "
              << tally.skipped << " skipped" << std::endl;
    return tally.failed == 0 && status == 0 ? 0 : 1;
}
