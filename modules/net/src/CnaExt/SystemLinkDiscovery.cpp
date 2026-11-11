// SPDX-License-Identifier: MS-PL
// Copyright (c) Robert Vokac and contributors
#include "CNA/Net/SystemLinkDiscovery.hpp"
#include "CNA/Internal/Net/SearchAddress.hpp"

#include "System/ArgumentException.hpp"

#include <atomic>
#include <mutex>

namespace CNA::Net
{
    namespace
    {
        std::mutex& SettingsMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        std::vector<std::string>& ExtraSearchAddresses()
        {
            static std::vector<std::string> addresses;
            return addresses;
        }

        std::atomic<std::uint16_t>& HostPort()
        {
            static std::atomic<std::uint16_t> port{0};
            return port;
        }
    }

    void SystemLinkDiscovery::setExtraSearchAddressesProperty(const std::vector<std::string>& addresses)
    {
        for (const auto& entry : addresses)
        {
            if (!CNA::Internal::Net::ParseSearchAddress(entry, DiscoveryPort))
                throw System::ArgumentException(
                    "A SystemLink search address is a host name or IPv4 address, optionally followed by :port "
                    "(1-65535): \"" + entry + "\"",
                    "addresses");
        }
        const std::lock_guard lock(SettingsMutex());
        ExtraSearchAddresses() = addresses;
    }

    std::vector<std::string> SystemLinkDiscovery::getExtraSearchAddressesProperty()
    {
        const std::lock_guard lock(SettingsMutex());
        return ExtraSearchAddresses();
    }

    void SystemLinkDiscovery::setHostPortProperty(std::uint16_t port)
    {
        HostPort().store(port);
    }

    std::uint16_t SystemLinkDiscovery::getHostPortProperty()
    {
        return HostPort().load();
    }
}
