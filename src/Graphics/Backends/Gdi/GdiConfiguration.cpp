// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Backends/Gdi/GdiConfiguration.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace CNA::Internal::Backends::Gdi
{
    namespace
    {
        [[nodiscard]] std::string QuoteDiagnosticValue(std::string_view value)
        {
            constexpr std::size_t MaximumReportedLength = 64;
            const std::size_t reportedLength =
                value.size() < MaximumReportedLength ? value.size() : MaximumReportedLength;
            std::string quoted;
            quoted.reserve(reportedLength + 5);
            quoted.push_back('"');
            for (std::size_t index = 0; index < reportedLength; ++index)
            {
                const unsigned char character = static_cast<unsigned char>(value[index]);
                switch (character)
                {
                    case '\\': quoted += "\\\\"; break;
                    case '"': quoted += "\\\""; break;
                    case '\n': quoted += "\\n"; break;
                    case '\r': quoted += "\\r"; break;
                    case '\t': quoted += "\\t"; break;
                    default:
                        quoted.push_back(character >= 0x20 && character <= 0x7e
                                             ? static_cast<char>(character)
                                             : '?');
                        break;
                }
            }
            if (value.size() > MaximumReportedLength)
                quoted += "...";
            quoted.push_back('"');
            return quoted;
        }

        void AppendInvalidValue(std::string& details, const char* name, std::string_view value,
                                const char* expected)
        {
            if (!details.empty())
                details += "; ";
            details += name;
            details += '=';
            details += QuoteDiagnosticValue(value);
            details += " (expected ";
            details += expected;
            details += ')';
        }

        void ParseSwitch(const std::optional<std::string_view>& value,
                         const char* name, bool& destination,
                         std::string& invalidDetails)
        {
            if (!value.has_value())
                return;
            if (*value == "0")
            {
                destination = false;
                return;
            }
            if (*value == "1")
            {
                destination = true;
                return;
            }
            AppendInvalidValue(invalidDetails, name, *value, "\"0\" or \"1\"");
        }

        [[nodiscard]] std::optional<std::string> ReadEnvironmentValue(const char* name)
        {
            const char* value = std::getenv(name);
            if (value == nullptr)
                return std::nullopt;
            return std::string(value);
        }

        [[nodiscard]] std::optional<std::string_view> ViewOf(
            const std::optional<std::string>& value)
        {
            if (!value.has_value())
                return std::nullopt;
            return std::string_view(*value);
        }
    }

    GdiConfigurationParseResult ParseGdiConfiguration(const GdiConfigurationValues& values)
    {
        GdiConfigurationParseResult result;
        std::string invalidDetails;

        if (values.presentationFilter.has_value())
        {
            if (*values.presentationFilter == "nearest")
            {
                result.configuration.presentationFilter = GdiPresentationFilter::Nearest;
            }
            else if (*values.presentationFilter == "halftone")
            {
                result.configuration.presentationFilter = GdiPresentationFilter::Halftone;
            }
            else
            {
                AppendInvalidValue(invalidDetails, "CNA_GDI_PRESENT_FILTER",
                                   *values.presentationFilter,
                                   "\"nearest\" or \"halftone\"");
            }
        }

        ParseSwitch(values.dirtyPresentation, "CNA_GDI_DIRTY_PRESENTATION",
                    result.configuration.dirtyPresentation, invalidDetails);
        ParseSwitch(values.dwmFlush, "CNA_GDI_DWM_FLUSH",
                    result.configuration.dwmFlush, invalidDetails);

        if (!invalidDetails.empty())
        {
            result.diagnostic = "GDI configuration contains invalid values: ";
            result.diagnostic += invalidDetails;
            result.diagnostic += "; using safe defaults for those settings.";
        }
        return result;
    }

    GdiConfiguration CaptureGdiConfigurationFromEnvironment()
    {
        // Copy immediately so the parser never retains pointers into mutable process-global
        // environment storage. Each setting is queried exactly once for this backend instance.
        const std::optional<std::string> presentationFilter =
            ReadEnvironmentValue("CNA_GDI_PRESENT_FILTER");
        const std::optional<std::string> dirtyPresentation =
            ReadEnvironmentValue("CNA_GDI_DIRTY_PRESENTATION");
        const std::optional<std::string> dwmFlush =
            ReadEnvironmentValue("CNA_GDI_DWM_FLUSH");
        GdiConfigurationParseResult result = ParseGdiConfiguration({
            ViewOf(presentationFilter), ViewOf(dirtyPresentation), ViewOf(dwmFlush)});
        if (!result.diagnostic.empty())
            std::fprintf(stderr, "%s\n", result.diagnostic.c_str());
        return result.configuration;
    }
}
