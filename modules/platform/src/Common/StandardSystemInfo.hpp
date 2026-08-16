// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"

#include <string>
#include <vector>

namespace CNA::Platform::Common {

    /**
     * @brief Host facts the C++ standard library can answer, and nothing else.
     *
     * Shared by every implementation with no platform library to ask. What the standard library
     * cannot determine is reported as *unknown* rather than guessed: a fabricated core count or
     * an invented battery percentage is worse than an honest zero, because a caller may display
     * it.
     *
     * Deliberately not in the public include tree: an implementation detail shared between
     * implementations, not part of the contract.
     */
    class StandardSystemInfo final : public IPlatformSystemInfo
    {
    public:
        /** @brief Gets the operating system's name. @return `"Linux"`, `"Windows"`, … or `"Unknown"`. */
        [[nodiscard]] std::string GetPlatformName() const override;

        /** @brief Gets installed memory. @return Zero; the standard library cannot tell. */
        [[nodiscard]] int GetSystemMemoryMegabytes() const override;

        /** @brief Gets the logical processor count. @return The hardware concurrency, or zero if unknown. */
        [[nodiscard]] int GetLogicalCoreCount() const override;

        /** @brief Gets the user's preferred locales. @return Empty; there is no portable source for these. */
        [[nodiscard]] std::vector<PlatformLocale> GetPreferredLocales() const override;

        /** @brief Gets battery and power-source state. @return An all-unknown value, never a fabricated one. */
        [[nodiscard]] PowerInfo GetPowerInfo() const override;

        /**
         * @brief Refuses to open a URL.
         *
         * @param url The URL that would be opened.
         * @return False always; there is no portable browser to hand it to.
         */
        bool OpenUrl(const std::string& url) override;
    };

} // namespace CNA::Platform::Common
