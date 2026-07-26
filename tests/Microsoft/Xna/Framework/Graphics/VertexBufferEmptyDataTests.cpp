// SPDX-License-Identifier: MS-PL
// REMED-GFX-103: public regression for CNA's zero-element vertex-buffer contract.
//
// A logical zero-capacity VertexBuffer is valid.  Once the public arguments have been
// validated, an empty upload is a true no-op: it must not reach pointer arithmetic, packing,
// shadow copies, or a graphics backend, and must not replace the most recent real upload.

#include <array>
#include <cstdint>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

using CNA::GraphicsCapability;
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

    VertexDeclaration OddStrideDeclaration()
    {
        return VertexDeclaration(
            13,
            {
                VertexElement(
                    0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            });
    }

    class VertexBufferEmptyDataTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void RequireVertexBuffers()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support vertex buffers";
        }
    };
}

TEST_F(VertexBufferEmptyDataTest, ZeroCapacityConstructionPreservesLogicalCapacity)
{
    RequireVertexBuffers();

    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);

    EXPECT_EQ(0, staticBuffer.getVertexCountProperty());
    EXPECT_EQ(0, dynamicBuffer.getVertexCountProperty());
    EXPECT_EQ(0, staticBuffer.GetBackend().GetVertexCount());
    EXPECT_EQ(0, dynamicBuffer.GetBackend().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, ZeroCountAcceptsNullForStaticAndDynamicBuffers)
{
    RequireVertexBuffers();

    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);

    EXPECT_NO_THROW(
        staticBuffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 0));
    EXPECT_NO_THROW(staticBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr), 0, 0));
    EXPECT_NO_THROW(dynamicBuffer.SetData(
        static_cast<const VertexPositionColor*>(nullptr),
        0,
        0,
        SetDataOptions::Discard));
}

TEST_F(VertexBufferEmptyDataTest, EmptyRawAndTypedUploadsDoNotReplaceRealData)
{
    RequireVertexBuffers();

    const VertexPositionColor vertex(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7));
    VertexBuffer typedBuffer(device, PositionColorDeclaration(), 1, BufferUsage::None);
    typedBuffer.SetData(&vertex, 1);
    ASSERT_EQ(1, typedBuffer.GetBackend().GetVertexCount());

    EXPECT_NO_THROW(typedBuffer.SetData(&vertex, 0));
    EXPECT_EQ(1, typedBuffer.GetBackend().GetVertexCount());

    const std::array<std::uint8_t, 13> raw{
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    VertexBuffer rawBuffer(device, OddStrideDeclaration(), 1, BufferUsage::None);
    rawBuffer.SetDataRaw(raw.data(), 1, 13);
    ASSERT_EQ(1, rawBuffer.GetBackend().GetVertexCount());

    EXPECT_NO_THROW(rawBuffer.SetDataRaw(raw.data(), 0, 13));
    EXPECT_EQ(1, rawBuffer.GetBackend().GetVertexCount());
}

TEST_F(VertexBufferEmptyDataTest, NonemptyUploadIntoZeroCapacityIsRejected)
{
    RequireVertexBuffers();

    const VertexPositionColor vertex(Vector3(1.0f, 2.0f, 3.0f), Color(4, 5, 6, 7));
    VertexBuffer staticBuffer(device, PositionColorDeclaration(), 0, BufferUsage::None);
    DynamicVertexBuffer dynamicBuffer(
        device, PositionColorDeclaration(), 0, BufferUsage::None);

    EXPECT_THROW(
        staticBuffer.SetData(&vertex, 1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(
        dynamicBuffer.SetData(&vertex, 0, 1, SetDataOptions::NoOverwrite),
        System::ArgumentOutOfRangeException);
    EXPECT_EQ(0, staticBuffer.getVertexCountProperty());
    EXPECT_EQ(0, dynamicBuffer.getVertexCountProperty());
}
