// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/SystemTray.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <utility>

namespace CNA::Devices
{
    bool SystemTray::getIsSupportedProperty()
    {
        const CNA::Platform::PlatformCapabilities capabilities =
            CNA::Platform::GetCurrentPlatform().GetCapabilities();
        return capabilities.tray;
    }

    SystemTray::SystemTray(const std::string& tooltip)
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        CNA::Platform::IPlatformTray* service = platform.GetTray();
        if (service == nullptr)
        {
            throw CNA::Platform::PlatformNotSupportedException(
                CNA::Platform::PlatformCapability::Tray, platform.GetName());
        }

        tray_ = service->CreateTray(tooltip);
        if (tray_ == nullptr)
        {
            throw CNA::Platform::PlatformException(
                "SystemTray", "the platform tray service returned no icon");
        }
    }

    SystemTray::~SystemTray() = default;

    void SystemTray::setTooltipProperty(const std::string& tooltip)
    {
        tray_->SetTooltip(tooltip);
    }

    std::size_t SystemTray::AddEntry(
        const std::string& label,
        bool checkable,
        bool initiallyChecked,
        bool initiallyEnabled,
        TrayEntryClickCallback onClick)
    {
        return tray_->AddEntry(
            label, checkable, initiallyChecked, initiallyEnabled, std::move(onClick));
    }

    void SystemTray::SetEntryLabel(std::size_t index, const std::string& label)
    {
        tray_->SetEntryLabel(index, label);
    }

    void SystemTray::SetEntryChecked(std::size_t index, bool checked)
    {
        tray_->SetEntryChecked(index, checked);
    }

    bool SystemTray::GetEntryChecked(std::size_t index) const
    {
        return tray_->GetEntryChecked(index);
    }

    void SystemTray::SetEntryEnabled(std::size_t index, bool enabled)
    {
        tray_->SetEntryEnabled(index, enabled);
    }

    bool SystemTray::GetEntryEnabled(std::size_t index) const
    {
        return tray_->GetEntryEnabled(index);
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
