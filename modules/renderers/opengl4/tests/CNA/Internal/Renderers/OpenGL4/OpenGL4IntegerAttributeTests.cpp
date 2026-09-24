// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0023: an integer-typed GLSL input (`uvecN`/`ivecN`) of a
// ShaderEffect is fed through glVertexAttribIPointer. The declaration-driven layout reads every
// element through the converting float path, which is right for everything XNA itself declares --
// Direct3D 9 vertex inputs are float registers -- and leaves an integer input undefined.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/NotSupportedException.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Color;

    constexpr const char* kVertexSource = R"GLSL(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in uvec4 aTag;
flat out uvec4 vTag;
void main()
{
    gl_Position = vec4(aPosition, 1.0);
    vTag = aTag;
}
)GLSL";

    constexpr const char* kFragmentSource = R"GLSL(#version 410 core
flat in uvec4 vTag;
out vec4 FragColor;
void main()
{
    FragColor = vec4(vTag) / 255.0;
}
)GLSL";

    /// A full-target quad of `stride`-byte records: Position at 0, the tag at 12.
    std::vector<std::uint8_t> QuadRecords(std::size_t stride, const void* tag, std::size_t tagBytes)
    {
        const std::array<std::array<float, 3>, 6> corners{{
            {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
        std::vector<std::uint8_t> bytes(corners.size() * stride, 0u);
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            std::memcpy(bytes.data() + i * stride, corners[i].data(), sizeof(corners[i]));
            std::memcpy(bytes.data() + i * stride + 12, tag, tagBytes);
        }
        return bytes;
    }

    void PrepareState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }
}

TEST(OpenGL4IntegerAttribute, AnIntegerShaderInputReadsTheStoredIntegers)
{
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    PrepareState(device);

    ShaderEffect effect(device, kVertexSource, kFragmentSource);
    ASSERT_TRUE(effect.IsEffectValid());

    const VertexDeclaration declaration(16, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0)});
    const std::array<std::uint8_t, 4> tag{200, 100, 50, 255};
    const std::vector<std::uint8_t> records = QuadRecords(16, tag.data(), tag.size());
    VertexBuffer vertices(device, declaration, 6, BufferUsage::None);
    vertices.SetDataRaw(records.data(), 6, 16);

    RenderTarget2D target(device, 4, 4);
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    effect.Apply();
    device.SetVertexBuffer(&vertices);
    device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    device.SetVertexBuffer(nullptr);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    std::array<Color, 16> pixels{};
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (const Color& pixel : pixels)
    {
        EXPECT_EQ(Color(200, 100, 50, 255), pixel)
            << "a uvec4 input fed through the float path reads undefined values";
    }
}

TEST(OpenGL4IntegerAttribute, AFloatStoredElementForAnIntegerInputIsRefusedByName)
{
    GraphicsDevice device;
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    PrepareState(device);

    ShaderEffect effect(device, kVertexSource, kFragmentSource);
    ASSERT_TRUE(effect.IsEffectValid());

    const VertexDeclaration declaration(28, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector4, VertexElementUsage::BlendIndices, 0)});
    const std::array<float, 4> tag{200.0f, 100.0f, 50.0f, 255.0f};
    const std::vector<std::uint8_t> records = QuadRecords(28, tag.data(), sizeof(tag));
    VertexBuffer vertices(device, declaration, 6, BufferUsage::None);
    vertices.SetDataRaw(records.data(), 6, 28);

    effect.Apply();
    device.SetVertexBuffer(&vertices);
    EXPECT_THROW(device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2),
                 System::NotSupportedException);
    device.SetVertexBuffer(nullptr);
}

#endif // CNA_RENDERER_OPENGL4
