// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/FileDialog.hpp"

#ifdef CNA_DEVICES

#include <mutex>

#include "CNA/Devices/Detail/PlatformFileDialogBackend.hpp"
#include "CNA/TargetPlatform.hpp"

namespace
{
    std::mutex& BackendMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    // Task REMED-DEVICES-001: shared_ptr, not unique_ptr -- GetBackend() below
    // returns a local copy (a new owning reference) while holding the lock, so a
    // concurrent SetBackendForTesting() reassigning this storage can never destroy
    // the object an in-flight call is still using. Holding BackendMutex() across
    // the actual backend call instead was rejected: it would serialize a
    // UI-blocking dialog call against every other FileDialog caller and invite
    // deadlock.
    std::shared_ptr<CNA::Devices::Detail::IFileDialogBackend>& BackendStorage()
    {
        static std::shared_ptr<CNA::Devices::Detail::IFileDialogBackend> backend =
            std::make_shared<CNA::Devices::Detail::PlatformFileDialogBackend>();
        return backend;
    }

    std::shared_ptr<CNA::Devices::Detail::IFileDialogBackend> GetBackend()
    {
        std::lock_guard<std::mutex> lock(BackendMutex());
        return BackendStorage();
    }
} // namespace

namespace CNA::Devices
{
    bool FileDialog::getIsSupportedProperty()
    {
        switch (CNA::getCurrentPlatform())
        {
        case CNA::TargetPlatform::Web:
        case CNA::TargetPlatform::iOS:
            return false;
        case CNA::TargetPlatform::Desktop:
        case CNA::TargetPlatform::Android:
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
            BackendStorage() = std::shared_ptr<Detail::IFileDialogBackend>(std::move(backend));
        }
        else
        {
            BackendStorage() = std::make_shared<Detail::PlatformFileDialogBackend>();
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
