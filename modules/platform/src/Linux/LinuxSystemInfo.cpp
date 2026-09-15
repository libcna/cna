// SPDX-License-Identifier: MS-PL

#include "LinuxSystemInfo.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>

#include <unistd.h>

namespace CNA::Platform::Linux {

    namespace {

        /// One attribute of a power supply, its trailing newline removed; nothing when absent.
        std::optional<std::string> Attribute(const std::filesystem::path& supply, const char* name)
        {
            std::ifstream file(supply / name);
            if (!file)
            {
                return std::nullopt;
            }
            std::string value;
            std::getline(file, value);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0)
            {
                value.pop_back();
            }
            return value;
        }

        std::optional<long long> Number(const std::filesystem::path& supply, const char* name)
        {
            const std::optional<std::string> text = Attribute(supply, name);
            if (!text || text->empty())
            {
                return std::nullopt;
            }
            char* end = nullptr;
            const long long value = std::strtoll(text->c_str(), &end, 10);
            if (end == text->c_str() || *end != '\0')
            {
                return std::nullopt;
            }
            return value;
        }

        bool EqualsIgnoringCase(const std::string_view left, const std::string_view right)
        {
            return left.size() == right.size() &&
                   std::equal(left.begin(), left.end(), right.begin(), [](const char a, const char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                   });
        }

    } // namespace

    PowerInfo ReadPowerSupplies(const std::string& root)
    {
        std::error_code error;
        std::filesystem::directory_iterator entries(root, error);
        if (error)
        {
            return PowerInfo{};
        }
        // Plugged in with no battery, unless a battery says otherwise.
        PowerInfo best;
        best.state = PowerState::NoBattery;
        for (const std::filesystem::directory_entry& entry : entries)
        {
            const std::filesystem::path supply = entry.path();
            const std::optional<std::string> type = Attribute(supply, "type");
            if (!type || !EqualsIgnoringCase(*type, "Battery"))
            {
                continue;  // Mains, USB, a UPS: not what powers the machine from inside.
            }
            if (const std::optional<std::string> scope = Attribute(supply, "scope");
                scope && EqualsIgnoringCase(*scope, "Device"))
            {
                continue;  // A gamepad's or a mouse's own battery.
            }

            PowerState state = PowerState::Unknown;
            if (const std::optional<long long> present = Number(supply, "present"); present && *present == 0)
            {
                state = PowerState::NoBattery;
            }
            else if (const std::optional<std::string> status = Attribute(supply, "status"))
            {
                if (EqualsIgnoringCase(*status, "Charging")) { state = PowerState::Charging; }
                else if (EqualsIgnoringCase(*status, "Discharging")) { state = PowerState::OnBattery; }
                else if (EqualsIgnoringCase(*status, "Full") || EqualsIgnoringCase(*status, "Not charging"))
                {
                    state = PowerState::Charged;
                }
            }

            int percent = -1;
            if (const std::optional<long long> capacity = Number(supply, "capacity"))
            {
                percent = static_cast<int>(std::clamp<long long>(*capacity, 0, 100));
            }

            int seconds = -1;
            if (const std::optional<long long> left = Number(supply, "time_to_empty_now"))
            {
                seconds = *left > 0 ? static_cast<int>(*left) : -1;
            }
            else if (state == PowerState::OnBattery)
            {
                // Energy in µWh over power in µW, or charge in µAh over current in µA: hours.
                const std::optional<long long> energy = Number(supply, "energy_now");
                const std::optional<long long> power = Number(supply, "power_now");
                const std::optional<long long> charge = Number(supply, "charge_now");
                const std::optional<long long> current = Number(supply, "current_now");
                if (energy && power && *energy >= 0 && *power > 0)
                {
                    seconds = static_cast<int>(3600LL * *energy / *power);
                }
                else if (charge && current && *charge >= 0 && *current > 0)
                {
                    seconds = static_cast<int>(3600LL * *charge / *current);
                }
            }

            // The battery with the most time left; failing any time, the fullest.
            bool choose = false;
            if (seconds < 0 && best.secondsRemaining < 0)
            {
                choose = (percent < 0 && best.percent < 0) || percent > best.percent;
            }
            else if (seconds > best.secondsRemaining)
            {
                choose = true;
            }
            if (choose)
            {
                best.state = state;
                best.percent = percent;
                best.secondsRemaining = seconds;
            }
        }
        return best;
    }

    std::vector<PlatformLocale> ParsePreferredLocales(const char* lang, const char* language)
    {
        std::vector<std::string> names;
        if (lang != nullptr && *lang != '\0')
        {
            names.emplace_back(lang);
        }
        if (language != nullptr)
        {
            const std::string_view list(language);
            std::size_t start = 0;
            while (start <= list.size())
            {
                std::size_t end = list.find(':', start);
                if (end == std::string_view::npos) { end = list.size(); }
                names.emplace_back(list.substr(start, end - start));
                start = end + 1;
            }
        }
        std::vector<PlatformLocale> locales;
        for (std::string name : names)
        {
            name = name.substr(0, name.find('.'));
            name = name.substr(0, name.find('@'));
            if (name.empty() || name == "C" || name == "POSIX")
            {
                continue;
            }
            PlatformLocale locale;
            const std::size_t separator = name.find_first_of("_-");
            locale.language = name.substr(0, separator);
            if (separator != std::string::npos)
            {
                locale.country = name.substr(separator + 1);
            }
            if (locale.language.empty())
            {
                continue;
            }
            const bool seen = std::any_of(locales.begin(), locales.end(), [&locale](const PlatformLocale& other) {
                return other.language == locale.language && other.country == locale.country;
            });
            if (!seen)
            {
                locales.push_back(std::move(locale));
            }
        }
        return locales;
    }

    std::string LinuxSystemInfo::GetPlatformName() const { return "Linux"; }

    int LinuxSystemInfo::GetSystemMemoryMegabytes() const
    {
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long pageSize = sysconf(_SC_PAGESIZE);
        if (pages <= 0 || pageSize <= 0)
        {
            return 0;
        }
        return static_cast<int>(static_cast<long long>(pages) * pageSize / (1024LL * 1024LL));
    }

    int LinuxSystemInfo::GetLogicalCoreCount() const
    {
        const long online = sysconf(_SC_NPROCESSORS_ONLN);
        return online > 0 ? static_cast<int>(online) : 0;
    }

    std::vector<PlatformLocale> LinuxSystemInfo::GetPreferredLocales() const
    {
        return ParsePreferredLocales(std::getenv("LANG"), std::getenv("LANGUAGE"));
    }

    PowerInfo LinuxSystemInfo::GetPowerInfo() const
    {
        return ReadPowerSupplies();
    }

    LinuxSystemInfo::LinuxSystemInfo(UrlOpener openUrl) : openUrl_(std::move(openUrl)) {}

    bool LinuxSystemInfo::OpenUrl(const std::string& url)
    {
        return openUrl_ && openUrl_(url);
    }

} // namespace CNA::Platform::Linux
