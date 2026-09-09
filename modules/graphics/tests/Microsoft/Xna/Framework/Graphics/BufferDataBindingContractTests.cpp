// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/InvalidOperationException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    const std::array<VertexPositionColor, 3> kVertices{
        VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), Color::Red),
        VertexPositionColor(Vector3(0.0f, 1.0f, 0.0f), Color::Green),
        VertexPositionColor(Vector3(1.0f, -1.0f, 0.0f), Color::Blue)};

    const std::array<VertexPositionColor, 3> kReplacementVertices{
        VertexPositionColor(Vector3(-0.5f, -0.5f, 0.0f), Color::White),
        VertexPositionColor(Vector3(0.0f, 0.5f, 0.0f), Color::White),
        VertexPositionColor(Vector3(0.5f, -0.5f, 0.0f), Color::White)};
}

TEST(BufferDataBindingContractTest, SecondaryBoundVertexBufferRejectsWritesButAllowsReadback)
{
    GraphicsDevice device;
    VertexBuffer first(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                       BufferUsage::None);
    VertexBuffer second(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                        BufferUsage::None);
    first.SetData(kVertices.data(), static_cast<int>(kVertices.size()));
    second.SetData(kVertices.data(), static_cast<int>(kVertices.size()));
    device.SetVertexBuffers({VertexBufferBinding(&first), VertexBufferBinding(&second)});

    EXPECT_THROW(second.SetData(kReplacementVertices.data(),
                                static_cast<int>(kReplacementVertices.size())),
                 System::InvalidOperationException);
    std::array<VertexPositionColor, 3> actual{};
    EXPECT_NO_THROW(second.GetData(actual.data(), static_cast<int>(actual.size())));
    EXPECT_EQ(actual, kVertices);

    device.SetVertexBuffers({});
    EXPECT_NO_THROW(second.SetData(kReplacementVertices.data(),
                                   static_cast<int>(kReplacementVertices.size())));
}

TEST(BufferDataBindingContractTest, BoundIndexBufferRejectsWritesButAllowsReadback)
{
    GraphicsDevice device;
    IndexBuffer buffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::array<std::uint16_t, 3> initial{0, 1, 2};
    const std::array<std::uint16_t, 3> replacement{2, 1, 0};
    buffer.SetData(initial.data(), static_cast<int>(initial.size()));
    device.SetIndexBuffer(&buffer);

    EXPECT_THROW(buffer.SetData(replacement.data(), static_cast<int>(replacement.size())),
                 System::InvalidOperationException);
    std::array<std::uint16_t, 3> actual{};
    EXPECT_NO_THROW(buffer.GetData(actual.data(), static_cast<int>(actual.size())));
    EXPECT_EQ(actual, initial);

    device.SetIndexBuffer(nullptr);
    EXPECT_NO_THROW(buffer.SetData(replacement.data(), static_cast<int>(replacement.size())));
}

TEST(BufferDataBindingContractTest, BoundDynamicVertexBufferHonorsStreamingOptionExemptions)
{
    GraphicsDevice device;
    DynamicVertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                               BufferUsage::None);
    buffer.SetData(kVertices.data(), 0, static_cast<int>(kVertices.size()),
                   SetDataOptions::Discard);
    device.SetVertexBuffer(&buffer);

    EXPECT_THROW(buffer.SetData(kReplacementVertices.data(), 0,
                                static_cast<int>(kReplacementVertices.size()),
                                SetDataOptions::None),
                 System::InvalidOperationException);
    EXPECT_NO_THROW(buffer.SetData(kReplacementVertices.data(), 0,
                                   static_cast<int>(kReplacementVertices.size()),
                                   SetDataOptions::Discard));
    EXPECT_NO_THROW(buffer.SetData(kVertices.data(), 0, static_cast<int>(kVertices.size()),
                                   SetDataOptions::NoOverwrite));
}

TEST(BufferDataBindingContractTest, BoundDynamicIndexBufferHonorsStreamingOptionExemptions)
{
    GraphicsDevice device;
    DynamicIndexBuffer buffer(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    const std::array<std::uint16_t, 3> initial{0, 1, 2};
    const std::array<std::uint16_t, 3> replacement{2, 1, 0};
    buffer.SetData(initial.data(), 0, static_cast<int>(initial.size()),
                   SetDataOptions::Discard);
    device.SetIndexBuffer(&buffer);

    EXPECT_THROW(buffer.SetData(replacement.data(), 0, static_cast<int>(replacement.size()),
                                SetDataOptions::None),
                 System::InvalidOperationException);
    EXPECT_NO_THROW(buffer.SetData(replacement.data(), 0,
                                   static_cast<int>(replacement.size()),
                                   SetDataOptions::Discard));
    EXPECT_NO_THROW(buffer.SetData(initial.data(), 0, static_cast<int>(initial.size()),
                                   SetDataOptions::NoOverwrite));
}
