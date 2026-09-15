// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include <functional>
#include <string>
#include <vector>

// Host facts a Linux process can read without a window system: memory and processors from the C
// library, the preferred languages from the POSIX locale variables, and battery state from the
// kernel's power-supply class in sysfs (plans/plan_x11.md X11-0163). Like the controllers, none of
// it touches X, so a future Wayland backend takes it as it is.

namespace CNA::Platform::Linux {

    /**
     * @brief Reads battery and power-source state from the kernel's power supplies.
     *
     * Only system batteries count -- a supply whose `scope` is `Device` is a gamepad's or a
     * mouse's. With several, the one reporting the most time left wins, else the fullest. With
     * none, the machine is plugged in with no battery. A battery that is not charging while
     * plugged in (a charge threshold) counts as charged. Time left is `time_to_empty_now`, else
     * energy over power, else charge over current.
     *
     * @param root The power-supply class directory; overridable for tests.
     * @return The state; `Unknown` when the directory cannot be read.
     */
    [[nodiscard]] PowerInfo ReadPowerSupplies(const std::string& root = "/sys/class/power_supply");

    /**
     * @brief Parses the POSIX locale variables into the user's preferred locales.
     *
     * `LANG` first, then each of `LANGUAGE`'s colon-separated entries; codesets (`.UTF-8`) and
     * modifiers (`@euro`) dropped, the `C` and `POSIX` locales skipped, duplicates kept once.
     *
     * @param lang `LANG`, or null.
     * @param language `LANGUAGE`, or null.
     * @return The locales, most preferred first.
     */
    [[nodiscard]] std::vector<PlatformLocale> ParsePreferredLocales(const char* lang, const char* language);

    /** @brief IPlatformSystemInfo from the Linux kernel and C library. */
    class LinuxSystemInfo final : public IPlatformSystemInfo
    {
    public:
        /** @brief Opens a URL on the platform's behalf: the desktop portal, where there is one. */
        using UrlOpener = std::function<bool(const std::string&)>;

        /**
         * @brief Answers from Linux, and opens URLs through @p openUrl.
         * @param openUrl What opens a URL; none refuses every one.
         */
        explicit LinuxSystemInfo(UrlOpener openUrl = {});

        /** @brief Gets the platform's name. @return `"Linux"`. */
        [[nodiscard]] std::string GetPlatformName() const override;
        /** @brief Gets physical memory. @return Megabytes, from `sysconf`; 0 when unknown. */
        [[nodiscard]] int GetSystemMemoryMegabytes() const override;
        /** @brief Gets online processors. @return The count, from `sysconf`; 0 when unknown. */
        [[nodiscard]] int GetLogicalCoreCount() const override;
        /** @brief Gets the preferred locales. @return From `LANG` and `LANGUAGE`. */
        [[nodiscard]] std::vector<PlatformLocale> GetPreferredLocales() const override;
        /** @brief Gets power state. @return From sysfs; see ReadPowerSupplies(). */
        [[nodiscard]] PowerInfo GetPowerInfo() const override;
        /**
         * @brief Opens a URL through the opener the platform gave: the desktop portal
         * (plans/plan_x11.md X11-0169).
         *
         * Never by starting another program (`xdg-open`), which this platform does not do on a
         * game's behalf -- the rule its message boxes follow too.
         *
         * @param url The URL.
         * @return True when it was accepted; false without an opener.
         */
        bool OpenUrl(const std::string& url) override;

    private:
        UrlOpener openUrl_;
    };

} // namespace CNA::Platform::Linux
