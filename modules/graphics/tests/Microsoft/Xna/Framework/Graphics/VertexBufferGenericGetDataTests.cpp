// SPDX-License-Identifier: MS-PL
// SAMPLE-066: XNA's VertexBuffer.GetData<T>(T[]) for an application-defined vertex type. CNA
// carried the built-in vertex types only, so a game reading back geometry it had authored itself
// -- ShipGame's collision mesh walks the 56-byte position/normal/binormal/tangent vertex its own
// content processor writes, to build its collision tree -- had no way to read it at all. The
// mirror of the generic SetData<T> SAMPLE-040 added.

#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    // ShipGame's own BoxCollider/CollisionMesh.cs CustomVertex, field for field.
    struct CustomVertex
    {
        Vector3 Position;
        Vector4 Normal;
        Vector4 Binormal;
        Vector3 Tangent;
    };

    static_assert(sizeof(CustomVertex) == 56);
    static_assert(std::is_trivially_copyable_v<CustomVertex>);

    VertexDeclaration CustomDeclaration()
    {
        return VertexDeclaration({
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::Normal, 0),
            VertexElement(28, VertexElementFormat::Vector4, VertexElementUsage::Binormal, 0),
            VertexElement(44, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0)});
    }

    CustomVertex MakeVertex(float seed)
    {
        return CustomVertex{Vector3(seed, seed + 1, seed + 2),
                            Vector4(seed + 3, seed + 4, seed + 5, seed + 6),
                            Vector4(seed + 7, seed + 8, seed + 9, seed + 10),
                            Vector3(seed + 11, seed + 12, seed + 13)};
    }
}

TEST(VertexBufferGenericGetDataTest, AnApplicationVertexTypeReadsBackWhatItUploaded)
{
    GraphicsDevice device;
    VertexBuffer buffer(device, CustomDeclaration(), 3, BufferUsage::None);

    const std::array<CustomVertex, 3> uploaded{MakeVertex(1.0f), MakeVertex(100.0f),
                                                MakeVertex(1000.0f)};
    buffer.SetData(uploaded.data(), 3);

    std::array<CustomVertex, 3> read{};
    buffer.GetData(read.data(), 3);

    EXPECT_EQ(std::memcmp(uploaded.data(), read.data(), sizeof(uploaded)), 0);
    EXPECT_EQ(read[1].Position, Vector3(100.0f, 101.0f, 102.0f));
    EXPECT_EQ(read[2].Tangent, Vector3(1011.0f, 1012.0f, 1013.0f));
}

// A partial read must start at the buffer's first vertex and stop where it was asked to, leaving
// the rest of the caller's array untouched -- the shape CollisionMesh's per-part loop relies on.
TEST(VertexBufferGenericGetDataTest, ReadingFewerVerticesLeavesTheRestOfTheArrayAlone)
{
    GraphicsDevice device;
    VertexBuffer buffer(device, CustomDeclaration(), 3, BufferUsage::None);

    const std::array<CustomVertex, 3> uploaded{MakeVertex(1.0f), MakeVertex(100.0f),
                                                MakeVertex(1000.0f)};
    buffer.SetData(uploaded.data(), 3);

    std::array<CustomVertex, 3> read{};
    const CustomVertex sentinel = MakeVertex(-1.0f);
    read[2] = sentinel;

    buffer.GetData(read.data(), 2);

    EXPECT_EQ(std::memcmp(uploaded.data(), read.data(), sizeof(CustomVertex) * 2), 0);
    EXPECT_EQ(std::memcmp(&sentinel, &read[2], sizeof(CustomVertex)), 0)
        << "reading two vertices must not have touched the third";
}
