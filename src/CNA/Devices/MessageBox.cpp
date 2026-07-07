// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/MessageBox.hpp"

#ifdef CNA_DEVICES

#include <mutex>

#include "CNA/Devices/Detail/SdlMessageBoxBackend.hpp"

namespace
{
    std::mutex& BackendMutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    std::unique_ptr<CNA::Devices::Detail::IMessageBoxBackend>& BackendStorage()
    {
        static std::unique_ptr<CNA::Devices::Detail::IMessageBoxBackend> backend =
            std::make_unique<CNA::Devices::Detail::SdlMessageBoxBackend>();
        return backend;
    }

    CNA::Devices::Detail::IMessageBoxBackend* GetBackend()
    {
        std::lock_guard<std::mutex> lock(BackendMutex());
        return BackendStorage().get();
    }
} // namespace

namespace CNA::Devices
{
    bool MessageBox::getIsSupportedProperty()
    {
        return true;
    }

    void MessageBox::ShowSimple(MessageBoxType type, const std::string& title, const std::string& message)
    {
        GetBackend()->ShowSimple(type, title, message);
    }

    int MessageBox::Show(
        MessageBoxType type,
        const std::string& title,
        const std::string& message,
        const std::vector<std::string>& buttonLabels)
    {
        return GetBackend()->Show(type, title, message, buttonLabels);
    }

    void MessageBox::SetBackendForTesting(std::unique_ptr<Detail::IMessageBoxBackend> backend)
    {
        std::lock_guard<std::mutex> lock(BackendMutex());
        if (backend)
        {
            BackendStorage() = std::move(backend);
        }
        else
        {
            BackendStorage() = std::make_unique<Detail::SdlMessageBoxBackend>();
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
