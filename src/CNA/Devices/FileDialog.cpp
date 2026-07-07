// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/FileDialog.hpp"

#ifdef CNA_DEVICES

#include <mutex>

#include "CNA/Devices/Detail/SdlFileDialogBackend.hpp"
#include "CNA/Platform.hpp"

namespace
{
    std::mutex& BackendMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unique_ptr<CNA::Devices::Detail::IFileDialogBackend>& BackendStorage()
    {
        static std::unique_ptr<CNA::Devices::Detail::IFileDialogBackend> backend =
            std::make_unique<CNA::Devices::Detail::SdlFileDialogBackend>();
        return backend;
    }

    CNA::Devices::Detail::IFileDialogBackend* GetBackend()
    {
        std::lock_guard<std::mutex> lock(BackendMutex());
        return BackendStorage().get();
    }
} // namespace

namespace CNA::Devices
{
    bool FileDialog::getIsSupportedProperty()
    {
        switch (CNA::getCurrentPlatform())
        {
        case CNA::Platform::Web:
        case CNA::Platform::iOS:
            return false;
        case CNA::Platform::Desktop:
        case CNA::Platform::Android:
        default:
            return true;
        }
    }

    void FileDialog::ShowOpenFile(
        ResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        GetBackend()->ShowOpenFile(std::move(onResult), filters, defaultLocation, allowMultiple);
    }

    void FileDialog::ShowSaveFile(
        ResultCallback onResult,
        const std::vector<FileDialogFilter>& filters,
        const std::string& defaultLocation)
    {
        GetBackend()->ShowSaveFile(std::move(onResult), filters, defaultLocation);
    }

    void FileDialog::ShowOpenFolder(
        ResultCallback onResult,
        const std::string& defaultLocation,
        bool allowMultiple)
    {
        GetBackend()->ShowOpenFolder(std::move(onResult), defaultLocation, allowMultiple);
    }

    void FileDialog::SetBackendForTesting(std::unique_ptr<Detail::IFileDialogBackend> backend)
    {
        std::lock_guard<std::mutex> lock(BackendMutex());
        if (backend)
        {
            BackendStorage() = std::move(backend);
        }
        else
        {
            BackendStorage() = std::make_unique<Detail::SdlFileDialogBackend>();
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
