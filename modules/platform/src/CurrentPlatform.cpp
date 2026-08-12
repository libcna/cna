// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/CurrentPlatform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <mutex>

namespace CNA::Platform {

    namespace {

        std::mutex& StateMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        /// Borrowed: installed by SetCurrentPlatform and owned by whoever installed it.
        IPlatform*& Installed()
        {
            static IPlatform* platform = nullptr;
            return platform;
        }

        /// Owned: created lazily only when the static API is used with nothing installed.
        std::unique_ptr<IPlatform>& LazyDefault()
        {
            static std::unique_ptr<IPlatform> platform;
            return platform;
        }

    } // namespace

    IPlatform& GetCurrentPlatform()
    {
        const std::lock_guard<std::mutex> guard(StateMutex());

        if (Installed() != nullptr)
        {
            return *Installed();
        }
        if (!LazyDefault())
        {
            LazyDefault() = PlatformFactory::Create();
            if (!LazyDefault())
            {
                throw PlatformException("GetCurrentPlatform", "the default platform could not be created");
            }
        }
        return *LazyDefault();
    }

    void SetCurrentPlatform(IPlatform* platform)
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        Installed() = platform;
    }

    bool HasCurrentPlatform()
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        return Installed() != nullptr || LazyDefault() != nullptr;
    }

    void ResetCurrentPlatform()
    {
        const std::lock_guard<std::mutex> guard(StateMutex());
        Installed() = nullptr;
        LazyDefault().reset();
    }

} // namespace CNA::Platform
