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

        void MakeCurrent(WindowId, GlContextHandle context) override
        {
            trace.emplace_back(context != nullptr ? "bind" : "unbind");
            if (failBind && context != nullptr) throw PlatformException("Fake::MakeCurrent");
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

        const std::vector<std::string> expected{
            "create", "bind", "swap", "unbind", "destroy", "create", "bind", "destroy"};
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
