// SPDX-License-Identifier: MS-PL
// GPU execution is owner-only through tools/platform/run_gpu_tests_private.sh --exec.
#if defined(CNA_RENDERER_SDLGPU) || defined(CNA_RENDERER_PRESENT_SDLGPU)
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include <gtest/gtest.h>
#include <array>
#include <vector>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

TEST(SdlGpuTextureTransfer, PaddedRowsUploadOnlyLogicalTexels)
{
    GraphicsDevice device;
    Texture2D source(device,3,2);
    auto& texture=dynamic_cast<SdlGpuTextureRenderer&>(source.GetRenderer());
    // Three RGBA texels = 12 bytes/row; stride 16 has four poison bytes that must be skipped.
    const std::uint8_t rows[]{255,0,0,255, 0,255,0,255, 0,0,255,255, 23,24,25,26,
                              0,255,255,255, 255,0,255,255, 255,255,0,255, 33,34,35,36};
    texture.UpdatePixels(rows,16);
    RenderTarget2D target(device,3,2); SpriteBatch sprites(device);
    device.SetRenderTarget(&target); device.Clear(Color::Black);
    sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&SamplerState::PointClamp,nullptr,nullptr);
    sprites.Draw(source,Rectangle(0,0,3,2),Color::White); sprites.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    std::array<Color,6> pixels; target.GetData(pixels.data(),6);
    const std::array<Color,6> expected{Color::Red,Color::Lime,Color::Blue,Color::Cyan,Color::Magenta,Color::Yellow};
    EXPECT_EQ(pixels,expected);
    EXPECT_THROW(texture.UpdatePixels(rows,11),std::invalid_argument);
}

TEST(SdlGpuTextureTransfer, OddWidthTargetSubrectanglePreservesNeighborsAndDestinationGuards)
{
    GraphicsDevice device;
    RenderTarget2D target(device,7,3,false,SurfaceFormat::Color,DepthFormat::None,0,RenderTargetUsage::PreserveContents);
    std::array<Color,21> initial; initial.fill(Color::Blue); target.SetData(initial.data(),21);
    const Rectangle patch(2,1,3,2);
    const std::array<Color,8> input{Color::Black,Color::Red,Color::Lime,Color::Yellow,
                                  Color::Cyan,Color::Magenta,Color::White,Color::Black};
    target.SetData(0,&patch,input.data(),1,6);
    std::array<Color,21> whole; target.GetData(whole.data(),21);
    // Source indices 1..6 overwrite exactly x=2..4,y=1..2; all other texels stay blue.
    for (int y=0;y<3;++y) for (int x=0;x<7;++x) {
        const Color expected=(y>=1 && x>=2 && x<5) ? input[1+(y-1)*3+x-2] : Color::Blue;
        EXPECT_EQ(whole[y*7+x],expected) << x << ',' << y;
    }
    std::array<Color,8> guarded; guarded.fill(Color::Transparent);
    target.GetData(0,&patch,guarded.data(),1,6);
    EXPECT_EQ(guarded.front(),Color::Transparent); EXPECT_EQ(guarded.back(),Color::Transparent);
    for (int i=1;i<7;++i) EXPECT_EQ(guarded[i],input[i]);
}

TEST(SdlGpuTextureTransfer, DxtRowsSubrectanglesAndTinyMipsUseBlockExtents)
{
    GraphicsDevice device;
    auto& renderer=dynamic_cast<SdlGpuRenderer&>(device.GetRenderer());
    for (auto format : {SurfaceFormat::Dxt1,SurfaceFormat::Dxt3,SurfaceFormat::Dxt5}) {
        const int blockBytes=format==SurfaceFormat::Dxt1 ? 8 : 16;
        // A 7x5 level uses ceil(7/4)*ceil(5/4)=4 blocks. Row stride has an extra block.
        CNA::Internal::Graphics::ImageData image{7,5,{},3,static_cast<int>(format)};
        image.pixels.resize(4*blockBytes);
        SdlGpuTextureRenderer texture(renderer,image);
        std::vector<std::uint8_t> source(6*blockBytes,0xee);
        for (int row=0;row<2;++row) for (int block=0;block<2;++block)
            for (int byte=0;byte<blockBytes;++byte) source[(row*3+block)*blockBytes+byte]=static_cast<std::uint8_t>((row*2+block)*32+byte);
        texture.UpdatePixels(source.data(),3*blockBytes);
        std::vector<std::uint8_t> out(2*blockBytes+2,0xcc);
        // x=4,w=3 is a legal edge block, y=0,h=5 spans both block rows.
        ASSERT_TRUE(texture.GetData(0,4,0,3,5,out.data()+1,2*blockBytes));
        EXPECT_EQ(out.front(),0xcc); EXPECT_EQ(out.back(),0xcc);
        for (int row=0;row<2;++row) for (int byte=0;byte<blockBytes;++byte)
            EXPECT_EQ(out[1+row*blockBytes+byte],(row*2+1)*32+byte);
        EXPECT_FALSE(texture.GetData(0,1,0,4,4,out.data(),2*blockBytes)); // unaligned origin
        EXPECT_FALSE(texture.GetData(0,0,0,3,4,out.data(),2*blockBytes)); // partial non-edge block
        EXPECT_FALSE(texture.GetData(0,4,0,3,5,out.data(),2*blockBytes-1));
        EXPECT_FALSE(texture.GetData(3,0,0,1,1,out.data(),blockBytes));
        // 7x5 -> 3x2 -> 1x1: each smaller level still occupies exactly ONE BC block.
        std::vector<std::uint8_t> mip(blockBytes,0x5a);
        texture.UpdatePixelsLevel(1,mip.data(),3,2);
        std::fill(mip.begin(),mip.end(),0xa5); texture.UpdatePixelsLevel(2,mip.data(),1,1);
        ASSERT_TRUE(texture.GetData(1,0,0,3,2,out.data(),blockBytes));
        for (int byte=0;byte<blockBytes;++byte) EXPECT_EQ(out[byte],0x5a);
        ASSERT_TRUE(texture.GetData(2,0,0,1,1,out.data(),blockBytes));
        for (int byte=0;byte<blockBytes;++byte) EXPECT_EQ(out[byte],0xa5);
    }
}

TEST(SdlGpuTextureTransfer, SamplerMipClampUsesMostDetailedAllowedLevel)
{
    GraphicsDevice device; Texture2D source(device,4,4,true,SurfaceFormat::Color);
    const std::array<Color,16> red{Color::Red,Color::Red,Color::Red,Color::Red,
        Color::Red,Color::Red,Color::Red,Color::Red,Color::Red,Color::Red,Color::Red,Color::Red,
        Color::Red,Color::Red,Color::Red,Color::Red};
    const std::array<Color,4> green{Color::Lime,Color::Lime,Color::Lime,Color::Lime};
    const Color blue=Color::Blue;
    source.SetData(0,nullptr,red.data(),0,16); source.SetData(1,nullptr,green.data(),0,4);
    source.SetData(2,nullptr,&blue,0,1);
    RenderTarget2D target(device,4,4); SpriteBatch sprites(device);
    for (int level : {0,1,2,-1,99,0}) {
        SamplerState sampler; sampler.setFilterProperty(TextureFilter::Point);
        sampler.setMaxMipLevelProperty(level);
        device.SetRenderTarget(&target); device.Clear(Color::Black);
        sprites.Begin(SpriteSortMode::Deferred,&BlendState::Opaque,&sampler,nullptr,nullptr);
        sprites.Draw(source,Rectangle(0,0,4,4),Color::White); sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        std::array<Color,16> pixels; target.GetData(pixels.data(),16);
        // XNA MaxMipLevel is the lowest allowed LOD. Negative signed values become a huge
        // unsigned D3D sampler value and clamp to the final stored level. XNA SamplerState.Apply
        // IL_0342..0355 passes cachedMaxMipLevel to D3DSAMP_MAXMIPLEVEL as uint32.
        const Color expected=level==0 ? Color::Red : level==1 ? Color::Lime : Color::Blue;
        for (const auto& pixel : pixels) EXPECT_EQ(pixel,expected) << level;
    }
}
#endif
