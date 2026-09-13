// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "../Common/StandardFileSystem.hpp"
#include "../Common/StandardSystemInfo.hpp"
#include "X11Clipboard.hpp"
#include "X11Display.hpp"
#include "X11Displays.hpp"
#include "X11GraphicsServices.hpp"
#include "X11Keyboard.hpp"
#include "X11Mouse.hpp"
#include "X11TextInput.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Window;

    /**
     * @brief The native X11 implementation of the CNA platform contract.
     *
     * ### No SDL, anywhere
     *
     * That is this backend's whole reason for existing. Windows come from `XCreateWindow`, events
     * from `XNextEvent`, keys from XKB, text from XIM, the pointer from the core protocol and
     * XInput2, monitors from XRandR, timing from `clock_gettime`. `modules/platform/src/X11/`
     * contains no SDL header, no SDL symbol and no SDL build dependency, and a test asserts that
     * rather than leaving it to a grep in a plan document.
     *
     * ### Xlib threading
     *
     * `XInitThreads()` is **not** called. To be correct it must run before any other Xlib call in
     * the process, and CNA is a library that does not own process startup — a host that has
     * already talked to X would get an unlocked display regardless, and one that has not would
     * have had its global Xlib state reconfigured by a library it merely linked. Instead every
     * Xlib call this backend makes goes through one per-instance mutex, and the contract's
     * "poll events once per frame" rule keeps that lock uncontended. A host that wants full Xlib
     * thread safety calls `XInitThreads()` itself before constructing the platform; this backend
     * neither requires that nor is broken by it.
     *
     * ### What else is left alone
     *
     * The X error handler is saved, chained and restored (`X11ErrorPolicy`). The locale is
     * touched only if `LC_CTYPE` is still the startup default, and then only that one category.
     * No signal handler is installed, no environment variable is set, and `XSetIOErrorHandler` is
     * never replaced.
     */
    class X11Platform final : public IPlatform
    {
    public:
        /**
         * @brief Creates the platform, opening an X connection when one is reachable.
         *
         * Connecting here rather than on `AcquireSubsystem(Video)` is what makes the capability
         * set **stable for the instance's lifetime**, which the contract requires and callers
         * depend on because they cache it once. Half of this backend's capabilities are answers
         * about a particular X server — whether the window manager advertises EWMH fullscreen,
         * whether XRandR exists, whether GLX does — and a set that changed when video was
         * acquired would silently invalidate every cached copy.
         *
         * A failure to connect is **not** an error here. A process that never opens a window
         * still constructs a platform to reach `StorageDevice` and `TitleContainer`, and a
         * throwing constructor would make those unreachable on a machine with no display. The
         * failure is recorded instead, every display-dependent capability reads false, and
         * `AcquireSubsystem(Video)` reports the original connection error.
         */
        X11Platform();

        /** @brief Destroys every window and service, then closes the connection. */
        ~X11Platform() override;

        X11Platform(const X11Platform&) = delete;
        X11Platform& operator=(const X11Platform&) = delete;

        /** @brief Gets the implementation name. @return `"X11"`. */
        [[nodiscard]] const std::string& GetName() const override;

        /** @brief Gets what this implementation can do. @return The capability set. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override;

        /**
         * @brief Acquires a subsystem.
         * @param subsystem The subsystem to acquire.
         * @throws PlatformException If the X server could not be reached.
         * @throws PlatformNotSupportedException For subsystems X11 has no facility for.
         */
        void AcquireSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Releases a subsystem. @param subsystem The subsystem to release. */
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;

        /**
         * @brief Gets whether a subsystem is up.
         * @param subsystem The subsystem to query.
         * @return True when it is initialised.
         */
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;

        /**
         * @brief Creates a window.
         * @param description The creation parameters.
         * @return The created window.
         * @throws PlatformException If the window could not be created.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override;

        /**
         * @brief Wraps a window this platform already owns, without taking ownership.
         * @param windowId The window's CNA id.
         * @return A non-owning wrapper.
         * @throws PlatformException If no window has that id.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId windowId) override;

        /**
         * @brief Wraps a window identified by its X window XID.
         * @param handle The XID.
         * @return A non-owning wrapper.
         * @throws PlatformException If the XID does not name a window on this display.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
            std::uintptr_t handle) override;

        /**
         * @brief Drains the X event queue into a batch.
         * @param destination Receives this frame's events.
         */
        void PollEvents(std::vector<PlatformEvent>& destination) override;

        /** @brief Gets the monotonic counter in nanoseconds. @return The counter. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;

        /** @brief Gets the counter frequency. @return One billion; the counter is nanoseconds. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;

        /** @brief Gets milliseconds since platform creation. @return The elapsed time. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;

        /** @brief Sleeps. @param milliseconds How long to sleep. */
        void Delay(std::uint32_t milliseconds) override;

        /** @brief Gets the keyboard service. @return The service, or null before `Video`. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        /** @brief Gets the mouse service. @return The service, or null before `Video`. */
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        /** @brief Gets the gamepad service. @return Null; X11 has no gamepad facility. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override { return nullptr; }
        /** @brief Gets the joystick service. @return Null; X11 has no joystick facility. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override { return nullptr; }
        /** @brief Gets the text input service. @return The service, or null before `Video`. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        /** @brief Gets the sensor service. @return Null; X11 has no sensor facility. */
        [[nodiscard]] IPlatformSensors* GetSensors() override { return nullptr; }
        /** @brief Gets the haptics service. @return Null; X11 has no haptics facility. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override { return nullptr; }
        /** @brief Gets the input device service. @return Null until XI2 enumeration lands. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override { return nullptr; }
        /** @brief Gets the clipboard service. @return The service, or null before `Video`. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        /** @brief Gets the display service. @return The service, or null without XRandR. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        /** @brief Gets the dialog service. @return Null; X11 has no native dialogs. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override { return nullptr; }
        /** @brief Gets the tray service. @return Null; a tray is a desktop-environment protocol. */
        [[nodiscard]] IPlatformTray* GetTray() override { return nullptr; }
        /** @brief Gets the camera provider. @return Null; X11 has no camera facility. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override { return nullptr; }
        /** @brief Gets the filesystem service. @return The portable implementation; never null. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override { return &fileSystem_; }
        /** @brief Gets the system information service. @return The portable implementation. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override { return &systemInfo_; }
        /** @brief Gets the GL context service. @return The service, or null without GLX. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        /** @brief Gets the Vulkan surface service. @return The service, or null before `Video`. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;

        /**
         * @brief Creates a CPU-frame presenter for a window.
         * @param window The window to present to.
         * @return The presenter.
         * @throws PlatformException If @p window does not belong to this platform.
         */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override;

    private:
        /// Wraps a window this platform owns, for AdoptWindow. Non-owning by contract: the
        /// caller's wrapper must not destroy a window the platform's own registry still tracks.
        class BorrowedWindow;

        [[nodiscard]] PlatformCapabilities ComputeCapabilities() const;
        void OpenConnection();
        void CloseConnection();
        [[nodiscard]] X11Connection& RequireConnection(const char* operation) const;
        void RegisterWindowWithServices(WindowId id, X11Window* window);
        void ForgetWindow(WindowId id);
        [[nodiscard]] X11Window* FindWindow(WindowId id) const;
        [[nodiscard]] X11Window* FindWindowByXid(::Window xid) const;
        void TranslateEvent(XEvent& event, std::vector<PlatformEvent>& destination);

        std::unique_ptr<X11Connection> connection_;
        /// Why the connection could not be opened, when it could not. Empty otherwise.
        std::string connectionError_;
        /// Computed once in the constructor. See the constructor comment for why it is cached
        /// rather than recomputed: the contract requires a stable set, and half of these answers
        /// come from the server.
        PlatformCapabilities capabilities_;
        /// Per-subsystem acquisition counts. Every subsystem refcounts, including the ones X11
        /// has no facility for -- the services report that absence, not the acquisition.
        std::map<PlatformSubsystem, int> refCounts_;

        std::map<WindowId, X11Window*> windows_;
        WindowId nextWindowId_ = 1;

        std::unique_ptr<X11Keyboard> keyboard_;
        std::unique_ptr<X11Mouse> mouse_;
        std::unique_ptr<X11TextInput> textInput_;
        std::unique_ptr<X11Clipboard> clipboard_;
        std::unique_ptr<X11Displays> displays_;
        std::unique_ptr<X11GlContext> glContext_;
        std::unique_ptr<X11VulkanSurface> vulkanSurface_;

        Common::StandardFileSystem fileSystem_{"cna-x11"};
        Common::StandardSystemInfo systemInfo_;

        std::chrono::steady_clock::time_point epoch_ = std::chrono::steady_clock::now();

        /// Double-click detection, which X11 leaves entirely to the client: the server reports
        /// presses and their timestamps and nothing else.
        ::Time lastClickTime_ = 0;
        unsigned int lastClickButton_ = 0;
        int lastClickX_ = 0;
        int lastClickY_ = 0;
        std::uint8_t clickCount_ = 0;

        /// Last reported pointer position, for deriving MouseMotionEvent deltas.
        int lastMotionX_ = 0;
        int lastMotionY_ = 0;
        bool hasLastMotion_ = false;
    };

} // namespace CNA::Platform::X11
