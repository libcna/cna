// SPDX-License-Identifier: MS-PL
//
// plans/plan_fx.md FX-026: SamplerState carries three addressing axes, but the renderer-neutral contract
// used to carry two. FNA3D filled the third by mirroring U, so a draw replaced whatever W an
// effect or the game had asked for. These tests pin the axis down where the assembled native
// sampler state is visible, which is the only place the substitution was observable.

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::SamplerState;
    using Microsoft::Xna::Framework::Graphics::Texture2D;
    using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
    using Microsoft::Xna::Framework::Graphics::VertexPositionTexture;

    /// Returns the renderer under test, or null when this build selected a different one.
    CNA::Internal::Renderers::Fna3d::Fna3dRenderer* Fna3dOf(GraphicsDevice& device)
    {
        return dynamic_cast<CNA::Internal::Renderers::Fna3d::Fna3dRenderer*>(&device.GetRenderer());
    }

    /// Draws one textured triangle. A primitive draw is what makes the device publish its whole
    /// sampler state collection to the renderer; SpriteBatch does not take that path.
    void DrawOnce(GraphicsDevice& device)
    {
        const std::array<VertexPositionTexture, 3> triangle{
            VertexPositionTexture(Vector3(-1.0f, -1.0f, 0.0f), Vector2(0.0f, 1.0f)),
            VertexPositionTexture(Vector3(1.0f, -1.0f, 0.0f), Vector2(1.0f, 1.0f)),
            VertexPositionTexture(Vector3(0.0f, 1.0f, 0.0f), Vector2(0.5f, 0.0f)),
        };
        Texture2D white = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        BasicEffect effect(device);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&white);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, triangle.data(), 0, 1);
    }
}

TEST(Fna3dSamplerAddressTest, DeviceAddressWReachesTheNativeSamplerState)
{
    GraphicsDevice device;
    auto* renderer = Fna3dOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the FNA3D renderer";

    SamplerState mixed;
    mixed.setAddressUProperty(TextureAddressMode::Clamp);
    mixed.setAddressVProperty(TextureAddressMode::Clamp);
    mixed.setAddressWProperty(TextureAddressMode::Mirror);
    device.getSamplerStatesProperty()[0] = mixed;

    DrawOnce(device);

    const FNA3D_SamplerState native = renderer->GetSamplerStateEXT(0);
    EXPECT_EQ(native.addressU, FNA3D_TEXTUREADDRESSMODE_CLAMP);
    EXPECT_EQ(native.addressV, FNA3D_TEXTUREADDRESSMODE_CLAMP);
    // Before FX-026 this mirrored addressU and the requested Mirror was silently dropped.
    EXPECT_EQ(native.addressW, FNA3D_TEXTUREADDRESSMODE_MIRROR);
}

TEST(Fna3dSamplerAddressTest, AddressWIsIndependentOfTheOtherTwoAxes)
{
    GraphicsDevice device;
    auto* renderer = Fna3dOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the FNA3D renderer";

    SamplerState wrapUvClampW;
    wrapUvClampW.setAddressUProperty(TextureAddressMode::Wrap);
    wrapUvClampW.setAddressVProperty(TextureAddressMode::Wrap);
    wrapUvClampW.setAddressWProperty(TextureAddressMode::Clamp);
    device.getSamplerStatesProperty()[1] = wrapUvClampW;

    DrawOnce(device);

    const FNA3D_SamplerState native = renderer->GetSamplerStateEXT(1);
    EXPECT_EQ(native.addressU, FNA3D_TEXTUREADDRESSMODE_WRAP);
    EXPECT_EQ(native.addressW, FNA3D_TEXTUREADDRESSMODE_CLAMP);
}

TEST(Fna3dSamplerAddressTest, DefaultSamplerStateKeepsWrapOnEveryAxis)
{
    GraphicsDevice device;
    auto* renderer = Fna3dOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the FNA3D renderer";

    device.getSamplerStatesProperty()[2] = SamplerState::LinearWrap;

    DrawOnce(device);

    const FNA3D_SamplerState native = renderer->GetSamplerStateEXT(2);
    EXPECT_EQ(native.addressU, FNA3D_TEXTUREADDRESSMODE_WRAP);
    EXPECT_EQ(native.addressV, FNA3D_TEXTUREADDRESSMODE_WRAP);
    EXPECT_EQ(native.addressW, FNA3D_TEXTUREADDRESSMODE_WRAP);
}

TEST(Fna3dSamplerAddressTest, OutOfRangeSlotReadsBackADefaultState)
{
    GraphicsDevice device;
    auto* renderer = Fna3dOf(device);
    if (renderer == nullptr) GTEST_SKIP() << "this build did not select the FNA3D renderer";

    EXPECT_EQ(renderer->GetSamplerStateEXT(-1).maxAnisotropy, 0);
    EXPECT_EQ(renderer->GetSamplerStateEXT(4096).maxAnisotropy, 0);
}
