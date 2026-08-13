// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "../Common/StandardFileSystem.hpp"
#include "../Common/StandardSystemInfo.hpp"

#include <map>
#include <memory>
#include <string>

namespace CNA::Platform::Sdl2 {

    /** SDL2 implementation of the CNA platform contract. */
    class Sdl2Platform final : public IPlatform
    {
    public:
        Sdl2Platform();
        ~Sdl2Platform() override;

        Sdl2Platform(const Sdl2Platform&) = delete;
        Sdl2Platform& operator=(const Sdl2Platform&) = delete;

        [[nodiscard]] const std::string& GetName() const override;
        [[nodiscard]] PlatformCapabilities GetCapabilities() const override;
        void AcquireSubsystem(PlatformSubsystem subsystem) override;
        void ReleaseSubsystem(PlatformSubsystem subsystem) override;
        [[nodiscard]] bool IsSubsystemInitialized(PlatformSubsystem subsystem) const override;
        [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
            const WindowDescription& description) override;
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindow(WindowId windowId) override;
        [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(std::uintptr_t handle) override;
        void PollEvents(std::vector<PlatformEvent>& destination) override;
        [[nodiscard]] std::uint64_t GetPerformanceCounter() const override;
        [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override;
        [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override;
        void Delay(std::uint32_t milliseconds) override;

        [[nodiscard]] IPlatformKeyboard* GetKeyboard() override;
        [[nodiscard]] IPlatformMouse* GetMouse() override;
        [[nodiscard]] IPlatformGamepad* GetGamepad() override;
        [[nodiscard]] IPlatformJoystick* GetJoystick() override;
        [[nodiscard]] IPlatformTextInput* GetTextInput() override;
        [[nodiscard]] IPlatformSensors* GetSensors() override;
        [[nodiscard]] IPlatformHaptics* GetHaptics() override;
        [[nodiscard]] IPlatformInputDevices* GetInputDevices() override;
        [[nodiscard]] IPlatformClipboard* GetClipboard() override;
        [[nodiscard]] IPlatformDisplays* GetDisplays() override;
        [[nodiscard]] IPlatformDialogs* GetDialogs() override;
        [[nodiscard]] IPlatformTray* GetTray() override;
        [[nodiscard]] IPlatformCameraProvider* GetCamera() override;
        [[nodiscard]] IPlatformFileSystem* GetFileSystem() override;
        [[nodiscard]] IPlatformSystemInfo* GetSystemInfo() override;
        [[nodiscard]] IPlatformGlContext* GetGlContext() override;
        [[nodiscard]] IPlatformVulkanSurface* GetVulkanSurface() override;
        [[nodiscard]] std::unique_ptr<IPlatformSurfacePresenter> CreateSurfacePresenter(
            IPlatformWindow& window) override;

    private:
        class GlContext final : public IPlatformGlContext
        {
        public:
            [[nodiscard]] GlContextHandle CreateContext(
                WindowId window, const GlContextDescription& description) override;
            void DestroyContext(GlContextHandle context) override;
            void MakeCurrent(WindowId window, GlContextHandle context) override;
            void SwapBuffers(WindowId window) override;
            bool SetSwapInterval(int interval) override;
            [[nodiscard]] void* GetProcAddress(const std::string& name) const override;
            [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override;
            [[nodiscard]] GlContextDescription GetContextAttributes(
                GlContextHandle context) const override;
        };

        std::map<PlatformSubsystem, int> ownedRefCounts_;
        Common::StandardFileSystem fileSystem_{"cna-sdl2"};
        Common::StandardSystemInfo systemInfo_;
        GlContext glContext_;
    };

} // namespace CNA::Platform::Sdl2
