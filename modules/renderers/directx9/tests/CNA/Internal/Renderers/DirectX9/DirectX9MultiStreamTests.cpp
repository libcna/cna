// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_DIRECTX9) || defined(CNA_RENDERER_PRESENT_DIRECTX9)
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "System/NotSupportedException.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::DirectX9;
using Microsoft::WRL::ComPtr;
struct Position { float x,y,z; };
struct ColorPair { std::uint32_t first,second; };
constexpr Position full[]{ {-1,-1,0.5f},{3,-1,0.5f},{-1,3,0.5f} };
constexpr std::uint32_t red=0xff0000ff,green=0xff00ff00,blue=0xffff0000;
VertexDeclaration Positions()
{
    return VertexDeclaration(12,{VertexElement(0,VertexElementFormat::Vector3,VertexElementUsage::Position,0)});
}
VertexDeclaration Colors(int offset=0,int stride=4)
{
    return VertexDeclaration(stride,{VertexElement(offset,VertexElementFormat::Color,VertexElementUsage::Color,0)});
}
void Apply(Effect& effect)
{
    effect.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
}
class DirectX9MultiStream : public testing::Test
{
protected:
    std::unique_ptr<GraphicsDevice> device;
    DirectX9Renderer* renderer=nullptr;
    void SetUp() override
    {
        device=std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::HiDef,PresentationParameters::CreateWindowedEXT(64,64));
        renderer=dynamic_cast<DirectX9Renderer*>(&device->GetRenderer());
        if (!renderer) GTEST_SKIP() << "requires the selected DIRECTX9 renderer";
    }
    void Begin(RenderTarget2D& target)
    {
        device->SetRenderTarget(&target);
        device->Clear(Color(0,0,255,255));
        device->setBlendStateProperty(BlendState::Opaque);
        device->setDepthStencilStateProperty(DepthStencilState::None);
        device->setRasterizerStateProperty(RasterizerState::CullNone);
        device->getSamplerStatesProperty()[0]=SamplerState::PointClamp;
        device->getSamplerStatesProperty()[1]=SamplerState::PointClamp;
    }
    void Pixel(RenderTarget2D& target,int x,int y,int r,int g,int b)
    {
        device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const Microsoft::Xna::Framework::Rectangle probe(x,y,1,1);
        Color pixel(0,0,0,0);
        target.GetData(0,&probe,&pixel,0,1);
        // Saturated endpoint arithmetic is exact; no interpolation/lighting/edge tolerance.
        EXPECT_EQ(pixel.getRProperty(),r); EXPECT_EQ(pixel.getGProperty(),g);
        EXPECT_EQ(pixel.getBProperty(),b); EXPECT_EQ(pixel.getAProperty(),255);
    }
    void Basic(BasicEffect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setDiffuseColorProperty(Vector3(1,1,1));
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setVertexColorEnabledProperty(true);
        Apply(effect);
    }
    void CleanStreams(int count)
    {
        for (int slot=0;slot<count;++slot)
        {
            UINT frequency=0;
            ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->GetStreamSourceFreq(slot,&frequency)));
            EXPECT_EQ(frequency,1u) << slot;
            ComPtr<IDirect3DVertexBuffer9> buffer;
            UINT offset=99,stride=99;
            ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->GetStreamSource(slot,buffer.GetAddressOf(),&offset,&stride)));
            EXPECT_EQ(buffer.Get(),nullptr) << "obsolete stream " << slot;
        }
    }
};

TEST_F(DirectX9MultiStream, SecondaryColorRebindAndSameStrideLayoutAreVisible)
{
    // Refresh SetVertexBuffers IL_00b7-0121 + DeclarationManager.CreateNativeDeclaration
    // IL_00c9-011b: each slot/local element offset is independent. Color bytes are RGBA.
    ASSERT_TRUE(device->SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput));
    D3DCAPS9 caps{};
    ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->GetDeviceCaps(&caps)));
    EXPECT_EQ(renderer->GetMaxVertexStreams(),static_cast<int>(std::min<UINT>(16,caps.MaxStreams)));
    VertexBuffer positions(*device,Positions(),3,BufferUsage::None);
    positions.SetDataRaw(full,3,12);
    const ColorPair data[]{ {red,green},{red,green},{red,green} };
    VertexBuffer first(*device,Colors(0,8),3,BufferUsage::None);
    VertexBuffer second(*device,Colors(4,8),3,BufferUsage::None);
    first.SetDataRaw(data,3,8); second.SetDataRaw(data,3,8);
    BasicEffect effect(*device);
    RenderTarget2D target(*device,64,64);
    for (int step=0;step<3;++step)
    {
        Begin(target); Basic(effect);
        device->SetVertexBuffers({VertexBufferBinding(&positions),
                                 VertexBufferBinding(step==1 ? &second : &first)});
        device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
        Pixel(target,32,32,step==1 ? 0 : 255,step==1 ? 255 : 0,0);
    }
}

TEST_F(DirectX9MultiStream, NativeDeclarationRetainsSlotsAndIncrementsDuplicateNonzeroUsage)
{
    // Refresh DeclarationManager.CreateNativeDeclaration IL_00c9-00dc and IL_011c-0191:
    // duplicate TEXCOORD1 increments to TEXCOORD2, even when TEXCOORD0 is unclaimed.
    struct Attributes { std::uint32_t color; float u,v; };
    struct Uv { float u,v; };
    VertexDeclaration first(12,{
        VertexElement(0,VertexElementFormat::Color,VertexElementUsage::Color,0),
        VertexElement(4,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,1)});
    VertexDeclaration second(8,{
        VertexElement(0,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,1)});
    VertexBuffer positions(*device,Positions(),3,BufferUsage::None);
    VertexBuffer a(*device,first,3,BufferUsage::None),b(*device,second,3,BufferUsage::None);
    positions.SetDataRaw(full,3,12);
    const Attributes av[]{ {red,0,0},{red,0,0},{red,0,0} };
    const Uv bv[]{ {0,0},{0,0},{0,0} };
    a.SetDataRaw(av,3,12); b.SetDataRaw(bv,3,8);
    BasicEffect effect(*device); RenderTarget2D target(*device,64,64);
    Begin(target); Basic(effect);
    device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&a),VertexBufferBinding(&b)});
    device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
    Pixel(target,32,32,255,0,0);
    ComPtr<IDirect3DVertexDeclaration9> declaration;
    ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->GetVertexDeclaration(declaration.GetAddressOf())));
    UINT count=0; ASSERT_TRUE(SUCCEEDED(declaration->GetDeclaration(nullptr,&count)));
    std::vector<D3DVERTEXELEMENT9> elements(count);
    ASSERT_TRUE(SUCCEEDED(declaration->GetDeclaration(elements.data(),&count)));
    ASSERT_EQ(elements.size(),5u); // POSITION, COLOR, TEX1, TEX2, END
    EXPECT_EQ(elements[2].Stream,1); EXPECT_EQ(elements[2].Offset,4);
    EXPECT_EQ(elements[2].Usage,D3DDECLUSAGE_TEXCOORD); EXPECT_EQ(elements[2].UsageIndex,1);
    EXPECT_EQ(elements[3].Stream,2); EXPECT_EQ(elements[3].Offset,0);
    EXPECT_EQ(elements[3].Usage,D3DDECLUSAGE_TEXCOORD); EXPECT_EQ(elements[3].UsageIndex,2);
}

TEST_F(DirectX9MultiStream, IndependentOffsetsBaseVertexAndBothIndexWidths)
{
    // Refresh SetVertexBuffers IL_0109-0121; DrawIndexedPrimitives forwards baseVertex and
    // startIndex separately. Fetch positions[1+1+i], colors[3+1+i], indices[3+i].
    const Position p[]{ {4,4,.5f},{4,4,.5f},full[0],full[1],full[2] };
    const std::uint32_t c[]{ blue,blue,blue,blue,red,red,red };
    VertexBuffer positions(*device,Positions(),5,BufferUsage::None);
    VertexBuffer colors(*device,Colors(),7,BufferUsage::None);
    positions.SetDataRaw(p,5,12); colors.SetDataRaw(c,7,4);
    const std::uint16_t i16[]{0,0,0,0,1,2};
    const std::uint32_t i32[]{0,0,0,0,1,2};
    IndexBuffer shortIndices(*device,IndexElementSize::SixteenBits,6,BufferUsage::None);
    IndexBuffer longIndices(*device,IndexElementSize::ThirtyTwoBits,6,BufferUsage::None);
    shortIndices.SetData(i16,6); longIndices.SetData(i32,6);
    BasicEffect effect(*device);
    RenderTarget2D target(*device,64,64);
    for (int route=0;route<3;++route)
    {
        SCOPED_TRACE(route);
        Begin(target); Basic(effect);
        device->SetVertexBuffers({VertexBufferBinding(&positions,1),VertexBufferBinding(&colors,3)});
        if (route==0) device->DrawPrimitives(PrimitiveType::TriangleList,1,1);
        else
        {
            device->SetIndexBuffer(route==1 ? &shortIndices : &longIndices);
            device->DrawIndexedPrimitives(PrimitiveType::TriangleList,1,0,3,3,1);
        }
        Pixel(target,32,32,255,0,0);
    }
}

TEST_F(DirectX9MultiStream, AlphaDualAndEnvironmentReadSecondaryTextureCoordinates)
{
    // Stock FX texture multiplication: PointClamp red/green two-texel texture, UV u=1/4 or
    // 3/4 selects the respective texel; white diffuse/emissive and white second texture are identities.
    struct Inputs { float nx,ny,nz,u,v,u1,v1; };
    VertexDeclaration inputs(28,{
        VertexElement(0,VertexElementFormat::Vector3,VertexElementUsage::Normal,0),
        VertexElement(12,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,0),
        VertexElement(20,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,1)});
    VertexBuffer positions(*device,Positions(),3,BufferUsage::None);
    VertexBuffer attributes(*device,inputs,3,BufferUsage::None);
    positions.SetDataRaw(full,3,12);
    Texture2D texture(*device,2,1),white(*device,1,1);
    const Color texels[]{Color(255,0,0,255),Color(0,255,0,255)},one[]{Color(255,255,255,255)};
    texture.SetData(texels,2); white.SetData(one,1);
    AlphaTestEffect alpha(*device); alpha.setTextureProperty(&texture);
    DualTextureEffect dual(*device); dual.setTextureProperty(&texture); dual.setTexture2Property(&white);
    EnvironmentMapEffect env(*device); env.setTextureProperty(&texture);
    env.setEnvironmentMapAmountProperty(0);
    env.setDiffuseColorProperty(Vector3(0,0,0)); env.setEmissiveColorProperty(Vector3(1,1,1));
    env.setEnvironmentMapSpecularProperty(Vector3(0,0,0));
    Effect* effects[]{&alpha,&dual,&env};
    RenderTarget2D target(*device,64,64);
    for (auto* effect:effects)
        for (int step=0;step<2;++step)
        {
            Inputs values[3];
            for (auto& value:values) value={0,0,1,step==0 ? .25f : .75f,.5f,.5f,.5f};
            // XNA SetData refuses a bound buffer; detach before changing the secondary stream.
            device->SetVertexBuffers({});
            attributes.SetDataRaw(values,3,28);
            Begin(target); Apply(*effect);
            device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&attributes)});
            device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
            Pixel(target,32,32,step==0 ? 255 : 0,step==1 ? 255 : 0,0);
        }
}

TEST_F(DirectX9MultiStream, GenuineSkinnedEffectUsesSecondaryWeightsAndBoneIndices)
{
    // Shipped SkinnedEffect.fx Skin: p' = sum(weight[j] * bone[index[j]] * p).
    // Bones translate by +/-1/2; weights (1,0) vs (0,1), then indices (1,0), move ONLY the skin stream.
    struct Vertex { float x,y,z,nx,ny,nz,u,v; };
    struct Skin { float weights[4]; std::uint8_t indices[4]; };
    const Vertex geometry[]{ {-.25f,-.25f,.5f,0,0,1,.5f,.5f},
        {.25f,-.25f,.5f,0,0,1,.5f,.5f},{0,.25f,.5f,0,0,1,.5f,.5f} };
    VertexDeclaration geom(32,{
        VertexElement(0,VertexElementFormat::Vector3,VertexElementUsage::Position,0),
        VertexElement(12,VertexElementFormat::Vector3,VertexElementUsage::Normal,0),
        VertexElement(24,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,0)});
    VertexDeclaration skin(20,{
        VertexElement(0,VertexElementFormat::Vector4,VertexElementUsage::BlendWeight,0),
        VertexElement(16,VertexElementFormat::Byte4,VertexElementUsage::BlendIndices,0)});
    VertexBuffer positions(*device,geom,3,BufferUsage::None),attributes(*device,skin,3,BufferUsage::None);
    positions.SetDataRaw(geometry,3,32);
    Texture2D white(*device,1,1); const Color one[]{Color(255,255,255,255)}; white.SetData(one,1);
    SkinnedEffect effect(*device); effect.setTextureProperty(&white);
    effect.setWeightsPerVertexProperty(2);
    effect.SetBoneTransforms({Matrix::CreateTranslation(-.5f,0,0),Matrix::CreateTranslation(.5f,0,0)});
    effect.setDiffuseColorProperty(Vector3(0,0,0)); effect.setEmissiveColorProperty(Vector3(0,1,0));
    effect.setSpecularColorProperty(Vector3(0,0,0));
    RenderTarget2D target(*device,64,64);
    for (int step=0;step<3;++step)
    {
        Skin values[3];
        for (auto& value:values) value= step==0 ? Skin{{1,0,0,0},{0,1,0,0}}
            : step==1 ? Skin{{0,1,0,0},{0,1,0,0}} : Skin{{1,0,0,0},{1,0,0,0}};
        // XNA SetData refuses a bound buffer; detach before changing the secondary stream.
        device->SetVertexBuffers({});
        attributes.SetDataRaw(values,3,20);
        Begin(target); Apply(effect);
        device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&attributes)});
        device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
        Pixel(target,step==0 ? 16 : 48,32,0,255,0);
        Pixel(target,step==0 ? 48 : 16,32,0,0,255);
    }
    // CNAEXT instance worlds must be applied AFTER the genuine bone transform, rather than
    // replacing the stock skin shader with the unskinned flat-color instancing shader.
    VertexDeclaration matrices(64,{
        VertexElement(0,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,1),
        VertexElement(16,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,2),
        VertexElement(32,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,3),
        VertexElement(48,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,4)});
    VertexBuffer instances(*device,matrices,3,BufferUsage::None);
    float worlds[48];
    Matrix::CreateTranslation(4,4,0).ToColumnMajor(worlds);
    Matrix::CreateTranslation(0,.5f,0).ToColumnMajor(worlds+16);
    Matrix::CreateTranslation(0,-.5f,0).ToColumnMajor(worlds+32);
    instances.SetDataRaw(worlds,3,64);
    const std::uint16_t indices[]{0,1,2};
    IndexBuffer ib(*device,IndexElementSize::SixteenBits,3,BufferUsage::None); ib.SetData(indices,3);
    device->SetIndexBuffer(&ib);
    Begin(target); Apply(effect);
    device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&attributes),
        VertexBufferBinding(&instances,1,2)});
    device->DrawInstancedPrimitives(PrimitiveType::TriangleList,0,0,3,0,1,4);
    // Bone index1 moves x to .5 -> pixel48; instance y +/- .5 gives rows16/48.
    Pixel(target,48,16,0,255,0); Pixel(target,48,48,0,255,0);
    Pixel(target,48,32,0,0,255); Pixel(target,16,16,0,0,255);
}

TEST_F(DirectX9MultiStream, SplitInstanceRowsRespectDivisorsAndOffsetsThenOrdinaryDraw)
{
    // CNAEXT Instanced3D.hlsl uses row-vector p*M. Rows 0..2 scale x/y;
    // row 3 translates x/y. The literal scale is 1/8; frequency 2 repeats row3 for instances 0 and 1.
    const Position geometry[]{ {4,4,.5f},full[0],full[1],full[2] };
    VertexBuffer positions(*device,Positions(),4,BufferUsage::None);
    positions.SetDataRaw(geometry,4,12);
    VertexDeclaration rows(48,{
        VertexElement(0,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,1),
        VertexElement(16,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,2),
        VertexElement(32,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,3)});
    VertexDeclaration translations(16,{
        VertexElement(0,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,4)});
    struct Rows { float value[12]; };
    struct Translation { float value[4]; };
    // Four instances use the same scale. The last-row stream alone controls the quadrant;
    // two records plus divisor 2 yield upper-left and lower-right, each twice.
    const Rows r[]{ {{0}},{{.125f,0,0,0, 0,.125f,0,0, 0,0,1,0}},
        {{.125f,0,0,0, 0,.125f,0,0, 0,0,1,0}},
        {{.125f,0,0,0, 0,.125f,0,0, 0,0,1,0}},
        {{.125f,0,0,0, 0,.125f,0,0, 0,0,1,0}} };
    const Translation t[]{ {{8,8,0,1}},{{-.5f,.5f,0,1}},{{.5f,-.5f,0,1}},{{.5f,.5f,0,1}},{{-.5f,-.5f,0,1}} };
    VertexBuffer matrixRows(*device,rows,5,BufferUsage::None),lastRow(*device,translations,5,BufferUsage::None);
    matrixRows.SetDataRaw(r,5,48); lastRow.SetDataRaw(t,5,16);
    const std::uint16_t indices[]{0,0,0,0,1,2};
    IndexBuffer ib(*device,IndexElementSize::SixteenBits,6,BufferUsage::None); ib.SetData(indices,6);
    device->SetIndexBuffer(&ib);
    BasicEffect effect(*device); effect.setDiffuseColorProperty(Vector3(0,1,0));
    effect.setVertexColorEnabledProperty(false);
    RenderTarget2D target(*device,64,64);
    for (int count:{1,4})
    {
        Begin(target); Apply(effect);
        device->SetVertexBuffers({VertexBufferBinding(&positions),
            VertexBufferBinding(&matrixRows,1,1),VertexBufferBinding(&lastRow,1,2)});
        device->DrawInstancedPrimitives(PrimitiveType::TriangleList,1,0,3,3,1,count);
        Pixel(target,16,16,0,255,0);
        Pixel(target,48,48,0,count==4 ? 255 : 0,count==4 ? 0 : 255);
        // Divisor 1 would additionally paint upper-right/lower-left from rows 3 and 4.
        Pixel(target,48,16,0,0,255); Pixel(target,16,48,0,0,255);
        CleanStreams(3);
    }
    // A later ordinary draw uses neither instance buffer nor its divisor.
    const std::uint32_t c[]{red,red,red,red};
    VertexBuffer colors(*device,Colors(),4,BufferUsage::None); colors.SetDataRaw(c,4,4);
    Begin(target); Basic(effect);
    device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&colors)});
    device->DrawPrimitives(PrimitiveType::TriangleList,1,1);
    Pixel(target,32,32,255,0,0);
}

TEST_F(DirectX9MultiStream, ResetAndShorterBindingListDoNotRetainSecondaryResources)
{
    // XNA SetVertexBuffers IL_0182-01df detaches trailing slots; native Reset loses binding state.
    VertexBuffer positions(*device,Positions(),3,BufferUsage::None),colors(*device,Colors(),3,BufferUsage::None);
    positions.SetDataRaw(full,3,12); const std::uint32_t c[]{green,green,green}; colors.SetDataRaw(c,3,4);
    BasicEffect effect(*device);
    for (int step=0;step<2;++step)
    {
        if (step==1)
        {
            renderer->DebugSimulateContextLoss(); renderer->DebugRestoreContext();
            ASSERT_FALSE(renderer->IsDeviceLostEXT());
        }
        RenderTarget2D target(*device,64,64);
        Begin(target); Basic(effect);
        device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&colors)});
        device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
        Pixel(target,32,32,0,255,0);
        CleanStreams(2);
        Begin(target);
        effect.setVertexColorEnabledProperty(false); effect.setDiffuseColorProperty(Vector3(1,0,0)); Apply(effect);
        device->SetVertexBuffer(&positions);
        device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
        Pixel(target,32,32,255,0,0);
        CleanStreams(2);
    }
}
TEST_F(DirectX9MultiStream, PbrDeclaredSplitLayoutReadsSecondaryUv)
{
    // CNAEXT declared-layout oracle, not an XNA PBR claim. No direct lights/emissive/IBL;
    // ambient=(1,1,1), occlusion=1, diffuse=(1,1,1), so Pbr3D PS returns the sampled albedo.
    struct Inputs { float nx,ny,nz,tx,ty,tz,tw,u,v; };
    VertexDeclaration inputs(36,{
        VertexElement(0,VertexElementFormat::Vector3,VertexElementUsage::Normal,0),
        VertexElement(12,VertexElementFormat::Vector4,VertexElementUsage::Tangent,0),
        VertexElement(28,VertexElementFormat::Vector2,VertexElementUsage::TextureCoordinate,0)});
    VertexBuffer positions(*device,Positions(),3,BufferUsage::None),attributes(*device,inputs,3,BufferUsage::None);
    positions.SetDataRaw(full,3,12);
    Texture2D texture(*device,2,1);
    const Color texels[]{Color(255,0,0,255),Color(0,255,0,255)}; texture.SetData(texels,2);
    PbrEffect effect(*device); effect.setTextureProperty(&texture);
    effect.setDiffuseColorProperty(Vector3(1,1,1)); effect.setAmbientLightColorProperty(Vector3(1,1,1));
    effect.getDirectionalLight0Property().setEnabledProperty(false);
    effect.getDirectionalLight1Property().setEnabledProperty(false);
    effect.getDirectionalLight2Property().setEnabledProperty(false);
    RenderTarget2D target(*device,64,64);
    for (int step=0;step<2;++step)
    {
        Inputs values[3];
        for (auto& value:values) value={0,0,1,1,0,0,1,step==0 ? .25f : .75f,.5f};
        // XNA SetData refuses a bound buffer; detach before changing the secondary stream.
        device->SetVertexBuffers({});
        attributes.SetDataRaw(values,3,36);
        Begin(target); Apply(effect);
        device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&attributes)});
        device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
        Pixel(target,32,32,step==0 ? 255 : 0,step==1 ? 255 : 0,0);
    }
}

TEST_F(DirectX9MultiStream, InvalidLaterStreamRefusesWithoutLeavingFrequencyState)
{
    // Internal renderer boundary: validate ALL bindings before native binding begins. A malformed
    // late stream must neither leave slot 0 instanced nor stop a later public draw from working.
    VertexBuffer positions(*device,Positions(),3,BufferUsage::None);
    positions.SetDataRaw(full,3,12);
    GpuDrawParams params;
    params.vertexStreamCount=3;
    params.vertexStreams[0].slot=0;
    params.vertexStreams[0].buffer=&positions.GetRenderer();
    params.vertexStreams[0].strideInBytes=12;
    VertexDeclaration matrixDeclaration(64,{
        VertexElement(0,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,1),
        VertexElement(16,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,2),
        VertexElement(32,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,3),
        VertexElement(48,VertexElementFormat::Vector4,VertexElementUsage::TextureCoordinate,4)});
    VertexBuffer matrices(*device,matrixDeclaration,1,BufferUsage::None);
    const float identityRows[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    matrices.SetDataRaw(identityRows,1,64);
    params.vertexStreams[1]=params.vertexStreams[0];
    params.vertexStreams[1].buffer=&matrices.GetRenderer();
    params.vertexStreams[1].strideInBytes=64;
    params.vertexStreams[1].slot=1;
    params.vertexStreams[1].instanceFrequency=2;
    params.vertexStreams[2]=params.vertexStreams[0];
    params.vertexStreams[2].slot=2;
    params.vertexStreams[2].strideInBytes=0; // invalid late binding, never a truncated draw
    const std::uint16_t indices[]{0,1,2};
    auto ib=renderer->CreateIndexBuffer16(3); ib->SetData16(indices,3);
    RenderTarget2D target(*device,64,64); Begin(target);
    const auto identity=Matrix::getIdentityProperty();
    EXPECT_THROW(renderer->DrawInstancedPrimitivesEx(positions.GetRenderer(),*ib,
        identity,identity,identity,PrimitiveType::TriangleList,1,4,params),System::NotSupportedException);
    Pixel(target,32,32,0,0,255);
    CleanStreams(3);
    // Reuse DX9's native HRESULT injector. Frequency 4 is installed before the first
    // SetStreamSource fails: cleanup must restore it even though binding did not complete.
    params.vertexStreamCount=2;
    Begin(target);
    renderer->InjectNextNativeFailureEXT(D3D9NativeOperationEXT::SetStreamSource,D3DERR_INVALIDCALL);
    EXPECT_THROW(renderer->DrawInstancedPrimitivesEx(positions.GetRenderer(),*ib,
        identity,identity,identity,PrimitiveType::TriangleList,1,4,params),std::runtime_error);
    EXPECT_FALSE(renderer->HasNativeFailureInjectionEXT());
    renderer->ClearNativeFailureInjectionEXT();
    Pixel(target,32,32,0,0,255);
    CleanStreams(3);
    const std::uint32_t c[]{red,red,red};
    VertexBuffer colors(*device,Colors(),3,BufferUsage::None); colors.SetDataRaw(c,3,4);
    BasicEffect effect(*device); Begin(target); Basic(effect);
    device->SetVertexBuffers({VertexBufferBinding(&positions),VertexBufferBinding(&colors)});
    device->DrawPrimitives(PrimitiveType::TriangleList,0,1);
    Pixel(target,32,32,255,0,0);
}

}
#endif
