// SPDX-License-Identifier: MS-PL
// plans/plan_directx12_parity.md DX12-0004: the DirectX 12 adapter and diagnostics policy.

#if defined(_WIN32) && defined(CNA_RENDERER_DIRECTX12)

#include "CNA/Internal/Renderers/DirectX12/D3D12Configuration.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace
{
    using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
    using CNA::Internal::Renderers::DirectX12::D3D12AdapterPreference;
    using CNA::Internal::Renderers::DirectX12::D3D12Configuration;
    using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
    using CNA::Internal::Renderers::DirectX12::ParseD3D12Configuration;

    /// Sets (or, with no value, removes) one CRT environment variable for a scope. The renderer
    /// reads its configuration with std::getenv, which is the CRT copy _putenv_s writes.
    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(const char* name, const char* value) : name_(name)
        {
            if (const char* previous = std::getenv(name))
                previous_ = std::string(previous);
            _putenv_s(name, value != nullptr ? value : "");
        }

        ~ScopedEnvironmentVariable()
        {
            _putenv_s(name_, previous_.has_value() ? previous_->c_str() : "");
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
        ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

    private:
        const char* name_;
        std::optional<std::string> previous_;
    };

    GraphicsRendererCreateArgs WindowlessArgs()
    {
        GraphicsRendererCreateArgs args{};
        args.virtualWidth = 16;
        args.virtualHeight = 16;
        return args;
    }
}

TEST(D3D12ConfigurationTest, AbsentValuesSelectTheProductionDefaults)
{
    const auto result = ParseD3D12Configuration({});
    EXPECT_EQ(result.configuration, D3D12Configuration{});
    EXPECT_EQ(result.configuration.adapter, D3D12AdapterPreference::Hardware);
    EXPECT_FALSE(result.configuration.debugLayer);
    EXPECT_FALSE(result.configuration.gpuBasedValidation);
    EXPECT_FALSE(result.configuration.dred);
    EXPECT_TRUE(result.diagnostic.empty());
}

TEST(D3D12ConfigurationTest, EveryValidValueIsHonoured)
{
    const auto warp = ParseD3D12Configuration({"warp", "1", "0", "1"});
    EXPECT_EQ(warp.configuration.adapter, D3D12AdapterPreference::Warp);
    EXPECT_TRUE(warp.configuration.debugLayer);
    EXPECT_FALSE(warp.configuration.gpuBasedValidation);
    EXPECT_TRUE(warp.configuration.dred);
    EXPECT_TRUE(warp.diagnostic.empty());

    const auto hardware = ParseD3D12Configuration({"hardware", "0", std::nullopt, "0"});
    EXPECT_EQ(hardware.configuration, D3D12Configuration{});
    EXPECT_TRUE(hardware.diagnostic.empty());
}

TEST(D3D12ConfigurationTest, GpuBasedValidationTurnsTheDebugLayerOn)
{
    const auto result = ParseD3D12Configuration({std::nullopt, "0", "1", std::nullopt});
    EXPECT_TRUE(result.configuration.gpuBasedValidation);
    EXPECT_TRUE(result.configuration.debugLayer);
}

TEST(D3D12ConfigurationTest, InvalidValuesKeepSafeDefaultsAndAreEachNamed)
{
    // A typo must not quietly mean "hardware": it is reported, by variable and value.
    const auto result = ParseD3D12Configuration({"wrap", "yes", "2", "true"});
    EXPECT_EQ(result.configuration, D3D12Configuration{});
    EXPECT_NE(result.diagnostic.find("CNA_D3D12_ADAPTER=\"wrap\""), std::string::npos);
    EXPECT_NE(result.diagnostic.find("CNA_D3D12_DEBUG_LAYER=\"yes\""), std::string::npos);
    EXPECT_NE(result.diagnostic.find("CNA_D3D12_GPU_VALIDATION=\"2\""), std::string::npos);
    EXPECT_NE(result.diagnostic.find("CNA_D3D12_DRED=\"true\""), std::string::npos);
}

TEST(D3D12ConfigurationTest, CaseMattersBecauseTheSpellingIsTheContract)
{
    const auto result = ParseD3D12Configuration({"WARP", std::nullopt, std::nullopt, std::nullopt});
    EXPECT_EQ(result.configuration.adapter, D3D12AdapterPreference::Hardware);
    EXPECT_FALSE(result.diagnostic.empty());
}

TEST(D3D12AdapterSelectionTest, ExplicitWarpCreatesTheDeviceOnTheSoftwareRasteriser)
{
    ScopedEnvironmentVariable adapter("CNA_D3D12_ADAPTER", "warp");
    std::unique_ptr<DirectX12Renderer> renderer;
    try
    {
        renderer = std::make_unique<DirectX12Renderer>(WindowlessArgs());
    }
    catch (const std::exception& error)
    {
        // WARP ships with every Windows 10 and later; a machine without it is an environment
        // answer, and the message must say it was the explicit WARP request that failed.
        const std::string message = error.what();
        ASSERT_NE(message.find("CNA_D3D12_ADAPTER=warp"), std::string::npos) << message;
        GTEST_SKIP() << "WARP is unavailable here: " << message;
    }

    const auto& info = renderer->GetAdapterInfoEXT();
    EXPECT_EQ(renderer->GetConfigurationEXT().adapter, D3D12AdapterPreference::Warp);
    EXPECT_EQ(info.selectedBy, D3D12AdapterPreference::Warp);
    EXPECT_TRUE(info.software) << info.description;
    // Microsoft's PCI vendor identifier. Asserted from the adapter, never matched by name.
    EXPECT_EQ(info.vendorId, 0x1414u) << info.description;
    EXPECT_FALSE(info.description.empty());
    EXPECT_GE(static_cast<int>(info.featureLevel), static_cast<int>(D3D_FEATURE_LEVEL_11_0));
    EXPECT_EQ(info.featureLevel, renderer->GetFeatureLevelEXT());
}

TEST(D3D12AdapterSelectionTest, TheDefaultNeverSelectsASoftwareAdapter)
{
    // Unset: the production path. On a machine with a D3D12 GPU this creates a hardware device; on
    // one without (the VirtualBox validation guest answers DXGI_ERROR_UNSUPPORTED) it must refuse
    // by name and point at the explicit switch -- never quietly land on WARP.
    ScopedEnvironmentVariable adapter("CNA_D3D12_ADAPTER", nullptr);
    try
    {
        DirectX12Renderer renderer(WindowlessArgs());
        const auto& info = renderer.GetAdapterInfoEXT();
        EXPECT_EQ(renderer.GetConfigurationEXT().adapter, D3D12AdapterPreference::Hardware);
        EXPECT_EQ(info.selectedBy, D3D12AdapterPreference::Hardware);
        EXPECT_FALSE(info.software) << info.description;
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("never used as a fallback"), std::string::npos) << message;
        EXPECT_NE(message.find("CNA_D3D12_ADAPTER=warp"), std::string::npos) << message;
    }
}

TEST(D3D12AdapterSelectionTest, AnInvalidAdapterValueKeepsTheHardwarePolicy)
{
    ScopedEnvironmentVariable adapter("CNA_D3D12_ADAPTER", "wrap");
    try
    {
        DirectX12Renderer renderer(WindowlessArgs());
        EXPECT_EQ(renderer.GetConfigurationEXT().adapter, D3D12AdapterPreference::Hardware);
        EXPECT_FALSE(renderer.GetAdapterInfoEXT().software);
    }
    catch (const std::exception& error)
    {
        EXPECT_NE(std::string(error.what()).find("never used as a fallback"), std::string::npos)
            << error.what();
    }
}

TEST(D3D12AdapterSelectionTest, TheDebugLayerIsOffUnlessRequested)
{
    ScopedEnvironmentVariable adapter("CNA_D3D12_ADAPTER", "warp");
    ScopedEnvironmentVariable debugLayer("CNA_D3D12_DEBUG_LAYER", nullptr);
    ScopedEnvironmentVariable gpuValidation("CNA_D3D12_GPU_VALIDATION", nullptr);
    std::unique_ptr<DirectX12Renderer> renderer;
    try
    {
        renderer = std::make_unique<DirectX12Renderer>(WindowlessArgs());
    }
    catch (const std::exception& error)
    {
        GTEST_SKIP() << "WARP is unavailable here: " << error.what();
    }
    EXPECT_FALSE(renderer->IsDebugLayerEnabledEXT());
    EXPECT_FALSE(renderer->IsGpuBasedValidationEnabledEXT());
    EXPECT_TRUE(renderer->DrainDebugMessagesEXT().empty());
}

#endif
