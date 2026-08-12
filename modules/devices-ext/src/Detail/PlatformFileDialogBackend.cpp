// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Detail/PlatformFileDialogBackend.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <utility>
#include <vector>

namespace
{
    using CNA::Devices::FileDialogFilter;
    using CNA::Devices::Detail::FileDialogResultCallback;

    std::vector<CNA::Platform::FileDialogFilter> ToPlatformFilters(
        const std::vector<FileDialogFilter>& filters)
    {
        std::vector<CNA::Platform::FileDialogFilter> converted;
        converted.reserve(filters.size());
        for (const FileDialogFilter& filter : filters)
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
    /// unavailable, and silently dropping the callback would leave that continuation pending
    /// forever -- a hang rather than a refusal. An empty result is the same thing a cancel
    /// produces, which every caller of this API already handles.
    void ReportUnavailable(const FileDialogResultCallback& onResult)
    {
        if (onResult)
        {
            onResult({});
        }
    }
} // namespace

namespace CNA::Devices::Detail
{
    void PlatformFileDialogBackend::ShowOpenFile(
        FileDialogResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        CNA::Platform::IPlatformDialogs* dialogs = Dialogs();
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
        catch (const CNA::Platform::PlatformException&)
        {
            // The callback was moved from, so it cannot be reported to here. A throw at this
            // point means the request was rejected outright rather than shown, which the caller
            // sees as a dialog that never opened.
        }
    }

    void PlatformFileDialogBackend::ShowSaveFile(
        FileDialogResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation)
    {
        CNA::Platform::IPlatformDialogs* dialogs = Dialogs();
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
        catch (const CNA::Platform::PlatformException&)
        {
        }
    }

    void PlatformFileDialogBackend::ShowOpenFolder(
        FileDialogResultCallback onResult,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        CNA::Platform::IPlatformDialogs* dialogs = Dialogs();
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
        catch (const CNA::Platform::PlatformException&)
        {
        }
    }
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
