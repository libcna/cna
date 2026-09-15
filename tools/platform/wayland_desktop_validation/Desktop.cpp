// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114/0120: the desktop the backend runs on -- what the compositor
// offers, what the backend makes of it, windows through their whole life, and the scale a high-DPI
// window gets on each output.

#include "Harness.hpp"

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include "../../../modules/platform/src/Wayland/WaylandOutputs.hpp"

#include <cmath>
#include <cstdlib>
#include <sstream>

namespace CnaWaylandValidation {

    namespace {

        std::string Environment(const char* name)
        {
            const char* value = std::getenv(name);
            return value != nullptr ? value : "(unset)";
        }

        std::string Flag(const bool value)
        {
            return value ? "yes" : "no";
        }

    } // namespace

    int RunInfo(const std::vector<std::string>&)
    {
        Info("WAYLAND_DISPLAY=" + Environment("WAYLAND_DISPLAY") + " XDG_CURRENT_DESKTOP=" +
             Environment("XDG_CURRENT_DESKTOP") + " XDG_SESSION_TYPE=" + Environment("XDG_SESSION_TYPE"));
        Session session;
        if (!Check(session.Ok(), "info.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        const Wayland::WaylandGlobals& globals = platform.GetConnectionForTesting()->GetGlobals();
        std::ostringstream bound;
        bound << "wl_compositor v" << globals.compositorVersion << ", xdg_wm_base v" << globals.wmBaseVersion
              << ", wl_data_device_manager v" << globals.dataDeviceManagerVersion
              << ", subcompositor " << Flag(globals.subcompositor != nullptr)
#if defined(CNA_WAYLAND_HAVE_VIEWPORTER)
              << ", viewporter " << Flag(globals.viewporter != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_FRACTIONAL_SCALE)
              << ", fractional-scale " << Flag(globals.fractionalScaleManager != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_OUTPUT)
              << ", xdg-output v" << globals.xdgOutputManagerVersion
#endif
#if defined(CNA_WAYLAND_HAVE_RELATIVE_POINTER)
              << ", relative-pointer " << Flag(globals.relativePointerManager != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_POINTER_CONSTRAINTS)
              << ", pointer-constraints " << Flag(globals.pointerConstraints != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_CURSOR_SHAPE)
              << ", cursor-shape v" << globals.cursorShapeManagerVersion
#endif
#if defined(CNA_WAYLAND_HAVE_TEXT_INPUT_V3)
              << ", text-input-v3 " << Flag(globals.textInputManager != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_PRIMARY_SELECTION)
              << ", primary-selection " << Flag(globals.primarySelectionManager != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_DECORATION)
              << ", xdg-decoration " << Flag(globals.decorationManager != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
              << ", xdg-activation " << Flag(globals.activation != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
              << ", idle-inhibit " << Flag(globals.idleInhibitManager != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_PRESENTATION_TIME)
              << ", presentation-time " << Flag(globals.presentation != nullptr)
#endif
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
              << ", xdg-foreign " << Flag(globals.exporter != nullptr)
#endif
            ;
        Info("bound: " + bound.str());
        Check(globals.compositor != nullptr && globals.wmBase != nullptr, "info.core-globals");
        Check(platform.GetConnectionForTesting()->HasXrgb8888(), "info.shm-xrgb8888");

        const std::vector<const Wayland::WaylandOutput*> outputs = platform.GetOutputs();
        Check(!outputs.empty(), "info.outputs-described", std::to_string(outputs.size()) + " output(s)");
        for (const Wayland::WaylandOutput* output : outputs)
        {
            const Wayland::WaylandOutputState& state = output->GetState();
            std::ostringstream text;
            text << "output '" << state.name << "' (" << state.description << ", " << state.make << " " << state.model
                 << "): mode " << state.modeWidth << "x" << state.modeHeight << "@" << state.refreshMilliHz / 1000.0
                 << " Hz, wl_output.scale " << state.scale << ", transform " << state.transform;
            if (state.hasLogical)
            {
                text << ", logical " << state.logicalWidth << "x" << state.logicalHeight << " at " << state.logicalX << ","
                     << state.logicalY;
            }
            text << ", content scale " << Wayland::OutputContentScale(state);
            Info(text.str());
        }
        for (const DisplayInfo& display : platform.GetDisplays()->GetDisplays())
        {
            Info("display " + std::to_string(display.id) + " '" + display.name + "' " + std::to_string(display.width) +
                 "x" + std::to_string(display.height) + " at " + std::to_string(display.x) + "," +
                 std::to_string(display.y) + ", scale " + std::to_string(display.contentScale));
        }
        Info("seats: " + std::to_string(platform.GetSeatCount()) + ", keyboard " +
             Flag(platform.GetKeyboard() != nullptr && platform.GetKeyboard()->HasKeyboard()));

        const PlatformCapabilities capabilities = platform.GetCapabilities();
        std::ostringstream flags;
        flags << "multipleWindows " << Flag(capabilities.multipleWindows) << ", highDpi " << Flag(capabilities.highDpi)
              << ", surfacePresentation " << Flag(capabilities.surfacePresentation) << ", openGlContext "
              << Flag(capabilities.openGlContext) << ", vulkanSurface " << Flag(capabilities.vulkanSurface)
              << ", relativeMouse " << Flag(capabilities.relativeMouse) << ", cursorShapes "
              << Flag(capabilities.cursorShapes) << ", ime " << Flag(capabilities.ime) << ", clipboard "
              << Flag(capabilities.clipboard) << ", primarySelection " << Flag(capabilities.primarySelection)
              << ", dragAndDrop " << Flag(capabilities.dragAndDrop) << ", nativeFileDialog "
              << Flag(capabilities.nativeFileDialog) << ", messageBox " << Flag(capabilities.messageBox)
              << ", borderlessFullscreen " << Flag(capabilities.borderlessFullscreen) << ", globalPointer "
              << Flag(capabilities.globalPointer) << ", gamepad " << Flag(capabilities.gamepad);
        Info("capabilities: " + flags.str());
        Check(!capabilities.globalPointer, "info.no-global-pointer", "Wayland gives a client none");
        Check((platform.GetClipboard() != nullptr) == capabilities.clipboard &&
                  (platform.GetGlContext() != nullptr) == capabilities.openGlContext &&
                  (platform.GetVulkanSurface() != nullptr) == capabilities.vulkanSurface &&
                  (platform.GetDisplays() != nullptr) == capabilities.multipleDisplays,
              "info.services-match-capabilities");
        return 0;
    }

    int RunLifecycle(const std::vector<std::string>& arguments)
    {
        const int windows = static_cast<int>(OptionInt(arguments, "windows", 3));
        Session session;
        if (!Check(session.Ok(), "lifecycle.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        const auto window = session.Make("CNA Wayland lifecycle", 800, 480);
        const WindowId id = window->GetId();
        const auto presenter = platform.CreateSurfacePresenter(*window);
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        Check(session.WaitFor(id, WindowEventKind::Exposed), "lifecycle.configured-and-exposed");
        Check(window->GetClientBounds().width == 800 && window->GetClientBounds().height == 480, "lifecycle.initial-size",
              std::to_string(window->GetClientBounds().width) + "x" + std::to_string(window->GetClientBounds().height));

        window->SetTitle("CNA Wayland lifecycle -- renamed");
        session.Clear();
        window->SetSize(1000, 600);
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        session.PumpFor(std::chrono::milliseconds(100));
        Check(window->GetClientBounds().width == 1000 && session.Count(id, WindowEventKind::Resized) == 1,
              "lifecycle.set-size", std::to_string(window->GetClientBounds().width));

        session.Clear();
        window->Maximize();
        window->Sync();
        session.PumpUntil([&] { return session.Count(id, WindowEventKind::Maximized) > 0; }, std::chrono::milliseconds(2000));
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        const WindowBounds maximized = window->GetClientBounds();
        Check(session.Count(id, WindowEventKind::Maximized) == 1 && maximized.width > 1000, "lifecycle.maximize",
              std::to_string(maximized.width) + "x" + std::to_string(maximized.height));
        session.Clear();
        window->Restore();
        window->Sync();
        session.PumpUntil([&] { return session.Count(id, WindowEventKind::Restored) > 0; }, std::chrono::milliseconds(2000));
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        Check(session.Count(id, WindowEventKind::Restored) == 1 && window->GetClientBounds().width == 1000,
              "lifecycle.restore", std::to_string(window->GetClientBounds().width));

        window->SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
        window->Sync();
        session.PumpUntil([&] { return window->GetFullscreenMode() != WindowFullscreenMode::Windowed; },
                          std::chrono::milliseconds(2000));
        PresentSolid(*presenter, *window, 0x000000);
        window->Sync();
        DisplayInfo display;
        const bool known = platform.GetDisplays()->TryGetDisplayForWindow(*window, display);
        Check(window->GetFullscreenMode() == WindowFullscreenMode::BorderlessFullscreen, "lifecycle.fullscreen-is-borderless",
              "asked for exclusive; " + std::to_string(window->GetClientBounds().width) + "x" +
                  std::to_string(window->GetClientBounds().height) +
                  (known ? " on '" + display.name + "' " + std::to_string(display.width) + "x" + std::to_string(display.height) : std::string()));
        if (known)
        {
            Check(window->GetClientBounds().width == display.width && window->GetClientBounds().height == display.height,
                  "lifecycle.fullscreen-covers-the-output");
        }
        window->SetFullscreenMode(WindowFullscreenMode::Windowed);
        window->Sync();
        session.PumpUntil([&] { return window->GetFullscreenMode() == WindowFullscreenMode::Windowed; },
                          std::chrono::milliseconds(2000));
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        Check(window->GetFullscreenMode() == WindowFullscreenMode::Windowed && window->GetClientBounds().width == 1000,
              "lifecycle.leave-fullscreen", std::to_string(window->GetClientBounds().width));

        window->SetBorderless(true);
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        window->SetBorderless(false);
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        Check(session.Alive(), "lifecycle.borderless-toggle");

        window->Hide();
        window->Sync();
        window->Show();
        PresentSolid(*presenter, *window, 0x2050A0);
        window->Sync();
        Check(session.Alive(), "lifecycle.hide-show");

        session.Clear();
        window->Minimize();
        window->Sync();
        session.PumpFor(std::chrono::milliseconds(300));
        Info(std::string("after Minimize: IsMinimized ") + (window->IsMinimized() ? "true" : "false") +
             " (the compositor decides; GNOME reports it as xdg_toplevel suspended)");

        std::vector<std::unique_ptr<IPlatformWindow>> more;
        std::vector<std::unique_ptr<IPlatformSurfacePresenter>> presenters;
        for (int index = 0; index < windows; ++index)
        {
            more.push_back(session.Make("CNA Wayland lifecycle " + std::to_string(index + 2), 320 + index * 40, 240));
            presenters.push_back(platform.CreateSurfacePresenter(*more.back()));
            PresentSolid(*presenters.back(), *more.back(), 0x40A040u + static_cast<std::uint32_t>(index * 0x10));
        }
        session.PumpFor(std::chrono::milliseconds(200));
        presenters.clear();
        more.clear();
        session.PumpFor(std::chrono::milliseconds(100));
        Check(session.Alive(), "lifecycle.multiple-windows", std::to_string(windows) + " more windows made and destroyed");
        Check(session.Alive(), "lifecycle.no-protocol-error", session.ConnectionError());
        return 0;
    }

    int RunScale(const std::vector<std::string>&)
    {
        Session session;
        if (!Check(session.Ok(), "scale.connect", session.Error()))
        {
            return 1;
        }
        Wayland::WaylandPlatform& platform = session.Platform();
        const auto hidpi = session.Make("CNA Wayland scale (high DPI)", 800, 600, true, WindowRenderIntent::None, true);
        const auto presenter = platform.CreateSurfacePresenter(*hidpi);
        for (int frame = 0; frame < 10; ++frame)
        {
            PresentSolid(*presenter, *hidpi, 0x808080);
            hidpi->Sync();
            session.Poll();
        }
        const float scale = hidpi->GetDisplayScale();
        const WindowSize pixels = hidpi->GetPixelSize();
        DisplayInfo display;
        const bool known = platform.GetDisplays()->TryGetDisplayForWindow(*hidpi, display);
        Info("high-DPI window: logical 800x600, pixels " + std::to_string(pixels.width) + "x" +
             std::to_string(pixels.height) + ", scale " + std::to_string(scale) + ", on '" + hidpi->GetDisplayName() + "'" +
             (known ? " whose content scale is " + std::to_string(display.contentScale) : std::string()));
        Check(pixels.width == static_cast<int>(std::lround(800.0 * scale)) &&
                  pixels.height == static_cast<int>(std::lround(600.0 * scale)),
              "scale.pixels-are-logical-times-scale");
        if (known)
        {
            Check(std::fabs(scale - display.contentScale) < 0.01f, "scale.matches-the-outputs-scale",
                  "window " + std::to_string(scale) + ", output " + std::to_string(display.contentScale));
        }
        else
        {
            Skip("scale.matches-the-outputs-scale", "the compositor has not said which output the window is on");
        }
        const auto plain = session.Make("CNA Wayland scale (not high DPI)", 400, 300);
        const auto plainPresenter = platform.CreateSurfacePresenter(*plain);
        PresentSolid(*plainPresenter, *plain, 0x404040);
        plain->Sync();
        Check(plain->GetDisplayScale() == 1.0f && plain->GetPixelSize().width == 400, "scale.not-high-dpi-is-unscaled",
              "the compositor scales it");
        Check(session.Alive(), "scale.no-protocol-error", session.ConnectionError());
        return 0;
    }

} // namespace CnaWaylandValidation
