// SPDX-License-Identifier: MS-PL
// REMED-GFX-104: deferred indexed-draw correctness and WebGPU native alignment.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    VertexDeclaration PositionColorDeclaration()
    {
        return VertexDeclaration(
            16,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(
                    12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
            });
    }

    std::array<VertexPositionColor, 3> CenterTriangle(const Color& color)
    {
        return {
            VertexPositionColor(Vector3(-0.8f, -0.8f, 0.5f), color),
            VertexPositionColor(Vector3(0.8f, -0.8f, 0.5f), color),
            VertexPositionColor(Vector3(0.0f, 0.8f, 0.5f), color),
        };
    }

    bool ColorNear(const Color& actual, const Color& expected, int tolerance = 16)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance &&
               std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance &&
               std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance;
    }

    Color ReadCenter(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        const Rectangle region(
            viewport.getWidthProperty() / 2,
            viewport.getHeightProperty() / 2,
            1,
            1);
        Color pixel = Color::Transparent;
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    class IndexedDrawDeferredTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void RequireIndexedRendering()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support indexed rendering";
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::None);
        }

        void ApplyVertexColorEffect(BasicEffect& effect)
        {
            effect.VertexColorEnabled = true;
            effect.Apply();
        }
    };
}

TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsNonzeroStartIndex)
{
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto green = CenterTriangle(Color::Lime);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], green[0], green[1], green[2],
    };
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 3, 4, 5};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 6);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        0,
        3,
        3,
        3,
        1);

    EXPECT_TRUE(ColorNear(ReadCenter(device), Color::Lime));
}

TEST_F(IndexedDrawDeferredTest, PersistentDrawHonorsPositiveBaseVertex)
{
    RequireIndexedRendering();

    const auto red = CenterTriangle(Color::Red);
    const auto blue = CenterTriangle(Color::Blue);
    const std::array<VertexPositionColor, 6> vertices{
        red[0], red[1], red[2], blue[0], blue[1], blue[2],
    };
    const std::array<std::uint32_t, 3> indices{0, 1, 2};
    VertexBuffer vertexBuffer(
        device, PositionColorDeclaration(), 6, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::ThirtyTwoBits, 3, BufferUsage::None);
    vertexBuffer.SetData(vertices.data(), 6);
    indexBuffer.SetData(indices.data(), 3);

    BasicEffect effect(device);
    ApplyVertexColorEffect(effect);
    device.Clear(Color::Black);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);
    device.DrawIndexedPrimitives(
        PrimitiveType::TriangleList,
        3,
        0,
        3,
        0,
        1);

    EXPECT_TRUE(ColorNear(ReadCenter(device), Color::Blue));
}
