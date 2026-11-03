// SPDX-License-Identifier: MS-PL
#if defined(CNA_RENDERER_SOFTWARE)
#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include <gtest/gtest.h>
#include <array>
#include <algorithm>

namespace {
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::Software;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using Pixel = std::array<std::uint8_t, 4>;
// The internal draw entry point consumes packed GPU bytes, not polymorphic XNA vertex objects.
struct PackedVertex { Vector3 position; Pixel color; };

class SoftwarePixelContract : public testing::Test {
protected:
    SoftwareRenderer renderer{8, 8};
    SoftwareTextureRenderer white{CNA::Internal::Graphics::ImageData{1, 1, {255,255,255,255}}};
    SoftwareSpriteBatchRenderer sprites{renderer};
    void SetUp() override {
        renderer.ApplyRasterizerState(0, 0, false);
        renderer.SetDepthTestEnabled(false);
        sprites.SetSamplerFilter(1); // Point.
    }
    void Clear(Pixel p) { renderer.Clear(p[0]/255.f, p[1]/255.f, p[2]/255.f, p[3]/255.f); }
    Pixel Read(int x = 2, int y = 2) {
        Pixel p{};
        renderer.ReadBackbuffer(x, y, 1, 1, p.data());
        return p;
    }
    void Draw(Pixel p, float depth = .25f) {
        sprites.Begin();
        sprites.Draw(white, Rectangle(0,0,8,8), Rectangle(0,0,1,1),
                     Color(p[0],p[1],p[2],p[3]), 0, Vector2::Zero, SpriteEffects::None, depth);
        sprites.End();
    }
    void Depth(int function, bool enabled = true, bool write = true) {
        renderer.ApplyDepthStencilState(enabled,write,function,false,0,0,0,0,255,255,0,false,0,0,0,0);
    }
};
}

TEST_F(SoftwarePixelContract, AdditiveRectangleOwnsEveryPixelExactlyOnce) {
    // Hand oracle: [1,5) x [2,6) covers sixteen centres; the shared diagonal belongs
    // to exactly one triangle. One+One makes double coverage observable in every channel.
    Clear({0,0,0,0});
    renderer.ApplyBlendState(0,0,0,0,0,0,{});
    sprites.Begin();
    sprites.Draw(white,Rectangle(1,2,4,4),Rectangle(0,0,1,1),Color(31,47,63,79));
    sprites.End();
    for (int y=0;y<8;++y) for (int x=0;x<8;++x)
        EXPECT_EQ(Read(x,y), (x>=1 && x<5 && y>=2 && y<6 ? Pixel{31,47,63,79} : Pixel{})) << x << ',' << y;
}

TEST_F(SoftwarePixelContract, EveryBlendFactorScalesBothSourceAndDestinationIndependently) {
    // FNA Graphics/States/Blend.cs equations, evaluated as rational fifths.
    // S=(1,2,3,4)/5, D=(4,3,2,1)/5, constant=(2,4,1,3)/5.
    // Each expected byte is round(255 * componentNumerator * factorNumerator / 25).
    // SourceAlphaSaturation uses min(As,1-Ad), alpha=1 (native blend contract).
    const std::array<std::array<int,4>,13> fifths{{
        {5,5,5,5},{0,0,0,0},{1,2,3,4},{4,3,2,1},{4,4,4,4},
        {1,1,1,1},{4,3,2,1},{1,2,3,4},{1,1,1,1},{4,4,4,4},
        {2,4,1,3},{3,1,4,2},{4,4,4,5}}};
    renderer.SetBlendFactor(.4f,.8f,.2f,.6f);
    for (int factor=0;factor<13;++factor) for (bool destination : {false,true}) {
        SCOPED_TRACE(testing::Message() << factor << " destination=" << destination);
        Clear({204,153,102,51});
        renderer.ApplyBlendState(destination?1:factor,destination?1:factor,
                                 destination?factor:1,destination?factor:1,0,0,{});
        Draw({51,102,153,204});
        Pixel expected{};
        for(int c=0;c<4;++c) expected[c]=static_cast<std::uint8_t>(
            (255*(destination?4-c:c+1)*fifths[factor][c]+12)/25);
        EXPECT_EQ(Read(),expected);
    }
}

TEST_F(SoftwarePixelContract, BlendEquationsAndIndependentAlphaHaveExactBytes) {
    // Hand arithmetic: S=(51,102,153,204), D=(204,153,102,51).
    // Add saturates at 255; subtraction clamps negative values at zero.
    const std::array<Pixel,5> expected{{{255,255,255,255},{0,0,51,153},
        {153,51,0,0},{51,102,102,51},{204,153,153,204}}};
    for (int colorFunction=0;colorFunction<5;++colorFunction)
        for(int alphaFunction=0;alphaFunction<5;++alphaFunction) {
            SCOPED_TRACE(testing::Message()<<colorFunction<<','<<alphaFunction);
            Clear({204,153,102,51});
            // MIN/MAX ignore factors, including Zero (native blend equation).
            renderer.ApplyBlendState(colorFunction>=3?1:0,alphaFunction>=3?1:0,
                colorFunction>=3?1:0,alphaFunction>=3?1:0,colorFunction,alphaFunction,{});
            Draw({51,102,153,204});
            Pixel p=expected[colorFunction]; p[3]=expected[alphaFunction][3];
            EXPECT_EQ(Read(),p);
        }
}

TEST_F(SoftwarePixelContract, AllChannelMasksPreserveUnselectedDestinationBytes) {
    // ColorWriteChannels bits R=1,G=2,B=4,A=8; masked channels retain D exactly.
    const Pixel src{17,61,129,233}, dst{213,149,83,29};
    for(int mask=0;mask<16;++mask) {
        SCOPED_TRACE(mask);
        Clear(dst);
        BlendWriteState write; write.colorWriteChannels[0]=mask;
        renderer.ApplyBlendState(0,0,1,1,0,0,write);
        Draw(src);
        Pixel expected=dst;
        for(int c=0;c<4;++c) if(mask & (1<<c)) expected[c]=src[c];
        EXPECT_EQ(Read(),expected);
    }
}

TEST_F(SoftwarePixelContract, AllDepthRelationsDistinguishLessEqualAndGreater) {
    // FNA CompareFunction.cs: incoming depth is the left operand. Stored depth=1/2.
    const bool passes[8][3]={{true,true,true},{false,false,false},{true,false,false},
        {true,true,false},{false,true,false},{false,true,true},{false,false,true},{true,false,true}};
    const float incoming[3]={.25f,.5f,.75f};
    for(int fn=0;fn<8;++fn) for(int relation=0;relation<3;++relation) {
        SCOPED_TRACE(testing::Message()<<fn<<','<<relation);
        Clear({0,0,0,0}); renderer.ClearDepth(.5f); Depth(fn);
        Draw({31,63,127,255},incoming[relation]);
        EXPECT_EQ(Read(),(passes[fn][relation]?Pixel{31,63,127,255}:Pixel{}));
        EXPECT_FLOAT_EQ(renderer.CurrentFramebuffer().depthBuffer[18],
                        passes[fn][relation]?incoming[relation]:.5f);
    }
}

TEST_F(SoftwarePixelContract, DisabledDepthAndReadOnlyDepthNeverWrite) {
    // DepthRead compares without storage; disabling depth suppresses comparison AND storage.
    for(bool enabled:{false,true}) {
        Clear({0,0,0,0}); renderer.ClearDepth(.5f); Depth(2,enabled,!enabled);
        Draw({17,31,47,63},.25f);
        EXPECT_EQ(Read(),(Pixel{17,31,47,63}));
        EXPECT_FLOAT_EQ(renderer.CurrentFramebuffer().depthBuffer[18],.5f);
        Draw({79,97,113,131},.75f);
        EXPECT_EQ(Read(),(enabled?Pixel{17,31,47,63}:Pixel{79,97,113,131}));
        EXPECT_FLOAT_EQ(renderer.CurrentFramebuffer().depthBuffer[18],.5f);
    }
}

TEST_F(SoftwarePixelContract, StencilOperationsWrapSaturateAndRespectWriteMaskOnEveryOutcome) {
    // FNA StencilOperation.cs: keep, zero, replace, increment/decrement modulo 256,
    // saturating increment/decrement, bitwise invert. Mask merges old and operated bits.
    const int initial[3]={0,0x5a,255};
    const int operated[3][8]={{0,0,0xa3,1,255,1,0,255},
        {0x5a,0,0xa3,0x5b,0x59,0x5b,0x59,0xa5},
        {255,0,0xa3,0,254,255,254,0}};
    for(int start=0;start<3;++start) for(int op=0;op<8;++op)
        for(int outcome=0;outcome<3;++outcome) for(int mask:{255,0x0f,0}) {
            SCOPED_TRACE(testing::Message()<<start<<','<<op<<','<<outcome<<','<<mask);
            Clear({0,0,0,0}); renderer.ClearDepth(.5f); renderer.ClearStencil(initial[start]);
            renderer.ApplyDepthStencilState(true,true,outcome==2?1:0,true,outcome==1?1:0,
                outcome==0?op:0,outcome==1?op:0,outcome==2?op:0,255,mask,0xa3,false,0,0,0,0);
            Draw({31,63,127,255});
            EXPECT_EQ(renderer.CurrentFramebuffer().stencilBuffer[18],
                      (initial[start]&(~mask&255))|(operated[start][op]&mask));
            EXPECT_EQ(Read(),(outcome==0?Pixel{31,63,127,255}:Pixel{}));
            EXPECT_FLOAT_EQ(renderer.CurrentFramebuffer().depthBuffer[18],outcome==0?.25f:.5f);
        }
}

TEST_F(SoftwarePixelContract, ScissorUsesAbsoluteCoordinatesInsideOffsetViewport) {
    // Sprite coordinates are viewport-local: (0,0,4,4) becomes [2,6)x[1,5).
    // Intersect with absolute scissor [3,5)x[2,4), all other pixels untouched.
    Clear({0,0,0,0}); renderer.SetViewport(2,1,4,4,0,1);
    renderer.SetScissorRect(3,2,2,2); renderer.ApplyRasterizerState(0,0,true);
    sprites.Begin();
    sprites.Draw(white,Rectangle(0,0,4,4),Rectangle(0,0,1,1),Color(17,61,129,233));
    sprites.End();
    for(int y=0;y<8;++y) for(int x=0;x<8;++x)
        EXPECT_EQ(Read(x,y),(x>=3&&x<5&&y>=2&&y<4?Pixel{17,61,129,233}:Pixel{}));
}

TEST_F(SoftwarePixelContract, PointLinearAndAnisotropicHaveHandComputedSamples) {
    // 2x2 texels: top-left R, top-right G, bottom-left B, bottom-right white.
    // At output (1,1) of a 4x4 sprite, u=v=3/8. Point picks R;
    // linear weights (3/4,1/4)^2 give (10,4,4)/16*255 -> (159,64,64).
    SoftwareTextureRenderer texture{CNA::Internal::Graphics::ImageData{2,2,
        {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255}}};
    for(int filter:{1,0,2}) {
        Clear({0,0,0,0}); sprites.SetSamplerFilter(filter);
        sprites.Begin(); sprites.Draw(texture,Rectangle(0,0,4,4),Rectangle(0,0,2,2),Color::White); sprites.End();
        EXPECT_EQ(Read(1,1),(filter==1?Pixel{255,0,0,255}:Pixel{159,64,64,255}));
    }
}

TEST_F(SoftwarePixelContract, AddressModesHandleNegativeAndPositivePeriods) {
    // 2x2 point sample at centres of source texels x=-1,2,3,4 and y=0.
    // Wrap indices=1,0,1,0; Clamp=0,1,1,1; Mirror=0,1,0,0.
    SoftwareTextureRenderer texture{CNA::Internal::Graphics::ImageData{2,2,
        {17,31,47,61, 79,97,113,131, 149,163,179,193, 211,227,239,251}}};
    const int xs[4]={-1,2,3,4};
    const int index[3][4]={{1,0,1,0},{0,1,1,1},{0,1,0,0}};
    for(int mode=0;mode<3;++mode) for(int i=0;i<4;++i) {
        SCOPED_TRACE(testing::Message()<<mode<<','<<xs[i]);
        Clear({0,0,0,0}); sprites.SetSamplerAddressMode(mode,1);
        sprites.Begin(); sprites.Draw(texture,Rectangle(0,0,1,1),Rectangle(xs[i],0,1,1),Color::White); sprites.End();
        EXPECT_EQ(Read(0,0),(index[mode][i]?Pixel{79,97,113,131}:Pixel{17,31,47,61}));
    }
}

TEST_F(SoftwarePixelContract, MipBiasAndMaxLevelSelectAuthoredChain) {
    // A 4x4 texture drawn 4x4 has lambda=0. Bias 1 selects 2x2, bias 2 selects 1x1;
    // MaxMipLevel=1 imposes a floor even with zero bias. No generated goldens.
    CNA::Internal::Graphics::ImageData image{4,4,std::vector<std::uint8_t>(64,17)};
    image.mipLevels=3;
    SoftwareTextureRenderer texture{image};
    const std::array<std::uint8_t,16> level1{31,61,97,127,31,61,97,127,31,61,97,127,31,61,97,127};
    const Pixel level2{149,179,211,239};
    texture.UpdatePixelsLevel(1,level1.data(),2,2);
    texture.UpdatePixelsLevel(2,level2.data(),1,1);
    for(int level=0;level<3;++level) {
        sprites.SetSamplerMipState(0,static_cast<float>(level));
        sprites.Begin(); sprites.Draw(texture,Rectangle(0,0,4,4),Rectangle(0,0,4,4),Color::White); sprites.End();
        EXPECT_EQ(Read(1,1),(level==0?Pixel{17,17,17,17}:level==1?Pixel{31,61,97,127}:level2));
    }
    sprites.SetSamplerMipState(1,0);
    sprites.Begin(); sprites.Draw(texture,Rectangle(0,0,4,4),Rectangle(0,0,4,4),Color::White); sprites.End();
    EXPECT_EQ(Read(1,1),(Pixel{31,61,97,127}));
}

TEST_F(SoftwarePixelContract, SaturationUsesInverseDestinationAlphaAndLeavesAlphaFactorOne) {
    // min(As=4/5,1-Ad=2/5)=2/5; RGB=(51,102,153)*2/5 -> (20,41,61).
    Clear({0,0,0,153});
    renderer.ApplyBlendState(12,12,1,1,0,0,{});
    Draw({51,102,153,204});
    EXPECT_EQ(Read(),(Pixel{20,41,61,204}));
}

TEST_F(SoftwarePixelContract, StencilRelationsCompareMaskedReferenceToStoredValue) {
    // FNA CompareFunction.cs, reference is the left operand; read mask 0x0f
    // removes different upper nibbles, leaving reference 4/5/6 against stored 5.
    const bool passes[8][3]={{true,true,true},{false,false,false},{true,false,false},
        {true,true,false},{false,true,false},{false,true,true},{false,false,true},{true,false,true}};
    for(int fn=0;fn<8;++fn) for(int relation=0;relation<3;++relation) {
        SCOPED_TRACE(testing::Message()<<fn<<','<<relation);
        Clear({0,0,0,0}); renderer.ClearStencil(0xa5);
        renderer.ApplyDepthStencilState(false,false,0,true,fn,0,0,0,15,255,
                                         0x34+relation,false,0,0,0,0);
        Draw({31,63,127,255});
        EXPECT_EQ(Read(),(passes[fn][relation]?Pixel{31,63,127,255}:Pixel{}));
        EXPECT_EQ(renderer.CurrentFramebuffer().stencilBuffer[18],0xa5);
    }
}

TEST_F(SoftwarePixelContract, OppositeWindingSelectsCullAndTwoSidedStencilState) {
    // XNA front faces wind clockwise on the screen. Identity clip positions below
    // map to (1,1),(6,1),(1,6); swapping the last pair reverses that winding.
    for(bool reverse:{false,true}) for(int cull=0;cull<3;++cull) {
        SCOPED_TRACE(testing::Message()<<reverse<<','<<cull);
        Clear({0,0,0,0}); renderer.ClearStencil(0);
        renderer.ApplyRasterizerState(cull,0,false);
        renderer.ApplyDepthStencilState(false,false,0,true,0,2,0,0,255,255,7,true,0,7,0,0);
        std::array<PackedVertex,3> vertices{{
            {Vector3(-.75f,.75f,.5f),Pixel{255,0,0,255}},
            {Vector3(.5f,.75f,.5f),Pixel{255,0,0,255}},
            {Vector3(-.75f,-.5f,.5f),Pixel{255,0,0,255}}}};
        if(reverse) std::swap(vertices[1],vertices[2]);
        SoftwareVertexBufferRenderer buffer{3}; buffer.SetData(vertices.data(),3,sizeof(PackedVertex));
        renderer.DrawColoredPrimitives(buffer,Matrix::getIdentityProperty(),Matrix::getIdentityProperty(),Matrix::getIdentityProperty(),
                                       PrimitiveType::TriangleList,1);
        const bool visible=cull==0 || (reverse?cull==1:cull==2);
        EXPECT_EQ(Read(),(visible?Pixel{255,0,0,255}:Pixel{}));
        EXPECT_EQ(renderer.CurrentFramebuffer().stencilBuffer[18],visible?(reverse?255:7):0);
    }
}

TEST_F(SoftwarePixelContract, ThreeDimensionalViewportMapsDepthAndOffset) {
    // Clip centre (0,0,z=1/4) maps to viewport centre (4,3) and depth
    // MinDepth + z*(MaxDepth-MinDepth) = 1/4 + (1/4)*(1/2) = 3/8.
    Clear({0,0,0,0}); renderer.ClearDepth(1); Depth(0);
    renderer.SetViewport(2,1,4,4,.25f,.75f);
    const std::array<PackedVertex,3> vertices{{
        {Vector3(-1,1,.25f),Pixel{0,0,255,255}}, {Vector3(1,1,.25f),Pixel{0,0,255,255}},
        {Vector3(0,-1,.25f),Pixel{0,0,255,255}}}};
    SoftwareVertexBufferRenderer buffer{3}; buffer.SetData(vertices.data(),3,sizeof(PackedVertex));
    renderer.DrawColoredPrimitives(buffer,Matrix::getIdentityProperty(),Matrix::getIdentityProperty(),Matrix::getIdentityProperty(),
                                   PrimitiveType::TriangleList,1);
    EXPECT_EQ(Read(4,3),(Pixel{0,0,255,255}));
    EXPECT_FLOAT_EQ(renderer.CurrentFramebuffer().depthBuffer[3*8+4],.375f);
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) if(x<2||x>=6||y<1||y>=5) {
        EXPECT_EQ(Read(x,y),Pixel{});
        EXPECT_FLOAT_EQ(renderer.CurrentFramebuffer().depthBuffer[y*8+x],1);
    }
}

TEST_F(SoftwarePixelContract, AnisotropicMinificationAveragesMajorAxisWithoutBlurringRows) {
    // Hand-built 4x4: outer columns red=200, inner columns red=0; every row has
    // a different green. A 4:1 horizontal footprint has four texel-centre taps:
    // red=(200+0+0+200)/4=100, while the one-texel vertical footprint retains green.
    // An isotropic linear sample at u=1/2 sees only the two black middle columns.
    const std::array<std::uint8_t,4> green{16,64,128,224};
    CNA::Internal::Graphics::ImageData image{4,4,{}};
    for(int y=0;y<4;++y) for(int x=0;x<4;++x)
        image.pixels.insert(image.pixels.end(),{static_cast<std::uint8_t>(x==0||x==3?200:0),green[y],0,255});
    SoftwareTextureRenderer texture{image};
    for(int filter:{0,2}) {
        sprites.SetSamplerFilter(filter); sprites.SetSamplerMaxAnisotropy(4);
        sprites.Begin(); sprites.Draw(texture,Rectangle(0,0,1,4),Rectangle(0,0,4,4),Color::White); sprites.End();
        for(int y=0;y<4;++y)
            EXPECT_EQ(Read(0,y),(Pixel{static_cast<std::uint8_t>(filter==2?100:0),green[y],0,255}));
    }
}
#endif
