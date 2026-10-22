// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
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
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,PresentationParameters());
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
    verify(SurfaceFormat::HalfSingle,HalfSingle(-2.0f));
    verify(SurfaceFormat::HalfVector2,HalfVector2(-2.0f,0.5f));
    verify(SurfaceFormat::HalfVector4,HalfVector4(-2.0f,0.5f,2.0f,1.0f));
    verify(SurfaceFormat::HdrBlendable,HalfVector4(-2.0f,0.5f,2.0f,1.0f));
    if (measured==0) GTEST_SKIP() << "this context reports no floating-point render targets";
    EXPECT_EQ(2*measured,ReadObserver::calls);
    EXPECT_TRUE(ReadObserver::rgbaFloatOnly);
}
