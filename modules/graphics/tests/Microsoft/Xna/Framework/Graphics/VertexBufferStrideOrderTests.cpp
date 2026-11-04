// SPDX-License-Identifier: MS-PL
// TQ-021: VertexBuffer.SetData/GetData(offsetInBytes, data, startIndex, elementCount,
// vertexStride) check a too-small vertexStride before the buffer's size.
//
// Oracle: Microsoft XNA 4.0 VertexBuffer::CopyData<T>.
//  IL_000d-0010 CheckDisposed; IL_0015-0023 null or empty data (ArgumentNullException "data",
//  thrown at IL_02be); IL_0026-0081 SetData only: a bound buffer -> InvalidOperationException;
//  IL_0082-009e GetData only: WriteOnly -> NotSupportedException; IL_009f-00a4
//  Helpers::ValidateCopyParameters (dataIndex, elementCount); IL_00b7-00d6 a non-zero
//  vertexStride smaller than sizeof(T) -> ArgumentOutOfRangeException("vertexStride");
//  IL_00eb-0100 elementCount * sizeof(T) (+ the stride gaps) + offsetInBytes > the buffer size ->
//  InvalidOperationException.
// For VertexPositionColor sizeof(T) is 16 bytes, so a stride of 8 is refused as "vertexStride"
// even when the elements would not fit the buffer either.
#include <gtest/gtest.h>

#include <array>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    template<typename TCallable>
    void ExpectStrideRefusal(TCallable&& transfer, const char* what)
    {
        try
        {
            transfer();
            ADD_FAILURE() << what << ": accepted";
        }
        catch (const System::ArgumentOutOfRangeException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::ArgumentOutOfRangeException)) << what;
            EXPECT_EQ(exception.getParamNameProperty(), "vertexStride") << what;
        }
        catch (...)
        {
            ADD_FAILURE() << what << ": threw something other than ArgumentOutOfRangeException";
        }
    }
}

TEST(VertexBufferStrideOrderTest, TooSmallStridePrecedesTheBufferSizeForTypedVertices)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no vertex buffers";

    // One 16-byte slot. Two elements need 32 bytes, so the size check alone would fail too.
    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 1,
                        BufferUsage::None);
    const VertexPositionColor original(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7));
    buffer.SetData(&original, 1);

    std::array<VertexPositionColor, 2> source{
        VertexPositionColor(Vector3(9.0f, 9.0f, 9.0f), Color(9, 9, 9, 9)),
        VertexPositionColor(Vector3(8.0f, 8.0f, 8.0f), Color(8, 8, 8, 8)),
    };
    ExpectStrideRefusal([&] { buffer.SetData(0, source.data(), 0, 2, 8); }, "SetData stride 8");
    ExpectStrideRefusal([&] { buffer.SetData(0, source.data(), 0, 2, 15); }, "SetData stride 15");
    ExpectStrideRefusal([&] { buffer.SetData(0, source.data(), 0, 2, -16); },
                        "SetData stride -16");

    std::array<VertexPositionColor, 2> destination{source};
    ExpectStrideRefusal([&] { buffer.GetData(0, destination.data(), 0, 2, 8); },
                        "GetData stride 8");

    // The refusals wrote nothing: the buffer still holds its one original vertex and the
    // destination array is untouched.
    VertexPositionColor readBack;
    buffer.GetData(&readBack, 1);
    EXPECT_EQ(readBack, original);
    EXPECT_EQ(destination, source);

    // With an acceptable stride the same over-long request is the size failure.
    try
    {
        buffer.SetData(0, source.data(), 0, 2, 16);
        ADD_FAILURE() << "two 16-byte elements fitted a 16-byte buffer";
    }
    catch (const System::InvalidOperationException& exception)
    {
        EXPECT_EQ(typeid(exception), typeid(System::InvalidOperationException));
    }
}
