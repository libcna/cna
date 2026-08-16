// SPDX-License-Identifier: MS-PL
#pragma once

// Shared test scaffolding for driving CNA against a controlled platform.
//
// Why this exists
// ---------------
// Before the platform contract, every subsystem in modules/input had its own injectable backend
// seam -- eight of them, each with an interface, a global pointer and a `*ForTests` setter, all
// existing solely so a test could hand the subsystem a canned answer. PLAT-77's mapping concluded
// that those seams should end up *fewer*, not renamed, which means the tests that depended on
// them need one replacement rather than eight.
//
// This is that replacement. `PlatformTestDecorator` forwards every `IPlatform` method to a real
// platform, so a test overrides only the one service it cares about and everything else keeps
// working. `ScopedCurrentPlatform` installs it for the duration of a scope and restores whatever
// was there before, so a test that fails part-way through does not leave the process pointed at a
// destroyed object.
//
// Not in the public include tree on purpose: this is scaffolding, and a production build has no
// business being able to include it.

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Platform::Testing {

    /**
     * @brief An IPlatform that forwards everything to another one.
     *
     * Every method is virtual and every implementation is a plain forward, so a test overrides
     * the one accessor it needs and inherits real behaviour for the rest. That matters more than
     * it looks: a fake that stubs out everything tends to pass because nothing else is exercised,
     * while a decorator keeps the surrounding machinery real.
     */
    class PlatformTestDecorator : public IPlatform
    {
    public:
        /**
         * @brief Wraps a platform, taking ownership of it.
         *
         * @param inner The platform to forward to. Must not be null.
         */
        explicit PlatformTestDecorator(std::unique_ptr<IPlatform> inner)
            : inner_(std::move(inner))
        {
        }

        /**
         * @brief Wraps a freshly created platform of the build's default implementation.
         */
        PlatformTestDecorator() : inner_(PlatformFactory::Create()) {}

        /** @brief Gets the wrapped platform. @return The inner platform. */
        [[nodiscard]] IPlatform& GetInner() const { return *inner_; }

        /** @brief Gets the wrapped platform's name. @return The inner platform's name. */
        [[nodiscard]] const std::string& GetName() const override { return inner_->GetName(); }
        /** @brief Gets the wrapped platform's capabilities. @return The inner capability set. */
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override
        {
            return inner_->GetCapabilities();
        }

        /** @brief Acquires a subsystem on the inner platform. @param subsystem Which subsystem. */
        void AcquireSubsystem(const PlatformSubsystem subsystem) override
        {
            inner_->AcquireSubsystem(subsystem);
        }
        /** @brief Releases a subsystem on the inner platform. @param subsystem Which subsystem. */
        void ReleaseSubsystem(const PlatformSubsystem subsystem) override
        {
            inner_->ReleaseSubsystem(subsystem);
        }
        /**
         * @brief Gets whether a subsystem is up on the inner platform.
         * @param subsystem Which subsystem.
         * @return True if it is up.
         */
        [[nodiscard]] bool IsSubsystemInitialized(const PlatformSubsystem subsystem) const override
        {
            return inner_->IsSubsystemInitialized(subsystem);
        }

        /**
         * @brief Creates a window on the inner platform.
         * @param description The window's creation parameters.
         * @return The created window.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override
        {
            return inner_->CreateWindow(description);
        }

        /**
         * @brief Adopts a window through the inner platform.
         * @param windowId The existing window id.
         * @return The non-owning wrapper.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindow(
            const WindowId windowId) override
        {
            return inner_->AdoptWindow(windowId);
        }

        /**
         * @brief Adopts a legacy window token through the inner platform.
         * @param handle The existing platform-specific token.
         * @return The non-owning wrapper.
         */
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
            const std::uintptr_t handle) override
        {
            return inner_->AdoptWindowHandle(handle);
        }

        /** @brief Polls the inner platform's events. @param destination Receives the batch. */
        void PollEvents(std::vector<PlatformEvent>& destination) override
        {
            inner_->PollEvents(destination);
        }

        /** @brief Gets the inner platform's counter. @return The counter value. */
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override
        {
            return inner_->GetPerformanceCounter();
        }
        /** @brief Gets the inner platform's counter frequency. @return The frequency. */
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override
        {
            return inner_->GetPerformanceFrequency();
        }
        /** @brief Gets the inner platform's elapsed milliseconds. @return The elapsed time. */
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override
        {
            return inner_->GetTicksMilliseconds();
        }
        /** @brief Sleeps via the inner platform. @param milliseconds How long to sleep. */
        void Delay(const std::uint32_t milliseconds) override { inner_->Delay(milliseconds); }

        /** @brief Gets the inner keyboard service. @return The service, or null. */
        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override { return inner_->GetKeyboard(); }
        /** @brief Gets the inner mouse service. @return The service, or null. */
        [[nodiscard]] IPlatformMouse* GetMouse() override { return inner_->GetMouse(); }
        /** @brief Gets the inner gamepad service. @return The service, or null. */
        [[nodiscard]] IPlatformGamepad* GetGamepad() override { return inner_->GetGamepad(); }
        /** @brief Gets the inner joystick service. @return The service, or null. */
        [[nodiscard]] IPlatformJoystick* GetJoystick() override { return inner_->GetJoystick(); }
        /** @brief Gets the inner text input service. @return The service, or null. */
        [[nodiscard]] IPlatformTextInput* GetTextInput() override { return inner_->GetTextInput(); }
        /** @brief Gets the inner sensor service. @return The service, or null. */
        [[nodiscard]] IPlatformSensors* GetSensors() override { return inner_->GetSensors(); }
        /** @brief Gets the inner haptics service. @return The service, or null. */
        [[nodiscard]] IPlatformHaptics* GetHaptics() override { return inner_->GetHaptics(); }
        /** @brief Gets the inner device enumeration service. @return The service, or null. */
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override
        {
            return inner_->GetInputDevices();
        }
        /** @brief Gets the inner clipboard service. @return The service, or null. */
        [[nodiscard]] IPlatformClipboard* GetClipboard() override { return inner_->GetClipboard(); }
        /** @brief Gets the inner display service. @return The service, or null. */
        [[nodiscard]] IPlatformDisplays* GetDisplays() override { return inner_->GetDisplays(); }
        /** @brief Gets the inner dialog service. @return The service, or null. */
        [[nodiscard]] IPlatformDialogs* GetDialogs() override { return inner_->GetDialogs(); }
        /** @brief Gets the inner tray service. @return The service, or null. */
        [[nodiscard]] IPlatformTray* GetTray() override { return inner_->GetTray(); }
        /** @brief Gets the inner camera provider. @return The provider, or null. */
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override { return inner_->GetCamera(); }
        /** @brief Gets the inner filesystem service. @return The service. */
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override
        {
            return inner_->GetFileSystem();
        }
        /** @brief Gets the inner system information service. @return The service. */
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override
        {
            return inner_->GetSystemInfo();
        }
        /** @brief Gets the inner OpenGL context service. @return The service, or null. */
        [[nodiscard]] IPlatformGlContext* GetGlContext() override { return inner_->GetGlContext(); }
        /** @brief Gets the inner Vulkan surface service. @return The service, or null. */
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override
        {
            return inner_->GetVulkanSurface();
        }

        /**
         * @brief Creates a surface presenter on the inner platform.
         * @param window The window to present to.
         * @return The presenter.
         */
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override
        {
            return inner_->CreateSurfacePresenter(window);
        }

    private:
        std::unique_ptr<IPlatform> inner_;
    };

    /**
     * @brief Installs a platform for the duration of a scope, then puts back what was there.
     *
     * Restoring rather than clearing matters: a test that ran inside a `Game`'s lifetime would
     * otherwise leave the game's static API surface aimed at nothing. Restoring on destruction
     * rather than at the end of the test body matters too — a failed `ASSERT_*` returns early,
     * and a manual reset at the bottom of the test would never run.
     */
    class ScopedCurrentPlatform
    {
    public:
        /**
         * @brief Installs a platform, remembering the previous one.
         *
         * @param platform The platform to install for this scope. Not owned; must outlive this.
         */
        explicit ScopedCurrentPlatform(IPlatform& platform)
            : previous_(HasCurrentPlatform() ? &GetCurrentPlatform() : nullptr)
        {
            SetCurrentPlatform(&platform);
        }

        /** @brief Restores the previously installed platform. */
        ~ScopedCurrentPlatform() { SetCurrentPlatform(previous_); }

        ScopedCurrentPlatform(const ScopedCurrentPlatform&) = delete;
        ScopedCurrentPlatform& operator=(const ScopedCurrentPlatform&) = delete;

    private:
        IPlatform* previous_;
    };

} // namespace CNA::Platform::Testing
