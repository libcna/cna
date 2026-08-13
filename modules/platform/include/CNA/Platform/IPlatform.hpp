// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformCamera.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformVulkanSurface.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/Input/IPlatformGamepad.hpp"
#include "CNA/Platform/Input/IPlatformHaptics.hpp"
#include "CNA/Platform/Input/IPlatformInputDevices.hpp"
#include "CNA/Platform/Input/IPlatformJoystick.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/IPlatformMouse.hpp"
#include "CNA/Platform/Input/IPlatformSensors.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/PlatformCapabilities.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Platform {

    /** @brief A platform subsystem that is acquired on demand and released when no longer needed. */
    enum class PlatformSubsystem
    {
        /** @brief Windowing and display. Implies event delivery. */
        Video,
        /** @brief Audio output and capture. */
        Audio,
        /** @brief Gamepads and joysticks. */
        Gamepad,
        /** @brief Haptic devices. */
        Haptic,
        /** @brief Motion and orientation sensors. */
        Sensor
    };

    /**
     * @brief The root of the CNA platform contract.
     *
     * One implementation is compiled per binary (`CNA_PLATFORM`), and the instance is created
     * once at startup through PlatformFactory and never replaced. The interface stays virtual
     * anyway, because the conformance suite needs two implementations live in one process.
     *
     * ### Subsystems are acquired, not initialised
     *
     * There is deliberately no `Initialize()`/`Shutdown()` pair. PLAT-4's audit established that
     * **CNA production code never calls `SDL_Init()` or `SDL_Quit()`** — all 30 files that do are
     * tests and examples, i.e. code acting as the application. Production CNA acquires individual
     * subsystems lazily and lets the underlying library refcount them, and global library
     * lifetime belongs to the host application.
     *
     * Turning that into a platform-owned `Initialize()`/`Shutdown()` would take global lifetime
     * away from applications that already call `SDL_Quit()` in their own `main()`, changing
     * observable behaviour for every existing game. So the contract is acquire/release with
     * refcounting, plus a query for the one place CNA inspects the count.
     *
     * See docs/platform-sdl-lifecycle-audit.md for the seven requirements this places on an
     * implementation — notably that unpaired releases are tolerated by design, and that subsystem
     * calls must never be routed through a main-thread dispatch.
     *
     * ### Events are polled in batches
     *
     * `PollEvents` fills a caller-owned vector once per frame. Per-event virtual dispatch is the
     * design `cnaplatform.md` explicitly warns against, and the batch signature is what makes it
     * impossible rather than merely discouraged.
     */
    class IPlatform
    {
    public:
        /** @brief Destroys the platform, releasing any subsystems it still holds. */
        virtual ~IPlatform() = default;

        /**
         * @brief Gets this implementation's name.
         *
         * @return A stable name such as `"SDL3"` or `"Headless"`, used in refusal messages and
         * conformance reports.
         */
        [[nodiscard]] virtual const std::string& GetName() const = 0;

        /**
         * @brief Gets what this implementation can do.
         *
         * Read **once** and cached by the owning subsystem. Calling this per frame is a defect.
         *
         * @return The capability set, by value.
         */
        [[nodiscard]] virtual PlatformCapabilities GetCapabilities() const = 0;

        // --- Subsystems --------------------------------------------------------------------

        /**
         * @brief Acquires a subsystem, initialising it if this is the first acquisition.
         *
         * Refcounted: nested acquisitions are cheap and each must be matched by a release,
         * except where an implementation deliberately pins a subsystem for the process lifetime.
         *
         * @param subsystem The subsystem to acquire.
         * @throws PlatformException If the subsystem exists but could not be initialised.
         * @throws PlatformNotSupportedException If this platform has no such subsystem at all.
         */
        virtual void AcquireSubsystem(PlatformSubsystem subsystem) = 0;

        /**
         * @brief Releases a subsystem previously acquired.
         *
         * Releasing a subsystem that was never acquired is a **no-op, not an error**. Cleanup
         * code may legitimately run after partial initialization, so implementations preserve
         * that tolerance even though normal owners balance every successful acquisition.
         *
         * @param subsystem The subsystem to release.
         */
        virtual void ReleaseSubsystem(PlatformSubsystem subsystem) = 0;

        /**
         * @brief Gets whether a subsystem is currently initialised.
         *
         * @param subsystem The subsystem to query.
         * @return True if it is up.
         */
        [[nodiscard]] virtual bool IsSubsystemInitialized(PlatformSubsystem subsystem) const = 0;

        // --- Windows -----------------------------------------------------------------------

        /**
         * @brief Creates a window.
         *
         * @param description The window's creation parameters.
         * @return The created window, owned by the caller and valid until destroyed.
         * @throws PlatformException If the window could not be created.
         * @throws PlatformNotSupportedException If a second window is requested and this platform
         * reports no `MultipleWindows` capability.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) = 0;

        /**
         * @brief Validates and wraps a caller-owned window already known to this platform.
         *
         * The returned wrapper never takes ownership of the caller's native window. The id must
         * belong to this platform implementation and remain valid until the wrapper is destroyed.
         * Implementations that cannot resolve external windows refuse explicitly.
         *
         * @param windowId The stable event id of the existing window.
         * @return A non-owning platform-window wrapper.
         * @throws PlatformException If the id is invalid, belongs to another implementation, or
         * external window adoption is unsupported.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId windowId);

        /**
         * @brief Validates and wraps a window identified by a legacy integer-handle token.
         *
         * Tokens come from `IPlatformWindow::GetWindowHandle()` or from an application using the
         * platform's compatibility API. The returned wrapper is non-owning. Implementations that
         * use their stable `WindowId` as the token inherit the default round trip; implementations
         * with a distinct toolkit handle override this method and interpret it locally.
         *
         * @param handle The non-zero platform-specific token.
         * @return A non-owning platform-window wrapper.
         * @throws PlatformException If the token is invalid or adoption is unsupported.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
            std::uintptr_t handle);

        // --- Events ------------------------------------------------------------------------

        /**
         * @brief Drains pending events into a caller-owned batch.
         *
         * The batch is cleared and refilled. Reusing the same vector across frames avoids
         * per-frame allocation, which is why the buffer is the caller's and not returned.
         *
         * @param destination Receives this frame's events.
         */
        virtual void PollEvents(std::vector<PlatformEvent>& destination) = 0;

        // --- Timing ------------------------------------------------------------------------

        /**
         * @brief Gets a high-resolution monotonic counter.
         *
         * The fixed-timestep game loop's only timing dependency.
         *
         * @return The counter's current value.
         */
        [[nodiscard]] virtual std::uint64_t GetPerformanceCounter() const = 0;

        /**
         * @brief Gets how many counter units make one second.
         *
         * @return The counter frequency; never zero.
         */
        [[nodiscard]] virtual std::uint64_t GetPerformanceFrequency() const = 0;

        /**
         * @brief Gets milliseconds elapsed since platform creation.
         *
         * @return The elapsed time in milliseconds.
         */
        [[nodiscard]] virtual std::uint64_t GetTicksMilliseconds() const = 0;

        /**
         * @brief Sleeps for approximately the requested duration.
         *
         * @param milliseconds How long to sleep. Zero yields the remainder of the time slice.
         */
        virtual void Delay(std::uint32_t milliseconds) = 0;

        // --- Services ----------------------------------------------------------------------
        //
        // Each returns null when the corresponding capability is false, rather than a stub that
        // silently does nothing. A null here means "ask the capability set first"; a
        // silently-inert stub would let a caller believe it had working clipboard support.

        /**
         * @brief Gets the keyboard service.
         *
         * @return The service, or null when this platform has no keyboard.
         */
        [[nodiscard]] virtual IPlatformKeyboard* GetKeyboard() = 0;

        /**
         * @brief Gets the mouse service.
         *
         * @return The service, or null when this platform has no pointer.
         */
        [[nodiscard]] virtual IPlatformMouse* GetMouse() = 0;

        /**
         * @brief Gets the gamepad service.
         *
         * @return The service, or null when this platform reports no `Gamepad` capability.
         */
        [[nodiscard]] virtual IPlatformGamepad* GetGamepad() = 0;

        /**
         * @brief Gets the raw joystick service.
         *
         * @return The service, or null when this platform reports no `Joystick` capability.
         */
        [[nodiscard]] virtual IPlatformJoystick* GetJoystick() = 0;

        /**
         * @brief Gets the text input service.
         *
         * @return The service, or null when this platform reports no `TextInput` capability.
         */
        [[nodiscard]] virtual IPlatformTextInput* GetTextInput() = 0;

        /**
         * @brief Gets the sensor service.
         *
         * @return The service, or null when this platform reports no `Sensors` capability.
         */
        [[nodiscard]] virtual IPlatformSensors* GetSensors() = 0;

        /**
         * @brief Gets the haptics service.
         *
         * @return The service, or null when this platform reports no `Haptics` capability.
         */
        [[nodiscard]] virtual IPlatformHaptics* GetHaptics() = 0;

        /**
         * @brief Gets the input device enumeration service.
         *
         * @return The service, or null when this platform reports no `InputDeviceEnumeration`
         * capability.
         */
        [[nodiscard]] virtual IPlatformInputDevices* GetInputDevices() = 0;

        /**
         * @brief Gets the clipboard service.
         *
         * @return The service, or null when this platform reports no `Clipboard` capability.
         */
        [[nodiscard]] virtual IPlatformClipboard* GetClipboard() = 0;

        /**
         * @brief Gets the display enumeration service.
         *
         * @return The service, or null when this platform has no displays.
         */
        [[nodiscard]] virtual IPlatformDisplays* GetDisplays() = 0;

        /**
         * @brief Gets the dialog service.
         *
         * @return The service, or null when this platform reports neither `MessageBox` nor
         * `NativeFileDialog`.
         */
        [[nodiscard]] virtual IPlatformDialogs* GetDialogs() = 0;

        /**
         * @brief Gets the system-tray service.
         *
         * @return The service, or null when this platform reports no `Tray` capability.
         */
        [[nodiscard]] virtual IPlatformTray* GetTray() = 0;

        /**
         * @brief Gets the camera provider.
         *
         * @return The provider, or null when this platform reports no `Camera` capability.
         */
        [[nodiscard]] virtual IPlatformCameraProvider* GetCamera() = 0;

        /**
         * @brief Gets the filesystem service.
         *
         * @return The service. Never null: every platform can resolve paths.
         */
        [[nodiscard]] virtual IPlatformFileSystem* GetFileSystem() = 0;

        /**
         * @brief Gets the system information service.
         *
         * @return The service. Never null, though individual queries may report unknown.
         */
        [[nodiscard]] virtual IPlatformSystemInfo* GetSystemInfo() = 0;

        /**
         * @brief Gets the OpenGL context service.
         *
         * @return The service, or null when this platform reports no `OpenGlContext` capability.
         */
        [[nodiscard]] virtual IPlatformGlContext* GetGlContext() = 0;

        /**
         * @brief Gets the Vulkan surface service.
         *
         * @return The service, or null when this platform reports no `VulkanSurface` capability.
         */
        [[nodiscard]] virtual IPlatformVulkanSurface* GetVulkanSurface() = 0;

        /**
         * @brief Creates a surface presenter for a window.
         *
         * @param window The window to present to.
         * @return The presenter, owned by the caller.
         * @throws PlatformNotSupportedException If this platform reports no `SurfacePresentation`
         * capability.
         */
        [[nodiscard]] virtual std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) = 0;
    };

    /**
     * @brief Returns the stable, human-readable name of a subsystem.
     *
     * @param subsystem The subsystem to name.
     * @return A stable name such as `"Video"`.
     */
    [[nodiscard]] const std::string& ToString(PlatformSubsystem subsystem);

} // namespace CNA::Platform
