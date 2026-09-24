// SPDX-License-Identifier: MS-PL
//
// plans/plan_opengl4_modern_graphics.md GL4-0037: the two Texture2DArray rules OpenGL4 keeps that
// the renderer-neutral suite cannot assert for every renderer.
//
//  * Texture2DArrayUsage::Filterable is the declaration that linear or mip filtering may be used.
//    GL would filter a non-Filterable array anyway, but Vulkan refuses the draw, so code that
//    worked here would fail there; OpenGL4 refuses the same draw the same way, and a Point sampler
//    -- or a Filterable declaration -- draws.
//  * An array bound with SetTextureArrayEXT belongs to that effect, as on Vulkan and WebGPU, not to
//    whichever effect bound GL texture unit N last: two effects with different arrays on unit 0,
//    drawn alternately, each sample their own.

#if defined(CNA_RENDERER_OPENGL4) && defined(CNA_CNAEXT)

#include "CNA/Graphics/Texture2DArray.hpp"
#include "CNA/GraphicsRendererType.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "System/NotSupportedException.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
    using namespace Microsoft::Xna::Framework::Graphics;
    using CNA::Graphics::Texture2DArray;
    using CNA::Graphics::Texture2DArrayDescriptor;
    using CNA::Graphics::Texture2DArrayUsage;
    using Microsoft::Xna::Framework::Color;

    constexpr const char* kVertex = R"GLSL(#version 410 core
layout(location = 0) in vec3 aPos;
void main() { gl_Position = vec4(aPos.xy, 0.0, 1.0); }
)GLSL";

    constexpr const char* kFragment = R"GLSL(#version 410 core
out vec4 FragColor;
uniform sampler2DArray uArray;
void main() { FragColor = texture(uArray, vec3(0.5, 0.5, 0.0)); }
)GLSL";

    class Fixture
    {
    public:
        Fixture()
            : device_(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                      PresentationParameters())
        {
            if (device_.GetGraphicsRendererType() != CNA::GraphicsRendererType::OpenGL4) return;
            ready_ = true;
            device_.setBlendStateProperty(BlendState::Opaque);
            device_.setDepthStencilStateProperty(DepthStencilState::None);
            device_.setRasterizerStateProperty(RasterizerState::CullNone);
            const std::array<std::array<float, 3>, 6> corners{{
                {-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
            quad_ = std::make_unique<VertexBuffer>(
                device_,
                VertexDeclaration(12, {VertexElement(0, VertexElementFormat::Vector3,
                                                     VertexElementUsage::Position, 0)}),
                6, BufferUsage::None);
            quad_->SetDataRaw(corners.data(), 6, 12);
            target_ = std::make_unique<RenderTarget2D>(device_, 2, 2);
        }

        [[nodiscard]] bool Ready() const { return ready_; }
        [[nodiscard]] GraphicsDevice& Device() { return device_; }

        std::unique_ptr<Texture2DArray> SolidArray(const Color& colour, Texture2DArrayUsage usage)
        {
            auto array = std::make_unique<Texture2DArray>(
                device_, Texture2DArrayDescriptor(1, 1, 1, 1, SurfaceFormat::Color,
                                                  usage | Texture2DArrayUsage::TransferDestination));
            const std::uint32_t texel = colour.getPackedValueProperty();
            array->setData(0, 0, nullptr, &texel, 4);
            return array;
        }

        /// Draws through @p effect and returns the target's first pixel, packed.
        std::uint32_t Draw(ShaderEffect& effect)
        {
            device_.SetRenderTarget(target_.get());
            device_.Clear(Color::Black);
            effect.Apply();
            device_.SetVertexBuffer(quad_.get());
            device_.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            device_.SetVertexBuffer(nullptr);
            device_.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::array<Color, 4> pixels{};
            target_->GetData(pixels.data(), static_cast<int>(pixels.size()));
            return pixels[0].getPackedValueProperty();
        }

    private:
        GraphicsDevice device_;
        std::unique_ptr<VertexBuffer> quad_;
        std::unique_ptr<RenderTarget2D> target_;
        bool ready_ = false;
    };
}

TEST(OpenGL4TextureArray, FilteringANonFilterableArrayIsRefusedAsOnVulkan)
{
    Fixture fixture;
    if (!fixture.Ready()) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto& device = fixture.Device();
    ShaderEffect effect(device, kVertex, kFragment);
    ASSERT_TRUE(effect.IsEffectValid());

    const auto plain = fixture.SolidArray(Color::Red, Texture2DArrayUsage::Sampled);
    effect.SetTextureArrayEXT(0, *plain);
    device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
    EXPECT_THROW(fixture.Draw(effect), System::NotSupportedException)
        << "a linear sampler read an array that did not declare Filterable";
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    EXPECT_EQ(Color::Red.getPackedValueProperty(), fixture.Draw(effect))
        << "a Point sampler may read any array";

    const auto filterable = fixture.SolidArray(
        Color::Lime, Texture2DArrayUsage::Sampled | Texture2DArrayUsage::Filterable);
    effect.SetTextureArrayEXT(0, *filterable);
    device.getSamplerStatesProperty()[0] = SamplerState::LinearClamp;
    EXPECT_EQ(Color::Lime.getPackedValueProperty(), fixture.Draw(effect))
        << "a Filterable array may be read linearly";
}

TEST(OpenGL4TextureArray, EachEffectSamplesTheArrayItBound)
{
    Fixture fixture;
    if (!fixture.Ready()) GTEST_SKIP() << "this run did not select the OpenGL4 renderer";
    auto& device = fixture.Device();
    device.getSamplerStatesProperty()[0] = SamplerState::PointClamp;
    ShaderEffect first(device, kVertex, kFragment);
    ShaderEffect second(device, kVertex, kFragment);
    const auto red = fixture.SolidArray(Color::Red, Texture2DArrayUsage::Sampled);
    const auto blue = fixture.SolidArray(Color::Blue, Texture2DArrayUsage::Sampled);
    first.SetTextureArrayEXT(0, *red);
    second.SetTextureArrayEXT(0, *blue);   // unit 0 again, bound last

    EXPECT_EQ(Color::Red.getPackedValueProperty(), fixture.Draw(first));
    EXPECT_EQ(Color::Blue.getPackedValueProperty(), fixture.Draw(second));
    EXPECT_EQ(Color::Red.getPackedValueProperty(), fixture.Draw(first))
        << "the first effect sampled the array another effect bound to the same unit";
}

#endif // CNA_RENDERER_OPENGL4 && CNA_CNAEXT
