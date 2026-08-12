// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/MessageBox.hpp"

#ifdef CNA_DEVICES

#include <mutex>

#include "CNA/Devices/Detail/PlatformMessageBoxBackend.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"

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
    // UI-blocking modal-dialog call against every other MessageBox caller and
    // invite deadlock.
    std::shared_ptr<CNA::Devices::Detail::IMessageBoxBackend>& BackendStorage()
    {
        static std::shared_ptr<CNA::Devices::Detail::IMessageBoxBackend> backend =
            std::make_shared<CNA::Devices::Detail::PlatformMessageBoxBackend>();
        return backend;
    }

    std::shared_ptr<CNA::Devices::Detail::IMessageBoxBackend> GetBackend()
    {
        std::lock_guard<std::mutex> lock(BackendMutex());
        return BackendStorage();
    }
} // namespace

namespace CNA::Devices
{
    bool MessageBox::getIsSupportedProperty()
    {
        // Previously an unconditional true, which was only ever right because SDL was the only
        // platform. It now reports the capability, so a headless or terminal host answers
        // honestly and a caller can offer a fallback instead of showing nothing.
        return Platform::GetCurrentPlatform().GetCapabilities().messageBox;
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
            BackendStorage() = std::shared_ptr<Detail::IMessageBoxBackend>(std::move(backend));
        }
        else
        {
            BackendStorage() = std::make_shared<Detail::PlatformMessageBoxBackend>();
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
