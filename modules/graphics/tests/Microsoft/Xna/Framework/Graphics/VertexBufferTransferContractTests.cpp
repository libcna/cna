// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>
#include <type_traits>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    struct CompactVertex
    {
        float X;
        float Y;

        friend bool operator==(const CompactVertex&, const CompactVertex&) = default;
    };

    static_assert(sizeof(CompactVertex) == 8);
    static_assert(std::is_trivially_copyable_v<CompactVertex>);

    VertexDeclaration CompactDeclaration(const int stride)
    {
        return VertexDeclaration(
            stride,
            {VertexElement(
                0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0)});
    }
}

TEST(VertexBufferTransferContractTest, GenericSourceAndDestinationArraySlicesRoundTrip)
{
    GraphicsDevice device;
    VertexBuffer buffer(device, CompactDeclaration(8), 3, BufferUsage::None);
    const std::array<CompactVertex, 4> source{
        CompactVertex{-1.0f, -1.0f},
        CompactVertex{1.0f, 2.0f},
        CompactVertex{3.0f, 4.0f},
        CompactVertex{5.0f, 6.0f},
    };
    buffer.SetData(source.data(), 1, 3);

    const CompactVertex sentinel{-9.0f, -9.0f};
    std::array<CompactVertex, 5> destination{
        sentinel, sentinel, sentinel, sentinel, sentinel};
    buffer.GetData(destination.data(), 2, 3);

    EXPECT_EQ(destination[0], sentinel);
    EXPECT_EQ(destination[1], sentinel);
    EXPECT_EQ(destination[2], source[1]);
    EXPECT_EQ(destination[3], source[2]);
    EXPECT_EQ(destination[4], source[3]);
}

TEST(VertexBufferTransferContractTest, GenericWindowUsesTightArrayElementsAndStridedBufferSlots)
{
    GraphicsDevice device;
    VertexBuffer buffer(device, CompactDeclaration(12), 3, BufferUsage::None);
    const std::array<CompactVertex, 4> source{
        CompactVertex{-1.0f, -1.0f},
        CompactVertex{10.0f, 20.0f},
        CompactVertex{30.0f, 40.0f},
        CompactVertex{50.0f, 60.0f},
    };
    buffer.SetData(0, source.data(), 1, 3, 12);

    const CompactVertex sentinel{-9.0f, -9.0f};
    std::array<CompactVertex, 5> destination{
        sentinel, sentinel, sentinel, sentinel, sentinel};
    buffer.GetData(0, destination.data(), 1, 3, 12);

    EXPECT_EQ(destination[0], sentinel);
    EXPECT_EQ(destination[1], source[1]);
    EXPECT_EQ(destination[2], source[2]);
    EXPECT_EQ(destination[3], source[3]);
    EXPECT_EQ(destination[4], sentinel);
}

TEST(VertexBufferTransferContractTest, DynamicWindowHonorsSourceIndexStrideAndBoundNoOverwrite)
{
    GraphicsDevice device;
    DynamicVertexBuffer buffer(device, CompactDeclaration(12), 4, BufferUsage::None);
    const std::array<CompactVertex, 4> initial{
        CompactVertex{1.0f, 2.0f},
        CompactVertex{3.0f, 4.0f},
        CompactVertex{5.0f, 6.0f},
        CompactVertex{7.0f, 8.0f},
    };
    buffer.SetData(0, initial.data(), 0, 4, 12);
    device.SetVertexBuffer(&buffer);

    const std::array<CompactVertex, 4> replacement{
        CompactVertex{-1.0f, -2.0f},
        CompactVertex{11.0f, 12.0f},
        CompactVertex{13.0f, 14.0f},
        CompactVertex{15.0f, 16.0f},
    };
    EXPECT_NO_THROW(buffer.SetData(
        12, replacement.data(), 1, 2, 12, SetDataOptions::NoOverwrite));

    std::array<CompactVertex, 4> result{};
    buffer.GetData(0, result.data(), 0, 4, 12);
    EXPECT_EQ(result[0], initial[0]);
    EXPECT_EQ(result[1], replacement[1]);
    EXPECT_EQ(result[2], replacement[2]);
    EXPECT_EQ(result[3], initial[3]);
}

TEST(VertexBufferTransferContractTest, BuiltInValuesPackForWindowsAndUseDestinationStartIndex)
{
    GraphicsDevice device;
    VertexBuffer buffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
    const std::array<VertexPositionColor, 3> initial{
        VertexPositionColor(Vector3(1.0f, 2.0f, 3.0f), Color(10, 20, 30, 40)),
        VertexPositionColor(Vector3(4.0f, 5.0f, 6.0f), Color(50, 60, 70, 80)),
        VertexPositionColor(Vector3(7.0f, 8.0f, 9.0f), Color(90, 100, 110, 120)),
    };
    buffer.SetData(initial.data(), static_cast<int>(initial.size()));

    const std::array<VertexPositionColor, 3> replacement{
        VertexPositionColor(Vector3(-1.0f, -1.0f, -1.0f), Color::Black),
        VertexPositionColor(Vector3(40.0f, 50.0f, 60.0f), Color::CornflowerBlue),
        VertexPositionColor(Vector3(-2.0f, -2.0f, -2.0f), Color::White),
    };
    buffer.SetData(16, replacement.data(), 1, 1, 16);

    const VertexPositionColor sentinel(Vector3(-9.0f, -9.0f, -9.0f), Color::Transparent);
    std::array<VertexPositionColor, 5> destination{
        sentinel, sentinel, sentinel, sentinel, sentinel};
    buffer.GetData(destination.data(), 2, 3);

    EXPECT_EQ(destination[0], sentinel);
    EXPECT_EQ(destination[1], sentinel);
    EXPECT_EQ(destination[2], initial[0]);
    EXPECT_EQ(destination[3], replacement[1]);
    EXPECT_EQ(destination[4], initial[2]);
}
