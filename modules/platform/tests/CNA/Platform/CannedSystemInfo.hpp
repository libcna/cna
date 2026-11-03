// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformTestDecorator.hpp"

namespace CNA::Platform::Testing {

// Host facts and URL results are scripted so device wrappers never inspect the machine or
// launch an external application. All other platform operations use the real Headless service.
class CannedSystemInfoPlatform final : public PlatformTestDecorator, public IPlatformSystemInfo
{
public:
    CannedSystemInfoPlatform() : PlatformTestDecorator(PlatformFactory::Create("Headless")) {}

    int cores = 7;
    int memoryMegabytes = 12345;
    std::vector<PlatformLocale> locales{{"cs", "CZ"}, {"en", ""}};
    PowerInfo power;
    bool acceptsUrl = false;
    std::vector<std::string> urls;

    PlatformCapabilities GetCapabilities() const override
    {
        auto capabilities = PlatformTestDecorator::GetCapabilities();
        capabilities.powerInfo = true;
        return capabilities;
    }
    IPlatformSystemInfo* GetSystemInfo() override { return this; }
    std::string GetPlatformName() const override { return "Canned"; }
    int GetSystemMemoryMegabytes() const override { return memoryMegabytes; }
    int GetLogicalCoreCount() const override { return cores; }
    std::vector<PlatformLocale> GetPreferredLocales() const override { return locales; }
    PowerInfo GetPowerInfo() const override { return power; }
    bool OpenUrl(const std::string& url) override
    {
        urls.push_back(url);
        return acceptsUrl;
    }
};

} // namespace CNA::Platform::Testing
