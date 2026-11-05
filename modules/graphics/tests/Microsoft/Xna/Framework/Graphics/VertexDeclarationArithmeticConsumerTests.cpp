// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <typeinfo>

#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    template<class Action>
    void CapacityError(Action&& action)
    {
        try { action(); ADD_FAILURE() << "expected capacity refusal"; }
        catch (const System::InvalidOperationException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::InvalidOperationException));
            EXPECT_EQ(error.getMessageProperty(),
                "The array is not the correct size for the amount of data requested.");
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
    }

    template<class Buffer>
    void CheckTransferArithmetic(GraphicsDevice& device)
    {
        const VertexDeclaration declaration(4, {
            VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
        Buffer buffer(device, declaration, 4, BufferUsage::None);
        const std::array<std::uint32_t, 4> source{11, 22, 33, 44};
        buffer.SetData(source.data(), 4);
        std::array<std::uint32_t, 4> destination{99, 99, 99, 99};
        // Refresh CopyData IL_00bb-00e5: for count 1 the span is sizeof(T)=4,
        // independently of a huge positive stride. Offset 4 selects source word 22.
        buffer.GetData(4, destination.data(), 1, 1, 2147483647);
        EXPECT_EQ(destination, (std::array<std::uint32_t, 4>{99, 22, 99, 99}));
        // One source word at array index 2 replaces buffer word 1, still a four-byte span.
        buffer.SetData(4, source.data(), 2, 1, 2147483647);
        destination.fill(99);
        buffer.GetData(destination.data(), 4);
        EXPECT_EQ(destination, (std::array<std::uint32_t, 4>{11, 33, 33, 44}));

        // Refresh CopyData IL_00eb-0100 unsigned capacity comparison:
        // INT_MAX+4=2147483651, or count 2 span=2*4+(INT_MAX-4)=2147483651;
        // both exceed capacity 4*4=16 and refuse BEFORE native Lock/copy.
        const std::array<std::uint32_t, 4> sentinel{99, 99, 99, 99};
        destination = sentinel;
        CapacityError([&] { buffer.GetData(2147483647, destination.data(), 0, 1, 0); });
        EXPECT_EQ(destination, sentinel);
        CapacityError([&] { buffer.GetData(0, destination.data(), 0, 2, 2147483647); });
        EXPECT_EQ(destination, sentinel);
        CapacityError([&] { buffer.SetData(2147483647, source.data(), 0, 1, 0); });
        CapacityError([&] { buffer.SetData(0, source.data(), 0, 2, 2147483647); });
        buffer.GetData(destination.data(), 4);
        EXPECT_EQ(destination, (std::array<std::uint32_t, 4>{11, 33, 33, 44}));
        EXPECT_EQ(buffer.getVertexDeclarationProperty().getVertexStrideProperty(), 4);
    }
}

// Refresh profile Validate IL_0092-00f9 rejects UsageIndex 16 after the wrapped
// structural check. Creation stops before the renderer receives a malformed offset.
TEST(VertexDeclarationArithmeticConsumerTest, WrappedExtentStillReachesProfileUsageValidation)
{
    GraphicsDevice device;
    for (auto profile : {GraphicsProfile::Reach, GraphicsProfile::HiDef})
    {
        device.SetGraphicsProfileEXT(profile);
        const VertexDeclaration declaration(4, {
            VertexElement(2147483644, VertexElementFormat::Single, VertexElementUsage::Normal, 16)});
        const auto verify = [](auto&& action) {
            try { action(); ADD_FAILURE() << "invalid usage index accepted"; }
            catch (const System::ArgumentException& error)
            {
                EXPECT_EQ(typeid(error), typeid(System::ArgumentException));
                EXPECT_TRUE(error.getParamNameProperty().empty());
                EXPECT_EQ(error.getMessageProperty(),
                    "Invalid VertexDeclaration. Usage Normal16 is out of range.");
            }
            catch (...) { ADD_FAILURE() << "wrong exception type"; }
        };
        verify([&] { VertexBuffer buffer(device, declaration, 1, BufferUsage::None); });
        verify([&] { DynamicVertexBuffer buffer(device, declaration, 1, BufferUsage::None); });
    }
}

TEST(VertexDeclarationArithmeticConsumerTest, StaticTransfersUseSafeExtremeStridesAndOffsets)
{
    GraphicsDevice device;
    CheckTransferArithmetic<VertexBuffer>(device);
}

TEST(VertexDeclarationArithmeticConsumerTest, DynamicTransfersUseSafeExtremeStridesAndOffsets)
{
    GraphicsDevice device;
    CheckTransferArithmetic<DynamicVertexBuffer>(device);
}

// Refresh SetVertexBuffer IL_0003-0010 constructs a binding; Binding::.ctor
// IL_0013-001e rejects negative/out-of-count offsets before SetVertexBuffers
// IL_0111-0115 multiplies stride by offset. Refusals preserve the current binding.
TEST(VertexDeclarationArithmeticConsumerTest, ExtremeBindingOffsetsRefuseBeforeByteMultiplication)
{
    GraphicsDevice device;
    const VertexDeclaration declaration(4, {
        VertexElement(0, VertexElementFormat::Single, VertexElementUsage::Position, 0)});
    VertexBuffer buffer(device, declaration, 4, BufferUsage::None);
    device.SetVertexBuffer(&buffer, 1);
    for (int offset : {(-2147483647 - 1), -1, 4, 2147483647})
    {
        SCOPED_TRACE(offset);
        try { device.SetVertexBuffer(&buffer, offset); ADD_FAILURE() << "invalid offset accepted"; }
        catch (const System::ArgumentOutOfRangeException& error)
        {
            EXPECT_EQ(typeid(error), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(error.getParamNameProperty(), "vertexOffset");
        }
        catch (...) { ADD_FAILURE() << "wrong exception type"; }
        const auto bindings = device.GetVertexBuffers();
        ASSERT_EQ(bindings.size(), 1U);
        EXPECT_EQ(bindings[0].getVertexBufferProperty(), &buffer);
        EXPECT_EQ(bindings[0].getVertexOffsetProperty(), 1);
    }
    device.SetVertexBuffer(&buffer, 3); // Last valid vertex: byte offset 3*4=12.
    ASSERT_EQ(device.GetVertexBuffers().size(), 1U);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexOffsetProperty(), 3);
    device.SetVertexBuffer(nullptr, (-2147483647 - 1));
    EXPECT_TRUE(device.GetVertexBuffers().empty()); // Refresh null branch ignores offset.
}
