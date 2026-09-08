// SPDX-License-Identifier: MS-PL
// SOFTWARE-176: adversarial XNA constant-depth-bias contract.
//
// RasterizerState.DepthBias is a normalized post-viewport depth offset. FNA3D converts that
// public value into the active native depth format's minimum-resolvable units for GL/D3D11; a CPU
// renderer that stores normalized depth applies it directly. The old Software and EasyGL paths
// instead treated the public float as one native unit, so realistic values such as 0.02 had no
// observable effect and only million-scale tests could move a polygon.

#include <array>
#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

using namespace CNA::Testing::Renderers;

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::CullMode;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    constexpr int kTargetSize = 16;

    std::array<VertexPositionColor, 6> FullTargetQuad(float depth, const Color& color)
    {
        return {{
            {Vector3(-1.0f, -1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
        }};
    }

    Color DrawBiasedSecondLayer(
        GraphicsDevice& device, DepthFormat depthFormat,
        float firstDepth, float secondDepth, float secondBias)
    {
        RenderTarget2D target(
            device, kTargetSize, kTargetSize, false, SurfaceFormat::Color,
            depthFormat, 0, RenderTargetUsage::PreserveContents);
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;

        RasterizerState unbiased;
        unbiased.setCullModeProperty(CullMode::None);
        device.setRasterizerStateProperty(unbiased);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);

        auto first = FullTargetQuad(firstDepth, Color::Red);
        effect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, first.data(), 0, 2);

        // Apply while the default framebuffer is current, then bind the requested target again.
        // EasyGL must rescale the remembered XNA bias for the destination's actual depth format;
        // merely converting it when the RasterizerState setter runs fails this transition.
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        RasterizerState biased;
        biased.setCullModeProperty(CullMode::None);
        biased.setDepthBiasProperty(secondBias);
        device.setRasterizerStateProperty(biased);
        device.SetRenderTarget(&target);

        auto second = FullTargetQuad(secondDepth, Color::Lime);
        effect.Apply();
        device.DrawUserPrimitives(
            PrimitiveType::TriangleList, second.data(), 0, 2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        const Rectangle centre(kTargetSize / 2, kTargetSize / 2, 1, 1);
        Color pixel;
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    }
}

TEST(RasterizerDepthBiasContractTest, ConstantBiasUsesNormalizedDepthAcrossDepthFormats)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGLES3);

    GraphicsDevice device;
    constexpr std::array<DepthFormat, 3> formats{
        DepthFormat::Depth16,
        DepthFormat::Depth24,
        DepthFormat::Depth24Stencil8,
    };

    for (const DepthFormat format : formats)
    {
        SCOPED_TRACE(static_cast<int>(format));

        // With no bias, the nearer second layer wins.
        EXPECT_EQ(
            Color::Lime,
            DrawBiasedSecondLayer(device, format, 0.50f, 0.49f, 0.0f));

        // A positive normalized bias of 0.02 pushes z=0.49 behind z=0.50.
        EXPECT_EQ(
            Color::Red,
            DrawBiasedSecondLayer(device, format, 0.50f, 0.49f, 0.02f));

        // A negative normalized bias of 0.02 pulls z=0.51 in front of z=0.50.
        EXPECT_EQ(
            Color::Lime,
            DrawBiasedSecondLayer(device, format, 0.50f, 0.51f, -0.02f));
    }
}
