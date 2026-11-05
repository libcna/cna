// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <string>
#include <typeinfo>
#include <vector>
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
using namespace Microsoft::Xna::Framework::Graphics;

// Oracle: Refresh VertexBuffer.CopyData<T> IL_00b1-00eb: the span is
// sizeof(T) + (count-1)*stride. Here 4 + 2*12 + offset 8 = capacity 36.
// IL_01eb-0250 copies tightly packed array elements into those buffer slots.
TEST(BufferTransferBoundaryTest, VertexWindowEndsAtLastElementByteAndKeepsEveryGap)
{
    GraphicsDevice device;
    const VertexDeclaration declaration(12, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    VertexBuffer buffer(device, declaration, 3, BufferUsage::None);
    std::array<std::uint8_t, 36> initial;
    initial.fill(0xA5);
    buffer.SetData(initial.data(), 36);
    using FourBytes = std::array<std::uint8_t, 4>;
    std::array<FourBytes, 4> source{{{90,91,92,93}, {1,2,3,4}, {5,6,7,8}, {9,10,11,12}}};
    buffer.SetData(8, source.data(), 1, 3, 12);
    source.fill(FourBytes{0xEE,0xEE,0xEE,0xEE}); // The upload owns a copy.
    const std::array<std::uint8_t, 36> expected{
        0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,1,2,3,4,
        0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,5,6,7,8,
        0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,0xA5,9,10,11,12};
    std::array<std::uint8_t, 36> bytes{};
    buffer.GetData(bytes.data(), 36); // Independent contiguous view of the strided write.
    EXPECT_EQ(bytes, expected);
    // Initialize a new byte pattern independently of the strided SetData under test.
    for (std::size_t i = 0; i < initial.size(); ++i) initial[i] = static_cast<std::uint8_t>(i);
    buffer.SetData(initial.data(), 36);
    std::array<FourBytes, 5> result;
    result.fill(FourBytes{0xDD,0xDD,0xDD,0xDD});
    buffer.GetData(8, result.data(), 1, 3, 12);
    EXPECT_EQ(result, (std::array<FourBytes, 5>{{
        {0xDD,0xDD,0xDD,0xDD}, {8,9,10,11}, {20,21,22,23},
        {32,33,34,35}, {0xDD,0xDD,0xDD,0xDD}}}));
    const auto preserved = result;
    EXPECT_THROW(buffer.GetData(9, result.data(), 1, 3, 12), System::InvalidOperationException);
    EXPECT_EQ(result, preserved);
}

// Refresh Dynamic{Vertex,Index}Buffer.SetContentLost IL_0000-0016 raises on true;
// CopyData<T> clears only after a successful SetData (VB IL_02a4-02b7, IB IL_017c-018f).
// A read/refused write keeps the flag and does not emit another loss event.
TEST(BufferTransferBoundaryTest, DynamicReadsAndRefusalsPreserveLostStateUntilAWindowWrite)
{
    GraphicsDevice device;
    const VertexDeclaration declaration(4, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
    DynamicVertexBuffer vertex(device, declaration, 2, BufferUsage::None);
    DynamicIndexBuffer index(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    const std::array<std::uint8_t, 8> initial{0,1,2,3,4,5,6,7};
    vertex.SetData(initial.data(), 8);
    index.SetData(initial.data(), 8);
    int vertexEvents = 0, indexEvents = 0;
    vertex.ContentLost += [&](System::Object* sender, const System::EventArgs&) {
        EXPECT_EQ(sender, &vertex);
        EXPECT_TRUE(vertex.getIsContentLostProperty());
        ++vertexEvents;
    };
    index.ContentLost += [&](System::Object* sender, const System::EventArgs&) {
        EXPECT_EQ(sender, &index);
        EXPECT_TRUE(index.getIsContentLostProperty());
        ++indexEvents;
    };
    EXPECT_FALSE(vertex.getIsContentLostProperty());
    EXPECT_FALSE(index.getIsContentLostProperty());
    vertex.NotifyContentLostEXT(); index.NotifyContentLostEXT();
    std::array<std::uint8_t, 8> read{};
    vertex.GetData(read.data(), 8); EXPECT_EQ(read, initial);
    index.GetData(read.data(), 8); EXPECT_EQ(read, initial);
    EXPECT_THROW(vertex.SetData(8, initial.data(), 0, 1, 1, SetDataOptions::Discard),
                 System::InvalidOperationException);
    EXPECT_THROW(index.SetData(8, initial.data(), 0, 1, SetDataOptions::Discard),
                 System::InvalidOperationException);
    EXPECT_TRUE(vertex.getIsContentLostProperty());
    EXPECT_TRUE(index.getIsContentLostProperty());
    EXPECT_EQ(vertexEvents, 1); EXPECT_EQ(indexEvents, 1);
    vertex.SetData(3, initial.data(), 7, 1, 1, SetDataOptions::NoOverwrite);
    index.SetData(3, initial.data(), 7, 1, SetDataOptions::NoOverwrite);
    EXPECT_FALSE(vertex.getIsContentLostProperty());
    EXPECT_FALSE(index.getIsContentLostProperty());
    EXPECT_EQ(vertexEvents, 1); EXPECT_EQ(indexEvents, 1);
    const std::array<std::uint8_t, 8> expected{0,1,2,7,4,5,6,7};
    vertex.GetData(read.data(), 8); EXPECT_EQ(read, expected);
    index.GetData(read.data(), 8); EXPECT_EQ(read, expected);
}
