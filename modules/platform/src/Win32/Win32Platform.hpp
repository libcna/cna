// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "Win32GraphicsServices.hpp"
#include "Win32InputServices.hpp"
#include "Win32SystemServices.hpp"
#include "Win32Window.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace CNA::Platform::Win32 {

    /**
     * @brief The native Win32 implementation of the CNA platform contract.
     *
     * Built directly on `user32`, `gdi32` and the shell APIs. It contains no SDL of any kind --
     * not for windowing, events, keyboard, mouse, text input, timing, clipboard, displays or
     * dialogs -- which is what makes it the evidence that SDL is a *backend* of CNA rather than
     * the substrate CNA is written against.
     *
     * ### Process policy belongs to the host
     *
     * The backend deliberately does not change DPI awareness, timer resolution, the current
     * directory, or the process's COM apartment beyond a balanced reference. CNA is a framework
     * inside somebody else's process, and each of those is global state that an application may
     * already have set for reasons this code cannot see. See plans/plan_win32.md section 10.
     *
     * ### Events
     *
     * `DispatchMessageW` calls a window procedure *synchronously*, so a window cannot hand events
     * back to `PollEvents` through a return value. Each window pushes into this platform's queue
     * instead, and `PollEvents` pumps the thread's message queue and then moves what accumulated
     * into the caller's batch -- one drain per frame, with the batch's capacity reused.
     */
    class Win32Platform final : public IPlatform, private Win32WindowHost, private Win32PlatformAccess
    {
    public:
        /** @brief Creates the platform. Acquires no subsystem: acquisition stays lazy. */
        Win32Platform();

        /** @brief Destroys the platform and every service it owns. */
        ~Win32Platform() override;

        Win32Platform(const Win32Platform&) = delete;
        Win32Platform& operator=(const Win32Platform&) = delete;

        /** @brief Gets this implementation's name. @return `"Win32"`. */
        [[nodiscard]] const std::string& GetName() const override;

        /** @brief Gets what this platform can do on this host. @return The capability set. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override;

        /** @brief Acquires a subsystem. @param subsystem The subsystem to acquire. */
        void AcquireSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Releases a subsystem. @param subsystem The subsystem to release. */
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Gets whether a subsystem is up. @param subsystem The subsystem. @return True if acquired. */
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;

        /** @brief Creates a native window. @param description Creation parameters. @return The window. */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override;

        /**
         * @brief Wraps one of this platform's own windows without taking ownership.
         *
         * @param windowId The stable id of a live window of this platform.
         * @return A non-owning wrapper.
         * @throws PlatformException If the id names no live window of this platform.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId windowId) override;

        /**
         * @brief Wraps an existing `HWND` without taking ownership.
         *
         * @param handle An `HWND` encoded as an integer, as `GetWindowHandle()` returns.
         * @return A non-owning wrapper.
         * @throws PlatformException If the token does not name a live window.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
            std::uintptr_t handle) override;

        /** @brief Pumps the thread's messages and drains the events they produced. @param destination Receives this frame's events. */
        void PollEvents(std::vector<PlatformEvent>& destination) override;

        /** @brief Gets the high-resolution counter. @return The `QueryPerformanceCounter` value. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;

        /** @brief Gets the counter frequency. @return The `QueryPerformanceFrequency` value; never zero. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;

        /** @brief Gets elapsed milliseconds since construction. @return The elapsed time. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;

        /** @brief Sleeps for approximately the requested duration. @param milliseconds How long to sleep. */
        void Delay(std::uint32_t milliseconds) override;

        /** @brief Gets the keyboard service. @return The Win32 keyboard; never null. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        /** @brief Gets the mouse service. @return The Win32 mouse; never null. */
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        /** @brief Gets the gamepad service. @return Null; XInput is not implemented yet. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override;
        /** @brief Gets the raw joystick service. @return Null; not implemented yet. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override;
        /** @brief Gets the text input service. @return The Win32 text input; never null. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        /** @brief Gets the sensor service. @return Null; there is no desktop sensor source. */
        [[nodiscard]] IPlatformSensors* GetSensors() override;
        /** @brief Gets the haptics service. @return Null; not implemented yet. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override;
        /** @brief Gets the input device enumeration service. @return The Raw Input enumerator. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override;
        /** @brief Gets the clipboard service. @return The Win32 clipboard; never null. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        /** @brief Gets the display service. @return The monitor enumerator; never null. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        /** @brief Gets the dialog service. @return The Win32 dialogs; never null. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override;
        /** @brief Gets the tray service. @return Null; the notification area is not implemented yet. */
        [[nodiscard]] IPlatformTray* GetTray() override;
        /** @brief Gets the camera provider. @return Null; Media Foundation capture is out of scope. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override;
        /** @brief Gets the filesystem service. @return The Win32 filesystem; never null. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override;
        /** @brief Gets the system information service. @return The Win32 system info; never null. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override;
        /** @brief Gets the OpenGL context service. @return The WGL service; never null. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        /** @brief Gets the Vulkan surface service. @return The service when a loader is present, else null. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;

        /** @brief Creates a GDI surface presenter. @param window The window. @return The presenter. */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override;

    private:
        // --- Win32EventSink / Win32WindowHost ---------------------------------------------------
        void Push(PlatformEvent event) override;
        void OnWindowDestroyed(Win32Window& window) override;
        void OnRawPointerDelta(int deltaX, int deltaY) override;
        void OnQuitRequested() override;

        // --- Win32PlatformAccess ---------------------------------------------------------------
        [[nodiscard]] Win32Window* GetFocusedWindow() override;
        [[nodiscard]] Win32Window* FindWindow(WindowId window) override;
        [[nodiscard]] Win32Window* GetAnyWindow() override;
        [[nodiscard]] PlatformCapabilities GetPlatformCapabilities() const override;
        [[nodiscard]] const std::string& GetPlatformName() const override;

        std::map<PlatformSubsystem, int> ownedRefCounts_;
        /// Owning wrappers only, keyed by the id events carry. This is what the services resolve
        /// a WindowId through, so exactly one entry per live native window is the invariant.
        std::map<WindowId, Win32Window*> windows_;

        /// Borrowed wrappers from AdoptWindow/AdoptWindowHandle. Deliberately NOT in the registry
        /// above -- they share an owner's id and would displace it -- but still tracked, because
        /// they hold a back-pointer to this platform that has to be cleared if it is destroyed
        /// first.
        std::vector<Win32Window*> adopted_;
        std::deque<PlatformEvent> pending_;
        WindowId nextWindowId_ = 1;
        std::uint64_t createdAtCounter_ = 0;
        std::uint64_t counterFrequency_ = 1;

        Win32Clipboard clipboard_;
        Win32Displays displays_;
        Win32Dialogs dialogs_;
        Win32FileSystem fileSystem_;
        Win32SystemInfo systemInfo_;
        Win32Keyboard keyboard_;
        Win32Mouse mouse_;
        Win32TextInput textInput_;
        Win32InputDevices inputDevices_;
        Win32GlContext glContext_;
        Win32VulkanSurface vulkanSurface_;
    };

} // namespace CNA::Platform::Win32
