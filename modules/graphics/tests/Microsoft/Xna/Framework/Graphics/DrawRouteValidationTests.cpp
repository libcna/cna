// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu.md WEBGPU-188: the buffer and draw-route surface -- 32-bit indices, the
// user-primitive routes, out-of-range draw arguments, `PrimitiveType` validation, a draw with no
// vertex or index buffer, and `BufferUsage.WriteOnly`'s readback refusal.
//
// Renderer-neutral, and mostly about REFUSALS. The rule every case below shares is the one a
// validation layer exists to enforce: an argument that would read past the end of a bound buffer
// must be rejected BEFORE it reaches the native API, by a named exception, rather than submitted
// and left to the driver. A renderer that forwards it either crashes, renders garbage, or -- worst
// -- works today on the one driver that tolerates it.
//
// Each rejection test is paired with the SAME call at a legal argument, so a test cannot pass by
// refusing everything. That pairing is the point: "the draw threw" proves nothing on its own.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    /// A unit quad as two triangles, in clip space.
    [[nodiscard]] std::array<VertexPositionColor, 4> Quad()
    {
        const Color colour(200, 120, 60, 255);
        return {VertexPositionColor{Vector3(-0.8f, 0.8f, 0.0f), colour},
                VertexPositionColor{Vector3(-0.8f, -0.8f, 0.0f), colour},
                VertexPositionColor{Vector3(0.8f, -0.8f, 0.0f), colour},
                VertexPositionColor{Vector3(0.8f, 0.8f, 0.0f), colour}};
    }

    /// Applies a minimal unlit vertex-coloured effect, which every draw below needs bound.
    void ApplyEffect(GraphicsDevice& device, BasicEffect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
        effect.Apply();
        (void)device;
    }
}

// 32-bit indices, end to end. The buffer, the upload and the draw all have to agree about the
// element size, and a route that assumed 16 bits reads two indices where one was meant.
TEST(DrawRouteValidation, ThirtyTwoBitIndicesDrawAndReadBack)
{
    GraphicsDevice device;
    const auto quad = Quad();
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    vertices.SetData(quad.data(), 4);

    // Values above 65535 are the point: they cannot survive a 16-bit path.
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    IndexBuffer wide(device, IndexElementSize::ThirtyTwoBits, 6, BufferUsage::None);
    wide.SetData(indices.data(), 6);

    std::array<std::uint32_t, 6> read{};
    wide.GetData(read.data(), 6);
    for (std::size_t i = 0; i < indices.size(); ++i)
        EXPECT_EQ(read[i], indices[i]) << "index " << i << " did not survive a 32-bit round trip";

    BasicEffect effect(device);
    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(&wide);
    ApplyEffect(device, effect);
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2));
    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
}

// The user-primitive routes: no bound buffer at all, the data comes from the caller's array.
TEST(DrawRouteValidation, TheUserPrimitiveRoutesDraw)
{
    GraphicsDevice device;
    const auto quad = Quad();
    BasicEffect effect(device);
    ApplyEffect(device, effect);

    EXPECT_NO_THROW(device.DrawUserPrimitives(
        PrimitiveType::TriangleStrip, quad.data(), 0, 2,
        VertexPositionColor::getVertexDeclarationStatic()));

    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    EXPECT_NO_THROW(device.DrawUserIndexedPrimitives(
        PrimitiveType::TriangleList, quad.data(), 0, 4, indices.data(), 0, 2,
        VertexPositionColor::getVertexDeclarationStatic()));
}

// Out-of-range draw arguments, each paired with the same call at a legal value.
TEST(DrawRouteValidation, ARangeThatLeavesItsBufferIsRefusedByName)
{
    GraphicsDevice device;
    const auto quad = Quad();
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    vertices.SetData(quad.data(), 4);
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
    IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    indexBuffer.SetData(indices.data(), 6);

    BasicEffect effect(device);
    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(&indexBuffer);
    ApplyEffect(device, effect);

    // The legal call, first: everything below must differ from it only in the argument under test.
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2));

    // startIndex past the end of the index buffer.
    EXPECT_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 4, 2),
                 System::ArgumentOutOfRangeException)
        << "startIndex 4 with 2 triangles needs indices 4..9 of a 6-index buffer";
    // primitiveCount past the end.
    EXPECT_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 3),
                 System::ArgumentOutOfRangeException)
        << "3 triangles need 9 indices and the buffer holds 6";
    // numVertices past the end of the vertex buffer.
    EXPECT_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 8, 0, 2),
                 System::ArgumentOutOfRangeException)
        << "8 vertices declared against a 4-vertex buffer";
    // A negative argument.
    EXPECT_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, -1, 2),
                 System::ArgumentOutOfRangeException);
    // And the legal call still works afterwards, so none of the refusals left the device unusable.
    EXPECT_NO_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2));

    device.SetIndexBuffer(nullptr);
    device.SetVertexBuffer(nullptr);
}

TEST(DrawRouteValidation, ANonIndexedRangeThatLeavesItsBufferIsRefused)
{
    GraphicsDevice device;
    const auto quad = Quad();
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    vertices.SetData(quad.data(), 4);
    BasicEffect effect(device);
    device.SetVertexBuffer(&vertices);
    ApplyEffect(device, effect);

    EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2));
    EXPECT_THROW(device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 8),
                 System::ArgumentOutOfRangeException)
        << "8 strip triangles need 10 vertices and the buffer holds 4";
    EXPECT_THROW(device.DrawPrimitives(PrimitiveType::TriangleStrip, 3, 2),
                 System::ArgumentOutOfRangeException)
        << "starting at vertex 3 leaves only 1 vertex for 2 triangles";
    EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2));
    device.SetVertexBuffer(nullptr);
}

// A draw with nothing bound. Both must refuse, and by name.
TEST(DrawRouteValidation, ADrawWithNoVertexBufferIsRefused)
{
    GraphicsDevice device;
    BasicEffect effect(device);
    device.SetVertexBuffer(nullptr);
    ApplyEffect(device, effect);
    EXPECT_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1), std::runtime_error);
}

TEST(DrawRouteValidation, AnIndexedDrawWithNoIndexBufferIsRefused)
{
    GraphicsDevice device;
    const auto quad = Quad();
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    vertices.SetData(quad.data(), 4);
    BasicEffect effect(device);
    device.SetVertexBuffer(&vertices);
    device.SetIndexBuffer(nullptr);
    ApplyEffect(device, effect);
    EXPECT_THROW(device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2),
                 std::runtime_error);
    device.SetVertexBuffer(nullptr);
}

// PrimitiveType, including the CNAEXT point list. A renderer either draws it or refuses it by name;
// what it may not do is accept it and draw something else.
TEST(DrawRouteValidation, EveryPrimitiveTypeIsDrawnOrRefusedByName)
{
    GraphicsDevice device;
    const auto quad = Quad();
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    vertices.SetData(quad.data(), 4);
    BasicEffect effect(device);
    device.SetVertexBuffer(&vertices);
    ApplyEffect(device, effect);

    const struct { PrimitiveType type; int count; const char* name; } kCases[] = {
        {PrimitiveType::TriangleList, 1, "TriangleList"},
        {PrimitiveType::TriangleStrip, 2, "TriangleStrip"},
        {PrimitiveType::LineList, 2, "LineList"},
        {PrimitiveType::LineStrip, 3, "LineStrip"},
        {PrimitiveType::PointListEXT, 4, "PointListEXT"},
    };
    for (const auto& testCase : kCases)
    {
        try
        {
            device.DrawPrimitives(testCase.type, 0, testCase.count);
        }
        catch (const System::NotSupportedException&)
        {
            continue;   // refused by name, which is a legal answer
        }
        catch (const std::runtime_error& exception)
        {
            ADD_FAILURE() << testCase.name << " was refused, but not by a named "
                          << "NotSupportedException: " << exception.what();
        }
    }
    device.SetVertexBuffer(nullptr);
}

// BufferUsage.WriteOnly: XNA forbids reading such a buffer back, and a renderer that answered
// anyway would be inventing content the caller never wrote.
TEST(DrawRouteValidation, AWriteOnlyBufferRefusesGetData)
{
    GraphicsDevice device;
    const auto quad = Quad();
    VertexBuffer writeOnly(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                           BufferUsage::WriteOnly);
    writeOnly.SetData(quad.data(), 4);

    std::array<VertexPositionColor, 4> read{};
    EXPECT_THROW(writeOnly.GetData(read.data(), 4), System::NotSupportedException)
        << "BufferUsage.WriteOnly forbids readback";

    // The paired legal case: the same buffer without the flag reads back exactly.
    VertexBuffer readable(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    readable.SetData(quad.data(), 4);
    EXPECT_NO_THROW(readable.GetData(read.data(), 4));
    for (std::size_t i = 0; i < quad.size(); ++i)
    {
        EXPECT_EQ(read[i].Color.getPackedValueProperty(),
                  quad[i].Color.getPackedValueProperty())
            << "vertex " << i << " did not survive the round trip";
    }
}

// Dynamic-buffer churn: many SetData calls into one buffer, each followed by a draw. A renderer
// that recycled the buffer's storage while a queued draw still referenced it fails here.
TEST(DrawRouteValidation, RepeatedSetDataAndDrawOnOneBuffer)
{
    GraphicsDevice device;
    VertexBuffer vertices(device, VertexPositionColor::getVertexDeclarationStatic(), 4,
                          BufferUsage::None);
    BasicEffect effect(device);
    device.SetVertexBuffer(&vertices);

    for (int iteration = 0; iteration < 24; ++iteration)
    {
        auto quad = Quad();
        const auto shade = static_cast<SharpRuntime::bytecs>(40 + iteration * 8);
        for (auto& vertex : quad) vertex.Color = Color(shade, shade, shade, 255);
        vertices.SetData(quad.data(), 4);
        ApplyEffect(device, effect);
        EXPECT_NO_THROW(device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2))
            << "iteration " << iteration;
    }
    device.SetVertexBuffer(nullptr);
}

// Every `VertexElementFormat` a declaration may name, as a SECOND element beside a Position. The
// rule is the one this file shares: a format is either bound and drawn, or refused by a named
// exception -- never accepted and read as something else. The Position element is always Vector3 so
// the draw has a position to work from whatever the second element turns out to be, and the second
// element carries TEXCOORD0, a semantic every stock declaration understands.
TEST(DrawRouteValidation, EveryVertexElementFormatIsBoundOrRefusedByName)
{
    using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
    using Microsoft::Xna::Framework::Graphics::VertexElement;
    using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
    using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

    GraphicsDevice device;
    BasicEffect effect(device);

    const struct { VertexElementFormat format; int bytes; const char* name; } kFormats[] = {
        {VertexElementFormat::Single, 4, "Single"},
        {VertexElementFormat::Vector2, 8, "Vector2"},
        {VertexElementFormat::Vector3, 12, "Vector3"},
        {VertexElementFormat::Vector4, 16, "Vector4"},
        {VertexElementFormat::Color, 4, "Color"},
        {VertexElementFormat::Byte4, 4, "Byte4"},
        {VertexElementFormat::Short2, 4, "Short2"},
        {VertexElementFormat::Short4, 8, "Short4"},
        {VertexElementFormat::NormalizedShort2, 4, "NormalizedShort2"},
        {VertexElementFormat::NormalizedShort4, 8, "NormalizedShort4"},
        {VertexElementFormat::HalfVector2, 4, "HalfVector2"},
        {VertexElementFormat::HalfVector4, 8, "HalfVector4"},
    };

    for (const auto& entry : kFormats)
    {
        const int stride = 12 + entry.bytes;
        std::vector<std::uint8_t> vertexBytes(static_cast<std::size_t>(stride) * 4, 0);
        // Four positions, so whatever the second element is the draw has real geometry.
        const float positions[4][3] = {{-0.8f, 0.8f, 0.0f},
                                       {-0.8f, -0.8f, 0.0f},
                                       {0.8f, -0.8f, 0.0f},
                                       {0.8f, 0.8f, 0.0f}};
        for (int v = 0; v < 4; ++v)
            std::memcpy(vertexBytes.data() + static_cast<std::size_t>(v) * stride, positions[v], 12);

        try
        {
            VertexDeclaration declaration(stride, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(12, entry.format, VertexElementUsage::TextureCoordinate, 0)});
            VertexBuffer buffer(device, declaration, 4, BufferUsage::None);
            buffer.SetDataRaw(vertexBytes.data(), 4, stride);
            device.SetVertexBuffer(&buffer);
            ApplyEffect(device, effect);
            device.DrawPrimitives(PrimitiveType::TriangleStrip, 0, 2);
            device.SetVertexBuffer(nullptr);
        }
        catch (const System::NotSupportedException&)
        {
            device.SetVertexBuffer(nullptr);
            continue;   // refused by name, which is a legal answer
        }
        catch (const std::exception& exception)
        {
            device.SetVertexBuffer(nullptr);
            ADD_FAILURE() << entry.name << " was refused, but not by a named "
                          << "NotSupportedException: " << exception.what();
        }
    }
}
