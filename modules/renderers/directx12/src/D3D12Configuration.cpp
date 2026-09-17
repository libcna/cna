// SPDX-License-Identifier: MS-PL
// plans/plan_directx12_parity.md DX12-0004.

#include "CNA/Internal/Renderers/DirectX12/D3D12Configuration.hpp"

#include "CNA/Logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::DirectX12
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

        void ParseSwitch(const std::optional<std::string_view>& value, const char* name,
                         bool& destination, std::string& invalidDetails)
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

        [[nodiscard]] std::optional<std::string_view> ViewOf(const std::optional<std::string>& value)
        {
            if (!value.has_value())
                return std::nullopt;
            return std::string_view(*value);
        }
    }

    D3D12ConfigurationParseResult ParseD3D12Configuration(const D3D12ConfigurationValues& values)
    {
        D3D12ConfigurationParseResult result;
        std::string invalidDetails;

        if (values.adapter.has_value())
        {
            if (*values.adapter == "hardware")
                result.configuration.adapter = D3D12AdapterPreference::Hardware;
            else if (*values.adapter == "warp")
                result.configuration.adapter = D3D12AdapterPreference::Warp;
            else
                AppendInvalidValue(invalidDetails, "CNA_D3D12_ADAPTER", *values.adapter,
                                   "\"hardware\" or \"warp\"");
        }

        ParseSwitch(values.debugLayer, "CNA_D3D12_DEBUG_LAYER",
                    result.configuration.debugLayer, invalidDetails);
        ParseSwitch(values.gpuBasedValidation, "CNA_D3D12_GPU_VALIDATION",
                    result.configuration.gpuBasedValidation, invalidDetails);
        ParseSwitch(values.dred, "CNA_D3D12_DRED", result.configuration.dred, invalidDetails);

        // GPU-based validation is a mode of the debug layer, not a separate layer.
        if (result.configuration.gpuBasedValidation)
            result.configuration.debugLayer = true;

        if (!invalidDetails.empty())
        {
            result.diagnostic = "DirectX 12 configuration contains invalid values: ";
            result.diagnostic += invalidDetails;
            result.diagnostic += "; using safe defaults for those settings.";
        }
        return result;
    }

    D3D12Configuration CaptureD3D12ConfigurationFromEnvironment()
    {
        // Copy immediately so the parser never keeps pointers into mutable process-global storage.
        const std::optional<std::string> adapter = ReadEnvironmentValue("CNA_D3D12_ADAPTER");
        const std::optional<std::string> debugLayer = ReadEnvironmentValue("CNA_D3D12_DEBUG_LAYER");
        const std::optional<std::string> gpuBasedValidation =
            ReadEnvironmentValue("CNA_D3D12_GPU_VALIDATION");
        const std::optional<std::string> dred = ReadEnvironmentValue("CNA_D3D12_DRED");
        D3D12ConfigurationParseResult result = ParseD3D12Configuration({
            ViewOf(adapter), ViewOf(debugLayer), ViewOf(gpuBasedValidation), ViewOf(dred)});
        if (!result.diagnostic.empty())
            CNA::Logger::Warn(result.diagnostic, CNA::LogCategory::RENDER);
        return result.configuration;
    }

    const char* D3D12AdapterPreferenceName(D3D12AdapterPreference preference) noexcept
    {
        return preference == D3D12AdapterPreference::Warp ? "warp" : "hardware";
    }
}
