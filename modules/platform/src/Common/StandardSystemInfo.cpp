// SPDX-License-Identifier: MS-PL

#include "StandardSystemInfo.hpp"

#include <thread>

namespace CNA::Platform::Common {

    std::string StandardSystemInfo::GetPlatformName() const
    {
#if defined(_WIN32)
        return "Windows";
#elif defined(__APPLE__)
        return "macOS";
#elif defined(__ANDROID__)
        return "Android";
#elif defined(__linux__)
        return "Linux";
#else
        return "Unknown";
#endif
    }

    int StandardSystemInfo::GetSystemMemoryMegabytes() const { return 0; }

    int StandardSystemInfo::GetLogicalCoreCount() const
    {
        return static_cast<int>(std::thread::hardware_concurrency());
    }

    std::vector<PlatformLocale> StandardSystemInfo::GetPreferredLocales() const { return {}; }

    PowerInfo StandardSystemInfo::GetPowerInfo() const
    {
        // Unknown, not "no battery": this cannot tell, and claiming otherwise would be a
        // fabricated answer a caller could display.
        return PowerInfo{};
    }

    bool StandardSystemInfo::OpenUrl(const std::string&) { return false; }

} // namespace CNA::Platform::Common
