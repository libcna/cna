// SPDX-License-Identifier: MS-PL

#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/TestSupport/CompiledEffectConformance.hpp"
#include "CNA/TestSupport/TestPaths.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
    using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using Microsoft::Xna::Framework::Graphics::PresentationParameters;

    struct HiDefGraphicsDevice final : GraphicsDevice
    {
        HiDefGraphicsDevice()
            : GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(),
                             GraphicsProfile::HiDef, PresentationParameters()) {}
    };

    std::vector<std::uint8_t> LoadEffect(const char* name)
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    DirectX12Renderer& RendererOf(GraphicsDevice& device)
    {
        auto* renderer = dynamic_cast<DirectX12Renderer*>(&device.GetRenderer());
        if (renderer == nullptr)
            throw std::runtime_error("test configuration did not select DirectX 12");
        return *renderer;
    }
}

TEST(DirectX12CompiledEffectTest, CapabilityAndPublicRuntimeContract)
{
    HiDefGraphicsDevice device;
    (void) RendererOf(device);
    ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(DirectX12CompiledEffectTest, CompilerProducedStockEffectsParse)
{
    HiDefGraphicsDevice device;
    DirectX12Renderer& renderer = RendererOf(device);
    for (const char* name : {"SpriteEffect.fxb", "BasicEffect.fxb", "AlphaTestEffect.fxb",
                             "DualTextureEffect.fxb", "EnvironmentMapEffect.fxb",
                             "SkinnedEffect.fxb", "CnaConformanceEffect.fxb"})
    {
        SCOPED_TRACE(name);
        const auto bytes = LoadEffect(name);
        ASSERT_FALSE(bytes.empty());
        auto runtime = renderer.CreateCompiledEffect(bytes.data(), bytes.size());
        ASSERT_NE(runtime, nullptr);
        EXPECT_FALSE(runtime->GetDescription().techniques.empty());
    }
}

TEST(DirectX12CompiledEffectTest, RuntimeDestructionAfterDeviceDestructionIsSafe)
{
    std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime;
    {
        HiDefGraphicsDevice device;
        const auto bytes = LoadEffect("CnaConformanceEffect.fxb");
        ASSERT_FALSE(bytes.empty());
        runtime = RendererOf(device).CreateCompiledEffect(bytes.data(), bytes.size());
        ASSERT_NE(runtime, nullptr);
    }

    runtime.reset();
}

TEST(DirectX12CompiledEffectDrawTest, SharedDrawMatrixContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedInstancingDrawContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedSpriteBatchContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedSamplerPixelContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedPassSelectionContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedOrientationContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedManyDrawsContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(DirectX12CompiledEffectDrawTest, SharedTruncationContract)
{
    HiDefGraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}

#endif  // CNA_DIRECTX12_COMPILED_EFFECTS
