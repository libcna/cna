// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0026: `DrawInstancedPrimitivesBaseInstanceEXT` begins
// the per-instance streams at the caller's first logical instance. No renderer-neutral suite draws
// through it (the shared cases only observe a refusal), so this is its OpenGL4 execution evidence,
// modelled on Vulkan's own check "J" in vulkan_shader_effect_3d_test: one instance drawn from
// instance one must read instance one's record, not instance zero's.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/RendererCapabilityProfile.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Color;

    constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aInstanceColour;
flat out vec4 vColour;
void main()
{
    gl_Position = vec4(aPosition, 1.0);
    vColour = aInstanceColour;
}
)GLSL";

    constexpr const char* kFragment = R"GLSL(#version 410 core
flat in vec4 vColour;
out vec4 FragColor;
void main() { FragColor = vColour; }
)GLSL";

    /// Draws one instance of a full-target quad from @p firstInstance and returns the pixels.
    std::vector<Color> DrawOneInstanceFrom(GraphicsDevice& device, ShaderEffect& effect,
                                           VertexBuffer& quad, VertexBuffer& instances,
                                           IndexBuffer& indices, RenderTarget2D& target,
                                           int firstInstance)
    {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        effect.Apply();
        device.SetVertexBuffers({VertexBufferBinding(&quad, 0, 0),
                                 VertexBufferBinding(&instances, 0, 1)});
        device.setIndicesProperty(&indices);
        device.DrawInstancedPrimitivesBaseInstanceEXT(PrimitiveType::TriangleList, 0, 0, 4, 0, 2,
                                                      1, firstInstance);
        device.setIndicesProperty(nullptr);
        device.SetVertexBuffer(nullptr);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::vector<Color> pixels(16);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels;
    }
}

TEST(OpenGL4BaseInstance, AnInstancedDrawReadsItsStreamsFromTheFirstLogicalInstance)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                          PresentationParameters());
    if (device.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4)
        GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    ASSERT_TRUE(device.SupportsRendererFeatureEXT(CNA::RendererFeature::BaseInstanceDrawing))
        << "a 4.2+ context resolves glDrawElementsInstancedBaseVertexBaseInstance";
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);

    ShaderEffect effect(device, kVertex, kFragment);
    ASSERT_TRUE(effect.IsEffectValid());

    const std::array<std::array<float, 3>, 4> corners{{
        {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    VertexBuffer quad(device,
                      VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                           VertexElementUsage::Position, 0)}),
                      4, BufferUsage::None);
    quad.SetDataRaw(corners.data(), 4, 12);
    const std::array<Color, 3> perInstance{Color::Red, Color::Lime, Color::Blue};
    VertexBuffer instances(device,
                           VertexDeclaration(4, {VertexElement(0, VertexElementFormat::Color,
                                                               VertexElementUsage::Color, 0)}),
                           3, BufferUsage::None);
    // Color is a polymorphic object, not four bytes: the stream receives its packed values.
    std::array<std::uint32_t, 3> packed{};
    for (std::size_t i = 0; i < packed.size(); ++i)
        packed[i] = perInstance[i].getPackedValueProperty();
    instances.SetDataRaw(packed.data(), 3, 4);
    const std::array<std::uint16_t, 6> order{0, 1, 2, 0, 2, 3};
    IndexBuffer indices(device, IndexElementSize::SixteenBits, 6, BufferUsage::None);
    indices.SetData(order.data(), 6);
    RenderTarget2D target(device, 4, 4);

    for (int first = 0; first < 3; ++first)
    {
        for (const Color& pixel :
             DrawOneInstanceFrom(device, effect, quad, instances, indices, target, first))
            EXPECT_EQ(perInstance[static_cast<std::size_t>(first)].getPackedValueProperty(),
                      pixel.getPackedValueProperty())
                << "one instance drawn from logical instance " << first;
    }

    // A negative first instance is refused before native submission. (A first instance past the
    // stream is not: like every XNA draw range it reaches the native API unvalidated on a renderer
    // that reads no host memory for it -- RequiresManagedBufferedDrawRangeValidationEXT.)
    effect.Apply();
    device.SetVertexBuffers({VertexBufferBinding(&quad, 0, 0),
                             VertexBufferBinding(&instances, 0, 1)});
    device.setIndicesProperty(&indices);
    EXPECT_THROW(device.DrawInstancedPrimitivesBaseInstanceEXT(PrimitiveType::TriangleList, 0, 0,
                                                               4, 0, 2, 1, -1),
                 System::ArgumentOutOfRangeException);
    device.setIndicesProperty(nullptr);
    device.SetVertexBuffer(nullptr);
}

#endif // CNA_RENDERER_OPENGL4
