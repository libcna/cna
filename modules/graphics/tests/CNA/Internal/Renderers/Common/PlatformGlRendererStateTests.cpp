// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace CNA::Internal::Renderers;
    using namespace CNA::Platform;

    class FakeGlContext final : public IPlatformGlContext
    {
    public:
        static void* Loader(const char*) { return reinterpret_cast<void*>(0x55); }

        [[nodiscard]] GlContextHandle CreateContext(
            const WindowId window, const GlContextDescription& description) override
        {
            trace.emplace_back("create");
            createdFor = window;
            requested = description;
            return reinterpret_cast<GlContextHandle>(++nextHandle);
        }

        void DestroyContext(const GlContextHandle context) override
        {
            if (context != nullptr) trace.emplace_back("destroy");
        }

        void MakeCurrent(const WindowId window, GlContextHandle context) override
        {
            trace.emplace_back(context != nullptr ? "bind" : "unbind");
            if (failBind && context != nullptr) throw PlatformException("Fake::MakeCurrent");
            currentWindow = context != nullptr ? window : 0;
            currentContext = context;
        }

        [[nodiscard]] GlContextBinding GetCurrentBinding() const override
        {
            return {currentWindow, currentContext};
        }

        void SwapBuffers(WindowId) override { trace.emplace_back("swap"); }
        bool SetSwapInterval(int interval) override
        {
            appliedInterval = interval;
            return true;
        }
        [[nodiscard]] void* GetProcAddress(const std::string&) const override { return Loader(""); }
        [[nodiscard]] GlProcAddressLoader GetProcAddressLoader() const override { return &Loader; }
        [[nodiscard]] GlContextDescription GetContextAttributes(GlContextHandle) const override
        {
            return granted;
        }

        bool failBind = false;
        std::uintptr_t nextHandle = 0x100;
        int appliedInterval = -99;
        WindowId createdFor = 0;
        WindowId currentWindow = 0;
        GlContextHandle currentContext = nullptr;
        GlContextDescription requested;
        GlContextDescription granted;
        std::vector<std::string> trace;
    };

    RendererSurfaceInfo Surface(WindowId id, int width, int height, float scale)
    {
        RendererSurfaceInfo surface;
        surface.windowId = id;
        surface.drawableSize = {width, height};
        surface.displayScale = scale;
        return surface;
    }

    TEST(PlatformGlContextOwnerTests, MissingServiceIsAPlatformCapabilityRefusal)
    {
        try
        {
            (void)RequirePlatformGlContext(nullptr, "TEST_GL");
            FAIL() << "missing service was accepted";
        }
        catch (const PlatformNotSupportedException& error)
        {
            EXPECT_EQ(error.GetCapability(), PlatformCapability::OpenGlContext);
        }
    }

    TEST(PlatformGlContextOwnerTests, OwnsBindsRecreatesAndPresentsThroughTheService)
    {
        FakeGlContext service;
        service.granted.multisampleBuffers = 1;
        service.granted.multisampleSamples = 4;
        GlContextDescription requested;
        requested.majorVersion = 4;
        requested.minorVersion = 1;

        {
            PlatformGlContextOwner owner(service, 77, requested);
            EXPECT_EQ(service.createdFor, 77u);
            EXPECT_EQ(service.requested.majorVersion, 4);
            EXPECT_EQ(service.requested.minorVersion, 1);
            EXPECT_EQ(owner.GetAttributes().multisampleSamples, 4);
            EXPECT_EQ(owner.GetLoader(), &FakeGlContext::Loader);
            EXPECT_EQ(LoadPlatformGlProcAddress("glTest"), reinterpret_cast<void*>(0x55));
            EXPECT_TRUE(owner.SetSwapInterval(2));
            EXPECT_EQ(service.appliedInterval, 2);
            owner.SwapBuffers();
            owner.Recreate();
        }

        // The last three entries used to be "create", "bind", "destroy": Recreate() unbound before
        // destroying but the DESTRUCTOR did not, so the owner's own two teardown paths disagreed
        // about the same operation. That asymmetry is now gone, and this trace is where it is
        // pinned -- both paths unbind first.
        //
        // It was not cosmetic. Destroying a context that is still current leaves the platform's GL
        // state pointing at a dead context, and on GLX that outlives the window: after `next`'s
        // RTR-P5-15 balanced GraphicsDevice's surplus video-subsystem reference -- so the subsystem
        // genuinely shuts down between devices instead of never coming down at all -- the IGL
        // renderer could no longer initialise SDL video for a second device in the same process
        // ("x11 not available"). IGL's Vulkan backend, OPENGLES3 and OPENGL1 all survived the same
        // loop, which is what narrowed it to this one line.
        const std::vector<std::string> expected{
            "create", "bind", "swap", "unbind", "destroy", "create", "bind", "unbind", "destroy"};
        EXPECT_EQ(service.trace, expected);
    }

    TEST(PlatformGlContextOwnerTests, FailedBindDestroysTheNewContextTransactionally)
    {
        FakeGlContext service;
        service.failBind = true;
        EXPECT_THROW((PlatformGlContextOwner(service, 9, {})), PlatformException);
        const std::vector<std::string> expected{"create", "bind", "destroy"};
        EXPECT_EQ(service.trace, expected);
    }

    TEST(PlatformGlContextOwnerTests, DestructionDoesNotUnbindAnotherDevicesContext)
    {
        FakeGlContext service;
        const auto otherContext = reinterpret_cast<GlContextHandle>(0x999);
        {
            PlatformGlContextOwner owner(service, 77, {});
            service.MakeCurrent(88, otherContext);
            service.trace.clear();
        }

        const std::vector<std::string> expected{"destroy"};
        EXPECT_EQ(service.trace, expected);
        EXPECT_EQ(service.GetCurrentBinding().window, 88u);
        EXPECT_EQ(service.GetCurrentBinding().context, otherContext);
    }

    TEST(PlatformGlSurfaceStateTests, SeparatesDrawablePixelsFromLogicalWindowUnits)
    {
        PlatformGlSurfaceState surface(Surface(42, 600, 300, 1.5f));
        int width = 0;
        int height = 0;
        surface.GetDrawableSize(width, height);
        EXPECT_EQ(width, 600);
        EXPECT_EQ(height, 300);
        surface.GetClientSize(width, height);
        EXPECT_EQ(width, 400);
        EXPECT_EQ(height, 200);
        EXPECT_FLOAT_EQ(surface.WindowToDrawable(100.0f), 150.0f);
        EXPECT_FLOAT_EQ(surface.DrawableToWindow(150.0f), 100.0f);

        surface.Update(Surface(42, 800, 400, 2.0f));
        surface.GetClientSize(width, height);
        EXPECT_EQ(width, 400);
        EXPECT_EQ(height, 200);
        EXPECT_THROW(surface.Update(Surface(43, 800, 400, 2.0f)), PlatformException);
    }
}
