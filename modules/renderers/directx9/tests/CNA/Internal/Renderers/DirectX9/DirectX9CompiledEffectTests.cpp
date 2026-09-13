// SPDX-License-Identifier: MS-PL

#if defined(CNA_DIRECTX9_COMPILED_EFFECTS)

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
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
#include <stdexcept>
#include <vector>

namespace
{
    using CNA::Internal::Renderers::DirectX9::DirectX9Renderer;
    using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
    using Microsoft::Xna::Framework::Graphics::PresentationParameters;

    std::vector<std::uint8_t> LoadEffect(const char* name)
    {
        const std::filesystem::path path =
            CNA::TestSupport::CompiledEffectDirectory() / name;
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    DirectX9Renderer& RendererOf(GraphicsDevice& device)
    {
        auto* renderer = dynamic_cast<DirectX9Renderer*>(&device.GetRenderer());
        if (renderer == nullptr)
            throw std::runtime_error("test configuration did not select DirectX 9");
        return *renderer;
    }
}

TEST(DirectX9CompiledEffectTest, CapabilityAndPublicRuntimeContract)
{
    GraphicsDevice device;
    (void) RendererOf(device);
    ASSERT_TRUE(CNA::TestSupport::SupportsCompiledEffects(device));
    CNA::TestSupport::RunCompiledEffectContract(device);
}

TEST(DirectX9CompiledEffectTest, CompilerProducedStockEffectsParse)
{
    GraphicsDevice device;
    DirectX9Renderer& renderer = RendererOf(device);
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

TEST(DirectX9CompiledEffectTest, RuntimeDestructionAfterDeviceDestructionIsSafe)
{
    std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime;
    {
        GraphicsDevice device;
        const auto bytes = LoadEffect("CnaConformanceEffect.fxb");
        ASSERT_FALSE(bytes.empty());
        runtime = RendererOf(device).CreateCompiledEffect(bytes.data(), bytes.size());
        ASSERT_NE(runtime, nullptr);
    }
    runtime.reset();
}

TEST(DirectX9CompiledEffectTest, RuntimeCloneAfterDeviceDestructionIsRejected)
{
    std::unique_ptr<CNA::Internal::Renderers::ICompiledEffectRuntime> runtime;
    {
        GraphicsDevice device;
        const auto bytes = LoadEffect("CnaConformanceEffect.fxb");
        ASSERT_FALSE(bytes.empty());
        runtime = RendererOf(device).CreateCompiledEffect(bytes.data(), bytes.size());
        ASSERT_NE(runtime, nullptr);
    }
    EXPECT_THROW((void) runtime->Clone(), std::runtime_error);
}

TEST(DirectX9CompiledEffectDrawTest, SharedDrawMatrixContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectDrawContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedMultiStreamDrawContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectMultiStreamDrawContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedInstancingDrawContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectInstancingDrawContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedSpriteBatchContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SpriteBatchInheritsStockVertexShaderForPixelOnlyEffect)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    GraphicsDevice device;
    CNA::TestSupport::SyntheticEffectOptions options;
    options.includeSampler = true;
    options.pixelShaderSamplesTexture = true;
    options.samplerRegister = 1;
    Effect effect(device, CNA::TestSupport::BuildSyntheticEffect(options));
    effect.getParametersProperty()["Tint"]->SetValue(Vector4::One);

    Texture2D sprite(device, 1, 1);
    const Color red[1] = {Color::Red};
    sprite.SetData(red, 1);
    Texture2D secondary(device, 1, 1);
    const Color green[1] = {Color::Green};
    secondary.SetData(green, 1);
    device.getTexturesProperty()(1, &secondary);
    device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;

    RenderTarget2D target(device, 8, 8);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, nullptr, nullptr, nullptr, &effect);
    batch.Draw(sprite, Microsoft::Xna::Framework::Rectangle(0, 0, 8, 8),
               Microsoft::Xna::Framework::Rectangle(0, 0, 1, 1), Color::White);
    batch.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    Color actual(0, 0, 0, 0);
    const Microsoft::Xna::Framework::Rectangle centre(4, 4, 1, 1);
    target.GetData(0, &centre, &actual, 0, 1);
    EXPECT_NEAR(actual.getRProperty(), 0, 3);
    EXPECT_NEAR(actual.getGProperty(), 128, 3);
    EXPECT_NEAR(actual.getBProperty(), 0, 3);
}

TEST(DirectX9CompiledEffectDrawTest, SharedSpriteBatchMultiPassContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchMultiPassContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedSpriteBatchTextureSlotContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchTextureSlotContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedSamplerPixelContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSamplerPixelContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedPassSelectionContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectPassSelectionContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedRenderTargetSourceContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectRenderTargetSourceContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedSpriteBatchRenderTargetSourceContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSpriteBatchRenderTargetSourceContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedStockDrawIsolationContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectStockDrawIsolationContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedOrientationContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectOrientationContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedEffectSwitchingContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectSwitchingContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedCubeAndVolumeSamplerContract)
{
    // XNA Reach intentionally forbids Texture3D; exercise D3D9's real cube/volume sampler route
    // under the profile where volume textures are part of the public contract.
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    CNA::TestSupport::RunCompiledEffectCubeAndVolumeSamplerContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedManyDrawsContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectManyDrawsContract(device);
}

TEST(DirectX9CompiledEffectDrawTest, SharedTruncationContract)
{
    GraphicsDevice device;
    CNA::TestSupport::RunCompiledEffectTruncationContract(device);
}

#endif  // CNA_DIRECTX9_COMPILED_EFFECTS
