// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "../Common/StandardFileSystem.hpp"
#include "../Common/StandardSystemInfo.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace CNA::Platform::Headless {

    /**
     * @brief A platform with no windowing system, no input devices and no display.
     *
     * The second implementation of the contract, and the reason the contract can be trusted. A
     * contract with one implementation is indirection; `Sdl3Platform` alone could have been
     * satisfied by an interface shaped entirely around SDL's assumptions without anyone noticing.
     * This one has to answer every method with no SDL underneath it at all, and reports most
     * capabilities as `false`, which is what exercises every refusal path.
     *
     * It is also useful in its own right: it runs game logic in CI with no display server, the
     * same argument `plan_headless.md` already made for the headless *renderer*.
     *
     * Always compiled, regardless of `CNA_PLATFORM`, because the conformance suite needs two
     * implementations live in one process.
     */
    class HeadlessPlatform final : public IPlatform
    {
    public:
        /** @brief Creates the platform. */
        HeadlessPlatform();

        /** @brief Destroys the platform and any windows it still owns. */
        ~HeadlessPlatform() override;

        HeadlessPlatform(const HeadlessPlatform&) = delete;
        HeadlessPlatform& operator=(const HeadlessPlatform&) = delete;

        /** @brief Gets this implementation's name. @return `"Headless"`. */
        [[nodiscard]] const std::string& GetName() const override;

        /** @brief Gets what this platform can do. @return A capability set that is mostly false. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override;

        /** @brief Acquires a subsystem. @param subsystem The subsystem to acquire. */
        void AcquireSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Releases a subsystem. @param subsystem The subsystem to release. */
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;

        /** @brief Gets whether a subsystem is up. @param subsystem The subsystem. @return True if acquired. */
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;

        /**
         * @brief Creates an in-memory window.
         *
         * Real enough to be resized, retitled and queried, so `GameWindow` and
         * `GraphicsDeviceManager` behave as they would anywhere. Its native handle reports
         * `Headless`, so a renderer needing one refuses deterministically.
         *
         * @param description The window's creation parameters.
         * @return The created window.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override;

        /** @brief Drains injected events. @param destination Receives this frame's events. */
        void PollEvents(std::vector<PlatformEvent>& destination) override;

        /**
         * @brief Injects an event to be delivered by the next PollEvents.
         *
         * The mechanism that makes input, runtime and lifecycle behaviour testable with no
         * display server: a test drives the exact event sequence it wants instead of hoping the
         * environment produces one.
         *
         * @param event The event to queue.
         */
        void InjectEvent(const PlatformEvent& event);

        /** @brief Gets a monotonic counter. @return Nanoseconds from a steady clock. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;

        /** @brief Gets the counter frequency. @return One billion; the counter is in nanoseconds. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;

        /** @brief Gets elapsed milliseconds since construction. @return The elapsed time. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;

        /** @brief Sleeps for the requested duration. @param milliseconds How long to sleep. */
        void Delay(std::uint32_t milliseconds) override;

        /** @brief Gets the keyboard service. @return Null; there is no keyboard. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        /** @brief Gets the mouse service. @return Null; there is no pointer. */
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        /** @brief Gets the gamepad service. @return Null; there are no gamepads. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override;
        /** @brief Gets the raw joystick service. @return Null; there are no joysticks. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override;
        /** @brief Gets the text input service. @return Null; there is no text input. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        /** @brief Gets the sensor service. @return Null; there are no sensors. */
        [[nodiscard]] IPlatformSensors* GetSensors() override;
        /** @brief Gets the haptics service. @return Null; there are no haptic devices. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override;
        /** @brief Gets the input device enumeration service. @return Null; no devices exist. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override;
        /** @brief Gets the clipboard service. @return Null; there is no clipboard. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        /** @brief Gets the display service. @return Null; there are no displays. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        /** @brief Gets the dialog service. @return Null; there is no one to show a dialog to. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override;
        /** @brief Gets the tray service. @return Null; there is no desktop notification area. */
        [[nodiscard]] IPlatformTray* GetTray() override;
        /** @brief Gets the camera provider. @return Null; there are no capture devices. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override;
        /** @brief Gets the filesystem service. @return A real filesystem; never null. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override;
        /** @brief Gets the system information service. @return Real host information; never null. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override;
        /** @brief Gets the OpenGL context service. @return Null; there is no context to create. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        /** @brief Gets the Vulkan surface service. @return Null; there is no surface to create. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;

        /**
         * @brief Refuses to create a surface presenter.
         *
         * @param window The window that would be presented to.
         * @return Never returns.
         * @throws PlatformNotSupportedException Always: there is nowhere to present.
         */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override;

    private:
        std::map<PlatformSubsystem, int> refCounts_;
        std::vector<PlatformEvent> queued_;
        std::uint64_t createdAtNanoseconds_ = 0;
        WindowId nextWindowId_ = 1;

        std::unique_ptr<Common::StandardFileSystem> fileSystem_;
        std::unique_ptr<Common::StandardSystemInfo> systemInfo_;
    };

} // namespace CNA::Platform::Headless
