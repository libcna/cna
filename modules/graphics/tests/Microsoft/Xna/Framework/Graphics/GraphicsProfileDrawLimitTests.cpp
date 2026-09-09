// SPDX-License-Identifier: MS-PL
// SOFTWARE-209: XNA profile draw-count and user-index-width ceilings.

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsAdapter;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PresentationParameters;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    class BoundTriangle
    {
    public:
        explicit BoundTriangle(GraphicsProfile profile)
            : device(GraphicsAdapter::getDefaultAdapterProperty(), profile,
                     PresentationParameters{}),
              vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                       BufferUsage::None),
              indices(device, IndexElementSize::SixteenBits, 3, BufferUsage::None),
              effect(device)
        {
            const std::array<VertexPositionColor, 3> vertexData{
                VertexPositionColor(Vector3(-0.5f, -0.5f, 0.5f), Color::White),
                VertexPositionColor(Vector3(0.0f, 0.5f, 0.5f), Color::White),
                VertexPositionColor(Vector3(0.5f, -0.5f, 0.5f), Color::White),
            };
            const std::array<std::uint16_t, 3> indexData{0, 1, 2};
            vertices.SetData(vertexData.data(), static_cast<int>(vertexData.size()));
            indices.SetData(indexData.data(), static_cast<int>(indexData.size()));
            effect.VertexColorEnabled = true;
            effect.Apply();
            device.SetVertexBuffer(&vertices);
            device.SetIndexBuffer(&indices);
        }

        GraphicsDevice device;
        VertexBuffer vertices;
        IndexBuffer indices;
        BasicEffect effect;
    };

    void ExpectPersistentDrawCeiling(GraphicsProfile profile, int maximum)
    {
        BoundTriangle fixture(profile);

        EXPECT_THROW(
            fixture.device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, maximum),
            System::ArgumentOutOfRangeException);
        EXPECT_THROW(
            fixture.device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, maximum + 1),
            System::NotSupportedException);

        EXPECT_THROW(
            fixture.device.DrawIndexedPrimitives(
                PrimitiveType::TriangleStrip, 0, 0, 3, 0, maximum),
            System::ArgumentOutOfRangeException);
        EXPECT_THROW(
            fixture.device.DrawIndexedPrimitives(
                PrimitiveType::TriangleStrip, 0, 0, 3, 0, maximum + 1),
            System::NotSupportedException);

        EXPECT_THROW(
            fixture.device.DrawInstancedPrimitives(
                PrimitiveType::TriangleStrip, 0, 0, 3, 0, maximum, 1),
            System::ArgumentOutOfRangeException);
        EXPECT_THROW(
            fixture.device.DrawInstancedPrimitives(
                PrimitiveType::TriangleStrip, 0, 0, 3, 0, maximum + 1, 1),
            System::NotSupportedException);

        EXPECT_THROW(
            fixture.device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, 3, 0, 2, maximum),
            System::ArgumentOutOfRangeException);
        EXPECT_THROW(
            fixture.device.DrawInstancedPrimitives(
                PrimitiveType::TriangleList, 0, 0, 3, 0, 2, maximum + 1),
            System::NotSupportedException);
    }
}

TEST(GraphicsProfileDrawLimitTest, PersistentDrawsUseTheReachCeiling)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    ExpectPersistentDrawCeiling(GraphicsProfile::Reach, 65'535);
}

TEST(GraphicsProfileDrawLimitTest, PersistentDrawsUseTheHiDefCeiling)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    ExpectPersistentDrawCeiling(GraphicsProfile::HiDef, 1'048'575);
}

TEST(GraphicsProfileDrawLimitTest, UserDrawsUseTheProfileCeilingAndReachRejectsWideIndices)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);
    BoundTriangle reach(GraphicsProfile::Reach);
    BoundTriangle hiDef(GraphicsProfile::HiDef);
    constexpr int reachMaximum = 65'535;
    constexpr int hiDefMaximum = 1'048'575;
    const VertexPositionColor vertex(Vector3(), Color::White);
    const std::uint16_t index16 = 0;
    const std::uint32_t index32 = 0;

    EXPECT_THROW(
        reach.device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, static_cast<const VertexPositionColor*>(nullptr),
            0, reachMaximum),
        System::ArgumentNullException);
    EXPECT_THROW(
        reach.device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, static_cast<const VertexPositionColor*>(nullptr),
            0, reachMaximum + 1),
        System::ArgumentNullException);
    EXPECT_THROW(
        reach.device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, &vertex, 0, reachMaximum + 1),
        System::NotSupportedException);

    EXPECT_THROW(
        reach.device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleStrip, static_cast<const VertexPositionColor*>(nullptr), 0, 1,
            static_cast<const std::uint16_t*>(nullptr), 0, reachMaximum),
        System::ArgumentNullException);
    EXPECT_THROW(
        reach.device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleStrip, static_cast<const VertexPositionColor*>(nullptr), 0, 1,
            static_cast<const std::uint16_t*>(nullptr), 0, reachMaximum + 1),
        System::ArgumentNullException);
    EXPECT_THROW(
        reach.device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleStrip, &vertex, 0, 1,
            &index16, 0, reachMaximum + 1),
        System::NotSupportedException);

    EXPECT_THROW(
        reach.device.DrawUserIndexedPrimitives(
            PrimitiveType::TriangleList, &vertex, 0, 1, &index32, 0, 1),
        System::NotSupportedException);

    EXPECT_THROW(
        hiDef.device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, static_cast<const VertexPositionColor*>(nullptr),
            0, hiDefMaximum),
        System::ArgumentNullException);
    EXPECT_THROW(
        hiDef.device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, static_cast<const VertexPositionColor*>(nullptr),
            0, hiDefMaximum + 1),
        System::ArgumentNullException);
    EXPECT_THROW(
        hiDef.device.DrawUserPrimitives(
            PrimitiveType::TriangleStrip, &vertex, 0, hiDefMaximum + 1),
        System::NotSupportedException);
}
