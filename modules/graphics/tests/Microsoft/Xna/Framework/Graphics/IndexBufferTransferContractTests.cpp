// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;

TEST(IndexBufferTransferContractTest, GenericSignedSliceAndDestinationStartIndexRoundTrip)
{
    GraphicsDevice device;
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::array<std::int16_t, 4> source{99, 11, 22, 33};
    buffer.SetData(source.data(), 1, 3);

    std::array<std::int16_t, 5> destination{-7, -7, -7, -7, -7};
    buffer.GetData(destination.data(), 2, 3);

    EXPECT_EQ(destination[0], -7);
    EXPECT_EQ(destination[1], -7);
    EXPECT_EQ(destination[2], 11);
    EXPECT_EQ(destination[3], 22);
    EXPECT_EQ(destination[4], 33);
}

TEST(IndexBufferTransferContractTest, GenericByteWindowsPermitUnalignedOffsets)
{
    GraphicsDevice device;
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    const std::array<std::uint8_t, 8> initial{0, 1, 2, 3, 4, 5, 6, 7};
    const std::array<std::uint8_t, 4> replacement{90, 91, 92, 93};
    buffer.SetData(initial.data(), static_cast<int>(initial.size()));
    buffer.SetData(1, replacement.data(), 1, 3);

    std::array<std::uint8_t, 8> whole{};
    buffer.GetData(whole.data(), static_cast<int>(whole.size()));
    EXPECT_EQ(whole, (std::array<std::uint8_t, 8>{0, 91, 92, 93, 4, 5, 6, 7}));

    std::array<std::uint8_t, 7> window{0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
    buffer.GetData(1, window.data(), 2, 3);
    EXPECT_EQ(window,
              (std::array<std::uint8_t, 7>{0xEE, 0xEE, 91, 92, 93, 0xEE, 0xEE}));
}

TEST(IndexBufferTransferContractTest, DynamicBufferKeepsInheritedAndWindowedStreamingOverloads)
{
    GraphicsDevice device;
    DynamicIndexBuffer buffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    const std::array<std::uint16_t, 4> initial{10, 20, 30, 40};
    buffer.SetData(initial.data(), static_cast<int>(initial.size()));
    device.SetIndexBuffer(&buffer);

    const std::array<std::int16_t, 3> replacement{111, 222, 333};
    EXPECT_NO_THROW(buffer.SetData(2, replacement.data(), 1, 2,
                                   SetDataOptions::NoOverwrite));

    std::array<std::int16_t, 4> result{};
    buffer.GetData(result.data(), static_cast<int>(result.size()));
    EXPECT_EQ(result, (std::array<std::int16_t, 4>{10, 222, 333, 40}));
}

TEST(IndexBufferTransferContractTest, OrdinaryGenericWindowStillRejectsABoundBuffer)
{
    GraphicsDevice device;
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::array<std::int16_t, 3> initial{0, 1, 2};
    buffer.SetData(initial.data(), static_cast<int>(initial.size()));
    device.SetIndexBuffer(&buffer);

    const std::array<std::uint8_t, 1> byte{9};
    EXPECT_THROW(buffer.SetData(1, byte.data(), 0, 1),
                 System::InvalidOperationException);
}

TEST(IndexBufferTransferContractTest, TransferElementTypeIsIndependentOfDrawIndexWidth)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);

    IndexBuffer sixteenBitBuffer(
        device, IndexElementSize::SixteenBits, 4, BufferUsage::None);
    const std::array<std::uint32_t, 2> wideSource{0x11223344U, 0x55667788U};
    EXPECT_NO_THROW(sixteenBitBuffer.SetData(
        wideSource.data(), static_cast<int>(wideSource.size())));
    std::array<std::uint32_t, 2> wideResult{};
    EXPECT_NO_THROW(sixteenBitBuffer.GetData(
        wideResult.data(), static_cast<int>(wideResult.size())));
    EXPECT_EQ(wideResult, wideSource);

    IndexBuffer thirtyTwoBitBuffer(
        device, IndexElementSize::ThirtyTwoBits, 2, BufferUsage::None);
    const std::array<std::uint16_t, 4> narrowSource{1, 2, 3, 4};
    EXPECT_NO_THROW(thirtyTwoBitBuffer.SetData(
        narrowSource.data(), static_cast<int>(narrowSource.size())));
    std::array<std::uint16_t, 4> narrowResult{};
    EXPECT_NO_THROW(thirtyTwoBitBuffer.GetData(
        narrowResult.data(), static_cast<int>(narrowResult.size())));
    EXPECT_EQ(narrowResult, narrowSource);
}

TEST(IndexBufferTransferContractTest, DynamicTypedTransferAlsoIgnoresDrawIndexWidth)
{
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    DynamicIndexBuffer buffer(
        device, IndexElementSize::ThirtyTwoBits, 2, BufferUsage::None);
    const std::array<std::uint16_t, 4> source{10, 20, 30, 40};

    EXPECT_NO_THROW(buffer.SetData(
        source.data(), 0, 4, SetDataOptions::Discard));
    std::array<std::uint16_t, 4> result{};
    EXPECT_NO_THROW(buffer.GetData(result.data(), static_cast<int>(result.size())));
    EXPECT_EQ(result, source);
}

TEST(IndexBufferTransferContractTest, BufferCapacityFailuresUseInvalidOperationAndPreserveData)
{
    GraphicsDevice device;
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 2, BufferUsage::None);
    const std::array<std::uint16_t, 2> initial{12, 34};
    buffer.SetData(initial.data(), static_cast<int>(initial.size()));

    const std::array<std::uint16_t, 3> oversized{90, 91, 92};
    EXPECT_THROW(buffer.SetData(oversized.data(), static_cast<int>(oversized.size())),
                 System::InvalidOperationException);
    std::array<std::uint16_t, 3> destination{};
    EXPECT_THROW(buffer.GetData(destination.data(), static_cast<int>(destination.size())),
                 System::InvalidOperationException);

    std::array<std::uint16_t, 2> result{};
    buffer.GetData(result.data(), static_cast<int>(result.size()));
    EXPECT_EQ(result, initial);
}
