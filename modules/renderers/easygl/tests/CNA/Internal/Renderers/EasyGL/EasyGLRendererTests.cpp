// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if defined(CNA_RENDERER_EASYGL)
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <stdexcept>
#include <string>

namespace {

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::CreateGraphicsRenderer;
using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::RendererSurfaceInfo;
using CNA::Internal::Renderers::EasyGL::EasyGLRenderer;
using CNA::Internal::Renderers::EasyGL::EasyGLSurfaceState;
using namespace CNA::Platform;

class FakeGlContext final : public IPlatformGlContext
{
public:
    static void* MissingProc(const char*) { return nullptr; }

    [[nodiscard]] GlContextHandle CreateContext(
        const WindowId window, const GlContextDescription& description) override
    {
        ++createCalls;
        createdFor = window;
        requested = description;
        if (throwOnCreate)
        {
            throw PlatformException("FakeGlContext::CreateContext", "injected failure");
        }
        return reinterpret_cast<GlContextHandle>(0x1234);
    }

    void DestroyContext(const GlContextHandle context) override
    {
        if (context != nullptr) ++destroyCalls;
    }

    void MakeCurrent(const WindowId window, const GlContextHandle context) override
    {
        ++makeCurrentCalls;
        currentFor = window;
        current = context;
    }

    void SwapBuffers(WindowId) override { ++swapCalls; }
    bool SetSwapInterval(int) override { return true; }
    [[nodiscard]] void* GetProcAddress(const std::string&) const override { return nullptr; }
    [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override
    {
        return &MissingProc;
    }
    [[nodiscard]] GlContextDescription GetContextAttributes(GlContextHandle) const override
    {
        return requested;
    }

    bool throwOnCreate = false;
    int createCalls = 0;
    int destroyCalls = 0;
    int makeCurrentCalls = 0;
    int swapCalls = 0;
    WindowId createdFor = 0;
    WindowId currentFor = 0;
    GlContextHandle current = nullptr;
    GlContextDescription requested;
};

RendererSurfaceInfo Surface(const WindowId id = 42, const int width = 64,
                            const int height = 64, const float scale = 1.0f)
{
    RendererSurfaceInfo result;
    result.windowId = id;
    result.drawableSize = {width, height};
    result.displayScale = scale;
    return result;
}

TEST(EasyGLRendererConstructor, FailedContextCreationLeavesNoDanglingRegistryEntry)
{
    FakeGlContext context;
    context.throwOnCreate = true;

    EXPECT_THROW(
        (EasyGLRenderer(Surface(), context, 64, 64,
                        CnaPresentationMode::FixedHeightDynamicWidth, false, 1, 1)),
        PlatformException);

    EXPECT_EQ(context.createCalls, 1);
    EXPECT_EQ(context.createdFor, 42u);
    EXPECT_EQ(IGraphicsRenderer::GetForWindow(42), nullptr);
}

TEST(EasyGLRendererConstructor, LaterInitializationFailureDestroysCreatedContext)
{
    FakeGlContext context;

    // The non-null loader deliberately resolves no functions. Device bootstrap therefore fails
    // after context creation/current binding, exercising constructor unwinding rather than the
    // service's own creation-failure path.
    EXPECT_THROW(
        (EasyGLRenderer(Surface(43), context, 64, 64,
                        CnaPresentationMode::FixedHeightDynamicWidth, false, 1, 1)),
        std::exception);

    EXPECT_EQ(context.createCalls, 1);
    EXPECT_EQ(context.makeCurrentCalls, 1);
    EXPECT_EQ(context.destroyCalls, 1);
    EXPECT_EQ(IGraphicsRenderer::GetForWindow(43), nullptr);
}

TEST(EasyGLRendererConstructor, RequestsTheSelectedProfileBeforeContextCreation)
{
    FakeGlContext context;
    context.throwOnCreate = true;

    EXPECT_THROW((EasyGLRenderer(Surface(), context)), PlatformException);

#if defined(CNA_GL_PROFILE_OPENGL33)
    EXPECT_EQ(context.requested.majorVersion, 3);
    EXPECT_EQ(context.requested.minorVersion, 3);
    EXPECT_EQ(context.requested.profile, GlProfile::Core);
#elif defined(CNA_GL_PROFILE_WEBGL1) || defined(CNA_GL_PROFILE_OPENGLES2)
    EXPECT_EQ(context.requested.majorVersion, 2);
    EXPECT_EQ(context.requested.minorVersion, 0);
    EXPECT_EQ(context.requested.profile, GlProfile::Es);
#else
    EXPECT_EQ(context.requested.majorVersion, 3);
    EXPECT_EQ(context.requested.minorVersion, 0);
    EXPECT_EQ(context.requested.profile, GlProfile::Es);
#endif
    EXPECT_EQ(context.requested.depthBits, 24);
    EXPECT_EQ(context.requested.stencilBits, 8);
    EXPECT_TRUE(context.requested.doubleBuffer);
}

TEST(EasyGLRendererFactory, MissingGlServiceIsAPlatformCapabilityRefusal)
{
    GraphicsRendererCreateArgs args;
    args.surface = Surface();

    try
    {
        (void)CreateGraphicsRenderer(args);
        FAIL() << "factory accepted a missing GL context service";
    }
    catch (const PlatformNotSupportedException& error)
    {
        EXPECT_EQ(error.GetCapability(), PlatformCapability::OpenGlContext);
    }
}

TEST(EasyGLSurfaceState, ResizeAndDisplayScaleUseLogicalInputAndPhysicalFramebufferUnits)
{
    EasyGLSurfaceState state(
        Surface(77, 400, 200, 2.0f), 100, 50,
        CnaPresentationMode::FixedHeightDynamicWidth);

    int width = 0;
    int height = 0;
    state.GetDrawableSize(width, height);
    EXPECT_EQ(width, 400);
    EXPECT_EQ(height, 200);
    state.GetLogicalSize(width, height);
    EXPECT_EQ(width, 100);
    EXPECT_EQ(height, 50);

    float x = 0.0f;
    float y = 0.0f;
    ASSERT_TRUE(state.WindowToLogical(100.0f, 50.0f, x, y));
    EXPECT_FLOAT_EQ(x, 50.0f);
    EXPECT_FLOAT_EQ(y, 25.0f);
    ASSERT_TRUE(state.LogicalToWindow(x, y, x, y));
    EXPECT_FLOAT_EQ(x, 100.0f);
    EXPECT_FLOAT_EQ(y, 50.0f);

    state.Update(Surface(77, 600, 300, 1.5f));
    state.GetDrawableSize(width, height);
    EXPECT_EQ(width, 600);
    EXPECT_EQ(height, 300);
    state.GetLogicalSize(width, height);
    EXPECT_EQ(width, 100);
    EXPECT_EQ(height, 50);
    ASSERT_TRUE(state.WindowToLogical(200.0f, 100.0f, x, y));
    EXPECT_FLOAT_EQ(x, 50.0f);
    EXPECT_FLOAT_EQ(y, 25.0f);
}

TEST(EasyGLSurfaceState, InvalidScaleFallsBackToUnscaledClientCoordinates)
{
    EasyGLSurfaceState state(Surface(78, 320, 180, 0.0f), 0, 0,
                             CnaPresentationMode::FixedHeightDynamicWidth);
    int width = 0;
    int height = 0;
    state.GetLogicalSize(width, height);
    EXPECT_EQ(width, 320);
    EXPECT_EQ(height, 180);
}

} // namespace
#endif
