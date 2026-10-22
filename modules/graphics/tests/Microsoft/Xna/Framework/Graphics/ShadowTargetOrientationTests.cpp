// SPDX-License-Identifier: MS-PL
// cna-engine UP-019: an asymmetric caster and receiver use the same light projection.
#include <gtest/gtest.h>
#include <array>
#include <type_traits>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

template<class T> class ShadowTargetOrientation : public testing::Test {};
using Receivers = testing::Types<BasicEffect, SkinnedEffect, PbrEffect, SkinnedPbrEffect>;
TYPED_TEST_SUITE(ShadowTargetOrientation, Receivers);

TYPED_TEST(ShadowTargetOrientation, RenderedUpperHalfOccluderShadowsOnlyUpperHalf)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsShadowSamplingEXT())
        GTEST_SKIP() << "active renderer declares no stock-effect shadow reception";
    // Orientation applies independently of precision. FNA3D currently exposes
    // only Color here; UP-018 separately requires Single on Software and EasyGL.
    const SurfaceFormat format = device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single)
        ? SurfaceFormat::Single : SurfaceFormat::Color;
    RenderTarget2D map(device, 64, 64, false, format, DepthFormat::None);
    RenderTarget2D output(device, 64, 64, false, SurfaceFormat::Color, DepthFormat::None);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    const std::array<Vector3, 6> positions{{
        {-1, 1, 0.25f}, {1, 1, 0.25f}, {1, 0, 0.25f},
        {-1, 1, 0.25f}, {1, 0, 0.25f}, {-1, 0, 0.25f}}};
    std::array<VertexPositionNormalTexture, 6> caster;
    struct SkinnedVertex
    {
        Vector3 position, normal;
        Vector4 tangent;
        Vector2 uv;
        Vector4 weights;
        std::array<std::uint8_t, 4> indices;
    };
    static_assert(sizeof(SkinnedVertex) == 68);
    std::array<SkinnedVertex, 6> floor;
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        caster[i] = {positions[i], Vector3(0, 0, 1), Vector2::Zero};
        floor[i] = {{positions[i].X, positions[i].Y * 2 - 1, 0.5f},
                    Vector3(0, 0, 1), Vector4(1, 0, 0, 1), Vector2::Zero,
                    Vector4(1, 0, 0, 0), {0, 0, 0, 0}};
    }
    BasicEffect writer(device);
    writer.setDiffuseColorProperty(Vector3(0.625f)); // 0.25 * 0.5 + 0.5, receiver depth contract
    writer.setLightingEnabledProperty(false);
    device.SetRenderTarget(&map);
    device.Clear(Color::White);
    writer.Apply();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, 2);
    device.SetRenderTarget(nullptr);
    if (format == SurfaceFormat::Single)
    {
        std::array<float, 64 * 64> depths{};
        map.GetData(depths.data(), static_cast<int>(depths.size()));
        EXPECT_FLOAT_EQ(depths[16 * 64 + 32], 0.625f);
        EXPECT_FLOAT_EQ(depths[48 * 64 + 32], 1.0f);
    }
    else
    {
        std::array<Color, 64 * 64> depths;
        map.GetData(depths.data(), static_cast<int>(depths.size()));
        EXPECT_EQ(depths[16 * 64 + 32].getRProperty(), Color(0.625f, 0.625f, 0.625f).getRProperty());
        EXPECT_EQ(depths[48 * 64 + 32], Color::White);
    }

    TypeParam effect(device);
    effect.setLightingEnabledProperty(true);
    effect.setDiffuseColorProperty(Vector3(1));
    effect.setAmbientLightColorProperty(Vector3::Zero);
    if constexpr (std::is_same_v<TypeParam, PbrEffect> ||
                  std::is_same_v<TypeParam, SkinnedPbrEffect>)
    {
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0);
        effect.setRoughnessFactorProperty(1);
    }
    else
        effect.setEmissiveColorProperty(Vector3::Zero);
    effect.getDirectionalLight0Property().setEnabledProperty(true);
    effect.getDirectionalLight0Property().setDirectionProperty(Vector3(0, 0, -1));
    effect.getDirectionalLight0Property().setDiffuseColorProperty(Vector3(1));
    effect.getDirectionalLight0Property().setSpecularColorProperty(Vector3::Zero);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    if constexpr (std::is_same_v<TypeParam, SkinnedEffect> ||
                  std::is_same_v<TypeParam, SkinnedPbrEffect>)
        effect.SetBoneTransforms({Matrix::getIdentityProperty()});
    Texture2D white(device, 1, 1);
    const Color whitePixel = Color::White;
    white.SetData(&whitePixel, 1);
    effect.setTextureProperty(&white);
    VertexBuffer floorBuffer(device, VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(),
                             6, BufferUsage::None);
    floorBuffer.SetDataRaw(floor.data(), 6, sizeof(SkinnedVertex));
    IShadowReceiverEXT& receiver = effect;
    receiver.setShadowMapEXT(&map);
    receiver.setLightViewProjectionEXT(Matrix::getIdentityProperty());
    receiver.setShadowDepthBiasEXT(0);
    receiver.setShadowFilterRadiusEXT(0);
    for (bool enabled : {false, true})
    {
        SCOPED_TRACE(enabled);
        receiver.setShadowsEnabledEXT(enabled);
        device.SetRenderTarget(&output);
        device.Clear(Color::Magenta);
        effect.Apply();
        device.SetVertexBuffer(&floorBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetRenderTarget(nullptr);
        std::array<Color, 64 * 64> pixels;
        output.GetData(pixels.data(), static_cast<int>(pixels.size()));
        const int top = pixels[16 * 64 + 32].getRProperty();
        const int bottom = pixels[48 * 64 + 32].getRProperty();
        EXPECT_GT(bottom, 50) << "the unobstructed half must remain lit";
        if (enabled)
            EXPECT_LT(top, 5) << "only the caster's upper half must be shadowed";
        else
            EXPECT_GT(top, 50) << "unshadowed control must illuminate both halves";
    }
}
