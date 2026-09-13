// SPDX-License-Identifier: MS-PL
//
// plans/plan_vulkan.md VULKAN-154 / VULKAN-157 (findings F-22 and F-23, which turned out to be one
// defect): an Xlib protocol error must not end the process.
//
// Xlib's DEFAULT error handler calls `exit()`. That is not a theoretical problem here -- it was
// measured twice. When the failing request comes from inside `Sdl3Platform::CreateWindow`, which
// holds `SdlGlobalStateMutex()` while it calls `SDL_CreateWindow`, `exit()` runs the static
// `unique_ptr<IPlatform>` destructor, which is `~Sdl3Platform`, which locks that same
// non-recursive mutex on the same thread. It never returns. Captured with gdb on 2026-09-05:
//
//     pthread_mutex_lock <- ~Sdl3Platform <- exit <- _XDefaultError <- _XError <- XSync
//                        <- X11_PumpEvents <- SDL_CreateWindow <- Sdl3Platform::CreateWindow
//
// So one X error either killed a test binary outright (F-23: 578 tests lost to one bad request) or
// hung it forever (F-22: 45 minutes of wall clock for 2 seconds of CPU, and `ctest` would not reap
// it) -- the difference being only whether the lock happened to be held at that instant.
//
// This file provokes the error deliberately and asserts the two things that matter: the process is
// still alive afterwards, and the connection is still usable. Surviving is not a weak "it did not
// crash" proxy here -- crashing IS the defect, and before the fix this test could not report a
// verdict at all, because the process it runs in would be gone.
//
// The bad request is issued through Xlib resolved with `dlsym`, exactly as the production handler
// installs itself: libX11 is present only when SDL chose its x11 video driver, and neither the
// renderer nor this test may grow an X11 build dependency for it.

#include "CNA/Platform/NativeWindowHandle.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#if defined(__linux__) || defined(__unix__)
#include <dlfcn.h>
#endif

namespace {

using namespace CNA::Platform;

class Sdl3XErrorHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const std::vector<std::string> available = PlatformFactory::GetAvailable();
        if (std::find(available.begin(), available.end(), "SDL3") == available.end())
        {
            GTEST_SKIP() << "built with CNA_PLATFORM != SDL3";
        }
        platform_ = PlatformFactory::Create("SDL3");
        try
        {
            platform_->AcquireSubsystem(PlatformSubsystem::Video);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "no video subsystem available here: " << error.what();
        }
        acquiredVideo_ = true;

        WindowDescription description;
        description.title = "CNA X error handler test";
        description.width = 64;
        description.height = 64;
        description.visible = false;
        try
        {
            window_ = platform_->CreateWindow(description);
        }
        catch (const PlatformException& error)
        {
            GTEST_SKIP() << "window creation unavailable here: " << error.what();
        }
        ASSERT_NE(window_, nullptr);

        // Asked through CNA's own typed accessor rather than through SDL: the platform boundary
        // is the point of this module, and the handle already answers "is this X11?" exactly.
        if (!TryGetX11(window_->GetNativeHandle(), x11_))
        {
            GTEST_SKIP() << "this test is about Xlib's error handler, and this window is not an "
                            "X11 window";
        }
    }

    void TearDown() override
    {
        window_.reset();
        if (acquiredVideo_)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    X11NativeWindow x11_{};
    bool acquiredVideo_ = false;
};

TEST_F(Sdl3XErrorHandlerTest, ADeliberateBadRequestDoesNotEndTheProcessAndLeavesTheConnectionUsable)
{
#if !defined(__linux__) && !defined(__unix__)
    GTEST_SKIP() << "no dlsym here";
#else
    void* display = x11_.display;
    ASSERT_NE(display, nullptr);

    using XChangePropertyFn = int (*)(void*, unsigned long, unsigned long, unsigned long, int, int,
                                      const unsigned char*, int);
    using XSyncFn = int (*)(void*, int);
    auto* xChangeProperty =
        reinterpret_cast<XChangePropertyFn>(dlsym(RTLD_DEFAULT, "XChangeProperty"));
    auto* xSync = reinterpret_cast<XSyncFn>(dlsym(RTLD_DEFAULT, "XSync"));
    if (xChangeProperty == nullptr || xSync == nullptr)
    {
        GTEST_SKIP() << "libX11 is loaded but XChangeProperty/XSync were not resolvable";
    }

    // X_ChangeProperty (major opcode 18) against a resource id that cannot be a live window --
    // the same request F-23 recorded failing for real. XA_STRING is the predefined atom 31, so no
    // XInternAtom round trip is needed and the only thing wrong with the request is the window.
    constexpr unsigned long kNotAWindow = 0xDEADBEEFul;
    constexpr unsigned long kXaString = 31ul;
    const unsigned char payload[] = {'c', 'n', 'a'};
    xChangeProperty(display, kNotAWindow, kXaString, kXaString, 8, /*PropModeReplace=*/0, payload,
                    static_cast<int>(sizeof(payload)));

    // The error is asynchronous; this is what delivers it, and it is where the old default handler
    // called exit(). Reaching the next line at all is the first half of the claim.
    testing::internal::CaptureStderr();
    xSync(display, /*discard=*/0);
    const std::string diagnostic = testing::internal::GetCapturedStderr();
    SUCCEED() << "the process survived a BadWindow on X_ChangeProperty";

    // The handler decodes XErrorEvent without X11 headers, so the DECODED VALUES are asserted, not
    // merely the fact that something was printed. BadWindow is error code 3 and X_ChangeProperty
    // is major opcode 18; the resource id is the one this test made up. The first version of that
    // handler mirrored the struct with `serial` and `resourceid` transposed and printed a message
    // that looked perfectly reasonable -- these three checks are what catch that.
    EXPECT_NE(diagnostic.find("X protocol error ignored"), std::string::npos) << diagnostic;
    EXPECT_NE(diagnostic.find("error_code=3"), std::string::npos)
        << "expected BadWindow (3): " << diagnostic;
    EXPECT_NE(diagnostic.find("request=18."), std::string::npos)
        << "expected X_ChangeProperty (18): " << diagnostic;
    EXPECT_NE(diagnostic.find("resource=0xdeadbeef"), std::string::npos)
        << "expected the bogus window id this test used: " << diagnostic;

    // The second half: the connection is not merely alive but still usable. Creating and
    // destroying another real window round-trips to the server several times.
    WindowDescription second;
    second.title = "CNA X error handler test (after)";
    second.width = 32;
    second.height = 32;
    second.visible = false;
    std::unique_ptr<IPlatformWindow> after;
    ASSERT_NO_THROW(after = platform_->CreateWindow(second));
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after->GetId(), 0u);
    after.reset();

    // And the original window is still there, which rules out the connection having been reset.
    EXPECT_NE(window_->GetId(), 0u);
#endif
}

}   // namespace
