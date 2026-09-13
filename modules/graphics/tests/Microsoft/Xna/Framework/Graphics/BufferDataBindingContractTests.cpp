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
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicIndexBuffer;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::SetDataOptions;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexBufferBinding;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    template<typename TException, typename TCallable>
    void ExpectExactNamedException(TCallable&& callable, const char* parameterName)
    {
        try
        {
            callable();
            FAIL() << "expected " << typeid(TException).name();
        }
        catch (const TException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(TException));
            EXPECT_EQ(exception.getParamNameProperty(), parameterName);
        }
        catch (...)
        {
            FAIL() << "unexpected exception type; expected " << typeid(TException).name();
        }
    }

    struct CompactVertex
    {
        float X;
        float Y;
    };

    VertexDeclaration CompactVertexDeclaration()
    {
        return VertexDeclaration(
            8,
            {VertexElement(
                0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0)});
    }

    template<typename TException, typename TCallable>
    void ExpectExactException(TCallable&& callable)
    {
        try
        {
            callable();
            FAIL() << "expected " << typeid(TException).name();
        }
        catch (const TException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(TException));
        }
        catch (...)
        {
            FAIL() << "unexpected exception type; expected " << typeid(TException).name();
        }
    }

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

TEST(BufferDataBindingContractTest, DynamicStreamingOptionsUseXnaBitPrecedence)
{
    GraphicsDevice device;
    DynamicVertexBuffer vertexBuffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    DynamicIndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    VertexPositionColor vertex(Vector3::Zero, Color::White);
    std::uint16_t index = 0;
    vertexBuffer.SetData(&vertex, 0, 1, SetDataOptions::Discard);
    indexBuffer.SetData(&index, 0, 1, SetDataOptions::Discard);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    const SetDataOptions combined = SetDataOptions::Discard | SetDataOptions::NoOverwrite;
    EXPECT_EQ(static_cast<SetDataOptions>(3), combined);
    EXPECT_EQ(SetDataOptions::Discard, combined & SetDataOptions::Discard);
    EXPECT_EQ(SetDataOptions::NoOverwrite, combined & SetDataOptions::NoOverwrite);
    SetDataOptions assigned = SetDataOptions::Discard;
    assigned |= SetDataOptions::NoOverwrite;
    EXPECT_EQ(combined, assigned);
    assigned &= SetDataOptions::NoOverwrite;
    EXPECT_EQ(SetDataOptions::NoOverwrite, assigned);
    EXPECT_EQ(combined, (~SetDataOptions::None) & combined);

    for (const SetDataOptions options : {
             combined,
             static_cast<SetDataOptions>(5),
             static_cast<SetDataOptions>(6)})
    {
        EXPECT_NO_THROW(vertexBuffer.SetData(&vertex, 0, 1, options));
        EXPECT_NO_THROW(indexBuffer.SetData(&index, 0, 1, options));
    }

    EXPECT_THROW(vertexBuffer.SetData(&vertex, 0, 1, static_cast<SetDataOptions>(4)),
                 System::InvalidOperationException);
    EXPECT_THROW(indexBuffer.SetData(&index, 0, 1, static_cast<SetDataOptions>(4)),
                 System::InvalidOperationException);
}

TEST(BufferDataBindingContractTest, GenericDynamicVertexCopyUsesClassicValidation)
{
    GraphicsDevice device;
    DynamicVertexBuffer buffer(
        device, CompactVertexDeclaration(), 1, BufferUsage::None);
    CompactVertex vertex{0.0f, 0.0f};

    ExpectExactNamedException<System::ArgumentNullException>(
        [&] {
            buffer.SetData(
                static_cast<const CompactVertex*>(nullptr), 0, 0, SetDataOptions::None);
        },
        "data");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { buffer.SetData(&vertex, 0, 0, SetDataOptions::None); }, "elementCount");

    buffer.SetData(&vertex, 0, 1, SetDataOptions::Discard);
    device.SetVertexBuffer(&buffer);
    ExpectExactException<System::InvalidOperationException>(
        [&] { buffer.SetData(&vertex, 0, 1, SetDataOptions::None); });
    ExpectExactException<System::InvalidOperationException>(
        [&] { buffer.SetData(&vertex, -1, 0, SetDataOptions::None); });
    EXPECT_NO_THROW(buffer.SetData(&vertex, 0, 1, SetDataOptions::Discard));
}

TEST(BufferDataBindingContractTest, ClassicCopiesRejectNullAndZeroCountExactly)
{
    GraphicsDevice device;
    VertexBuffer vertexBuffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    DynamicVertexBuffer dynamicVertexBuffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    DynamicIndexBuffer dynamicIndexBuffer(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    VertexPositionColor vertex(Vector3::Zero, Color::White);
    std::uint16_t index = 0;

    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { vertexBuffer.SetData(static_cast<const VertexPositionColor*>(nullptr), 0); },
        "data");
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { indexBuffer.SetData(static_cast<const std::uint16_t*>(nullptr), 0); },
        "data");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { vertexBuffer.SetData(&vertex, 0); }, "elementCount");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { indexBuffer.SetData(&index, 0); }, "elementCount");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] {
            dynamicVertexBuffer.SetData(
                &vertex, 0, 0, SetDataOptions::Discard);
        },
        "elementCount");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] {
            dynamicIndexBuffer.SetData(
                &index, 0, 0, SetDataOptions::NoOverwrite);
        },
        "elementCount");
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { vertexBuffer.GetData(static_cast<VertexPositionColor*>(nullptr), 0); },
        "data");
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { indexBuffer.GetData(static_cast<std::uint16_t*>(nullptr), 0); },
        "data");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { vertexBuffer.GetData(&vertex, 0); }, "elementCount");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { indexBuffer.GetData(&index, 0); }, "elementCount");
}

TEST(BufferDataBindingContractTest, ResourceStatePrecedesCopyWindowValidation)
{
    GraphicsDevice device;
    VertexBuffer vertexBuffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    VertexPositionColor vertex(Vector3::Zero, Color::White);
    std::uint16_t index = 0;
    vertexBuffer.SetData(&vertex, 1);
    indexBuffer.SetData(&index, 1);
    device.SetVertexBuffer(&vertexBuffer);
    device.SetIndexBuffer(&indexBuffer);

    EXPECT_THROW(vertexBuffer.SetData(&vertex, -1, 0),
                 System::InvalidOperationException);
    EXPECT_THROW(indexBuffer.SetData(&index, -1, 0),
                 System::InvalidOperationException);

    device.SetVertexBuffer(nullptr);
    device.SetIndexBuffer(nullptr);
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { vertexBuffer.SetData(&vertex, -1, 1); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { indexBuffer.SetData(&index, -1, 1); }, "dataIndex");

    VertexBuffer writeOnlyVertex(
        device, VertexPositionColor::getVertexDeclarationStatic(), 1,
        BufferUsage::WriteOnly);
    IndexBuffer writeOnlyIndex(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::WriteOnly);
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { writeOnlyVertex.GetData(static_cast<VertexPositionColor*>(nullptr), -1, 0); },
        "data");
    ExpectExactNamedException<System::ArgumentNullException>(
        [&] { writeOnlyIndex.GetData(static_cast<std::uint16_t*>(nullptr), -1, 0); },
        "data");
    EXPECT_THROW(writeOnlyVertex.GetData(&vertex, -1, 0),
                 System::NotSupportedException);
    EXPECT_THROW(writeOnlyIndex.GetData(&index, -1, 0),
                 System::NotSupportedException);
}

TEST(BufferDataBindingContractTest, CopyWindowPrecedesBufferOffsetAndUsesResourceSizeFailure)
{
    GraphicsDevice device;
    VertexBuffer vertexBuffer(
        device, VertexPositionColor::getVertexDeclarationStatic(), 1, BufferUsage::None);
    IndexBuffer indexBuffer(
        device, IndexElementSize::SixteenBits, 1, BufferUsage::None);
    VertexPositionColor vertex(Vector3::Zero, Color::White);
    std::uint16_t index = 0;

    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { vertexBuffer.SetData(-64, &vertex, -1, 1, 16); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { indexBuffer.SetData(-64, &index, -1, 1); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { vertexBuffer.GetData(-64, &vertex, -1, 1, 16); }, "dataIndex");
    ExpectExactNamedException<System::ArgumentOutOfRangeException>(
        [&] { indexBuffer.GetData(-64, &index, -1, 1); }, "dataIndex");

    ExpectExactException<System::InvalidOperationException>(
        [&] { vertexBuffer.SetData(-64, &vertex, 0, 1, 16); });
    ExpectExactException<System::InvalidOperationException>(
        [&] { indexBuffer.SetData(-64, &index, 0, 1); });
    ExpectExactException<System::InvalidOperationException>(
        [&] { vertexBuffer.GetData(-64, &vertex, 0, 1, 16); });
    ExpectExactException<System::InvalidOperationException>(
        [&] { indexBuffer.GetData(-64, &index, 0, 1); });
}
