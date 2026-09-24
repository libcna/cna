// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0025: a compute dispatch leaves the draw state it
// touched as it found it. GL texture units and indexed shader-storage bindings are context state
// that compute and graphics share, so a dispatch that installed its own inputs and left them there
// would have the next draw sample the compute pass's texture or read its storage block -- with the
// game's XNA state, set before the dispatch, apparently unchanged. Both cases are drawn here with
// a dispatch between the state and the draw, and the drawn colour says whose binding the draw saw.

#if defined(CNA_RENDERER_OPENGL4)

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Color;

    constexpr const char* kQuadVertex = R"GLSL(#version 430 core
layout(location = 0) in vec3 aPosition;
void main() { gl_Position = vec4(aPosition, 1.0); }
)GLSL";

    constexpr const char* kSampleFragment = R"GLSL(#version 430 core
uniform sampler2D uTexture;
out vec4 FragColor;
void main() { FragColor = texture(uTexture, vec2(0.5, 0.5)); }
)GLSL";

    constexpr const char* kStorageFragment = R"GLSL(#version 430 core
layout(std430, binding = 0) readonly buffer Colour { vec4 colour; };
out vec4 FragColor;
void main() { FragColor = colour; }
)GLSL";

    /// Reads texel (0,0) of unit 0 into binding 0 -- a compute pass with inputs of its own.
    constexpr const char* kReadTexture = R"GLSL(#version 430 core
layout(local_size_x = 1) in;
layout(std430, binding = 0) writeonly buffer Result { vec4 result; };
uniform sampler2D uSource;
void main() { result = texelFetch(uSource, ivec2(0, 0), 0); }
)GLSL";

    /// Writes a constant into binding 0.
    constexpr const char* kWriteBlue = R"GLSL(#version 430 core
layout(local_size_x = 1) in;
layout(std430, binding = 0) writeonly buffer Result { vec4 result; };
void main() { result = vec4(0.0, 0.0, 1.0, 1.0); }
)GLSL";

    class Fixture
    {
    public:
        Fixture()
        {
            if (device_.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4) return;
            if (!device_.GetRenderer().SupportsComputeShadersEXT()) return;
            ready_ = true;
            device_.setBlendStateProperty(BlendState::Opaque);
            device_.setDepthStencilStateProperty(DepthStencilState::None);
            device_.setRasterizerStateProperty(RasterizerState::CullNone);

            const std::array<std::array<float, 3>, 6> corners{{
                {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
            const VertexDeclaration declaration(12, {
                VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
            quad_ = std::make_unique<VertexBuffer>(device_, declaration, 6, BufferUsage::None);
            quad_->SetDataRaw(corners.data(), 6, 12);
            target_ = std::make_unique<RenderTarget2D>(device_, 4, 4);
        }

        [[nodiscard]] bool Ready() const { return ready_; }
        [[nodiscard]] GraphicsDevice& Device() { return device_; }
        [[nodiscard]] CNA::Internal::Renderers::IGraphicsRenderer& Renderer()
        {
            return device_.GetRenderer();
        }

        std::unique_ptr<Texture2D> SolidTexture(const Color& colour)
        {
            auto texture = std::make_unique<Texture2D>(device_, 1, 1);
            texture->SetData(&colour, 1);
            return texture;
        }

        void BeginTarget()
        {
            device_.SetRenderTarget(target_.get());
            device_.Clear(Color::Black);
        }

        /// Draws the full-target quad and returns the target's pixels.
        std::vector<Color> DrawQuad()
        {
            device_.SetVertexBuffer(quad_.get());
            device_.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device_.SetVertexBuffer(nullptr);
            device_.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::vector<Color> pixels(16);
            target_->GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels;
        }

    private:
        GraphicsDevice device_;
        std::unique_ptr<VertexBuffer> quad_;
        std::unique_ptr<RenderTarget2D> target_;
        bool ready_ = false;
    };
}

TEST(OpenGL4ComputeIsolation, ADispatchLeavesTheAppliedEffectsTextureOnItsUnit)
{
    Fixture fixture;
    if (!fixture.Ready()) GTEST_SKIP() << "this run has no OpenGL4 compute";

    const auto red = fixture.SolidTexture(Color::Red);
    const auto green = fixture.SolidTexture(Color::Lime);
    ShaderEffect effect(fixture.Device(), kQuadVertex, kSampleFragment);
    ASSERT_TRUE(effect.IsEffectValid());

    fixture.BeginTarget();
    effect.SetTexture(0, *red);
    effect.Apply();

    // A compute pass sampling a different texture on the same unit, between Apply and the draw.
    auto& renderer = fixture.Renderer();
    auto result = renderer.CreateStorageBuffer(16);
    auto reader = renderer.CreateComputeShader(kReadTexture);
    ASSERT_NE(reader, nullptr);
    ASSERT_TRUE(reader->IsValid()) << reader->GetCompileError();
    reader->BindTexture(0, &green->GetRenderer());
    reader->BindStorageBuffer(0, result.get());
    renderer.DispatchCompute(reader.get(), 1, 1, 1);

    std::array<float, 4> read{};
    result->GetData(read.data(), sizeof(read));
    EXPECT_EQ((std::array<float, 4>{0.0f, 1.0f, 0.0f, 1.0f}), read)
        << "the dispatch did not sample the texture bound for it";

    for (const Color& pixel : fixture.DrawQuad())
        EXPECT_EQ(Color::Red, pixel) << "the draw sampled the compute pass's texture";
}

TEST(OpenGL4ComputeIsolation, ADispatchLeavesTheDrawsStorageBlockBound)
{
    Fixture fixture;
    if (!fixture.Ready()) GTEST_SKIP() << "this run has no OpenGL4 compute";

    auto& renderer = fixture.Renderer();
    auto drawColour = renderer.CreateStorageBuffer(16);
    const std::array<float, 4> red{1.0f, 0.0f, 0.0f, 1.0f};
    drawColour->SetData(red.data(), sizeof(red));
    ShaderEffect effect(fixture.Device(), kQuadVertex, kStorageFragment);
    ASSERT_TRUE(effect.IsEffectValid());

    fixture.BeginTarget();
    effect.Apply();
    renderer.BindStorageBufferForDrawEXT(0, *drawColour);

    // A compute pass writing another buffer through the same binding point.
    auto computeColour = renderer.CreateStorageBuffer(16);
    auto writer = renderer.CreateComputeShader(kWriteBlue);
    ASSERT_NE(writer, nullptr);
    ASSERT_TRUE(writer->IsValid()) << writer->GetCompileError();
    writer->BindStorageBuffer(0, computeColour.get());
    renderer.DispatchCompute(writer.get(), 1, 1, 1);

    std::array<float, 4> written{};
    computeColour->GetData(written.data(), sizeof(written));
    EXPECT_EQ((std::array<float, 4>{0.0f, 0.0f, 1.0f, 1.0f}), written);

    for (const Color& pixel : fixture.DrawQuad())
        EXPECT_EQ(Color::Red, pixel) << "the draw read the compute pass's storage block";
}

#endif // CNA_RENDERER_OPENGL4
