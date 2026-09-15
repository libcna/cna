// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/Input/IPlatformInputDevices.hpp"

#include "WaylandProtocols.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::Freedesktop {
    class DesktopPortal;
}

namespace CNA::Platform::Wayland {

    class WaylandConnection;
    class WaylandSeat;
    class WaylandWindow;

    /**
     * @brief Keeps the screen on through `zwp_idle_inhibitor_v1` (plans/plan_wayland.md D-26,
     * WAYLAND-0092).
     *
     * An inhibitor is an object on a surface, and the compositor honours it while that surface is
     * visible -- a game's exact case: the screen stays on while the game is on screen, and blanks
     * when the user switches away. `SetScreenSaverEnabled(false)` gives every window one, and
     * every window created afterwards one; each is destroyed before its surface.
     */
    class WaylandIdleInhibitor
    {
    public:
        /**
         * @brief Creates the service.
         * @param connection The connection.
         */
        explicit WaylandIdleInhibitor(WaylandConnection& connection) : connection_(connection) {}

        /** @brief Destroys every inhibitor. */
        ~WaylandIdleInhibitor();

        WaylandIdleInhibitor(const WaylandIdleInhibitor&) = delete;
        WaylandIdleInhibitor& operator=(const WaylandIdleInhibitor&) = delete;

        /** @brief Gets whether the compositor offers the protocol. @return True when it does. */
        [[nodiscard]] bool IsSupported() const;

        /**
         * @brief Tracks a window.
         * @param id The window.
         * @param surface Its surface.
         * @param inhibit Whether to inhibit on it now.
         */
        void AddWindow(WindowId id, wl_surface* surface, bool inhibit);

        /** @brief Forgets a window; its inhibitor goes first. @param id The window. */
        void RemoveWindow(WindowId id);

        /** @brief Inhibits or allows idling on every tracked window. @param inhibited Inhibit. */
        void SetInhibited(bool inhibited);

        /** @brief Gets how many inhibitors exist (tests). @return The count. */
        [[nodiscard]] std::size_t GetInhibitorCount() const;

    private:
        struct Entry
        {
            wl_surface* surface = nullptr;
            void* inhibitor = nullptr;
        };

        void Create(Entry& entry);
        static void Destroy(Entry& entry);

        WaylandConnection& connection_;
        std::map<WindowId, Entry> windows_;
        bool inhibited_ = false;
    };

    /**
     * @brief Asks the compositor to focus a window the way xdg-activation allows (WAYLAND-0093).
     *
     * An ordinary Wayland client cannot take focus: it asks with an activation token, and a
     * compositor grants a token only for input the client really received. The token is requested
     * with the latest input serial and seat, and the compositor decides -- no focus is claimed.
     *
     * @param connection The connection.
     * @param window The window to activate.
     * @param seat The seat of the latest input, or null.
     * @param serial The latest input serial, or 0.
     */
    void ActivateWindow(WaylandConnection& connection, WaylandWindow& window, wl_seat* seat, std::uint32_t serial);

    /**
     * @brief Activates a window with a token this client was given rather than one it asked for:
     * the launcher's, from XDG_ACTIVATION_TOKEN (WAYLAND-0093).
     *
     * @param connection The connection.
     * @param window The window to activate.
     * @param token The token.
     */
    void ActivateWindowWithToken(WaylandConnection& connection, WaylandWindow& window, const std::string& token);

    /**
     * @brief File dialogs through the desktop portal, parented through xdg-foreign
     * (WAYLAND-0091, WAYLAND-0094).
     *
     * The portal (src/Freedesktop/, shared with X11) takes a parent window as a string; a Wayland
     * window has no global name, so it is exported with `zxdg_exporter_v2` and named
     * `wayland:<handle>`, which the portal's backend imports to put its dialog above it. A window
     * is exported once and stays exported while it lives.
     *
     * Message boxes: `HasMessageBox()` is false -- see WAYLAND-0094 for what one would need.
     */
    class WaylandDialogs final : public IPlatformDialogs
    {
    public:
        /**
         * @brief Creates the service.
         * @param connection The connection.
         * @param portal The desktop portal, or null.
         * @param resolve Finds this platform's window behind a contract window.
         */
        WaylandDialogs(WaylandConnection& connection, Freedesktop::DesktopPortal* portal,
                       std::function<WaylandWindow*(const IPlatformWindow*)> resolve);

        /** @brief Destroys every export. */
        ~WaylandDialogs() override;

        WaylandDialogs(const WaylandDialogs&) = delete;
        WaylandDialogs& operator=(const WaylandDialogs&) = delete;

        /** @brief Gets whether any dialog can be shown. @return True with a portal or message boxes. */
        [[nodiscard]] bool IsUseful() const { return HasFileDialogs() || HasMessageBox(); }
        /** @brief Gets whether file dialogs work. @return True with a portal. */
        [[nodiscard]] bool HasFileDialogs() const { return portal_ != nullptr; }
        /** @brief Gets whether message boxes work. @return False (WAYLAND-0094). */
        [[nodiscard]] bool HasMessageBox() const { return false; }

        /** @brief Forgets a window's export before it goes. @param window The window. */
        void ForgetWindow(WindowId window);

        /**
         * @brief Refuses: no message box (WAYLAND-0094).
         * @param severity Ignored.
         * @param title Ignored.
         * @param message Ignored.
         * @param parent Ignored.
         * @throws PlatformNotSupportedException Always.
         */
        void ShowMessageBox(MessageBoxSeverity severity, const std::string& title, const std::string& message,
                            IPlatformWindow* parent) override;
        /**
         * @brief Refuses: no message box (WAYLAND-0094).
         * @param severity Ignored.
         * @param title Ignored.
         * @param message Ignored.
         * @param buttons Ignored.
         * @param parent Ignored.
         * @return Never.
         * @throws PlatformNotSupportedException Always.
         */
        [[nodiscard]] int ShowMessageBoxWithButtons(MessageBoxSeverity severity, const std::string& title,
                                                    const std::string& message, const std::vector<std::string>& buttons,
                                                    IPlatformWindow* parent) override;
        /**
         * @brief Shows the portal's open-file dialog.
         * @param onResult Called once from a later PollEvents.
         * @param filters The filters.
         * @param defaultLocation Where it starts.
         * @param allowMultiple Several files.
         * @param parent The parent window, or null.
         */
        void ShowOpenFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation, bool allowMultiple, IPlatformWindow* parent) override;
        /**
         * @brief Shows the portal's save-file dialog.
         * @param onResult Called once from a later PollEvents.
         * @param filters The filters.
         * @param defaultLocation Where it starts.
         * @param parent The parent window, or null.
         */
        void ShowSaveFileDialog(FileDialogCallback onResult, const std::vector<FileDialogFilter>& filters,
                                const std::string& defaultLocation, IPlatformWindow* parent) override;
        /**
         * @brief Shows the portal's folder dialog.
         * @param onResult Called once from a later PollEvents.
         * @param defaultLocation Where it starts.
         * @param allowMultiple Several folders.
         * @param parent The parent window, or null.
         */
        void ShowOpenFolderDialog(FileDialogCallback onResult, const std::string& defaultLocation, bool allowMultiple,
                                  IPlatformWindow* parent) override;

        /**
         * @brief Gets a window's name in the portal's notation, exporting it if needed.
         * @param window The window, or null.
         * @return `wayland:<handle>`, or empty without a parent or without xdg-foreign.
         */
        [[nodiscard]] std::string ParentHandle(IPlatformWindow* window);

    private:
        struct Export
        {
            void* exported = nullptr;
            std::string handle;
        };

        WaylandConnection& connection_;
        Freedesktop::DesktopPortal* portal_ = nullptr;
        std::function<WaylandWindow*(const IPlatformWindow*)> resolve_;
        std::map<WindowId, Export> exports_;
    };

    /**
     * @brief The input devices attached now: each seat's keyboard, pointer and touch device, and
     * the Linux evdev controllers (WAYLAND-0060).
     *
     * A seat's devices have no names or ids of their own on Wayland -- a seat is the unit the
     * compositor exposes, and it may merge many physical keyboards into one -- so each is listed
     * once per seat, named after the seat, with an id derived from the seat's global name.
     */
    class WaylandInputDevices final : public IPlatformInputDevices
    {
    public:
        /**
         * @brief Creates the service.
         * @param seats The seats, now.
         * @param controllers The evdev devices of a kind (gamepads, joysticks, haptics).
         */
        WaylandInputDevices(std::function<std::vector<const WaylandSeat*>()> seats,
                            std::function<std::vector<InputDeviceInfo>(InputDeviceKind)> controllers)
            : seats_(std::move(seats)), controllers_(std::move(controllers))
        {
        }

        /** @brief Lists devices of a kind. @param kind The kind. @return The devices. */
        [[nodiscard]] std::vector<InputDeviceInfo> GetDevices(InputDeviceKind kind) const override;
        /** @brief Gets whether a device of a kind is attached. @param kind The kind. @return True if so. */
        [[nodiscard]] bool HasDevice(InputDeviceKind kind) const override;

    private:
        std::function<std::vector<const WaylandSeat*>()> seats_;
        std::function<std::vector<InputDeviceInfo>(InputDeviceKind)> controllers_;
    };

} // namespace CNA::Platform::Wayland
