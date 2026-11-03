// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include <metagl/metagl.hpp>
#include <array>
#include <cstring>
#include <memory>
#include "CNA/RendererTestGate.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"

namespace {
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;
using namespace CNA::Testing::Renderers;

// Observe the actual native transfer, not only its values: desktop Mesa accepts HALF_FLOAT
// even on an ES context, hiding the unsupported pair used on the Android guest driver.
struct ReadObserver {
    using ReadFn = void (GL_APIENTRY *)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
    static inline metagl::GlGetProcAddressFn original = nullptr;
    static inline ReadFn read = nullptr;
    static inline int calls = 0;
    static inline bool rgbaFloatOnly = true;
    static void GL_APIENTRY Observe(GLint x,GLint y,GLsizei w,GLsizei h,GLenum format,GLenum type,void* data) {
        ++calls;
        rgbaFloatOnly = rgbaFloatOnly && format == GL_RGBA && type == GL_FLOAT;
        read(x,y,w,h,format,type,data);
    }
    static void* Load(const char* name) {
        return std::strcmp(name,"glReadPixels")==0 ? reinterpret_cast<void*>(&Observe) : original(name);
    }
};

// Supply the loader at the existing platform seam so it reaches the renderer's dispatch
// even when the test executable and shared CNA carry separate private meta-gl tables.
class ObservedContext final : public CNA::Platform::IPlatformGlContext {
    IPlatformGlContext& inner;
public:
    explicit ObservedContext(IPlatformGlContext& context):inner(context) {}
    CNA::Platform::GlContextHandle CreateContext(CNA::Platform::WindowId window,const CNA::Platform::GlContextDescription& desc) override {return inner.CreateContext(window,desc);}
    void DestroyContext(CNA::Platform::GlContextHandle context) override {inner.DestroyContext(context);}
    void MakeCurrent(CNA::Platform::WindowId window,CNA::Platform::GlContextHandle context) override {inner.MakeCurrent(window,context);}
    CNA::Platform::GlContextBinding GetCurrentBinding() const override {return inner.GetCurrentBinding();}
    void SwapBuffers(CNA::Platform::WindowId window) override {inner.SwapBuffers(window);}
    bool SetSwapInterval(int interval) override {return inner.SetSwapInterval(interval);}
    void* GetProcAddress(const std::string& name) const override {return inner.GetProcAddress(name);}
    CNA::Platform::GlProcAddressLoader GetProcAddressLoader() const override {
        ReadObserver::original=inner.GetProcAddressLoader();
        ReadObserver::read=reinterpret_cast<ReadObserver::ReadFn>(ReadObserver::original("glReadPixels"));
        return &ReadObserver::Load;
    }
    CNA::Platform::GlContextDescription GetContextAttributes(CNA::Platform::GlContextHandle context) const override {return inner.GetContextAttributes(context);}
};
class ObservedPlatform final : public CNA::Platform::Testing::PlatformTestDecorator {
    std::unique_ptr<ObservedContext> context;
public:
    ObservedPlatform() {
        if (auto* inner=GetInner().GetGlContext()) context=std::make_unique<ObservedContext>(*inner);
    }
    CNA::Platform::IPlatformGlContext* GetGlContext() override {return context.get();}
};
}

TEST(EasyGLFloatReadbackTest, EsFloatTransfersUseRgbaFloatAndPreservePackedSubrectangles) {
    if (!CNA_RENDERER_IS(OpenGLES3, WebGL2))
        GTEST_SKIP() << "GLES transfer contract; desktop GL may read native channel/half formats";
    ObservedPlatform platform;
    if (!platform.GetGlContext()) GTEST_SKIP() << "the platform has no GL context service";
    CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,PresentationParameters::CreateWindowedEXT(800, 480));
    auto* renderer=dynamic_cast<CNA::Internal::Renderers::EasyGL::EasyGLRenderer*>(&device.GetRenderer());
    ASSERT_NE(nullptr,renderer);
    ReadObserver::calls=0;ReadObserver::rgbaFloatOnly=true;
    int measured=0;
    const Rectangle rectangle(1,1,3,2);
    const auto verify=[&]<class T>(SurfaceFormat format,const T& expected) {
        SCOPED_TRACE(static_cast<int>(format));
        if (!device.SupportsSurfaceFormatAsRenderTargetEXT(format)) return;
        ++measured;
        RenderTarget2D target(device,5,5,false,format,DepthFormat::None);
        device.SetRenderTarget(&target);
        device.Clear(-2.0f,0.5f,2.0f,1.0f);
        device.SetRenderTarget(nullptr);
        std::array<T,8> values{};
        target.GetData(0,&rectangle,values.data(),1,6);
        EXPECT_EQ(T{},values.front());EXPECT_EQ(T{},values.back());
        for (int i=1;i<=6;++i) EXPECT_EQ(expected,values[i]);
        RenderTargetCube cube(device,5,false,format,DepthFormat::None);
        device.SetRenderTarget(&cube,CubeMapFace::PositiveX);
        device.Clear(-2.0f,0.5f,2.0f,1.0f);
        device.SetRenderTarget(nullptr);
        values.fill(T{});
        cube.GetData(CubeMapFace::PositiveX,0,&rectangle,values.data(),1,6);
        EXPECT_EQ(T{},values.front());EXPECT_EQ(T{},values.back());
        for (int i=1;i<=6;++i) EXPECT_EQ(expected,values[i]);
    };
    verify(SurfaceFormat::Single,-2.0f);
    verify(SurfaceFormat::Vector2,Vector2(-2.0f,0.5f));
    verify(SurfaceFormat::Vector4,Vector4(-2.0f,0.5f,2.0f,1.0f));
    // IEEE 754 binary16: -2=C000, 0.5=3800, 2=4000, 1=3C00.
    // Literal packed bits avoid sharing the renderer's float-to-half conversion oracle.
    HalfSingle half; half.setPackedValueProperty(0xC000u);
    HalfVector2 half2; half2.setPackedValueProperty(0x3800C000u);
    HalfVector4 half4; half4.setPackedValueProperty(0x3C0040003800C000ull);
    verify(SurfaceFormat::HalfSingle,half);
    verify(SurfaceFormat::HalfVector2,half2);
    verify(SurfaceFormat::HalfVector4,half4);
    verify(SurfaceFormat::HdrBlendable,half4);
    if (measured==0) GTEST_SKIP() << "this context reports no floating-point render targets";
    EXPECT_EQ(2*measured,ReadObserver::calls);
    EXPECT_TRUE(ReadObserver::rgbaFloatOnly);
}


TEST(EasyGLFloatReadbackTest, OddWidthUploadAndAsymmetricTargetSubrectangleKeepRowsAndGuards)
{
    if (!CNA_RENDERER_IS(OpenGLES3,OpenGL33,WebGL2)) GTEST_SKIP();
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(800,480));
    // Oracle: literal RGBA8 texels, point sampled one-to-one. Nonuniform rows expose
    // a missing/double vertical flip; x=1,y=2 exposes subrectangle offset mistakes.
    std::array<Color,25> sourcePixels;
    for (int y=0;y<5;++y)
        for (int x=0;x<5;++x)
            sourcePixels[y*5+x]=Color(10+x*30,20+y*40,50+x+y,255);
    Texture2D source(device,5,5);
    source.SetData(sourcePixels.data(),25);
    RenderTarget2D target(device,5,5);
    device.SetRenderTarget(&target);
    device.Clear(Color::Magenta);
    SpriteBatch batch(device);
    auto sampler=SamplerState::PointClamp;
    batch.Begin(SpriteSortMode::Immediate,BlendState::Opaque,&sampler,nullptr,nullptr);
    batch.Draw(source,Rectangle(0,0,5,5),Color::White);
    batch.End();
    device.SetRenderTarget(nullptr);
    // 3 RGBA8 pixels = 12 bytes per row, not 16 (GL_PACK_ALIGNMENT=8 would pad).
    const Rectangle rectangle(1,2,3,2);
    std::array<Color,8> actual;
    actual.fill(Color::Magenta);
    target.GetData(0,&rectangle,actual.data(),1,6);
    EXPECT_EQ(actual.front(),Color::Magenta);
    EXPECT_EQ(actual.back(),Color::Magenta);
    for (int y=0;y<2;++y)
        for (int x=0;x<3;++x) {
            SCOPED_TRACE(y*3+x);
            EXPECT_EQ(actual[1+y*3+x],Color(40+x*30,100+y*40,53+x+y,255));
        }

    // Bgr565 is RRRRRGGGGGGBBBBB (FNA PackedVector/Bgr565.cs): three
    // texels take six bytes, so GL_UNPACK_ALIGNMENT=4 would pad each row to eight.
    // Sampling observes the GPU upload, not the texture's CPU readback mirror.
    Texture2D packed(device,3,2,false,SurfaceFormat::Bgr565);
    const std::array<SharpRuntime::ushortcs,6> packedWords{0xF800,0x07E0,0x001F,0xFFFF,0x0000,0xFFE0};
    packed.SetData(packedWords.data(),6);
    RenderTarget2D packedTarget(device,3,2);
    device.SetRenderTarget(&packedTarget);
    batch.Begin(SpriteSortMode::Immediate,BlendState::Opaque,&sampler,nullptr,nullptr);
    batch.Draw(packed,Rectangle(0,0,3,2),Color::White);
    batch.End();
    device.SetRenderTarget(nullptr);
    std::array<Color,6> packedPixels;
    packedTarget.GetData(packedPixels.data(),6);
    const std::array<Color,6> expected{Color::Red,Color::Lime,Color::Blue,
                                       Color::White,Color::Black,Color::Yellow};
    for (int i=0;i<6;++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(packedPixels[i],expected[i]);
    }
}
