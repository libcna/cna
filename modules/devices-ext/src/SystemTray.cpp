// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/SystemTray.hpp"

#ifdef CNA_DEVICES

#include "CNA/Devices/Detail/SdlTrayBackend.hpp"
#include "CNA/TargetPlatform.hpp"

namespace CNA::Devices
{
    bool SystemTray::getIsSupportedProperty()
    {
        switch (CNA::getCurrentPlatform())
        {
        case CNA::TargetPlatform::Desktop:
            return true;
        case CNA::TargetPlatform::Android:
        case CNA::TargetPlatform::iOS:
        case CNA::TargetPlatform::Web:
        default:
            return false;
        }
    }

    SystemTray::SystemTray(const std::string& tooltip)
        : backend_(std::make_unique<Detail::SdlTrayBackend>())
    {
        backend_->Create(tooltip);
    }

    SystemTray::SystemTray(const std::string& tooltip, std::unique_ptr<Detail::ITrayBackend> backend)
        : backend_(std::move(backend))
    {
        if (backend_)
        {
            backend_->Create(tooltip);
        }
    }

    SystemTray::~SystemTray()
    {
        if (backend_)
        {
            backend_->Destroy();
        }
    }

    void SystemTray::setTooltipProperty(const std::string& tooltip)
    {
        if (backend_)
        {
            backend_->SetTooltip(tooltip);
        }
    }

    std::size_t SystemTray::AddEntry(
        const std::string& label,
        bool checkable,
        bool initiallyChecked,
        bool initiallyEnabled,
        Detail::TrayEntryClickCallback onClick)
    {
        if (backend_)
        {
            return backend_->AddEntry(label, checkable, initiallyChecked, initiallyEnabled, std::move(onClick));
        }
        return 0;
    }

    void SystemTray::SetEntryLabel(std::size_t index, const std::string& label)
    {
        if (backend_)
        {
            backend_->SetEntryLabel(index, label);
        }
    }

    void SystemTray::SetEntryChecked(std::size_t index, bool checked)
    {
        if (backend_)
        {
            backend_->SetEntryChecked(index, checked);
        }
    }

    bool SystemTray::GetEntryChecked(std::size_t index) const
    {
        return backend_ ? backend_->GetEntryChecked(index) : false;
    }

    void SystemTray::SetEntryEnabled(std::size_t index, bool enabled)
    {
        if (backend_)
        {
            backend_->SetEntryEnabled(index, enabled);
        }
    }

    bool SystemTray::GetEntryEnabled(std::size_t index) const
    {
        return backend_ ? backend_->GetEntryEnabled(index) : false;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
