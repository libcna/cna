// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "../Common/StandardFileSystem.hpp"
#include "WaylandConnection.hpp"
#include "WaylandWindow.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Platform::Freedesktop {
    class DesktopPortal;
    class ScreenSaverBusInhibition;
}

namespace CNA::Platform::Wayland {

    class WaylandClipboard;
    class WaylandDataDevices;
    class WaylandDialogs;
    class WaylandDisplays;
    class WaylandGlContext;
    class WaylandIdleInhibitor;
    class WaylandKeyboard;
    class WaylandMouse;
    class WaylandOutput;
    class WaylandSeat;
    class WaylandTextInput;
    class WaylandTablet;
    class WaylandTouch;
    class WaylandVulkanSurface;

    /**
     * @brief The native Wayland implementation of the CNA platform contract (plans/plan_wayland.md).
     *
     * ### No SDL, no X11
     *
     * Windows are `xdg_toplevel`s, events come from the compositor's socket through libwayland,
     * the keyboard is xkbcommon on the compositor's keymap, GL is EGL on the Wayland platform,
     * Vulkan is `VK_KHR_wayland_surface`, CPU frames are `wl_shm` buffers, timing is
     * `clock_gettime`, and gamepads, joysticks, haptics, power and the desktop portal come from the
     * Linux and freedesktop.org code the X11 backend shares (src/Linux/, src/Freedesktop/). Neither
     * this directory nor anything it links contains an SDL or X11 symbol, and a test asserts that.
     *
     * ### One connection per instance (D-3, D-4)
     *
     * The connection is opened in the constructor, so the capability set -- half of which is what
     * this compositor offers -- is fixed for the instance's lifetime, as the contract requires. A
     * process with no compositor still constructs a platform (the connection error is kept and
     * reported by `AcquireSubsystem(Video)`), because `StorageDevice` and `TitleContainer` need one.
     *
     * ### What else is left alone
     *
     * No signal handler is installed, no environment variable is set, the locale is not touched
     * (xkbcommon's compose table is looked up by the environment's locale name), and nothing is
     * started on the application's behalf.
     */
    class WaylandPlatform final : public IPlatform, private WaylandRegistryObserver, private WaylandWindowHost
    {
    public:
        /** @brief Creates the platform and connects to the compositor when one is reachable. */
        WaylandPlatform();

        /** @brief Destroys every service and proxy, then disconnects. */
        ~WaylandPlatform() override;

        WaylandPlatform(const WaylandPlatform&) = delete;
        WaylandPlatform& operator=(const WaylandPlatform&) = delete;

        /** @brief Gets the implementation name. @return `"Wayland"`. */
        [[nodiscard]] const std::string& GetName() const override;
        /** @brief Gets the capability set, computed once. @return The capabilities. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override { return capabilities_; }
        /**
         * @brief Acquires a subsystem.
         * @param subsystem The subsystem.
         * @throws PlatformException For Video when no compositor was reachable.
         */
        void AcquireSubsystem(PlatformSubsystem subsystem) override;
        /** @brief Releases a subsystem; an unpaired release is a no-op. @param subsystem The subsystem. */
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;
        /** @brief Gets whether a subsystem is up. @param subsystem The subsystem. @return True when acquired. */
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;
        /**
         * @brief Creates a window, shown and configured when the description asks for a visible one.
         * @param description The creation parameters.
         * @return The window.
         * @throws PlatformException Without a connection, for a non-positive size, or when the
         * compositor does not configure it.
         * @throws PlatformNotSupportedException For an OpenGL window without EGL.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(const WindowDescription& description) override;
        /**
         * @brief Wraps a window this platform owns, without taking ownership.
         * @param windowId The window's id.
         * @return A non-owning wrapper.
         * @throws PlatformException For an unknown id.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId windowId) override;
        /**
         * @brief Wraps a window by its legacy token -- its id; a foreign surface cannot be adopted,
         * because Wayland gives a client no way to learn anything about another client's surface.
         * @param handle The token.
         * @return A non-owning wrapper.
         * @throws PlatformException For a token that is not one of this platform's windows.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(std::uintptr_t handle) override;
        /** @brief Drains the compositor's events without blocking (D-5). @param destination Receives them. */
        void PollEvents(std::vector<PlatformEvent>& destination) override;
        /** @brief Gets the monotonic counter in nanoseconds. @return The counter. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;
        /** @brief Gets the counter frequency. @return One billion. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;
        /** @brief Gets milliseconds since creation. @return The elapsed time. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;
        /** @brief Sleeps. @param milliseconds How long. */
        void Delay(std::uint32_t milliseconds) override;

        /** @brief Gets the keyboard. @return The service, or null without a compositor. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        /** @brief Gets the mouse. @return The service, or null without a compositor. */
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        /** @brief Gets the gamepad service (Linux evdev). @return The service, or null. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override;
        /** @brief Gets the joystick service (Linux evdev). @return The service, or null. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override;
        /** @brief Gets text input. @return The service, or null without a compositor. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        /** @brief Gets sensors. @return Null: not a window-system facility. */
        [[nodiscard]] IPlatformSensors* GetSensors() override { return nullptr; }
        /** @brief Gets haptics (Linux evdev force feedback). @return The service, or null. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override;
        /** @brief Gets input-device enumeration. @return The service, or null. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override;
        /** @brief Gets the clipboard. @return The service, or null. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        /** @brief Gets the primary selection. @return The service, or null. */
        [[nodiscard]] IPlatformClipboard* GetPrimarySelection() override;
        /** @brief Gets the displays. @return The service, or null without a compositor. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        /** @brief Gets dialogs. @return The service, or null. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override;
        /** @brief Gets the tray. @return Null: Wayland has no tray protocol. */
        [[nodiscard]] IPlatformTray* GetTray() override { return nullptr; }
        /** @brief Gets cameras. @return Null: not a window-system facility. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override { return nullptr; }
        /** @brief Gets the filesystem. @return The portable implementation; never null. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override { return &fileSystem_; }
        /** @brief Gets system information. @return Never null. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override { return systemInfo_.get(); }
        /** @brief Gets the GL context service. @return The service, or null without EGL. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        /** @brief Gets the Vulkan surface service. @return The service, or null. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;
        /**
         * @brief Creates a `wl_shm` presenter for a window.
         * @param window The window.
         * @return The presenter.
         * @throws PlatformException For a window of another platform.
         * @throws PlatformNotSupportedException Without `wl_shm`.
         */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(IPlatformWindow& window) override;

        // --- for tests and the validation harness -------------------------------------------

        /** @brief Gets the connection, or null. @return The connection. */
        [[nodiscard]] WaylandConnection* GetConnectionForTesting() const { return connection_.get(); }
        /** @brief Finds a window by id. @param id The id. @return The window, or null. */
        [[nodiscard]] WaylandWindow* FindWindow(WindowId id) const;
        /** @brief Gets every described output. @return The outputs. */
        [[nodiscard]] std::vector<const WaylandOutput*> GetOutputs() const;
        /** @brief Gets the seat count. @return The number of seats. */
        [[nodiscard]] std::size_t GetSeatCount() const { return seats_.size(); }
        /** @brief Gets why the connection failed, or empty. @return The error. */
        [[nodiscard]] const std::string& GetConnectionError() const { return connectionError_; }

    private:
        class BorrowedWindow;
        struct Controllers;
        struct ControllersDeleter
        {
            void operator()(Controllers* controllers) const;
        };
        struct PortalDeleter
        {
            void operator()(Freedesktop::DesktopPortal* portal) const;
        };

        // WaylandRegistryObserver
        void OnSeatAnnounced(wl_registry* registry, std::uint32_t name, std::uint32_t version) override;
        void OnOutputAnnounced(wl_registry* registry, std::uint32_t name, std::uint32_t version) override;
        void OnGlobalRemoved(std::uint32_t name) override;
        void OnDisconnecting() override;

        // WaylandWindowHost
        [[nodiscard]] WaylandConnection& GetConnection() override { return *connection_; }
        void PostEvent(PlatformEvent event) override;
        void OnWindowDestroyed(WaylandWindow& window) override;
        [[nodiscard]] const WaylandOutput* FindOutput(const wl_output* output) const override;
        [[nodiscard]] std::size_t GetWindowCount() const override { return windows_.size(); }
        void MapSurface(wl_surface* surface, WindowId window) override;
        void RequestActivation(WaylandWindow& window) override;
        [[nodiscard]] std::uint32_t GetLatestInputSerial(wl_seat*& seat) const override;
        [[nodiscard]] bool HasKeyboard() const override;
        void MapFrameSurface(wl_surface* surface, WaylandFrame* frame) override;
        void SetFrameCursor(wl_pointer* pointer, std::uint32_t serial, SystemCursor cursor) override;

        void CreateServices();
        void AttachSeatServices(WaylandSeat& seat);
        void RecordSerial(wl_seat* seat, std::uint32_t serial);
        [[nodiscard]] WindowId ResolveSurface(wl_surface* surface) const;
        [[nodiscard]] PlatformCapabilities ComputeCapabilities() const;
        [[nodiscard]] WaylandConnection& RequireConnection(const char* operation) const;
        void EnsureControllerSubsystem();
        [[nodiscard]] std::vector<InputDeviceInfo> ControllerDevices(InputDeviceKind kind);
        void SetScreenSaverEnabled(bool enabled);

        std::unique_ptr<WaylandConnection> connection_;
        std::string connectionError_;
        bool connectionLostReported_ = false;
        PlatformCapabilities capabilities_;
        std::map<PlatformSubsystem, int> refCounts_;

        std::map<WindowId, WaylandWindow*> windows_;
        WindowId nextWindowId_ = 1;
        std::unordered_map<wl_surface*, WindowId> surfaces_;
        std::unordered_map<wl_surface*, WaylandFrame*> frameSurfaces_;
        std::vector<std::unique_ptr<WaylandOutput>> outputs_;
        std::vector<std::unique_ptr<WaylandSeat>> seats_;
        std::vector<PlatformEvent> pending_;

        std::unique_ptr<WaylandKeyboard> keyboard_;
        std::unique_ptr<WaylandMouse> mouse_;
        std::unique_ptr<WaylandTouch> touch_;
        std::unique_ptr<WaylandTablet> tablet_;
        std::unique_ptr<WaylandTextInput> textInput_;
        std::unique_ptr<WaylandDataDevices> dataDevices_;
        std::unique_ptr<WaylandDisplays> displays_;
        std::unique_ptr<WaylandGlContext> glContext_;
        std::unique_ptr<WaylandVulkanSurface> vulkanSurface_;
        std::unique_ptr<WaylandDialogs> dialogs_;
        std::unique_ptr<IPlatformInputDevices> inputDevices_;

        std::unique_ptr<Controllers, ControllersDeleter> controllers_;
        std::unique_ptr<Freedesktop::DesktopPortal, PortalDeleter> portal_;
        bool controllerSubsystemEnsured_ = false;

        bool screenSaverEnabled_ = true;
        std::unique_ptr<WaylandIdleInhibitor> idleInhibitor_;
        std::unique_ptr<Freedesktop::ScreenSaverBusInhibition> screenSaverBus_;

        wl_seat* serialSeat_ = nullptr;
        std::uint32_t serial_ = 0;
        bool launchTokenSpent_ = false;

        Common::StandardFileSystem fileSystem_{"cna-wayland"};
        std::unique_ptr<IPlatformSystemInfo> systemInfo_;
        std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();
    };

} // namespace CNA::Platform::Wayland
