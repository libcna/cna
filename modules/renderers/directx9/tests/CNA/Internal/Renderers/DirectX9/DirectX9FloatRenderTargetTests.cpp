// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_DIRECTX9) || defined(CNA_RENDERER_PRESENT_DIRECTX9)
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/ArgumentException.hpp"
#include "System/NotSupportedException.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::DirectX9;
using Microsoft::WRL::ComPtr;

struct Layout { SurfaceFormat format; D3DFORMAT native; int channels; bool half; };
// Oracle: D3D9 D3DFORMAT storage layout; byte widths = channels * component width.
constexpr Layout layouts[]{
    {SurfaceFormat::Single,D3DFMT_R32F,1,false},
    {SurfaceFormat::Vector2,D3DFMT_G32R32F,2,false},
    {SurfaceFormat::Vector4,D3DFMT_A32B32G32R32F,4,false},
    {SurfaceFormat::HalfSingle,D3DFMT_R16F,1,true},
    {SurfaceFormat::HalfVector2,D3DFMT_G16R16F,2,true},
    {SurfaceFormat::HalfVector4,D3DFMT_A16B16G16R16F,4,true},
    {SurfaceFormat::HdrBlendable,D3DFMT_A16B16G16R16F,4,true}};

std::vector<std::uint8_t> Texel(Layout layout, int face = 0)
{
    // Independent IEEE binary32/binary16 literals. First channel -2,4,8,-4,2,16;
    // other channels 3,1/2,1/4. Half -2: sign=1, exponent=16, mantissa=0 -> 0xc000.
    constexpr std::uint32_t first32[]{0xc0000000,0x40800000,0x41000000,
                                     0xc0800000,0x40000000,0x41800000};
    constexpr std::uint16_t first16[]{0xc000,0x4400,0x4800,0xc400,0x4000,0x4c00};
    const std::uint32_t full[]{first32[face],0x40400000,0x3f000000,0x3e800000};
    const std::uint16_t half[]{first16[face],0x4200,0x3800,0x3400};
    const int bytes = layout.channels * (layout.half ? 2 : 4);
    std::vector<std::uint8_t> result(bytes);
    std::memcpy(result.data(), layout.half ? static_cast<const void*>(half) : full, bytes);
    return result;
}
std::vector<std::uint8_t> Image(Layout layout, int width, int height, int face = 0)
{
    std::vector<std::uint8_t> result;
    const auto pixel=Texel(layout,face);
    for (int i=0; i<width*height; ++i) result.insert(result.end(),pixel.begin(),pixel.end());
    return result;
}

class DirectX9FloatTarget : public testing::TestWithParam<Layout>
{
protected:
    std::unique_ptr<GraphicsDevice> device;
    DirectX9Renderer* renderer = nullptr;
    ComPtr<IDirect3D9> d3d;
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DDISPLAYMODE mode{};
    void SetUp() override
    {
        device=std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::HiDef,PresentationParameters::CreateWindowedEXT(64,64));
        renderer=dynamic_cast<DirectX9Renderer*>(&device->GetRenderer());
        if (!renderer) GTEST_SKIP() << "requires the selected DIRECTX9 renderer";
        ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->GetDirect3D(d3d.GetAddressOf())));
        ASSERT_TRUE(SUCCEEDED(renderer->GetDeviceEXT()->GetCreationParameters(&creation)));
        ASSERT_TRUE(SUCCEEDED(d3d->GetAdapterDisplayMode(creation.AdapterOrdinal,&mode)));
        RecordProperty("surface_format",static_cast<int>(GetParam().format));
        RecordProperty("native_2d_rt",NativeSupport(D3DRTYPE_TEXTURE) ? 1 : 0);
        RecordProperty("native_cube_rt",NativeSupport(D3DRTYPE_CUBETEXTURE) ? 1 : 0);
        RecordProperty("native_samples_preference_4",NativeSamples(4));
    }
    bool NativeSupport(D3DRESOURCETYPE kind) const
    {
        // Refresh GraphicsAdapter.IsValidSurfaceFormat IL_005a-0096; HiDef profile
        // IL_0662-06ab excludes formats 13..18 from blending, but not HdrBlendable.
        const DWORD usage=D3DUSAGE_RENDERTARGET |
            (GetParam().format==SurfaceFormat::HdrBlendable ? D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING : 0);
        return SUCCEEDED(d3d->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,
            mode.Format,usage,kind,GetParam().native));
    }
    int NativeSamples(int request) const
    {
        // Refresh QueryMultiSampleCount IL_0000-0046: exact descending integers, cap 16.
        // This oracle does not call CNA's negotiation or mapping helpers.
        for (int n=std::min(16,request); n>1; --n)
            if (SUCCEEDED(d3d->CheckDeviceMultiSampleType(creation.AdapterOrdinal,
                creation.DeviceType,GetParam().native,TRUE,static_cast<D3DMULTISAMPLE_TYPE>(n),nullptr)))
                return n;
        return 0;
    }
    std::unique_ptr<IRenderTargetRenderer> Target(int width=3,int height=2,bool mips=false,int samples=0)
    {
        return renderer->CreateRenderTarget2DEXT(width,height,0,true,mips,samples,
                                                 static_cast<int>(GetParam().format));
    }
    void ExpectBytes(IRenderTargetRenderer& target,int level,int width,int height,int face=0)
    {
        const auto expected=Image(GetParam(),width,height,face);
        std::vector<std::uint8_t> actual(expected.size()+2,0xa5);
        ASSERT_TRUE(target.GetData(level,0,0,width,height,actual.data()+1,expected.size()));
        EXPECT_EQ(actual.front(),0xa5); EXPECT_EQ(actual.back(),0xa5);
        EXPECT_TRUE(std::equal(expected.begin(),expected.end(),actual.begin()+1));
    }
};

TEST_P(DirectX9FloatTarget, NativeQueryAndPublicPreferredFormatAgree)
{
    const auto format=GetParam().format;
    const bool native=NativeSupport(D3DRTYPE_TEXTURE);
    // A broken CNA query returning false cannot suppress any native-supported case.
    EXPECT_EQ(renderer->ClassifyRenderTargetFormatEXT(static_cast<int>(format))==
        RendererFormatVerdict::Supported,native);
    EXPECT_EQ(device->SupportsSurfaceFormatAsRenderTargetEXT(format),native);
    SurfaceFormat selected; DepthFormat depth; int samples=-1;
    const bool exact=GraphicsAdapter::getDefaultAdapterProperty().QueryRenderTargetFormat(
        GraphicsProfile::HiDef,format,DepthFormat::None,0,selected,depth,samples);
    EXPECT_EQ(exact,native);
    // Refresh QuerySurfaceFormat IL_0000-000e preserves a supported preference.
    // Do not prescribe CNA's unconditional Color fallback: Refresh has ordered fallback lists.
    // An unsupported preference must be changed; exact ordered fallback is a separate shared bug.
    if (native) EXPECT_EQ(selected,format); else EXPECT_NE(selected,format);
    RenderTarget2D target(*device,3,2,false,format,DepthFormat::None);
    EXPECT_EQ(target.getFormatProperty(),selected);
    auto* storage=dynamic_cast<D3D9RenderTargetRenderer*>(target.GetRenderTargetRenderer());
    ASSERT_NE(storage,nullptr);
    D3DSURFACE_DESC desc{};
    ASSERT_TRUE(SUCCEEDED(storage->GetTextureEXT()->GetLevelDesc(0,&desc)));
    if (native) EXPECT_EQ(desc.Format,GetParam().native);
    if (!native)
        EXPECT_THROW((void)Target(),System::NotSupportedException);
    const bool cubeNative=NativeSupport(D3DRTYPE_CUBETEXTURE);
    EXPECT_EQ(renderer->ClassifyRenderTargetCubeFormatEXT(static_cast<int>(format))==
        RendererFormatVerdict::Supported,cubeNative);
    RenderTargetCube cube(*device,4,false,format,DepthFormat::None);
    if (cubeNative) EXPECT_EQ(cube.getFormatProperty(),format);
    else EXPECT_NE(cube.getFormatProperty(),format);
    auto* cubeStorage=dynamic_cast<D3D9RenderTargetCubeRenderer*>(cube.GetRenderTargetCubeRenderer());
    ASSERT_NE(cubeStorage,nullptr);
    ASSERT_TRUE(SUCCEEDED(cubeStorage->GetTextureEXT()->GetLevelDesc(0,&desc)));
    if (cubeNative) EXPECT_EQ(desc.Format,GetParam().native);
}

TEST_P(DirectX9FloatTarget, PaddedUploadAndPartialReadUseTheExactNativeLayout)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE)) GTEST_SKIP() << "native 2D format unavailable";
    auto target=Target();
    const int texel=Texel(GetParam()).size(), row=3*texel, pitch=row+7;
    std::vector<std::uint8_t> padded(2*pitch,0x5a);
    for (int y=0;y<2;++y)
        for (int x=0;x<3;++x)
        {
            const auto pixel=Texel(GetParam(),y*3+x);
            std::copy(pixel.begin(),pixel.end(),padded.begin()+y*pitch+x*texel);
        }
    target->UpdatePixels(padded.data(),pitch);
    std::vector<std::uint8_t> actual(row*2+2,0xa5),expected;
    for (int face=0;face<6;++face) { const auto pixel=Texel(GetParam(),face);
        expected.insert(expected.end(),pixel.begin(),pixel.end()); }
    ASSERT_TRUE(target->GetData(0,0,0,3,2,actual.data()+1,row*2));
    EXPECT_TRUE(std::equal(expected.begin(),expected.end(),actual.begin()+1));
    EXPECT_EQ(actual.front(),0xa5); EXPECT_EQ(actual.back(),0xa5);
    std::vector<std::uint8_t> part(texel+2,0xa5);
    ASSERT_TRUE(target->GetData(0,1,1,1,1,part.data()+1,texel));
    const auto last=Texel(GetParam(),4);
    EXPECT_TRUE(std::equal(last.begin(),last.end(),part.begin()+1));
    EXPECT_EQ(part.front(),0xa5); EXPECT_EQ(part.back(),0xa5);
    EXPECT_THROW((void)target->GetData(0,0,0,1,1,part.data(),texel-1),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(target->UpdatePixels(padded.data(),row-1),std::invalid_argument);
}

TEST_P(DirectX9FloatTarget, CubeFacesMipUploadsAndPartialUpdatesKeepLiteralBytes)
{
    if (!NativeSupport(D3DRTYPE_CUBETEXTURE)) GTEST_SKIP() << "native cube format unavailable";
    auto cube=renderer->CreateRenderTargetCubeEXT(4,0,true,true,4,static_cast<int>(GetParam().format));
    auto* storage=dynamic_cast<D3D9RenderTargetCubeRenderer*>(cube.get());
    ASSERT_NE(storage,nullptr);
    D3DSURFACE_DESC desc{};
    ASSERT_TRUE(SUCCEEDED(storage->GetTextureEXT()->GetLevelDesc(1,&desc)));
    EXPECT_EQ(desc.Format,GetParam().native); EXPECT_EQ(desc.Width,2u);
    EXPECT_EQ(cube->GetMultiSampleCount(),0); // D3D9 cube MSAA is explicitly unsupported.
    for (int face=0;face<6;++face)
        for (int level=0;level<3;++level)
        {
            const int extent=4>>level;
            const auto expected=Image(GetParam(),extent,extent,face);
            ASSERT_TRUE(cube->SetData(face,level,0,0,extent,extent,expected.data(),expected.size()));
        }
    const auto changed=Texel(GetParam(),5);
    ASSERT_TRUE(cube->SetData(1,1,1,0,1,1,changed.data(),changed.size()));
    for (int face=0;face<6;++face)
        for (int level=0;level<3;++level)
        {
            const int extent=4>>level;
            auto expected=Image(GetParam(),extent,extent,face);
            if (face==1 && level==1) std::copy(changed.begin(),changed.end(),expected.begin()+changed.size());
            std::vector<std::uint8_t> actual(expected.size()+2,0xa5);
            ASSERT_TRUE(cube->GetData(face,level,0,0,extent,extent,actual.data()+1,expected.size()));
            EXPECT_TRUE(std::equal(expected.begin(),expected.end(),actual.begin()+1));
            EXPECT_EQ(actual.front(),0xa5); EXPECT_EQ(actual.back(),0xa5);
        }
    EXPECT_FALSE(cube->SetData(0,0,0,0,1,1,changed.data(),changed.size()-1));
}

TEST_P(DirectX9FloatTarget, PublicCubeTransfersUseDeclaredFormatHooks)
{
    if (!NativeSupport(D3DRTYPE_CUBETEXTURE)) GTEST_SKIP() << "native cube format unavailable";
    RenderTargetCube cube(*device,4,true,GetParam().format,DepthFormat::None,0,
                          RenderTargetUsage::PreserveContents);
    ASSERT_EQ(cube.getFormatProperty(),GetParam().format);
    constexpr auto face=CubeMapFace::NegativeY;
    device->SetRenderTarget(&cube,face);
    device->Clear(-2,3,0.5f,0.25f);
    device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

    // Public byte and packed-vector overloads dispatch non-Color layouts through
    // GetDataBytesEXT/SetDataBytesEXT, not the native GetData/SetData tested above.
    // Expected IEEE words are derived in Texel; all values are exactly representable.
    auto expected=Image(GetParam(),4,4);
    std::vector<std::uint8_t> actual(expected.size()+2,0xa5);
    cube.GetData(face,0,nullptr,actual.data(),1,expected.size());
    EXPECT_TRUE(std::equal(expected.begin(),expected.end(),actual.begin()+1));
    EXPECT_EQ(actual.front(),0xa5); EXPECT_EQ(actual.back(),0xa5);

    const auto replacement=Texel(GetParam(),5); // (16,3,1/2,1/4), with declared channels.
    const Microsoft::Xna::Framework::Rectangle region(1,1,1,1);
    cube.SetData(face,0,&region,replacement.data(),0,replacement.size());
    std::copy(replacement.begin(),replacement.end(),expected.begin()+5*replacement.size());
    std::fill(actual.begin(),actual.end(),0xa5);
    cube.GetData(face,0,nullptr,actual.data(),1,expected.size());
    EXPECT_TRUE(std::equal(expected.begin(),expected.end(),actual.begin()+1));
    EXPECT_EQ(actual.front(),0xa5); EXPECT_EQ(actual.back(),0xa5);

    const auto mip=Image(GetParam(),2,2,1); // Authored level 1: first component 4.
    cube.SetData(CubeMapFace::PositiveZ,1,nullptr,mip.data(),0,mip.size());
    std::vector<std::uint8_t> mipRead(mip.size()+2,0xa5);
    cube.GetData(CubeMapFace::PositiveZ,1,nullptr,mipRead.data(),1,mip.size());
    EXPECT_TRUE(std::equal(mip.begin(),mip.end(),mipRead.begin()+1));
    EXPECT_EQ(mipRead.front(),0xa5); EXPECT_EQ(mipRead.back(),0xa5);
}

TEST(DirectX9CubeTransferContract, RefusedColorUploadPreservesTheRenderedFace)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64,64));
    if (!dynamic_cast<DirectX9Renderer*>(&device.GetRenderer()))
        GTEST_SKIP() << "requires the selected DIRECTX9 renderer";
    RenderTargetCube cube(device,4,false,SurfaceFormat::Color,DepthFormat::None,0,
                          RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&cube,CubeMapFace::PositiveX);
    device.Clear(Color::Red);
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    const std::array<Color,16> replacement{Color::Blue,Color::Blue,Color::Blue,Color::Blue,
        Color::Blue,Color::Blue,Color::Blue,Color::Blue,Color::Blue,Color::Blue,Color::Blue,Color::Blue,
        Color::Blue,Color::Blue,Color::Blue,Color::Blue};
    // Existing DX9 Color-cube policy: explicit NotSupported, with no GPU/shadow write.
    // RenderTargetCubeSetDataContractTest.StoresTheFaceOrRefusesButNeverSilentlyDiscardsIt
    // requires this refusal. A failed upload must retain the rendered (255,0,0,255) texels.
    EXPECT_THROW(cube.SetData(CubeMapFace::PositiveX,replacement.data(),replacement.size()),
                 System::NotSupportedException);
    std::array<Color,16> actual{};
    cube.GetData(CubeMapFace::PositiveX,actual.data(),actual.size());
    for (const auto& pixel:actual) EXPECT_EQ(pixel,Color::Red);
}

TEST_P(DirectX9FloatTarget, CubeRenderedFacesGenerateIndependentFloatMips)
{
    if (!NativeSupport(D3DRTYPE_CUBETEXTURE)) GTEST_SKIP() << "native cube format unavailable";
    auto cube=renderer->CreateRenderTargetCubeEXT(4,0,true,true,0,static_cast<int>(GetParam().format));
    constexpr float first[]{-2,4,8,-4,2,16};
    for (int face=0;face<6;++face)
    {
        renderer->SetRenderTargetCubeFace(cube.get(),face);
        renderer->Clear(first[face],3,0.5f,0.25f);
    }
    renderer->SetRenderTarget2D(nullptr);
    for (int face=0;face<6;++face)
        for (int level=0;level<3;++level)
        {
            const int extent=4>>level;
            const auto expected=Image(GetParam(),extent,extent,face);
            std::vector<std::uint8_t> actual(expected.size(),0xa5);
            ASSERT_TRUE(cube->GetData(face,level,0,0,extent,extent,actual.data(),actual.size()));
            EXPECT_EQ(actual,expected);
        }
}

TEST_P(DirectX9FloatTarget, ClassicVectorClearRetainsRefreshColorPacking)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE)) GTEST_SKIP() << "native 2D format unavailable";
    RenderTarget2D target(*device,1,1,false,GetParam().format,DepthFormat::None);
    device->SetRenderTarget(&target);
    // Refresh GraphicsDevice.Clear(options,Vector4,...) IL_0000-0013 constructs Color.
    device->Clear(ClearOptions::Target,Vector4(-2,3,0.5f,0.25f),1,0);
    device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    // Color clamps and rounds: (0,255,128,64), then native UNORM expansion divides by 255.
    // Nearest half: 128/255 -> 514/1024 = 0x3804; 64/255 -> 257/1024 = 0x3404.
    const std::uint32_t full[]{0,0x3f800000,std::bit_cast<std::uint32_t>(128.0f/255.0f),
                              std::bit_cast<std::uint32_t>(64.0f/255.0f)};
    const std::uint16_t half[]{0,0x3c00,0x3804,0x3404};
    const int bytes=Texel(GetParam()).size();
    std::vector<std::uint8_t> expected(bytes),actual(bytes,0xa5);
    std::memcpy(expected.data(),GetParam().half ? static_cast<const void*>(half) : full,bytes);
    ASSERT_TRUE(target.GetRenderTargetRenderer()->GetData(0,0,0,1,1,actual.data(),bytes));
    if (GetParam().half) EXPECT_EQ(actual,expected);
    else
    {
        // Native UNORM expansion may use a reciprocal multiply rather than division (one ULP);
        // GTest's four-ULP comparison allows that while rejecting 127/255 or 63/255 quantization.
        std::array<float,4> unpacked{};
        std::memcpy(unpacked.data(),actual.data(),bytes);
        const float expectedFloat[]{0,1,128.0f/255.0f,64.0f/255.0f};
        for (int c=0;c<GetParam().channels;++c) EXPECT_FLOAT_EQ(unpacked[c],expectedFloat[c]);
    }
}

TEST_P(DirectX9FloatTarget, StockRasterAndSamplingPreserveSignedHdrChannels)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE)) GTEST_SKIP() << "native 2D format unavailable";
    RenderTarget2D source(*device,4,4,false,GetParam().format,DepthFormat::None);
    Texture2D input(*device,1,1,false,SurfaceFormat::HalfVector4);
    const PackedVector::HalfVector4 value(-2.0f,3.0f,0.5f,0.25f);
    input.SetData(&value,1);
    SpriteBatch batch(*device);
    device->SetRenderTarget(&source);
    batch.Begin(SpriteSortMode::Deferred,BlendState::Opaque,&SamplerState::PointClamp,nullptr,nullptr);
    batch.Draw(input,Microsoft::Xna::Framework::Rectangle(0,0,4,4),Color::White); batch.End();
    device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    // Read native bytes, bypassing Texture2D's CPU upload shadow and half unpacker.
    ExpectBytes(*source.GetRenderTargetRenderer(),0,4,4);
    if (!SUCCEEDED(d3d->CheckDeviceFormat(creation.AdapterOrdinal,creation.DeviceType,mode.Format,
            D3DUSAGE_RENDERTARGET,D3DRTYPE_TEXTURE,D3DFMT_A32B32G32R32F))) return;
    RenderTarget2D destination(*device,4,4,false,SurfaceFormat::Vector4,DepthFormat::None);
    ASSERT_EQ(destination.getFormatProperty(),SurfaceFormat::Vector4);
    device->SetRenderTarget(&destination);
    batch.Begin(SpriteSortMode::Deferred,BlendState::Opaque,&SamplerState::PointClamp,nullptr,nullptr);
    batch.Draw(source,Microsoft::Xna::Framework::Rectangle(0,0,4,4),Color::White); batch.End();
    device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    std::array<Vector4,16> actual{};
    destination.GetData(actual.data(),actual.size());
    // D3D9 missing sampled channels expand to 1; stored values are exact binary powers/fractions.
    for (const auto& pixel:actual)
    {
        EXPECT_FLOAT_EQ(pixel.X,-2); EXPECT_FLOAT_EQ(pixel.Y,GetParam().channels>=2 ? 3 : 1);
        EXPECT_FLOAT_EQ(pixel.Z,GetParam().channels==4 ? 0.5f : 1);
        EXPECT_FLOAT_EQ(pixel.W,GetParam().channels==4 ? 0.25f : 1);
    }
}

TEST_P(DirectX9FloatTarget, RawFloatClearAndGeneratedMipKeepSignedHdrValues)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE)) GTEST_SKIP() << "native 2D format unavailable";
    auto target=Target(4,4,true);
    renderer->SetRenderTarget2D(target.get());
    auto* native=renderer->GetDeviceEXT();
    const D3DVIEWPORT9 small{1,1,1,1,0.25f,0.75f};
    ASSERT_TRUE(SUCCEEDED(native->SetViewport(&small)));
    const RECT scissor{1,1,2,2};
    ASSERT_TRUE(SUCCEEDED(native->SetScissorRect(&scissor)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_COLORWRITEENABLE,1)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE)));
    renderer->Clear(-2,3,0.5f,0.25f);
    D3DVIEWPORT9 after{}; ASSERT_TRUE(SUCCEEDED(native->GetViewport(&after)));
    EXPECT_EQ(after.X,small.X); EXPECT_EQ(after.Y,small.Y);
    EXPECT_EQ(after.Width,small.Width); EXPECT_EQ(after.Height,small.Height);
    EXPECT_FLOAT_EQ(after.MinZ,small.MinZ); EXPECT_FLOAT_EQ(after.MaxZ,small.MaxZ);
    for (const auto state : {D3DRS_SCISSORTESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE})
    {
        DWORD value=0; ASSERT_TRUE(SUCCEEDED(native->GetRenderState(state,&value)));
        EXPECT_EQ(value,1u);
    }
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_COLORWRITEENABLE,15)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)));
    renderer->SetRenderTarget2D(nullptr);
    // Uniform box/linear downsampling: (-2+...+-2)/N = -2; every level is exact.
    for (int level=0;level<3;++level) ExpectBytes(*target,level,4>>level,4>>level);
    const auto authored=Image(GetParam(),2,2,1);
    target->UpdatePixelsLevel(1,authored.data(),2,2);
    ExpectBytes(*target,1,2,2,1);
}

TEST_P(DirectX9FloatTarget, NativeFormatDeterminesSampleCountAndResolve)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE)) GTEST_SKIP() << "native 2D format unavailable";
    for (const int request : {0,1,2,3,4,8,17})
    {
        SCOPED_TRACE(request);
        auto target=Target(4,4,false,request);
        EXPECT_EQ(target->GetMultiSampleCount(),NativeSamples(request));
        SurfaceFormat selected; DepthFormat depth; int samples=-1;
        (void)GraphicsAdapter::getDefaultAdapterProperty().QueryRenderTargetFormat(
            GraphicsProfile::HiDef,GetParam().format,DepthFormat::None,request,selected,depth,samples);
        EXPECT_EQ(selected,GetParam().format); EXPECT_EQ(samples,NativeSamples(request));
        RenderTarget2D publicTarget(*device,4,4,false,GetParam().format,DepthFormat::None,request);
        EXPECT_EQ(publicTarget.getMultiSampleCountProperty(),NativeSamples(request));
        auto* storage=dynamic_cast<D3D9RenderTargetRenderer*>(target.get());
        ASSERT_NE(storage,nullptr);
        D3DSURFACE_DESC desc{};
        ASSERT_TRUE(SUCCEEDED(storage->GetActiveColorSurfaceEXT()->GetDesc(&desc)));
        EXPECT_EQ(desc.Format,GetParam().native);
        EXPECT_EQ(static_cast<int>(desc.MultiSampleType),NativeSamples(request));
        renderer->SetRenderTarget2D(target.get()); renderer->Clear(-2,3,0.5f,0.25f);
        renderer->SetRenderTarget2D(nullptr);
        ExpectBytes(*target,0,4,4);
    }
}

TEST_P(DirectX9FloatTarget, MsaaUploadSeedsTheRenderSurfaceBeforeResolve)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE) || NativeSamples(4)==0)
        GTEST_SKIP() << "native format has no multisampled target for this request";
    auto target=Target(4,4,false,4);
    const auto expected=Image(GetParam(),4,4);
    target->UpdatePixels(expected.data(),4*Texel(GetParam()).size());
    renderer->SetRenderTarget2D(target.get());
    // PreserveContents: no clear/draw is needed to retain a completed SetData. Resolving stale
    // samples over the single-sample upload would destroy the literal negative/HDR payload.
    renderer->SetRenderTarget2D(nullptr);
    ExpectBytes(*target,0,4,4);
}

TEST_P(DirectX9FloatTarget, RealResetRecreatesTheRequestedStorage)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE) || !NativeSupport(D3DRTYPE_CUBETEXTURE))
        GTEST_SKIP() << "native 2D/cube format unavailable";
    auto target=Target();
    auto cube=renderer->CreateRenderTargetCubeEXT(4,0,true,false,0,static_cast<int>(GetParam().format));
    // Exercise both resources before Reset; repaint afterwards with a distinct value. Contents
    // are allowed to be lost, but the recreated storage must retain its original float format.
    renderer->SetRenderTarget2D(target.get()); renderer->Clear(4,2,1,1);
    renderer->SetRenderTargetCubeFace(cube.get(),0); renderer->Clear(4,2,1,1);
    renderer->SetRenderTarget2D(nullptr);
    renderer->DebugSimulateContextLoss(); renderer->DebugRestoreContext();
    renderer->SetRenderTarget2D(target.get()); renderer->Clear(-2,3,0.5f,0.25f);
    renderer->SetRenderTarget2D(nullptr);
    ExpectBytes(*target,0,3,2);
    renderer->SetRenderTargetCubeFace(cube.get(),3); renderer->Clear(-2,3,0.5f,0.25f);
    renderer->SetRenderTarget2D(nullptr);
    const auto expected=Image(GetParam(),4,4);
    std::vector<std::uint8_t> actual(expected.size());
    ASSERT_TRUE(cube->GetData(3,0,0,0,4,4,actual.data(),actual.size()));
    EXPECT_EQ(actual,expected);
    auto* storage=dynamic_cast<D3D9RenderTargetCubeRenderer*>(cube.get());
    ASSERT_NE(storage,nullptr);
    D3DSURFACE_DESC desc{};
    ASSERT_TRUE(SUCCEEDED(storage->GetTextureEXT()->GetLevelDesc(0,&desc)));
    EXPECT_EQ(desc.Format,GetParam().native);
}

TEST_P(DirectX9FloatTarget, TwoDimensionalAndCubeMrtKeepNativeBytes)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE) || !NativeSupport(D3DRTYPE_CUBETEXTURE))
        GTEST_SKIP() << "native 2D/cube format unavailable";
    RenderTarget2D first(*device,4,4,false,GetParam().format,DepthFormat::None);
    RenderTargetCube second(*device,4,false,GetParam().format,DepthFormat::None);
    ASSERT_EQ(first.getFormatProperty(),GetParam().format);
    ASSERT_EQ(second.getFormatProperty(),GetParam().format);
    device->SetRenderTargets({RenderTargetBinding(&first),RenderTargetBinding(&second,CubeMapFace::NegativeZ)});
    device->Clear(-2,3,0.5f,0.25f);
    device->SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    ExpectBytes(*first.GetRenderTargetRenderer(),0,4,4);
    const auto expected=Image(GetParam(),4,4);
    std::vector<std::uint8_t> actual(expected.size());
    ASSERT_TRUE(second.GetRenderTargetCubeRenderer()->GetData(5,0,0,0,4,4,actual.data(),actual.size()));
    EXPECT_EQ(actual,expected);
}
TEST_P(DirectX9FloatTarget, MixedColorMrtQuantizesOnlyTheColorAttachment)
{
    if (!NativeSupport(D3DRTYPE_TEXTURE)) GTEST_SKIP() << "native 2D format unavailable";
    RenderTarget2D floating(*device,4,4,false,GetParam().format,DepthFormat::None);
    RenderTarget2D color(*device,4,4,false,SurfaceFormat::Color,DepthFormat::None);
    // Refresh SetRenderTargets IL_0168-0172 requires matching pixel sizes even if the
    // native driver exposes MRTINDEPENDENTBITDEPTHS. Single/HalfVector2 are 4 bytes like Color.
    if (GetParam().channels*(GetParam().half ? 2 : 4)!=4)
    {
        EXPECT_THROW(device->SetRenderTargets({RenderTargetBinding(&floating),RenderTargetBinding(&color)}),
                     System::ArgumentException);
        return;
    }
    device->SetRenderTargets({RenderTargetBinding(&floating),RenderTargetBinding(&color)});
    device->Clear(-2,3,0.5f,0.25f);
    device->SetRenderTargets({});
    ExpectBytes(*floating.GetRenderTargetRenderer(),0,4,4);
    std::array<Color,16> actual{}; color.GetData(actual.data(),actual.size());
    // UNORM8: clamp -2->0 and 3->1; (1/2)*255=127.5 and (1/4)*255=63.75.
    // Allow one code for native tie/quantization rules, no tolerance on saturation.
    for (const auto& pixel:actual)
    {
        EXPECT_EQ(pixel.getRProperty(),0); EXPECT_EQ(pixel.getGProperty(),255);
        EXPECT_NEAR(pixel.getBProperty(),128,1); EXPECT_NEAR(pixel.getAProperty(),64,1);
    }
}

INSTANTIATE_TEST_SUITE_P(ClassicLayouts,DirectX9FloatTarget,testing::ValuesIn(layouts));
}
#endif
