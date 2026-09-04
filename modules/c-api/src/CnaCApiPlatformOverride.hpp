// SPDX-License-Identifier: MS-PL

/* plans/plan_binding.md CBIND-048: one seam where the C API used to have four.
 *
 * Before the platform separation, the message box, the file dialog, the tray and the camera each
 * had their own injectable backend in `CNA::Devices::Detail`, and the C API's
 * `cna_*_set_test_backend_ext` routes installed a fake into each. The platform campaign removed all
 * four interfaces on purpose: those services are things a *platform* hands out now, and its own
 * mapping concluded that the seams should end up fewer rather than renamed.
 *
 * So this is the one replacement. `PlatformOverride` forwards every `IPlatform` method to the real
 * platform and substitutes only the services a test asked to fake, which matters more than it
 * looks: a fake that stubs out everything passes because nothing else is exercised, while a
 * decorator keeps the surrounding machinery real. `ScopedPlatformOverride` owns the installation
 * and puts back what was there before, so a failing test cannot leave the process pointing at a
 * destroyed object.
 *
 * The platform module has scaffolding of exactly this shape in its own test tree and deliberately
 * does not publish it -- "a production build has no business being able to include it" -- and the
 * C API's fakes live in production code, behind `_ext` routes a consumer can call. So this is the
 * C API's own, kept in `src/` where nothing outside the library can reach it.
 */

#ifndef CNA_C_API_PLATFORM_OVERRIDE_HPP
#define CNA_C_API_PLATFORM_OVERRIDE_HPP

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "CNA/Platform/WindowDescription.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace CNA::C::Detail {

/**
 * @brief An IPlatform that forwards everything except the services a caller substitutes.
 *
 * Every accessor forwards to the platform this decorator wraps. The three services the C API can
 * fake -- dialogs, tray and camera -- return the substitute when one is set and the real service
 * otherwise, so installing a fake camera leaves the keyboard, the clipboard and the file system
 * exactly as they were.
 */
class PlatformOverride final : public CNA::Platform::IPlatform {
public:
    explicit PlatformOverride(CNA::Platform::IPlatform& inner) noexcept
        : inner_(&inner)
    {
    }

    /**
     * @brief Re-points the decorator at the platform currently in force.
     *
     * The wrapped platform is resolved when the override is installed, never once and for all: a
     * `Game` owns its platform and installs it on a stack, so the thing to forward to is whatever
     * is current at that moment. Holding the first one seen outlives it and dangles.
     */
    void SetInner(CNA::Platform::IPlatform& inner) noexcept { inner_ = &inner; }

    /** @brief Substitutes the dialogs service, or restores the real one when given null. */
    void SetDialogs(CNA::Platform::IPlatformDialogs* const dialogs) noexcept { dialogs_ = dialogs; }

    /** @brief Substitutes the tray service, or restores the real one when given null. */
    void SetTray(CNA::Platform::IPlatformTray* const tray) noexcept { tray_ = tray; }

    /** @brief Substitutes the camera provider, or restores the real one when given null. */
    void SetCamera(CNA::Platform::IPlatformCameraProvider* const camera) noexcept
    {
        camera_ = camera;
    }

    /**
     * @brief Restores the real provider if, and only if, @p camera is the substituted one.
     *
     * The substitution is a raw pointer into the resource that installed it, so whoever frees
     * that resource has to take the pointer back out. Doing it unconditionally would unhook a
     * different, still-live camera's provider when an older one is destroyed, which is why the
     * caller names the pointer it is retiring rather than just clearing. Answers whether it was
     * the substituted one, so a caller whose release then fails can put it back.
     */
    [[nodiscard]] bool ClearCameraIf(
        const CNA::Platform::IPlatformCameraProvider* const camera) noexcept
    {
        if (camera == nullptr || camera_ != camera) {
            return false;
        }
        camera_ = nullptr;
        return true;
    }

    /** @brief Whether any service is currently substituted. */
    [[nodiscard]] bool HasAnyOverride() const noexcept
    {
        return dialogs_ != nullptr || tray_ != nullptr || camera_ != nullptr;
    }

    [[nodiscard]] const std::string& GetName() const override { return inner_->GetName(); }

    [[nodiscard]] CNA::Platform::PlatformCapabilities GetCapabilities() const override
    {
        return inner_->GetCapabilities();
    }

    [[nodiscard]] std::uint64_t GetPerformanceCounter() const override
    {
        return inner_->GetPerformanceCounter();
    }

    [[nodiscard]] std::uint64_t GetPerformanceFrequency() const override
    {
        return inner_->GetPerformanceFrequency();
    }

    [[nodiscard]] std::uint64_t GetTicksMilliseconds() const override
    {
        return inner_->GetTicksMilliseconds();
    }

    void AcquireSubsystem(const CNA::Platform::PlatformSubsystem subsystem) override
    {
        inner_->AcquireSubsystem(subsystem);
    }

    void ReleaseSubsystem(const CNA::Platform::PlatformSubsystem subsystem) override
    {
        inner_->ReleaseSubsystem(subsystem);
    }

    [[nodiscard]] bool IsSubsystemInitialized(
        const CNA::Platform::PlatformSubsystem subsystem) const override
    {
        return inner_->IsSubsystemInitialized(subsystem);
    }

    [[nodiscard]] std::unique_ptr<CNA::Platform::IPlatformWindow> CreateWindow(
        const CNA::Platform::WindowDescription& description) override
    {
        return inner_->CreateWindow(description);
    }

    [[nodiscard]] std::unique_ptr<CNA::Platform::IPlatformSurfacePresenter> CreateSurfacePresenter(
        CNA::Platform::IPlatformWindow& window) override
    {
        return inner_->CreateSurfacePresenter(window);
    }

    void PollEvents(std::vector<CNA::Platform::PlatformEvent>& destination) override
    {
        inner_->PollEvents(destination);
    }

    void Delay(const std::uint32_t milliseconds) override { inner_->Delay(milliseconds); }

    [[nodiscard]] CNA::Platform::IPlatformGlContext* GetGlContext() override
    {
        return inner_->GetGlContext();
    }

    [[nodiscard]] CNA::Platform::IPlatformVulkanSurface* GetVulkanSurface() override
    {
        return inner_->GetVulkanSurface();
    }

    [[nodiscard]] CNA::Platform::IPlatformKeyboard* GetKeyboard() override
    {
        return inner_->GetKeyboard();
    }

    [[nodiscard]] CNA::Platform::IPlatformMouse* GetMouse() override { return inner_->GetMouse(); }

    [[nodiscard]] CNA::Platform::IPlatformGamepad* GetGamepad() override
    {
        return inner_->GetGamepad();
    }

    [[nodiscard]] CNA::Platform::IPlatformJoystick* GetJoystick() override
    {
        return inner_->GetJoystick();
    }

    [[nodiscard]] CNA::Platform::IPlatformTextInput* GetTextInput() override
    {
        return inner_->GetTextInput();
    }

    [[nodiscard]] CNA::Platform::IPlatformSensors* GetSensors() override
    {
        return inner_->GetSensors();
    }

    [[nodiscard]] CNA::Platform::IPlatformHaptics* GetHaptics() override
    {
        return inner_->GetHaptics();
    }

    [[nodiscard]] CNA::Platform::IPlatformInputDevices* GetInputDevices() override
    {
        return inner_->GetInputDevices();
    }

    [[nodiscard]] CNA::Platform::IPlatformClipboard* GetClipboard() override
    {
        return inner_->GetClipboard();
    }

    [[nodiscard]] CNA::Platform::IPlatformDisplays* GetDisplays() override
    {
        return inner_->GetDisplays();
    }

    [[nodiscard]] CNA::Platform::IPlatformDialogs* GetDialogs() override
    {
        return dialogs_ != nullptr ? dialogs_ : inner_->GetDialogs();
    }

    [[nodiscard]] CNA::Platform::IPlatformTray* GetTray() override
    {
        return tray_ != nullptr ? tray_ : inner_->GetTray();
    }

    [[nodiscard]] CNA::Platform::IPlatformCameraProvider* GetCamera() override
    {
        return camera_ != nullptr ? camera_ : inner_->GetCamera();
    }

    [[nodiscard]] CNA::Platform::IPlatformFileSystem* GetFileSystem() override
    {
        return inner_->GetFileSystem();
    }

    [[nodiscard]] CNA::Platform::IPlatformSystemInfo* GetSystemInfo() override
    {
        return inner_->GetSystemInfo();
    }

private:
    CNA::Platform::IPlatform* inner_;
    CNA::Platform::IPlatformDialogs* dialogs_ = nullptr;
    CNA::Platform::IPlatformTray* tray_ = nullptr;
    CNA::Platform::IPlatformCameraProvider* camera_ = nullptr;
};

/**
 * @brief Gets the process-wide override, installing it over the real platform on first use.
 *
 * The decorator is a function-local static so it outlives every C call that can reach it, and the
 * platform it wraps is resolved once: `SetCurrentPlatform` takes a borrowed pointer and the
 * documentation is explicit that ownership stays with the caller.
 *
 * @return The installed override.
 */
[[nodiscard]] PlatformOverride& GetPlatformOverride();

/**
 * @brief Removes the override when nothing is substituted any more.
 *
 * Called after every clear, so a program that installs a fake and then removes it leaves the
 * process pointing at the real platform rather than at a decorator that merely forwards.
 */
void ReleasePlatformOverrideIfUnused();

/**
 * @brief Drops every substitution and uninstalls the override.
 *
 * Called from the game-destroy path. A `Game` owns the platform this decorator forwards to, so an
 * override left installed past the game's death would forward into freed memory -- which is exactly
 * what the sanitized tree caught the first time this seam was built.
 */
void ResetPlatformOverride();

} // namespace CNA::C::Detail

#endif
