// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Common/GpuPresentation.hpp"
#include <gtest/gtest.h>
#include <limits>
#include <vector>
using namespace CNA::Internal::Renderers;
namespace {
using Request = IGraphicsRenderer::RendererPresentRegionEXT;
Request Client(int width=16,int height=16)
{ Request r; r.targetWidth=width; r.targetHeight=height; return r; }
class NativeRecorder final : public CNA::Platform::IPlatformSurfacePresenter
{
public:
    void SetScaleMode(CNA::Platform::PresentScaleMode m,CNA::Platform::PresentFilter f) override
    { mode=m; filter=f; }
    bool SetVSync(bool) override { return false; }
    void GetTargetSize(int& w,int& h) const override { w=24; h=20; }
    void Present(const CNA::Platform::SurfaceFrame&) override { FAIL()<<"must use dirty region"; }
    bool PresentRegion(const CNA::Platform::SurfaceFrame& f,const CNA::Platform::SurfaceDestination& d) override
    {
        ++calls; destination=d; width=f.width; height=f.height;
        const int stride=f.strideBytes?f.strideBytes:f.width*4;
        for(int y=0;y<f.height;++y) bytes.insert(bytes.end(),f.pixels+y*stride,f.pixels+y*stride+f.width*4);
        return accepts;
    }
    CNA::Platform::PresentScaleMode mode=CNA::Platform::PresentScaleMode::Letterbox;
    CNA::Platform::PresentFilter filter=CNA::Platform::PresentFilter::Linear;
    CNA::Platform::SurfaceDestination destination;
    int calls=0,width=0,height=0;
    bool accepts=true;
    std::vector<std::uint8_t> bytes;
};
}
TEST(GpuPresentation, SourceSelectionUsesLogicalToPhysicalCoordinates)
{
    auto r=Client(); r.hasSourceRectangle=true;
    r.sourceX=4; r.sourceY=2; r.sourceWidth=2; r.sourceHeight=4;
    // 8x8 logical ->16x16 physical: source(4,2,2,4) becomes(8,4,4,8).
    const auto m=ResolveGpuPresentMappingEXT(r,8,8,{0,0,16,16},16,16);
    EXPECT_EQ(m.source,(std::array<float,4>{8,4,4,8}));
    EXPECT_EQ(m.capture.x,8); EXPECT_EQ(m.capture.y,4);
    EXPECT_EQ(m.capture.width,4); EXPECT_EQ(m.capture.height,8);
    EXPECT_EQ(m.sourceWidth,2); EXPECT_EQ(m.sourceHeight,4);
    EXPECT_EQ(m.destination.width,16); EXPECT_EQ(m.destination.height,16);
}
TEST(GpuPresentation, ClippingPreservesTheOriginalDestinationAndBothSourceAxes)
{
    auto r=Client(); r.hasDestinationRectangle=true;
    r.destinationX=-4; r.destinationY=-4; r.destinationWidth=8; r.destinationHeight=8;
    const auto m=ResolveGpuPresentMappingEXT(r,8,8,{0,0,16,16},16,16);
    // dx=-4+sx: visible[0,4)^2 comes from logical source[4,8)^2, no restretch.
    EXPECT_EQ(m.destination.x,-4); EXPECT_EQ(m.destination.y,-4);
    EXPECT_EQ(m.destination.width,8); EXPECT_EQ(m.destination.height,8);
    EXPECT_EQ(m.clip.x,0); EXPECT_EQ(m.clip.y,0);
    EXPECT_EQ(m.clip.width,4); EXPECT_EQ(m.clip.height,4);
    EXPECT_EQ(m.source,(std::array<float,4>{0,0,16,16}));
}
TEST(GpuPresentation, LetterboxBarsAreNotSourcePixelsAndNullDestinationUsesForeignBounds)
{
    auto r=Client(24,20);
    // 8x4 logical image in16x16 physical: 16x8 image, y=4 bars outside it.
    const auto m=ResolveGpuPresentMappingEXT(r,8,4,{0,4,16,8},16,16);
    EXPECT_EQ(m.source,(std::array<float,4>{0,4,16,8}));
    EXPECT_EQ(m.destination.width,24); EXPECT_EQ(m.destination.height,20);
    EXPECT_EQ(m.clip.width,24); EXPECT_EQ(m.clip.height,20);
}
TEST(GpuPresentation, FractionalSourceAndExtremeDestinationDoNotOverflowOrRestretch)
{
    auto r=Client(); r.hasSourceRectangle=true;
    r.sourceX=1; r.sourceY=1; r.sourceWidth=1; r.sourceHeight=1;
    r.hasDestinationRectangle=true; r.destinationX=-1; r.destinationY=15;
    r.destinationWidth=std::numeric_limits<int>::max(); r.destinationHeight=std::numeric_limits<int>::max();
    // 4 logical ->6 physical: selected[1,2) has edges1.5,3 and capture[1,3).
    const auto m=ResolveGpuPresentMappingEXT(r,4,4,{0,0,6,6},6,6);
    EXPECT_EQ(m.source,(std::array<float,4>{1.5f,1.5f,1.5f,1.5f}));
    EXPECT_EQ(m.capture.x,1); EXPECT_EQ(m.capture.width,2);
    EXPECT_EQ(m.destination.x,-1); EXPECT_EQ(m.destination.width,std::numeric_limits<int>::max());
    EXPECT_EQ(m.clip.width,16); EXPECT_EQ(m.clip.y,15); EXPECT_EQ(m.clip.height,1);
}
TEST(GpuPresentation, NativeAliasUsesOwnHistoryAndDistinctStableIdUsesReadback)
{
    // Native override token alone does not make a window foreign: shared adoption resolves stable ids.
    auto r=Client(); r.targetWindowId=7; r.overrideWindowHandle=0x1234;
    EXPECT_FALSE(IsForeignGpuPresentEXT(r,7));
    r.targetWindowId=9; EXPECT_TRUE(IsForeignGpuPresentEXT(r,7));
    r.targetWindowId=0; EXPECT_FALSE(IsForeignGpuPresentEXT(r,7));
}
TEST(GpuPresentation, ReadbackSelectsSourceTexelCentersAndPreservesOriginalNativeDestination)
{
    auto r=Client(24,20); r.hasSourceRectangle=true;
    r.sourceX=1; r.sourceY=1; r.sourceWidth=2; r.sourceHeight=2;
    r.hasDestinationRectangle=true; r.destinationX=-2; r.destinationY=2;
    r.destinationWidth=8; r.destinationHeight=8;
    NativeRecorder presenter; r.targetPresenter=&presenter;
    const auto m=ResolveGpuPresentMappingEXT(r,4,4,{0,0,8,8},8,8);
    int reads=0;
    ASSERT_TRUE(PresentGpuReadbackEXT(r,m,[&](int x,int y,int w,int h,std::uint8_t* data) {
        ++reads;
        EXPECT_EQ(x,2); EXPECT_EQ(y,2); EXPECT_EQ(w,4); EXPECT_EQ(h,4);
        // Independent authored physical bytes: R=x,G=y,B=37,A=255.
        for(int row=0;row<h;++row) for(int col=0;col<w;++col) {
            auto* p=data+(row*w+col)*4; p[0]=x+col; p[1]=y+row; p[2]=37; p[3]=255;
        }
    }));
    // Logical centers at physical(3,3),(5,3),(3,5),(5,5); no CNA result is a golden.
    EXPECT_EQ(presenter.bytes,(std::vector<std::uint8_t>{3,3,37,255,5,3,37,255,3,5,37,255,5,5,37,255}));
    EXPECT_EQ(reads,1); EXPECT_EQ(presenter.calls,1);
    EXPECT_EQ(presenter.width,2); EXPECT_EQ(presenter.height,2);
    EXPECT_EQ(presenter.destination.x,-2); EXPECT_EQ(presenter.destination.y,2);
    EXPECT_EQ(presenter.destination.width,8); EXPECT_EQ(presenter.destination.height,8);
    EXPECT_EQ(presenter.filter,CNA::Platform::PresentFilter::Nearest);
    EXPECT_EQ(presenter.mode,CNA::Platform::PresentScaleMode::Stretch);
}
TEST(GpuPresentation, MissingPresenterDoesNotReadAndRefusalNeverFallsBackToOrdinaryPresent)
{
    auto r=Client(); const auto m=ResolveGpuPresentMappingEXT(r,8,8,{0,0,8,8},8,8);
    int reads=0;
    const auto read=[&](int,int,int w,int h,std::uint8_t* p) {++reads;std::fill_n(p,w*h*4,255);};
    EXPECT_FALSE(PresentGpuReadbackEXT(r,m,read)); EXPECT_EQ(reads,0);
    NativeRecorder presenter; presenter.accepts=false; r.targetPresenter=&presenter;
    EXPECT_FALSE(PresentGpuReadbackEXT(r,m,read));
    EXPECT_EQ(reads,1); EXPECT_EQ(presenter.calls,1);
}
TEST(GpuPresentation, ReadbackFailureDoesNotPresentAndLaterCallCanRecover)
{
    // Owner's readback route must propagate download failure before any native update.
    auto r=Client(); NativeRecorder presenter; r.targetPresenter=&presenter;
    const auto m=ResolveGpuPresentMappingEXT(r,1,1,{0,0,1,1},1,1);
    EXPECT_THROW(PresentGpuReadbackEXT(r,m,[](int,int,int,int,std::uint8_t*) {
        throw std::runtime_error("injected readback failure");
    }),std::runtime_error);
    EXPECT_EQ(presenter.calls,0); EXPECT_TRUE(presenter.bytes.empty());
    ASSERT_TRUE(PresentGpuReadbackEXT(r,m,[](int,int,int,int,std::uint8_t* p) {
        p[0]=0; p[1]=0; p[2]=255; p[3]=255; // independent opaque blue input
    }));
    EXPECT_EQ(presenter.calls,1);
    EXPECT_EQ(presenter.bytes,(std::vector<std::uint8_t>{0,0,255,255}));
}
TEST(GpuPresentation, OverscanKeepsExistingEdgeClampAndMalformedSourcesAreRefused)
{
    auto r=Client();
    // CNA PresentedPixelSpan clamps overscan to visible edges; it is not native XNA parity.
    const auto m=ResolveGpuPresentMappingEXT(r,8,8,{-4,0,24,16},16,16);
    EXPECT_EQ(m.source,(std::array<float,4>{-4,0,24,16}));
    EXPECT_EQ(m.capture.x,0); EXPECT_EQ(m.capture.y,0);
    EXPECT_EQ(m.capture.width,16); EXPECT_EQ(m.capture.height,16);
    NativeRecorder presenter; r.targetPresenter=&presenter;
    ASSERT_TRUE(PresentGpuReadbackEXT(r,m,[](int,int,int w,int h,std::uint8_t* data) {
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            auto* p=data+(y*w+x)*4; p[0]=x; p[1]=y; p[2]=0; p[3]=255;
        }
    }));
    // x=-4+(i+.5)*3 -> [-2.5,.5,3.5,6.5,9.5,12.5,15.5,18.5], floor+clamp.
    const std::uint8_t expectedX[8]={0,0,3,6,9,12,15,15};
    ASSERT_EQ(presenter.bytes.size(),256u);
    for(int x=0;x<8;++x) EXPECT_EQ(presenter.bytes[x*4],expectedX[x]);
    EXPECT_THROW(ResolveGpuPresentMappingEXT(r,0,8,{0,0,16,16},16,16),std::invalid_argument);
    r.hasSourceRectangle=true; r.sourceX=7; r.sourceY=0; r.sourceWidth=2; r.sourceHeight=1;
    EXPECT_THROW(ResolveGpuPresentMappingEXT(r,8,8,{0,0,16,16},16,16),std::invalid_argument);
}
