// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/FileDialog.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/TargetPlatform.hpp"

#include <atomic>
#include <memory>
#include <utility>

namespace
{
    std::vector<CNA::Platform::FileDialogFilter> ToPlatformFilters(
        const std::vector<CNA::Devices::FileDialogFilter>& filters)
    {
        std::vector<CNA::Platform::FileDialogFilter> converted;
        converted.reserve(filters.size());
        for (const CNA::Devices::FileDialogFilter& filter : filters)
        {
            converted.push_back(CNA::Platform::FileDialogFilter{filter.Name, filter.Pattern});
        }
        return converted;
    }

    // The dialog service, or null when this platform has no native file dialogs.
    CNA::Platform::IPlatformDialogs* Dialogs()
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        return platform.Supports(CNA::Platform::PlatformCapability::NativeFileDialog)
                   ? platform.GetDialogs()
                   : nullptr;
    }

    CNA::Devices::Detail::FileDialogResultCallback CompleteOnce(
        CNA::Devices::Detail::FileDialogResultCallback onResult)
    {
        if (!onResult) { return {}; }
        struct Completion
        {
            std::atomic<bool> completed{false};
            CNA::Devices::Detail::FileDialogResultCallback callback;
        };
        auto completion = std::make_shared<Completion>();
        completion->callback = std::move(onResult);
        return [completion](const std::vector<std::string>& paths)
        {
            // A provider callback can race rejection, or arrive after the rejection was handled.
            if (!completion->completed.exchange(true))
            {
                auto callback = std::move(completion->callback);
                callback(paths);
            }
        };
    }

    void ReportUnavailable(const CNA::Devices::Detail::FileDialogResultCallback& onResult)
    {
        if (onResult)
        {
            onResult({});
        }
    }
} // namespace

namespace CNA::Devices
{
    bool FileDialog::getIsSupportedProperty()
    {
        // The build target still decides this rather than the platform capability: a desktop
        // build reports supported before any dialog is shown, which is what callers gate their
        // UI on, and the capability answers the narrower question of whether this particular
        // platform implementation can show one.
        switch (CNA::getCurrentPlatform())
        {
        case CNA::TargetPlatform::Web:
        case CNA::TargetPlatform::iOS:
            return false;
        case CNA::TargetPlatform::Desktop:
        case CNA::TargetPlatform::Android:
        default:
            return Platform::GetCurrentPlatform().Supports(
                Platform::PlatformCapability::NativeFileDialog);
        }
    }

    void FileDialog::ShowOpenFile(
        ResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        Platform::IPlatformDialogs* dialogs = Dialogs();
        if (dialogs == nullptr)
        {
            ReportUnavailable(onResult);
            return;
        }

        onResult = CompleteOnce(std::move(onResult));
        try
        {
            dialogs->ShowOpenFileDialog(onResult, ToPlatformFilters(filters),
                                        defaultLocation, allowMultiple, nullptr);
        }
        catch (const Platform::PlatformException&)
        {
            ReportUnavailable(onResult);
        }
    }

    void FileDialog::ShowSaveFile(
        ResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation)
    {
        Platform::IPlatformDialogs* dialogs = Dialogs();
        if (dialogs == nullptr)
        {
            ReportUnavailable(onResult);
            return;
        }

        onResult = CompleteOnce(std::move(onResult));
        try
        {
            dialogs->ShowSaveFileDialog(onResult, ToPlatformFilters(filters),
                                        defaultLocation, nullptr);
        }
        catch (const Platform::PlatformException&)
        {
            ReportUnavailable(onResult);
        }
    }

    void FileDialog::ShowOpenFolder(
        ResultCallback onResult,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        Platform::IPlatformDialogs* dialogs = Dialogs();
        if (dialogs == nullptr)
        {
            ReportUnavailable(onResult);
            return;
        }

        onResult = CompleteOnce(std::move(onResult));
        try
        {
            dialogs->ShowOpenFolderDialog(onResult, defaultLocation, allowMultiple,
                                          nullptr);
        }
        catch (const Platform::PlatformException&)
        {
            ReportUnavailable(onResult);
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
