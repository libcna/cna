// SPDX-License-Identifier: MS-PL
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace CNA::Internal::Backends::Gdi
{
    /** @brief Final-window filter selected once for a GDI backend instance. */
    enum class GdiPresentationFilter
    {
        Nearest,
        Halftone
    };

    /** @brief Validated, immutable-at-runtime GDI presentation policy. */
    struct GdiConfiguration
    {
        GdiPresentationFilter presentationFilter = GdiPresentationFilter::Nearest;
        bool dirtyPresentation = false;
        bool dwmFlush = false;

        bool operator==(const GdiConfiguration&) const = default;
    };

    /** @brief Raw optional values accepted by the pure configuration parser. */
    struct GdiConfigurationValues
    {
        std::optional<std::string_view> presentationFilter;
        std::optional<std::string_view> dirtyPresentation;
        std::optional<std::string_view> dwmFlush;
    };

    /** @brief Parsed policy plus one aggregated diagnostic for all invalid values. */
    struct GdiConfigurationParseResult
    {
        GdiConfiguration configuration{};
        std::string diagnostic;
    };

    /**
     * Parses the three GDI environment settings without consulting process-global state.
     * Missing values select defaults. Invalid values retain the corresponding safe default and
     * are reported together in one single-line diagnostic.
     */
    [[nodiscard]] GdiConfigurationParseResult ParseGdiConfiguration(
        const GdiConfigurationValues& values);

    /** Reads every GDI environment setting once, reports any diagnostic once, and returns it. */
    [[nodiscard]] GdiConfiguration CaptureGdiConfigurationFromEnvironment();
}
