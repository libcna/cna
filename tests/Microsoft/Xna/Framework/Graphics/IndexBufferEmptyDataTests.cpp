// SPDX-License-Identifier: MS-PL
// REMED-GFX-054: public regression for CNA's established empty-index-buffer contract.
//
// Empty model parts are represented by a logical zero-capacity IndexBuffer.  Uploading an empty
// range, including the pointer returned by an empty std::vector (which may be null), is a true
// no-op: it must not mutate the backend's last real upload.  The native allocation, if a backend
// needs a non-zero minimum internally, must not make a non-empty upload legal.

#include <array>
#include <cstdint>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;

namespace
{
    class IndexBufferEmptyDataTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        void RequireIndexBuffers()
        {
            if (!device.SupportsCapability(GraphicsCapability::ThreeD))
                GTEST_SKIP() << "Backend explicitly does not support index buffers";
        }
    };
}

TEST_F(IndexBufferEmptyDataTest, ZeroCapacityConstructionPreservesLogicalCapacity)
{
    RequireIndexBuffers();

    IndexBuffer static16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer static32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);

    EXPECT_EQ(0, static16.getIndexCountProperty());
    EXPECT_EQ(0, static32.getIndexCountProperty());
    EXPECT_EQ(0, dynamic16.getIndexCountProperty());
    EXPECT_EQ(0, dynamic32.getIndexCountProperty());
}

TEST_F(IndexBufferEmptyDataTest, ZeroCountAcceptsNullForStaticAndDynamicBuffers)
{
    RequireIndexBuffers();

    IndexBuffer static16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer static32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    DynamicIndexBuffer dynamic32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);

    EXPECT_NO_THROW(static16.SetData(static_cast<const std::uint16_t*>(nullptr), 0));
    EXPECT_NO_THROW(static32.SetData(static_cast<const std::uint32_t*>(nullptr), 0));
    EXPECT_NO_THROW(dynamic16.SetData(
        static_cast<const std::uint16_t*>(nullptr), 0, 0, SetDataOptions::Discard));
    EXPECT_NO_THROW(dynamic32.SetData(
        static_cast<const std::uint32_t*>(nullptr), 0, 0, SetDataOptions::NoOverwrite));
}

TEST_F(IndexBufferEmptyDataTest, ZeroCountIsANoOpAfterARealUpload)
{
    RequireIndexBuffers();

    const std::array<std::uint16_t, 4> source16{0, 1, 2, 3};
    const std::array<std::uint32_t, 4> source32{10, 11, 12, 13};
    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 4, BufferUsage::None);
    buffer16.SetData(source16.data(), static_cast<int>(source16.size()));
    buffer32.SetData(source32.data(), static_cast<int>(source32.size()));

    ASSERT_EQ(4, buffer16.GetBackend().GetIndexCount());
    ASSERT_EQ(4, buffer32.GetBackend().GetIndexCount());

    EXPECT_NO_THROW(buffer16.SetData(source16.data(), 0));
    EXPECT_NO_THROW(buffer32.SetData(source32.data(), 0));
    EXPECT_NO_THROW(buffer16.SetData(static_cast<const std::uint16_t*>(nullptr), 0));
    EXPECT_NO_THROW(buffer32.SetData(static_cast<const std::uint32_t*>(nullptr), 0));

    EXPECT_EQ(4, buffer16.GetBackend().GetIndexCount());
    EXPECT_EQ(4, buffer32.GetBackend().GetIndexCount());

    std::array<std::uint16_t, 4> result16{};
    std::array<std::uint32_t, 4> result32{};
    buffer16.GetData(result16.data(), static_cast<int>(result16.size()));
    buffer32.GetData(result32.data(), static_cast<int>(result32.size()));
    EXPECT_EQ(source16, result16);
    EXPECT_EQ(source32, result32);
}

TEST_F(IndexBufferEmptyDataTest, NativePaddingDoesNotPermitNonzeroUploadIntoZeroCapacity)
{
    RequireIndexBuffers();

    const std::uint16_t source16 = 0;
    const std::uint32_t source32 = 0;
    IndexBuffer buffer16(device, IndexElementSize::SixteenBits, 0, BufferUsage::None);
    IndexBuffer buffer32(device, IndexElementSize::ThirtyTwoBits, 0, BufferUsage::None);

    EXPECT_ANY_THROW(buffer16.SetData(&source16, 1));
    EXPECT_ANY_THROW(buffer32.SetData(&source32, 1));
    EXPECT_EQ(0, buffer16.getIndexCountProperty());
    EXPECT_EQ(0, buffer32.getIndexCountProperty());
}
