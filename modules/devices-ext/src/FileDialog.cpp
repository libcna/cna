// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/FileDialog.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/TargetPlatform.hpp"

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

    /// The dialog service, or null when this platform has no native file dialogs.
    CNA::Platform::IPlatformDialogs* Dialogs()
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        return platform.GetCapabilities().nativeFileDialog ? platform.GetDialogs() : nullptr;
    }

    /// Delivers "the user chose nothing" to a caller waiting on a dialog that will never appear.
    ///
    /// A caller has already registered a continuation by the time it learns the dialog is
    /// unavailable, so silently dropping the callback leaves that continuation pending forever --
    /// a hang rather than a refusal. An empty result is what a cancel produces, which every
    /// caller of this API already handles.
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
            return Platform::GetCurrentPlatform().GetCapabilities().nativeFileDialog;
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

        try
        {
            dialogs->ShowOpenFileDialog(std::move(onResult), ToPlatformFilters(filters),
                                        defaultLocation, allowMultiple, nullptr);
        }
        catch (const Platform::PlatformException&)
        {
            // The callback was moved from and cannot be reported to here. A throw at this point
            // means the request was rejected rather than shown, which the caller sees as a dialog
            // that never opened.
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

        try
        {
            dialogs->ShowSaveFileDialog(std::move(onResult), ToPlatformFilters(filters),
                                        defaultLocation, nullptr);
        }
        catch (const Platform::PlatformException&)
        {
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

        try
        {
            dialogs->ShowOpenFolderDialog(std::move(onResult), defaultLocation, allowMultiple,
                                          nullptr);
        }
        catch (const Platform::PlatformException&)
        {
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
