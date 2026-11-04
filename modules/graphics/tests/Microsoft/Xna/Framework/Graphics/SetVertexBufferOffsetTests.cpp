// SPDX-License-Identifier: MS-PL
// TQ-021: GraphicsDevice.SetVertexBuffer(VertexBuffer, int vertexOffset) accepts exactly the
// offsets a VertexBufferBinding accepts.
//
// Oracle: Microsoft XNA 4.0. SetVertexBuffer(VertexBuffer, int) IL_0001-0010 constructs
// `new VertexBufferBinding(vertexBuffer, vertexOffset)` for a non-null buffer before calling the
// shared SetVertexBuffers; VertexBufferBinding::.ctor(VertexBuffer, int) IL_0013-001e compares the
// offset with VertexCount as an unsigned value and IL_0037-0041 throws
// ArgumentOutOfRangeException("vertexOffset"). A null buffer skips the binding entirely
// (IL_0017-001a), so its offset is never examined. FNA's VertexBufferBinding constructor agrees
// (src/Graphics/Vertices/VertexBufferBinding.cs, `(uint) vertexOffset >= (uint) VertexCount`).
// The refusal precedes every state change: CNA used to clear the current binding first and leave
// the refused buffer as the current one.
#include <gtest/gtest.h>

#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    void ExpectVertexOffsetRefusal(GraphicsDevice& device, const VertexBuffer& buffer, int offset)
    {
        try
        {
            device.SetVertexBuffer(&buffer, offset);
            ADD_FAILURE() << "offset " << offset << " was accepted";
        }
        catch (const System::ArgumentOutOfRangeException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(exception.getParamNameProperty(), "vertexOffset") << "offset " << offset;
        }
    }
}

TEST(SetVertexBufferOffsetTest, OffsetsOutsideTheBufferAreRefusedAndKeepTheCurrentBinding)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no vertex buffers";

    VertexBuffer current(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                         BufferUsage::None);
    VertexBuffer three(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                       BufferUsage::None);
    device.SetVertexBuffer(&current, 1);

    // VertexCount is 3: 3 is the first invalid offset, -1 is invalid as an unsigned 0xFFFFFFFF.
    ExpectVertexOffsetRefusal(device, three, 3);
    ExpectVertexOffsetRefusal(device, three, 4);
    ExpectVertexOffsetRefusal(device, three, -1);

    // Refused before SetVertexBuffers ran: the previous single binding is untouched, and the
    // refused buffer never became the current one.
    EXPECT_EQ(device.GetVertexBuffer(), &current);
    ASSERT_EQ(device.GetVertexBuffers().size(), 1u);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexBufferProperty(), &current);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexOffsetProperty(), 1);
    EXPECT_EQ(device.GetVertexBuffers()[0].getInstanceFrequencyProperty(), 0);

    // The last element is a legal offset and is what the binding then reports.
    device.SetVertexBuffer(&three, 2);
    ASSERT_EQ(device.GetVertexBuffers().size(), 1u);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexBufferProperty(), &three);
    EXPECT_EQ(device.GetVertexBuffers()[0].getVertexOffsetProperty(), 2);

    // A null buffer unbinds without looking at the offset.
    EXPECT_NO_THROW(device.SetVertexBuffer(nullptr, 99));
    EXPECT_TRUE(device.GetVertexBuffers().empty());
}

// The explicit binding constructor and the convenience overload must agree on the boundary.
TEST(SetVertexBufferOffsetTest, ConvenienceOverloadAndBindingConstructorShareTheBoundary)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no vertex buffers";

    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 2,
                        BufferUsage::None);
    for (const int offset : {-1, 0, 1, 2})
    {
        bool bindingAccepted = true;
        try
        {
            (void)VertexBufferBinding(&buffer, offset, 0);
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            bindingAccepted = false;
        }
        bool overloadAccepted = true;
        try
        {
            device.SetVertexBuffer(&buffer, offset);
        }
        catch (const System::ArgumentOutOfRangeException&)
        {
            overloadAccepted = false;
        }
        EXPECT_EQ(overloadAccepted, bindingAccepted) << "offset " << offset;
        EXPECT_EQ(bindingAccepted, offset == 0 || offset == 1) << "offset " << offset;
    }
}

// VertexBufferBinding::.ctor(VertexBuffer, int, int): null buffer (IL_0000-0012,
// ArgumentNullException "vertexBuffer") before the offset (IL_0013-001e -> IL_0046-0050,
// "vertexOffset") before the frequency (IL_0020-002e, "instanceFrequency").
TEST(SetVertexBufferOffsetTest, BindingConstructorNamesEachParameterInCheckOrder)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no vertex buffers";
    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 2,
                        BufferUsage::None);

    try
    {
        (void)VertexBufferBinding(nullptr, -1, -1);
        ADD_FAILURE() << "null buffer accepted";
    }
    catch (const System::ArgumentNullException& exception)
    {
        EXPECT_EQ(exception.getParamNameProperty(), "vertexBuffer");
    }

    const auto expectRange = [&](int offset, int frequency, const char* parameterName)
    {
        try
        {
            (void)VertexBufferBinding(&buffer, offset, frequency);
            ADD_FAILURE() << "accepted offset " << offset << ", frequency " << frequency;
        }
        catch (const System::ArgumentOutOfRangeException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::ArgumentOutOfRangeException));
            EXPECT_EQ(exception.getParamNameProperty(), parameterName)
                << "offset " << offset << ", frequency " << frequency;
        }
    };
    expectRange(2, -1, "vertexOffset");
    expectRange(-1, -1, "vertexOffset");
    expectRange(1, -1, "instanceFrequency");
}
