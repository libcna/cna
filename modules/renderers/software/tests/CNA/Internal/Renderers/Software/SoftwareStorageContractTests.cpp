// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SOFTWARE)
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include <gtest/gtest.h>
#include <array>
#include <vector>

namespace {
using namespace CNA::Internal::Renderers::Software;
using CNA::Internal::Graphics::ImageData;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
struct FormatCase {
    SurfaceFormat format;
    std::vector<std::uint8_t> bytes;
    std::array<float,4> rgba;
};
// Oracle: FNA PackedVector/*.cs ToVector* bit fields, IEEE-754 binary32/binary16.
// Sampling's absent channels follow D3D9 D3DFORMAT, not PackedVector.ToVector4:
// https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dformat (Remarks).
// Absent channels are ONE except A8's RGB=ZERO.
// Unequal channels distinguish RGBA/BGRA swaps; negative/HDR floats detect RGBA8 narrowing.
const std::vector<FormatCase> formats{
    {SurfaceFormat::Color,{17,61,129,233},{17/255.f,61/255.f,129/255.f,233/255.f}},
    // R=17,G=35,B=9: (17<<11)|(35<<5)|9 = 0x8c69.
    {SurfaceFormat::Bgr565,{0x69,0x8c},{17/31.f,35/63.f,9/31.f,1}},
    // A=1,R=17,G=19,B=9: 0x8000|0x4400|0x260|9 = 0xc669.
    {SurfaceFormat::Bgra5551,{0x69,0xc6},{17/31.f,19/31.f,9/31.f,1}},
    {SurfaceFormat::Bgra4444,{0x59,0xc3},{3/15.f,5/15.f,9/15.f,12/15.f}},
    // R=1,G=2,B=3,A=2: 1|(2<<10)|(3<<20)|(2<<30) = 0x80300801.
    {SurfaceFormat::Rgba1010102,{1,8,0x30,0x80},{1/1023.f,2/1023.f,3/1023.f,2/3.f}},
    {SurfaceFormat::Rg32,{0x34,0x12,0xcd,0xab},{4660/65535.f,43981/65535.f,1,1}},
    {SurfaceFormat::Rgba64,{0x34,0x12,0xcd,0xab,0x78,0x56,0xff,0xff},
        {4660/65535.f,43981/65535.f,22136/65535.f,1}},
    {SurfaceFormat::Alpha8,{0x63},{0,0,0,99/255.f}},
    {SurfaceFormat::Single,{0,0,0x20,0xc0},{-2.5f,1,1,1}},
    {SurfaceFormat::Vector2,{0,0,0x20,0xc0,0,0,0x50,0x40},{-2.5f,3.25f,1,1}},
    {SurfaceFormat::Vector4,{0,0,0x20,0xc0,0,0,0x50,0x40,0,0,0,0x3e,0,0,0,0x3f},
        {-2.5f,3.25f,.125f,.5f}},
    {SurfaceFormat::HalfSingle,{0,0xc1},{-2.5f,1,1,1}},
    {SurfaceFormat::HalfVector2,{0,0xc1,0x80,0x42},{-2.5f,3.25f,1,1}},
    {SurfaceFormat::HalfVector4,{0,0xc1,0x80,0x42,0,0x30,0,0x38},{-2.5f,3.25f,.125f,.5f}}
};
}

TEST(SoftwareStorageContract, EveryDeclaredFormatPreservesBytesAndDecodesIndependentChannels) {
    for(const auto& test:formats) {
        SCOPED_TRACE(static_cast<int>(test.format));
        ImageData image{1,1,test.bytes}; image.surfaceFormat=static_cast<int>(test.format);
        SoftwareTextureRenderer texture{image};
        std::vector<std::uint8_t> read(test.bytes.size()+2,0xa5);
        ASSERT_TRUE(texture.GetData(0,0,0,1,1,read.data()+1,static_cast<int>(test.bytes.size())));
        EXPECT_EQ(read.front(),0xa5); EXPECT_EQ(read.back(),0xa5);
        EXPECT_EQ(std::vector<std::uint8_t>(read.begin()+1,read.end()-1),test.bytes);
        float r=0,g=0,b=0,a=0; texture.FetchColorTexel(0,0,0,r,g,b,a);
        EXPECT_FLOAT_EQ(r,test.rgba[0]); EXPECT_FLOAT_EQ(g,test.rgba[1]);
        EXPECT_FLOAT_EQ(b,test.rgba[2]); EXPECT_FLOAT_EQ(a,test.rgba[3]);
    }
}

TEST(SoftwareStorageContract, Texture3DSubBoxPreservesOtherSlicesRowsAndMip) {
    // Row-major box: x changes fastest, then y, then z. Marker byte = 1+x+3*y+6*z.
    SoftwareTexture3DRenderer texture{3,2,2,true,static_cast<int>(SurfaceFormat::Color)};
    std::array<std::uint8_t,48> base{};
    for(int voxel=0;voxel<12;++voxel) for(int c=0;c<4;++c) base[voxel*4+c]=1+voxel+c*32;
    ASSERT_TRUE(texture.SetData(0,0,0,0,3,2,2,base.data(),base.size()));
    const std::array<std::uint8_t,4> mip{197,211,227,239};
    ASSERT_TRUE(texture.SetData(1,0,0,0,1,1,1,mip.data(),4));
    const std::array<std::uint8_t,8> patch{17,31,47,61,79,97,113,131};
    ASSERT_TRUE(texture.SetData(0,1,1,1,2,1,1,patch.data(),8));
    // Only voxels 10 and 11 change: 1+3*1+6*1 = 10.
    std::copy(patch.begin(),patch.end(),base.begin()+40);
    std::array<std::uint8_t,48> actual{};
    ASSERT_TRUE(texture.GetData(0,0,0,0,3,2,2,actual.data(),actual.size()));
    EXPECT_EQ(actual,base);
    std::array<std::uint8_t,4> actualMip{};
    ASSERT_TRUE(texture.GetData(1,0,0,0,1,1,1,actualMip.data(),4));
    EXPECT_EQ(actualMip,mip);
    std::array<std::uint8_t,8> readPatch{};
    ASSERT_TRUE(texture.GetData(0,1,1,1,2,1,1,readPatch.data(),8));
    EXPECT_EQ(readPatch,patch);
}

TEST(SoftwareStorageContract, RenderTargetCubeFacesRetainDistinctClearColors) {
    // Six independently bound faces must retain their own colour plane across switches.
    SoftwareRenderer renderer{2,2};
    auto cube=renderer.CreateRenderTargetCube(2,0,true,false,0);
    ASSERT_NE(cube,nullptr);
    for(int face=0;face<6;++face) {
        renderer.SetRenderTargetCubeFace(cube.get(),face);
        renderer.Clear((face+1)*17/255.f,(face+1)*31/255.f,(face+1)*41/255.f,1);
    }
    renderer.SetRenderTarget2D(nullptr);
    for(int face=0;face<6;++face) {
        SCOPED_TRACE(face);
        std::array<std::uint8_t,16> bytes{};
        ASSERT_TRUE(cube->GetData(face,0,0,0,2,2,bytes.data(),bytes.size()));
        for(int pixel=0;pixel<4;++pixel) {
            EXPECT_EQ(bytes[pixel*4],(face+1)*17);
            EXPECT_EQ(bytes[pixel*4+1],(face+1)*31);
            EXPECT_EQ(bytes[pixel*4+2],(face+1)*41);
            EXPECT_EQ(bytes[pixel*4+3],255);
        }
    }
}
#endif
