// SPDX-License-Identifier: MS-PL
#ifdef CNA_RENDERER_DIRECTX11

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "EngineTestSupport.hpp"

#include <array>
#include <memory>

TEST(D3D11EffectTextureLifetimeTest, ATextureSurvivesItsPublicWrapperUntilReboundOrDisposed)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using namespace Microsoft::Xna::Framework::Graphics;
    CnaTest::EngineLayer::HiDefDevice device;
    ShaderEffect effect(device, R"HLSL(
struct Input { float3 position : POSITION; float4 color : COLOR; };
float4 main(Input input) : SV_Position { return float4(input.position, 1.0f); }
)HLSL", R"HLSL(
Texture2D<float4> Source : register(t0);
SamplerState SourceSampler : register(s0);
float4 main(float4 position : SV_Position) : SV_Target0
{
    return Source.SampleLevel(SourceSampler, float2(0.5f, 0.5f), 0.0f);
}
)HLSL");
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    const std::array<VertexPositionColor, 3> triangle{
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(-1.0f, 3.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(3.0f, -1.0f, 0.0f), Color::White),
    };
    constexpr int size = 32;
    RenderTarget2D target(device, size, size);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

    const auto drawCentre = [&] {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
        device.SetRenderTarget(nullptr);
        std::array<Color, size * size> pixels{};
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[(size / 2) * size + size / 2];
    };

    auto first = std::make_unique<Texture2D>(device, 1, 1);
    const Color red(213, 0, 0, 255);
    first->SetData(&red, 1);
    std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> firstRenderer =
        first->GetRenderer().shared_from_this();
    effect.SetTexture(0, *first);
    first.reset();
    ASSERT_FALSE(firstRenderer.expired());
    EXPECT_EQ(drawCentre().getRProperty(), red.getRProperty());

    auto second = std::make_unique<Texture2D>(device, 1, 1);
    const Color green(0, 187, 0, 255);
    second->SetData(&green, 1);
    std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> secondRenderer =
        second->GetRenderer().shared_from_this();
    effect.SetTexture(0, *second);
    EXPECT_TRUE(firstRenderer.expired());
    second.reset();
    ASSERT_FALSE(secondRenderer.expired());
    EXPECT_EQ(drawCentre().getGProperty(), green.getGProperty());

    static_cast<GraphicsResource&>(effect).Dispose();
    EXPECT_TRUE(secondRenderer.expired());
}

TEST(D3D11EffectTextureLifetimeTest, CubeAndVolumeTexturesSurviveTheirPublicWrappers)
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector3;
    using namespace Microsoft::Xna::Framework::Graphics;
    CnaTest::EngineLayer::HiDefDevice device;
    ShaderEffect effect(device, R"HLSL(
struct Input { float3 position : POSITION; float4 color : COLOR; };
float4 main(Input input) : SV_Position { return float4(input.position, 1.0f); }
)HLSL", R"HLSL(
TextureCube<float4> Cube : register(t0);
Texture3D<float4> Volume : register(t1);
SamplerState CubeSampler : register(s0);
SamplerState VolumeSampler : register(s1);
float4 main(float4 position : SV_Position) : SV_Target0
{
    return 0.5f * Cube.SampleLevel(CubeSampler, float3(1, 0, 0), 0)
         + 0.5f * Volume.SampleLevel(VolumeSampler, float3(0.5f, 0.5f, 0.5f), 0);
}
)HLSL");
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    auto cube = std::make_unique<TextureCube>(device, 1, false, SurfaceFormat::Color);
    const Color blue(0, 0, 202, 255);
    cube->SetData(CubeMapFace::PositiveX, &blue, 1);
    std::weak_ptr<CNA::Internal::Renderers::ITextureCubeRenderer> cubeRenderer =
        cube->GetRenderer().shared_from_this();
    effect.SetTexture(0, *cube);
    cube.reset();
    ASSERT_FALSE(cubeRenderer.expired());

    auto volume = std::make_unique<Texture3D>(device, 1, 1, 1, false,
                                              SurfaceFormat::Color);
    const Color green(0, 182, 0, 255);
    volume->SetData(&green, 1);
    std::weak_ptr<CNA::Internal::Renderers::ITexture3DRenderer> volumeRenderer =
        volume->GetRenderer().shared_from_this();
    effect.SetTexture(1, *volume);
    volume.reset();
    ASSERT_FALSE(volumeRenderer.expired());

    constexpr int size = 32;
    RenderTarget2D target(device, size, size);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    device.getSamplerStatesProperty()[1] = SamplerState::PointClamp;
    const std::array<VertexPositionColor, 3> triangle{
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(-1.0f, 3.0f, 0.0f), Color::White),
        VertexPositionColor(Vector3(3.0f, -1.0f, 0.0f), Color::White),
    };
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    device.SetRenderTarget(nullptr);
    std::array<Color, size * size> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    const Color centre = pixels[(size / 2) * size + size / 2];
    EXPECT_NEAR(centre.getGProperty(), green.getGProperty() / 2, 2);
    EXPECT_NEAR(centre.getBProperty(), blue.getBProperty() / 2, 2);

    static_cast<GraphicsResource&>(effect).Dispose();
    EXPECT_TRUE(cubeRenderer.expired());
    EXPECT_TRUE(volumeRenderer.expired());
}

#endif
