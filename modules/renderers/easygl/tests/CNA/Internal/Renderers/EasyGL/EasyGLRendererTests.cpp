// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

// EasyGL is a FAMILY, not a public identity: cmake emits one PRESENT_ macro per PUBLIC IDENTITY, so
// CNA_RENDERER_PRESENT_EASYGL is never defined and a guard naming it is dead (see 0bb99795e).
#if defined(CNA_RENDERER_EASYGL) \
    || defined(CNA_RENDERER_PRESENT_OPENGLES2) || defined(CNA_RENDERER_PRESENT_OPENGLES3) \
    || defined(CNA_RENDERER_PRESENT_OPENGL33) \
    || defined(CNA_RENDERER_PRESENT_WEBGL1) || defined(CNA_RENDERER_PRESENT_WEBGL2)
// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_EASYGL is defined project-wide.
// EasyGL is a FAMILY, not a public identity, so cmake never generates
// (CNA_RENDERER_IDENTITIES). Naming the family here made the guard dead, so this suite still
// compiled only for the default renderer: exactly the gap RTR-P9-9 existed to close, reintroduced
// by the fix for it. Verified against a real multi build, whose build.ninja defines PRESENT_ only
// for the identities in the set. The five public identities this family serves are named instead.
#include <stdexcept>
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/EasyGL/EasyGLRenderer.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include <string>

namespace {

using CNA::Internal::Renderers::CnaPresentationMode;
using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::IGraphicsRenderer;
using CNA::Internal::Renderers::RendererSurfaceInfo;
using CNA::Internal::Renderers::EasyGL::EasyGLRenderer;
using CNA::Internal::Renderers::EasyGL::EasyGLSurfaceState;
using CNA::Internal::Renderers::EasyGL::UsesEs2ApiGeneration;
using EasyGlProfile = CNA::Internal::Renderers::EasyGL::GlProfile;
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

TEST(EasyGLProfile, Es2ApiGenerationIncludesWebGl1)
{
    EXPECT_TRUE(UsesEs2ApiGeneration(EasyGlProfile::OpenGLES2));
    EXPECT_TRUE(UsesEs2ApiGeneration(EasyGlProfile::WebGL1));
    EXPECT_FALSE(UsesEs2ApiGeneration(EasyGlProfile::OpenGLES3));
    EXPECT_FALSE(UsesEs2ApiGeneration(EasyGlProfile::OpenGL33));
    EXPECT_FALSE(UsesEs2ApiGeneration(EasyGlProfile::WebGL2));
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
        // plans/plan_runtimerenderer.md design decision 4: the factory lives in the FAMILY's namespace
        // so several renderer archives can link into one binary. Called qualified, exactly as the
        // GDI family's own programs do. (IGraphicsRenderer.hpp used to also declare a bare
        // CNA::Internal::Renderers::CreateGraphicsRenderer, which made an unqualified call
        // ambiguous; that leftover declaration is gone, but qualifying the call is still what
        // states which family is under test.)
        (void)CNA::Internal::Renderers::EasyGL::CreateGraphicsRenderer(args);
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

// The regression this pair of methods exists to keep apart: GetLogicalSize() is what the game
// thinks its resolution is, GetDefaultViewportRect() is which drawable pixels that lands on.
// EasyGL used to have only the first and inherited IGraphicsRenderer's default for the second --
// which returns the LOGICAL size as if it were physical pixels. Since GraphicsDevice::Reset()
// gives this renderer a virtual resolution on every device creation and the default presentation
// mode is FixedHeightDynamicWidth, that default was wrong on any window whose aspect differs from
// the virtual one: the game rendered into a sub-rectangle and the rest of the window kept the
// clear colour (reported against galaxy-eggbert 2026-08-21 -- resizing or F11 did not enlarge it).
TEST(EasyGLSurfaceState, FixedHeightDynamicWidthViewportRectCoversTheWholeDrawable)
{
    EasyGLSurfaceState state(
        Surface(79, 400, 200, 1.0f), 100, 50,
        CnaPresentationMode::FixedHeightDynamicWidth);

    int width = 0;
    int height = 0;
    state.GetLogicalSize(width, height);
    EXPECT_EQ(width, 100);   // logical: height pinned to 50, width follows the 2:1 drawable aspect
    EXPECT_EQ(height, 50);

    int x = -1;
    int y = -1;
    state.GetDefaultViewportRect(x, y, width, height);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(width, 400);   // physical: the whole drawable, NOT the logical 200x50
    EXPECT_EQ(height, 200);

    // A window whose aspect no longer matches the virtual resolution is the case that used to
    // leave part of the window unrendered.
    state.Update(Surface(79, 1200, 800, 1.0f));
    state.GetDefaultViewportRect(x, y, width, height);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(width, 1200);
    EXPECT_EQ(height, 800);
}

TEST(EasyGLSurfaceState, StretchAndNativeBackBufferAlsoCoverTheWholeDrawable)
{
    for (const CnaPresentationMode mode :
         {CnaPresentationMode::Stretch, CnaPresentationMode::NativeBackBuffer})
    {
        EasyGLSurfaceState state(Surface(80, 1200, 800, 1.0f), 100, 50, mode);
        int x = -1, y = -1, width = 0, height = 0;
        state.GetDefaultViewportRect(x, y, width, height);
        EXPECT_EQ(x, 0);
        EXPECT_EQ(y, 0);
        EXPECT_EQ(width, 1200);
        EXPECT_EQ(height, 800);
    }
}

// Letterbox shrinks to fit and centres (bars); Overscan grows to cover and centres (cropping).
// Virtual 100x50 is 2:1 inside a square 400x400 drawable, so the two scales are 4 and 8.
TEST(EasyGLSurfaceState, LetterboxAndOverscanScaleUniformlyAndCentre)
{
    EasyGLSurfaceState letterbox(Surface(81, 400, 400, 1.0f), 100, 50,
                                 CnaPresentationMode::Letterbox);
    int x = -1, y = -1, width = 0, height = 0;
    letterbox.GetDefaultViewportRect(x, y, width, height);
    EXPECT_EQ(width, 400);
    EXPECT_EQ(height, 200);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 100);

    EasyGLSurfaceState overscan(Surface(81, 400, 400, 1.0f), 100, 50,
                                CnaPresentationMode::Overscan);
    overscan.GetDefaultViewportRect(x, y, width, height);
    EXPECT_EQ(width, 800);
    EXPECT_EQ(height, 400);
    EXPECT_EQ(x, -200);
    EXPECT_EQ(y, 0);
}

TEST(EasyGLSurfaceState, DegenerateDrawableYieldsAnEmptyViewportRect)
{
    EasyGLSurfaceState state(Surface(82, 0, 0, 1.0f), 100, 50,
                             CnaPresentationMode::Letterbox);
    int x = -1, y = -1, width = -1, height = -1;
    state.GetDefaultViewportRect(x, y, width, height);
    EXPECT_EQ(x, 0);
    EXPECT_EQ(y, 0);
    EXPECT_EQ(width, 0);
    EXPECT_EQ(height, 0);
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
