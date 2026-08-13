// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "Sdl3Camera.hpp"
#include "Sdl3GraphicsServices.hpp"
#include "Sdl3DeviceServices.hpp"
#include "Sdl3InputServices.hpp"
#include "Sdl3SystemServices.hpp"
#include "Sdl3Tray.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief The SDL3 implementation of the CNA platform contract.
     *
     * The first implementation, and the reference the conformance suite compares others against.
     * It reproduces CNA's existing SDL3 behaviour exactly; it is not an opportunity to redesign
     * semantics. See docs/platform-sdl-lifecycle-audit.md for the seven constraints this must
     * honour, and note in particular that it deliberately does **not** call `SDL_Init()` or
     * `SDL_Quit()` — global SDL lifetime belongs to the host application.
     */
    class Sdl3Platform final : public IPlatform
    {
    public:
        /** @brief Creates the platform. Acquires no subsystem: acquisition stays lazy. */
        Sdl3Platform();

        /** @brief Releases every subsystem this instance still holds. */
        ~Sdl3Platform() override;

        Sdl3Platform(const Sdl3Platform&) = delete;
        Sdl3Platform& operator=(const Sdl3Platform&) = delete;

        /** @brief Gets this implementation's name. @return `"SDL3"`. */
        [[nodiscard]] const std::string& GetName() const override;

        /** @brief Gets what SDL3 can do on this host. @return The capability set. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override;

        /** @brief Acquires a subsystem. @param subsystem The subsystem to acquire. */
        void AcquireSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Releases a subsystem. @param subsystem The subsystem to release. */
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Gets whether a subsystem is up. @param subsystem The subsystem. @return True if initialised. */
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;

        /** @brief Creates a window. @param description Creation parameters. @return The window. */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override;

        /**
         * @brief Wraps an existing SDL window without taking native ownership.
         * @param windowId The existing SDL window id.
         * @return A non-owning platform window wrapper.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId windowId) override;

        /**
         * @brief Wraps an existing SDL window supplied through the legacy integer-handle API.
         * @param handle The non-owning SDL window pointer encoded as an integer.
         * @return A non-owning platform window wrapper.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
            std::uintptr_t handle) override;

        /** @brief Drains pending events. @param destination Receives this frame's events. */
        void PollEvents(std::vector<PlatformEvent>& destination) override;

        /** @brief Gets the high-resolution counter. @return Its current value. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;

        /** @brief Gets the counter frequency. @return Counter units per second. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;

        /** @brief Gets elapsed milliseconds. @return Milliseconds since SDL started counting. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;

        /** @brief Sleeps. @param milliseconds How long to sleep for. */
        void Delay(std::uint32_t milliseconds) override;

        /** @brief Gets the keyboard service. @return The SDL3 keyboard; never null. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        /** @brief Gets the mouse service. @return The SDL3 mouse; never null. */
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        /** @brief Gets the gamepad service. @return The SDL3 gamepad service; never null. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override;
        /** @brief Gets the raw joystick service. @return The SDL3 joystick service; never null. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override;
        /** @brief Gets the text input service. @return The SDL3 text input service; never null. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        /** @brief Gets the sensor service. @return The SDL3 sensor service; never null. */
        [[nodiscard]] IPlatformSensors* GetSensors() override;
        /** @brief Gets the haptics service. @return The SDL3 haptics service; never null. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override;
        /** @brief Gets the input device enumeration service. @return The service; never null. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override;
        /** @brief Gets the clipboard service. @return The SDL3 clipboard; never null. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        /** @brief Gets the display service. @return The SDL3 display enumerator; never null. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        /** @brief Gets the dialog service. @return The SDL3 dialog service; never null. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override;
        /** @brief Gets the tray service. @return The service on desktop targets, otherwise null. */
        [[nodiscard]] IPlatformTray* GetTray() override;
        /** @brief Gets the camera provider. @return The provider when SDL has a camera driver. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override;
        /** @brief Gets the filesystem service. @return The SDL3 filesystem; never null. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override;
        /** @brief Gets the system information service. @return The SDL3 system info; never null. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override;
        /** @brief Gets the OpenGL context service. @return The SDL3 GL service; never null. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        /** @brief Gets the Vulkan surface service. @return The SDL3 Vulkan service; never null. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;

        /**
         * @brief Creates a surface presenter for a window.
         * @param window The window to present to.
         * @return The presenter.
         */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override;

    private:
        /// Per-subsystem acquisition count owned by THIS instance. SDL keeps its own global
        /// refcount; this one exists so the destructor can release exactly what it acquired and
        /// no more, which matters when the host application holds subsystems of its own.
        std::map<PlatformSubsystem, int> ownedRefCounts_;
        mutable std::mutex subsystemMutex_;

        /// Services are owned by the platform and outlive every caller's use of them, which is
        /// what makes returning a raw pointer safe: a caller never owns what it is handed.
        Sdl3Clipboard clipboard_;
        Sdl3Displays displays_;
        Sdl3FileSystem fileSystem_;
        Sdl3SystemInfo systemInfo_;
        Sdl3GlContext glContext_;
        Sdl3VulkanSurface vulkanSurface_;
        Sdl3Keyboard keyboard_;
        Sdl3Mouse mouse_;
        Sdl3Gamepad gamepad_;
        Sdl3Joystick joystick_;
        Sdl3TextInput textInput_;
        Sdl3Sensors sensors_;
        Sdl3Haptics haptics_;
        Sdl3InputDevices inputDevices_;
        Sdl3Dialogs dialogs_;
        Sdl3Tray tray_;
        Sdl3CameraProvider camera_;
    };

} // namespace CNA::Platform::Sdl3
