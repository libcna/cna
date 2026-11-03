// SPDX-License-Identifier: MS-PL
// GPU execution is owner-only through tools/platform/run_gpu_tests_private.sh --exec.
#if defined(CNA_RENDERER_SDLGPU) || defined(CNA_RENDERER_PRESENT_SDLGPU)
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include <gtest/gtest.h>
#include <array>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
namespace {
GraphicsDevice Device()
{
    auto p=PresentationParameters::CreateWindowedEXT(800,480);
    return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,p);
}
std::array<VertexPositionColor,6> Quad(float z,Color color)
{
    return {VertexPositionColor(Vector3(-1,1,z),color),VertexPositionColor(Vector3(1,1,z),color),
        VertexPositionColor(Vector3(-1,-1,z),color),VertexPositionColor(Vector3(1,1,z),color),
        VertexPositionColor(Vector3(1,-1,z),color),VertexPositionColor(Vector3(-1,-1,z),color)};
}
void Draw(GraphicsDevice& device,float z,Color color)
{
    BasicEffect effect(device); effect.setVertexColorEnabledProperty(true);
    effect.Apply(); const auto vertices=Quad(z,color);
    device.DrawUserPrimitives(PrimitiveType::TriangleList,vertices.data(),0,2);
}
Color Pixel(RenderTarget2D& target)
{
    Color result=Color::Transparent; const Rectangle rect(3,4,1,1);
    target.GetData(0,&rect,&result,0,1); return result;
}
void Prepare(GraphicsDevice& device,RenderTarget2D& target)
{
    device.SetRenderTarget(&target); device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.Clear(ClearOptions::Target|ClearOptions::DepthBuffer|ClearOptions::Stencil,Color::Black,1,0);
}
}

TEST(SdlGpuPipelinePixels, DisabledDepthNeitherRejectsNorWrites)
{
    // XNA DepthBufferEnable=false: draw at .75 must pass despite Never; draw at .25 must
    // not occlude the final depth-tested .5 quad. Both write-flag combinations are exercised.
    auto device=Device();
    RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    for (bool write : {false,true}) {
        Prepare(device,target);
        DepthStencilState disabled; disabled.setDepthBufferEnableProperty(false);
        disabled.setDepthBufferWriteEnableProperty(write);
        disabled.setDepthBufferFunctionProperty(CompareFunction::Never);
        device.setDepthStencilStateProperty(disabled); Draw(device,.75f,Color::Red);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        EXPECT_EQ(Pixel(target),Color::Red) << "depth-disabled draw was rejected";
        Prepare(device,target);
        device.setDepthStencilStateProperty(disabled); Draw(device,.25f,Color::Red);
        device.setDepthStencilStateProperty(DepthStencilState::Default); Draw(device,.5f,Color::Lime);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        EXPECT_EQ(Pixel(target),Color::Lime) << "disabled depth wrote .25 into the attachment";
    }
}

TEST(SdlGpuPipelinePixels, DepthWriteAndReadOnlySelectDifferentPipelines)
{
    auto device=Device();
    RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    for (bool write : {true,false,true}) {
        Prepare(device,target);
        DepthStencilState state; state.setDepthBufferWriteEnableProperty(write);
        device.setDepthStencilStateProperty(state); Draw(device,.25f,Color::Red);
        device.setDepthStencilStateProperty(DepthStencilState::Default); Draw(device,.5f,Color::Lime);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // LessEqual: .5 fails against .25 if written, otherwise passes against clear depth 1.
        EXPECT_EQ(Pixel(target),write ? Color::Red : Color::Lime);
    }
}

TEST(SdlGpuPipelinePixels, StencilClockwiseAndCounterclockwiseSelectDifferentOperations)
{
    auto device=Device();
    RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    for (bool reverse : {false,true}) {
        Prepare(device,target);
        DepthStencilState mark; mark.setDepthBufferEnableProperty(false);
        mark.setStencilEnableProperty(true); mark.setTwoSidedStencilModeProperty(true);
        mark.setReferenceStencilProperty(5); mark.setStencilFunctionProperty(CompareFunction::Always);
        mark.setStencilPassProperty(StencilOperation::Replace);
        mark.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
        mark.setCounterClockwiseStencilPassProperty(StencilOperation::Invert);
        device.setDepthStencilStateProperty(mark);
        BasicEffect effect(device); effect.setVertexColorEnabledProperty(true); effect.Apply();
        auto quad=Quad(.5f,Color::White);
        if (reverse) { std::swap(quad[0],quad[1]); std::swap(quad[3],quad[4]); }
        device.DrawUserPrimitives(PrimitiveType::TriangleList,quad.data(),0,2);
        DepthStencilState probe; probe.setDepthBufferEnableProperty(false);
        probe.setStencilEnableProperty(true); probe.setStencilFunctionProperty(CompareFunction::Equal);
        probe.setReferenceStencilProperty(5); device.setDepthStencilStateProperty(probe); Draw(device,.5f,Color::Red);
        DepthStencilState inverted; inverted.setDepthBufferEnableProperty(false);
        inverted.setStencilEnableProperty(true); inverted.setStencilFunctionProperty(CompareFunction::Equal);
        inverted.setReferenceStencilProperty(255); device.setDepthStencilStateProperty(inverted); Draw(device,.5f,Color::Lime);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // CW ordinary Replace writes 5; reversed CCW Invert changes clear stencil 0 to 255.
        EXPECT_EQ(Pixel(target),reverse ? Color::Lime : Color::Red);
    }
}

TEST(SdlGpuPipelinePixels, IndexElementWidthPreservesHighVertexIndices)
{
    auto device=Device(); RenderTarget2D target(device,8,8);
    for (bool wide : {false,true}) {
        // 32-bit indices exceed 65535: truncating them reads intentionally degenerate vertices.
        const int first=wide ? 65536 : 4;
        std::vector<VertexPositionColor> vertices(first+6,VertexPositionColor(Vector3(0,0,0),Color::Blue));
        auto quad=Quad(.5f,Color::Red); std::copy(quad.begin(),quad.end(),vertices.begin()+first);
        VertexBuffer vb(device,VertexPositionColor::getVertexDeclarationStatic(),static_cast<int>(vertices.size()),BufferUsage::WriteOnly);
        vb.SetData(vertices.data(),static_cast<int>(vertices.size()));
        IndexBuffer ib(device,wide ? IndexElementSize::ThirtyTwoBits : IndexElementSize::SixteenBits,6,BufferUsage::WriteOnly);
        if (wide) { const std::uint32_t data[]{65536,65537,65538,65539,65540,65541}; ib.SetData(data,6); }
        else { const std::uint16_t data[]{4,5,6,7,8,9}; ib.SetData(data,6); }
        device.SetRenderTarget(&target); device.Clear(Color::Black);
        device.setBlendStateProperty(BlendState::Opaque); device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        BasicEffect effect(device); effect.setVertexColorEnabledProperty(true); effect.Apply();
        device.SetVertexBuffer(&vb); device.setIndicesProperty(&ib);
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList,0,first,6,0,2);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr)); EXPECT_EQ(Pixel(target),Color::Red) << wide;
        device.SetVertexBuffer(nullptr); device.setIndicesProperty(nullptr);
    }
}

TEST(SdlGpuPipelinePixels, SpriteCacheSeparatesDepthAttachmentAndEveryMrtFormat)
{
    auto device=Device();
    RenderTarget2D primary(device,8,8,false,SurfaceFormat::Color,DepthFormat::None);
    RenderTarget2D depth(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    RenderTarget2D extraColor(device,8,8,false,SurfaceFormat::Color,DepthFormat::None);
    RenderTarget2D extraFloat(device,8,8,false,SurfaceFormat::Single,DepthFormat::None);
    Texture2D white(device,1,1); const Color value=Color::White; white.SetData(&value,1);
    SpriteBatch sprites(device);
    for (int iteration=0; iteration<6; ++iteration) {
        auto& first=iteration%2 ? depth : primary;
        auto& second=iteration%3 ? extraFloat : extraColor;
        device.SetRenderTargets({RenderTargetBinding(&first),RenderTargetBinding(&second)});
        device.Clear(Color::Black);
        sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,&DepthStencilState::None,&RasterizerState::CullNone);
        sprites.Draw(white,Rectangle(0,0,8,8),Color::Red); sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // Sprite output is constant red on slot zero; incompatible attachments must not poison reuse.
        EXPECT_EQ(Pixel(first),Color::Red) << iteration;
    }
}

TEST(SdlGpuPipelinePixels, MinMaxBlendingUsesMicrosoftOrdinals)
{
    auto device=Device(); RenderTarget2D target(device,8,8);
    for (auto function : {BlendFunction::Min,BlendFunction::Max,BlendFunction::Min}) {
        device.SetRenderTarget(&target); device.Clear(Color::Lime);
        BlendState blend; blend.setColorSourceBlendProperty(Blend::One);
        blend.setColorDestinationBlendProperty(Blend::One);
        blend.setColorBlendFunctionProperty(function);
        blend.setAlphaSourceBlendProperty(Blend::One); blend.setAlphaDestinationBlendProperty(Blend::Zero);
        device.setBlendStateProperty(blend); device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone); Draw(device,.5f,Color::Red);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // min((1,0,0),(0,1,0))=black, max=yellow. Alpha is source One + destination Zero.
        EXPECT_EQ(Pixel(target),function==BlendFunction::Min ? Color::Black : Color::Yellow);
    }
}
// XNA SpriteBatch applies the supplied DepthStencilState before drawing with either
// stock or custom effects. Never rejects every fragment, leaving the cleared black texel.
namespace {
const char* spriteVertex=R"GLSL(#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 color;
layout(location=0) out vec2 fragUV;
layout(location=1) out vec4 fragColor;
layout(set=1,binding=0) uniform PC { vec4 vp; mat4 matrix; vec4 tint; vec4 slot0; } pc;
void main() { vec2 ndc=position.xy/pc.vp.xy*2.0-1.0;
gl_Position=vec4(ndc.x,-ndc.y,position.z,1); fragUV=uv; fragColor=color; }
)GLSL";
const char* spriteFragment=R"GLSL(#version 450
layout(location=0) in vec2 fragUV;
layout(location=1) in vec4 fragColor;
layout(location=0) out vec4 outputColor;
layout(set=2,binding=0) uniform sampler2D tex;
void main() { outputColor=texture(tex,fragUV)*fragColor; }
)GLSL";
}
TEST(SdlGpuPipelinePixels, CustomSpriteMustHonorDepthNever)
{
    auto device=Device(); RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    ShaderEffect custom(device,spriteVertex,spriteFragment); ASSERT_TRUE(custom.IsEffectValid()) << custom.GetCompileErrorEXT();
    Texture2D white(device,1,1); const Color value=Color::White; white.SetData(&value,1);
    SpriteBatch sprites(device);
    DepthStencilState never; never.setDepthBufferFunctionProperty(CompareFunction::Never);
    for (Effect* effect : {static_cast<Effect*>(nullptr),static_cast<Effect*>(&custom)}) {
        Prepare(device,target);
        sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,&never,&RasterizerState::CullNone,effect);
        sprites.Draw(white,Rectangle(0,0,8,8),Color::Red); sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        EXPECT_EQ(Pixel(target),Color::Black) << (effect ? "custom" : "stock");
    }
}
TEST(SdlGpuPipelinePixels, CustomSpriteMustHonorStencilNever)
{
    auto device=Device(); RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    ShaderEffect custom(device,spriteVertex,spriteFragment); ASSERT_TRUE(custom.IsEffectValid()) << custom.GetCompileErrorEXT();
    Texture2D white(device,1,1); const Color value=Color::White; white.SetData(&value,1);
    SpriteBatch sprites(device);
    DepthStencilState never; never.setDepthBufferEnableProperty(false);
    never.setStencilEnableProperty(true); never.setStencilFunctionProperty(CompareFunction::Never);
    for (Effect* effect : {static_cast<Effect*>(nullptr),static_cast<Effect*>(&custom)}) {
        Prepare(device,target);
        sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,&never,&RasterizerState::CullNone,effect);
        sprites.Draw(white,Rectangle(0,0,8,8),Color::Red); sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        EXPECT_EQ(Pixel(target),Color::Black) << (effect ? "custom" : "stock");
    }
}


TEST(SdlGpuPipelinePixels, CustomSpriteDepthWriteControlsOcclusion)
{
    // LessEqual: a .25 write occludes .5; a read-only .25 draw leaves clear depth 1.
    auto device=Device();
    RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    ShaderEffect custom(device,spriteVertex,spriteFragment);
    ASSERT_TRUE(custom.IsEffectValid()) << custom.GetCompileErrorEXT();
    Texture2D white(device,1,1); const Color value=Color::White; white.SetData(&value,1);
    SpriteBatch sprites(device);
    for (Effect* effect : {static_cast<Effect*>(nullptr),static_cast<Effect*>(&custom)}) {
        for (bool write : {false,true,false}) {
            Prepare(device,target);
            DepthStencilState state; state.setDepthBufferWriteEnableProperty(write);
            sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,
                          &state,&RasterizerState::CullNone,effect);
            sprites.Draw(white,Rectangle(0,0,8,8),std::nullopt,Color::Red,0,Vector2::Zero,SpriteEffects::None,.25f);
            sprites.End();
            sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,
                          &DepthStencilState::Default,&RasterizerState::CullNone,effect);
            sprites.Draw(white,Rectangle(0,0,8,8),std::nullopt,Color::Lime,0,Vector2::Zero,SpriteEffects::None,.5f);
            sprites.End();
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            EXPECT_EQ(Pixel(target),write ? Color::Red : Color::Lime) << write << '/' << (effect ? "custom" : "stock");
        }
    }
}

TEST(SdlGpuPipelinePixels, CustomSpriteStencilReferenceChangesWithCachedPipeline)
{
    // Replace writes 5. Equal/5 draws red, then Equal/7 must fail while reusing the same
    // pipeline: the reference is dynamic, not part of immutable pipeline identity.
    auto device=Device();
    RenderTarget2D target(device,8,8,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8);
    ShaderEffect custom(device,spriteVertex,spriteFragment);
    ASSERT_TRUE(custom.IsEffectValid()) << custom.GetCompileErrorEXT();
    Texture2D white(device,1,1); const Color value=Color::White; white.SetData(&value,1);
    SpriteBatch sprites(device);
    for (Effect* effect : {static_cast<Effect*>(nullptr),static_cast<Effect*>(&custom)}) {
        Prepare(device,target);
        DepthStencilState mark; mark.setDepthBufferEnableProperty(false);
        mark.setStencilEnableProperty(true); mark.setReferenceStencilProperty(5);
        mark.setStencilPassProperty(StencilOperation::Replace);
        sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,
                      &mark,&RasterizerState::CullNone,effect);
        sprites.Draw(white,Rectangle(0,0,8,8),Color::Blue); sprites.End();
        for (int reference : {5,7}) {
            DepthStencilState probe; probe.setDepthBufferEnableProperty(false);
            probe.setStencilEnableProperty(true); probe.setStencilFunctionProperty(CompareFunction::Equal);
            probe.setReferenceStencilProperty(reference);
            sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,
                          &probe,&RasterizerState::CullNone,effect);
            sprites.Draw(white,Rectangle(0,0,8,8),reference==5 ? Color::Red : Color::Lime); sprites.End();
        }
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        EXPECT_EQ(Pixel(target),Color::Red) << (effect ? "custom" : "stock");
    }
}
#endif
