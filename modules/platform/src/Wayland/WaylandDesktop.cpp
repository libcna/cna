// SPDX-License-Identifier: MS-PL

#include "WaylandDesktop.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "../Freedesktop/DesktopPortal.hpp"
#include "WaylandConnection.hpp"
#include "WaylandSeat.hpp"
#include "WaylandWindow.hpp"

#include <chrono>
#include <cstdlib>

namespace CNA::Platform::Wayland {

    // --- WaylandIdleInhibitor ---------------------------------------------------------------------

    WaylandIdleInhibitor::~WaylandIdleInhibitor()
    {
        for (auto& [id, entry] : windows_)
        {
            (void) id;
            Destroy(entry);
        }
        windows_.clear();
    }

    bool WaylandIdleInhibitor::IsSupported() const
    {
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        return connection_.GetGlobals().idleInhibitManager != nullptr;
#else
        return false;
#endif
    }

    void WaylandIdleInhibitor::Create(Entry& entry)
    {
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        zwp_idle_inhibit_manager_v1* manager = connection_.GetGlobals().idleInhibitManager;
        if (manager == nullptr || entry.inhibitor != nullptr || entry.surface == nullptr)
        {
            return;
        }
        entry.inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(manager, entry.surface);
        connection_.Flush();
#else
        (void) entry;
#endif
    }

    void WaylandIdleInhibitor::Destroy(Entry& entry)
    {
#if defined(CNA_WAYLAND_HAVE_IDLE_INHIBIT)
        if (entry.inhibitor != nullptr)
        {
            zwp_idle_inhibitor_v1_destroy(static_cast<zwp_idle_inhibitor_v1*>(entry.inhibitor));
            entry.inhibitor = nullptr;
        }
#else
        (void) entry;
#endif
    }

    void WaylandIdleInhibitor::AddWindow(const WindowId id, wl_surface* surface, const bool inhibit)
    {
        Entry entry;
        entry.surface = surface;
        if (inhibit || inhibited_)
        {
            Create(entry);
        }
        windows_[id] = entry;
    }

    void WaylandIdleInhibitor::RemoveWindow(const WindowId id)
    {
        const auto found = windows_.find(id);
        if (found == windows_.end())
        {
            return;
        }
        Destroy(found->second);
        windows_.erase(found);
        connection_.Flush();
    }

    void WaylandIdleInhibitor::SetInhibited(const bool inhibited)
    {
        inhibited_ = inhibited;
        for (auto& [id, entry] : windows_)
        {
            (void) id;
            if (inhibited)
            {
                Create(entry);
            }
            else
            {
                Destroy(entry);
            }
        }
        connection_.Flush();
    }

    std::size_t WaylandIdleInhibitor::GetInhibitorCount() const
    {
        std::size_t count = 0;
        for (const auto& [id, entry] : windows_)
        {
            (void) id;
            if (entry.inhibitor != nullptr)
            {
                ++count;
            }
        }
        return count;
    }

    // --- activation ----------------------------------------------------------------------------------

    void ActivateWindow(WaylandConnection& connection, WaylandWindow& window, wl_seat* seat, const std::uint32_t serial)
    {
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
        xdg_activation_v1* activation = connection.GetGlobals().activation;
        if (activation == nullptr || window.GetSurface() == nullptr)
        {
            return;
        }
        struct Pending
        {
            std::string token;
            bool done = false;
        } pending;
        static const xdg_activation_token_v1_listener listener = {
            .done = [](void* data, xdg_activation_token_v1*, const char* token) {
                auto* state = static_cast<Pending*>(data);
                state->token = token != nullptr ? token : "";
                state->done = true;
            },
        };
        xdg_activation_token_v1* request = xdg_activation_v1_get_activation_token(activation);
        xdg_activation_token_v1_add_listener(request, &listener, &pending);
        // The serial and seat of input this client received, and the surface it arrived on: what
        // a compositor's focus-stealing prevention checks before granting a token that works.
        if (seat != nullptr && serial != 0)
        {
            xdg_activation_token_v1_set_serial(request, serial, seat);
        }
        xdg_activation_token_v1_set_surface(request, window.GetSurface());
        xdg_activation_token_v1_commit(request);
        (void) connection.DispatchUntil([&pending] { return pending.done; }, std::chrono::milliseconds(500));
        // The token object must not outlive the stack frame its listener data points into.
        xdg_activation_token_v1_destroy(request);
        if (!pending.token.empty())
        {
            xdg_activation_v1_activate(activation, pending.token.c_str(), window.GetSurface());
            connection.Flush();
        }
#else
        (void) connection;
        (void) window;
        (void) seat;
        (void) serial;
#endif
    }

    void ActivateWindowWithToken(WaylandConnection& connection, WaylandWindow& window, const std::string& token)
    {
#if defined(CNA_WAYLAND_HAVE_XDG_ACTIVATION)
        xdg_activation_v1* activation = connection.GetGlobals().activation;
        if (activation == nullptr || window.GetSurface() == nullptr || token.empty())
        {
            return;
        }
        xdg_activation_v1_activate(activation, token.c_str(), window.GetSurface());
        connection.Flush();
#else
        (void) connection;
        (void) window;
        (void) token;
#endif
    }

    // --- WaylandDialogs ------------------------------------------------------------------------------

    WaylandDialogs::WaylandDialogs(WaylandConnection& connection, Freedesktop::DesktopPortal* portal,
                                   std::function<WaylandWindow*(const IPlatformWindow*)> resolve)
        : connection_(connection), portal_(portal), resolve_(std::move(resolve))
    {
    }

    WaylandDialogs::~WaylandDialogs()
    {
        for (auto& [id, entry] : exports_)
        {
            (void) id;
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
            if (entry.exported != nullptr)
            {
                zxdg_exported_v2_destroy(static_cast<zxdg_exported_v2*>(entry.exported));
            }
#endif
            (void) entry;
        }
        exports_.clear();
    }

    void WaylandDialogs::ForgetWindow(const WindowId window)
    {
        const auto found = exports_.find(window);
        if (found == exports_.end())
        {
            return;
        }
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
        if (found->second.exported != nullptr)
        {
            zxdg_exported_v2_destroy(static_cast<zxdg_exported_v2*>(found->second.exported));
        }
#endif
        exports_.erase(found);
    }

    std::string WaylandDialogs::ParentHandle(IPlatformWindow* window)
    {
#if defined(CNA_WAYLAND_HAVE_XDG_FOREIGN)
        WaylandWindow* target = resolve_ ? resolve_(window) : nullptr;
        zxdg_exporter_v2* exporter = connection_.GetGlobals().exporter;
        if (target == nullptr || exporter == nullptr || target->GetToplevel() == nullptr)
        {
            return {};
        }
        Export& entry = exports_[target->GetId()];
        if (entry.exported == nullptr)
        {
            static const zxdg_exported_v2_listener listener = {
                .handle = [](void* data, zxdg_exported_v2*, const char* handle) {
                    static_cast<Export*>(data)->handle = handle != nullptr ? handle : "";
                },
            };
            auto* exported = zxdg_exporter_v2_export_toplevel(exporter, target->GetSurface());
            // The map's node is stable while the entry lives, and the entry is erased only after
            // its proxy is destroyed.
            zxdg_exported_v2_add_listener(exported, &listener, &entry);
            entry.exported = exported;
            (void) connection_.DispatchUntil([&entry] { return !entry.handle.empty(); }, std::chrono::milliseconds(500));
        }
        return entry.handle.empty() ? std::string() : "wayland:" + entry.handle;
#else
        (void) window;
        return {};
#endif
    }

    void WaylandDialogs::ShowMessageBox(MessageBoxSeverity, const std::string&, const std::string&, IPlatformWindow*)
    {
        throw PlatformNotSupportedException(PlatformCapability::MessageBox,
                                            "Wayland (no message box: plans/plan_wayland.md WAYLAND-0094)");
    }

    int WaylandDialogs::ShowMessageBoxWithButtons(MessageBoxSeverity, const std::string&, const std::string&,
                                                  const std::vector<std::string>&, IPlatformWindow*)
    {
        throw PlatformNotSupportedException(PlatformCapability::MessageBox,
                                            "Wayland (no message box: plans/plan_wayland.md WAYLAND-0094)");
    }

    void WaylandDialogs::ShowOpenFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                            const std::string& defaultLocation, const bool allowMultiple,
                                            IPlatformWindow* parent)
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->ShowFileDialog(Freedesktop::DesktopPortal::FileRequest::Open, std::move(onResult), filters,
                                    defaultLocation, allowMultiple, ParentHandle(parent));
            return;
        }
#endif
        (void) onResult; (void) filters; (void) defaultLocation; (void) allowMultiple; (void) parent;
        throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog, "Wayland (no desktop portal)");
    }

    void WaylandDialogs::ShowSaveFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                            const std::string& defaultLocation, IPlatformWindow* parent)
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->ShowFileDialog(Freedesktop::DesktopPortal::FileRequest::Save, std::move(onResult), filters,
                                    defaultLocation, false, ParentHandle(parent));
            return;
        }
#endif
        (void) onResult; (void) filters; (void) defaultLocation; (void) parent;
        throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog, "Wayland (no desktop portal)");
    }

    void WaylandDialogs::ShowOpenFolderDialog(FileDialogCallback onResult, const std::string& defaultLocation,
                                              const bool allowMultiple, IPlatformWindow* parent)
    {
#if defined(CNA_PLATFORM_HAVE_DBUS)
        if (portal_ != nullptr)
        {
            portal_->ShowFileDialog(Freedesktop::DesktopPortal::FileRequest::OpenFolder, std::move(onResult), {},
                                    defaultLocation, allowMultiple, ParentHandle(parent));
            return;
        }
#endif
        (void) onResult; (void) defaultLocation; (void) allowMultiple; (void) parent;
        throw PlatformNotSupportedException(PlatformCapability::NativeFileDialog, "Wayland (no desktop portal)");
    }

    // --- WaylandInputDevices ----------------------------------------------------------------------

    std::vector<InputDeviceInfo> WaylandInputDevices::GetDevices(const InputDeviceKind kind) const
    {
        std::vector<InputDeviceInfo> devices;
        std::uint32_t wanted = 0;
        switch (kind)
        {
            case InputDeviceKind::Keyboard: wanted = WL_SEAT_CAPABILITY_KEYBOARD; break;
            case InputDeviceKind::Mouse: wanted = WL_SEAT_CAPABILITY_POINTER; break;
            case InputDeviceKind::Touch: wanted = WL_SEAT_CAPABILITY_TOUCH; break;
            default: break;
        }
        if (wanted != 0)
        {
            for (const WaylandSeat* seat : seats_())
            {
                if ((seat->GetCapabilities() & wanted) == 0)
                {
                    continue;
                }
                // An id no controller id can collide with: the evdev hub's ids are small and
                // counted from zero; these carry the seat's global name above bit 40.
                const DeviceId id = (static_cast<DeviceId>(seat->GetGlobalName()) << 40) |
                                    static_cast<DeviceId>(static_cast<unsigned>(kind));
                devices.push_back({id, kind, seat->GetName().empty() ? std::string("seat") : seat->GetName()});
            }
            if (kind == InputDeviceKind::Touch && tabletTools_)
            {
                // A pen reports as a touch, so it is listed as one: a game that asks whether a
                // touch device is attached before reading `TouchPanel` gets a truthful yes.
                DeviceId index = 0;
                for (const std::string& tool : tabletTools_())
                {
                    devices.push_back({(static_cast<DeviceId>(0x7Fu) << 40) | (++index), kind, tool});
                }
            }
            return devices;
        }
        return controllers_ ? controllers_(kind) : devices;
    }

    bool WaylandInputDevices::HasDevice(const InputDeviceKind kind) const
    {
        return !GetDevices(kind).empty();
    }

} // namespace CNA::Platform::Wayland
