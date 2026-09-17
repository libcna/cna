// SPDX-License-Identifier: MS-PL
#pragma once

// plans/plan_directx12_parity.md DX12-0004: the DirectX 12 renderer's adapter and diagnostics
// policy, captured once per renderer instance from the process environment.
//
// Nothing here is XNA API and nothing here is public CNA API. It exists so that a validation run can
// ask for the WARP software rasteriser and for the D3D12 debug layer explicitly, while an ordinary
// application keeps the production behaviour: a hardware adapter, no debug layer, and no silent
// software fallback.

#include <optional>
#include <string>
#include <string_view>

namespace CNA::Internal::Renderers::DirectX12
{
    /** @brief Which kind of DXGI adapter the renderer creates its Direct3D 12 device on. */
    enum class D3D12AdapterPreference
    {
        /** @brief A non-software DXGI adapter; the production default. Never falls back to WARP. */
        Hardware,
        /** @brief The WARP software rasteriser, selected by IDXGIFactory4::EnumWarpAdapter. */
        Warp
    };

    /** @brief Validated, immutable-for-a-device DirectX 12 adapter and diagnostics policy. */
    struct D3D12Configuration
    {
        /** @brief The adapter kind to create the device on. */
        D3D12AdapterPreference adapter = D3D12AdapterPreference::Hardware;
        /** @brief Enables ID3D12Debug, the DXGI debug factory and info-queue capture. */
        bool debugLayer = false;
        /** @brief Enables GPU-based validation; implies the debug layer. */
        bool gpuBasedValidation = false;
        /** @brief Enables DRED auto-breadcrumbs and page-fault reporting before device creation. */
        bool dred = false;

        /**
         * @brief Compares two configurations field by field.
         * @return true when every field is equal.
         */
        bool operator==(const D3D12Configuration&) const = default;
    };

    /** @brief Raw optional environment values accepted by the pure configuration parser. */
    struct D3D12ConfigurationValues
    {
        /** @brief CNA_D3D12_ADAPTER: "hardware" or "warp". */
        std::optional<std::string_view> adapter;
        /** @brief CNA_D3D12_DEBUG_LAYER: "0" or "1". */
        std::optional<std::string_view> debugLayer;
        /** @brief CNA_D3D12_GPU_VALIDATION: "0" or "1". */
        std::optional<std::string_view> gpuBasedValidation;
        /** @brief CNA_D3D12_DRED: "0" or "1". */
        std::optional<std::string_view> dred;
    };

    /** @brief A parsed configuration plus one aggregated diagnostic for every invalid value. */
    struct D3D12ConfigurationParseResult
    {
        /** @brief The effective configuration; invalid values keep their safe defaults. */
        D3D12Configuration configuration{};
        /** @brief Empty when every supplied value was valid; otherwise one single-line message. */
        std::string diagnostic;
    };

    /**
     * @brief Parses the DirectX 12 environment settings without consulting process-global state.
     *
     * Missing values select the production defaults. An invalid value keeps that setting's safe
     * default and is named in the diagnostic, so a typo such as `CNA_D3D12_ADAPTER=wrap` is visible
     * rather than silently meaning "hardware". GPU-based validation turns the debug layer on, because
     * it cannot run without it.
     *
     * @param values The raw values, each absent when its variable is unset.
     * @return The effective configuration and the diagnostic.
     */
    [[nodiscard]] D3D12ConfigurationParseResult ParseD3D12Configuration(
        const D3D12ConfigurationValues& values);

    /**
     * @brief Reads every DirectX 12 environment setting once and reports any diagnostic once.
     * @return The effective configuration.
     */
    [[nodiscard]] D3D12Configuration CaptureD3D12ConfigurationFromEnvironment();

    /**
     * @brief The stable spelling of an adapter preference, as CNA_D3D12_ADAPTER accepts it.
     * @param preference The preference to name.
     * @return "hardware" or "warp".
     */
    [[nodiscard]] const char* D3D12AdapterPreferenceName(D3D12AdapterPreference preference) noexcept;
}
