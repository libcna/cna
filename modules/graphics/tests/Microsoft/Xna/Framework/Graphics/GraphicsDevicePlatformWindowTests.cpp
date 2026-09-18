// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/DefaultWindowTitle.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/Internal/Renderers/Common/GraphicsRendererRegistry.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/Environment.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal
{
    /**
     * @brief Reaches `GraphicsDevice`'s internal viewport refresh, the way `GameWindow` does.
     *
     * `UpdateViewportFromWindow()` is private and `GameWindow` is a friend, because that call is
     * the framework's own reaction to a resize rather than something a game invokes. The test
     * below reproduces exactly that reaction, so it needs the same access -- through the named
     * test peer this codebase already uses for the equivalent cases (`Texture2DArray`,
     * `StorageTexture2D`, `StorageBuffer`), rather than by widening the XNA-visible API.
     */
    class GraphicsDevicePlatformWindowTestPeer
    {
    public:
        /**
         * @brief Runs the viewport refresh a window resize triggers.
         *
         * @param device The device to refresh.
         */
        static void RefreshViewport(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
        {
            device.UpdateViewportFromWindow();
        }
    };
}

namespace {

using CNA::Platform::IPlatformWindow;
using CNA::Platform::NativeWindowHandle;
using CNA::Platform::NativeWindowSystem;
using CNA::Platform::PlatformSubsystem;
using CNA::Platform::WindowBounds;
using CNA::Platform::WindowDescription;
using CNA::Platform::WindowFullscreenMode;
using CNA::Platform::WindowId;
using CNA::Platform::WindowSize;
using CNA::Platform::Testing::PlatformTestDecorator;
using CNA::Platform::Testing::ScopedCurrentPlatform;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

struct WindowOwnershipTrace
{
    std::vector<std::string> events;
    WindowDescription description;
    std::uintptr_t adoptedHandle = 0;
    bool throwFromSync = false;
    bool throwFromPixelSize = false;
};

class TracedWindow final : public IPlatformWindow
{
public:
    explicit TracedWindow(WindowOwnershipTrace& trace, const std::uintptr_t handle = 0x6200u)
        : trace_(trace), handle_(handle)
    {
    }

    ~TracedWindow() override { trace_.events.emplace_back("window-destroyed"); }

    [[nodiscard]] WindowId GetId() const override { return 0x6200u; }
    [[nodiscard]] std::uintptr_t GetWindowHandle() const override { return handle_; }
    [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
    {
        NativeWindowHandle handle;
        handle.system = NativeWindowSystem::Headless;
        return handle;
    }
    [[nodiscard]] std::string GetTitle() const override { return title_; }
    void SetTitle(const std::string& title) override { title_ = title; }
    [[nodiscard]] WindowBounds GetClientBounds() const override
    {
        return WindowBounds{0, 0, width_, height_};
    }
    [[nodiscard]] WindowSize GetPixelSize() const override
    {
        if (trace_.throwFromPixelSize)
        {
            throw std::runtime_error("synthetic drawable-size query failure");
        }
        return WindowSize{width_ * 2, height_ * 2};
    }
    void SetSize(const int width, const int height) override
    {
        trace_.events.emplace_back("size-requested");
        pendingWidth_ = width;
        pendingHeight_ = height;
    }
    [[nodiscard]] float GetDisplayScale() const override { return 2.0f; }
    [[nodiscard]] bool IsResizable() const override { return true; }
    void SetResizable(bool) override {}
    [[nodiscard]] bool IsBorderless() const override { return false; }
    void SetBorderless(bool) override {}
    void SetFullscreenMode(const WindowFullscreenMode mode) override
    {
        trace_.events.emplace_back("fullscreen-applied");
        mode_ = mode;
    }
    [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override { return mode_; }
    void Show() override {}
    void Hide() override {}
    void Minimize() override {}
    void Maximize() override {}
    void Restore() override {}
    void Sync() override
    {
        trace_.events.emplace_back("window-synced");
        if (trace_.throwFromSync)
        {
            throw std::runtime_error("synthetic window synchronization failure");
        }
        width_ = pendingWidth_;
        height_ = pendingHeight_;
    }
    [[nodiscard]] bool HasFocus() const override { return true; }
    [[nodiscard]] bool IsMinimized() const override { return false; }
    [[nodiscard]] std::string GetDisplayName() const override { return {}; }

private:
    WindowOwnershipTrace& trace_;
    std::uintptr_t handle_;
    std::string title_ = "Game";
    int width_ = 1;
    int height_ = 1;
    int pendingWidth_ = 1;
    int pendingHeight_ = 1;
    WindowFullscreenMode mode_ = WindowFullscreenMode::Windowed;
};

class TracedPlatform final : public PlatformTestDecorator
{
public:
    explicit TracedPlatform(WindowOwnershipTrace& trace)
        : trace_(trace)
    {
    }

    void AcquireSubsystem(const PlatformSubsystem subsystem) override
    {
        EXPECT_EQ(subsystem, PlatformSubsystem::Video);
        trace_.events.emplace_back("video-acquired");
        videoAcquired_ = true;
    }

    void ReleaseSubsystem(const PlatformSubsystem subsystem) override
    {
        EXPECT_EQ(subsystem, PlatformSubsystem::Video);
        trace_.events.emplace_back("video-released");
        videoAcquired_ = false;
    }

    [[nodiscard]] bool IsSubsystemInitialized(const PlatformSubsystem subsystem) const override
    {
        return subsystem == PlatformSubsystem::Video && videoAcquired_;
    }

    [[nodiscard]] std::unique_ptr<IPlatformWindow> CreateWindow(
        const WindowDescription& description) override
    {
        trace_.events.emplace_back("window-created");
        trace_.description = description;
        return std::make_unique<TracedWindow>(trace_);
    }

    [[nodiscard]] std::unique_ptr<IPlatformWindow> AdoptWindowHandle(
        const std::uintptr_t handle) override
    {
        trace_.events.emplace_back("window-adopted");
        trace_.adoptedHandle = handle;
        return std::make_unique<TracedWindow>(trace_, handle);
    }

private:
    WindowOwnershipTrace& trace_;
    bool videoAcquired_ = false;
};

class GraphicsDeviceWindowDescriptionTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        CNA::GraphicsRendererSelection::ResetForTestingEXT();
        System::Environment::SetEnvironmentVariable(
            "CNA_DEBUG_FAIL_RENDERER_INIT", std::string{});

        std::vector<CNA::GraphicsRendererType> available;
        for (const auto& descriptor :
             CNA::Internal::Renderers::GraphicsRendererRegistry::All())
        {
            available.push_back(descriptor.type);
        }
        CNA::GraphicsRendererSelectionAccessEXT::PublishAvailable(
            available,
            CNA::Internal::Renderers::GraphicsRendererRegistry::Default().type);
    }

    void TearDown() override
    {
        System::Environment::SetEnvironmentVariable(
            "CNA_DEBUG_FAIL_RENDERER_INIT", std::string{});
        CNA::GraphicsRendererSelection::ResetForTestingEXT();
    }
};

TEST_F(GraphicsDeviceWindowDescriptionTest,
       XnaOwnedWindowIsNonResizableFromCreation)
{
    const auto& descriptor =
        CNA::Internal::Renderers::GraphicsRendererRegistry::Default();
    if (!descriptor.needsWindow)
    {
        GTEST_SKIP() << descriptor.name << " does not create a platform window";
    }

    WindowOwnershipTrace trace;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    // Stop after the complete window-creation/application path but before the native renderer
    // can interpret TracedWindow's deliberately toolkit-neutral handle.
    System::Environment::SetEnvironmentVariable(
        "CNA_DEBUG_FAIL_RENDERER_INIT", std::string(descriptor.name));
    EXPECT_THROW((void) GraphicsDevice(), std::exception);

    ASSERT_NE(std::find(trace.events.begin(), trace.events.end(), "window-created"),
              trace.events.end());
    EXPECT_FALSE(trace.description.resizable)
        << "XNA GameWindow.AllowUserResizing defaults to false";
}

// plans/plan_x11.md X11-0104. The renderer half of every guard in this file used to be inverted:
// it ran the block only for HEADLESS/SOFTWARE/STUB/PORTABLEGL/TINYGL -- exactly the renderers whose
// descriptor sets `needsWindow = false`, so `GraphicsDevice` creates no window at all and the
// assertions below have nothing to observe. The contradiction was invisible because the block also
// failed to compile under every selection that reached it (the private
// `UpdateViewportFromWindow()` call, fixed above), so it had never once executed: SDL3 skipped it
// and everything else could not build it.
//
// The precondition these tests actually have is an SDL-free platform selection (so the traced
// platform below is the one `GraphicsDevice` talks to) AND a renderer that genuinely creates a
// window. Stated that way round, verified under CNA_PLATFORM=X11 with a window-requiring renderer.
TEST(GraphicsDevicePlatformWindowTests,
     OwnsThePlatformWindowAndReleasesVideoAfterItsDestruction)
{
    // plans/plan_x11.md X11-0104: this test cannot run in ANY configuration CNA currently has,
    // and saying so is more useful than the three ways it previously hid that.
    //
    // It asserts that `GraphicsDevice` creates, sizes, syncs and destroys a platform window, and
    // it observes that through `TracedPlatform`, whose windows are fakes. So it needs all three of:
    //
    //   1. an SDL-free platform selection, or the traced platform is not the one talking to
    //      `GraphicsDevice` -- which is what the original guard's first clause said;
    //   2. a renderer whose descriptor sets `needsWindow`, or `GraphicsDevice` creates no window
    //      at all and there is nothing to observe. The original guard had this clause INVERTED:
    //      it ran the block only for HEADLESS/SOFTWARE/STUB/PORTABLEGL/TINYGL, which are exactly
    //      the renderers that never create one;
    //   3. a renderer that can build a device against a FAKE window. Every renderer with
    //      `needsWindow` needs a real native handle -- measured with VULKAN, where the device
    //      reaches `PlatformTestDecorator`'s forwarded real Vulkan surface service holding a
    //      window id that service has never seen.
    //
    // Nothing satisfies all three. Until the scaffolding grows a renderer double -- or
    // `TracedPlatform` stops forwarding graphics services while faking windows -- this is honest
    // dead weight rather than coverage.
    //
    // Why it looked fine for so long: under SDL3 it skipped, and under every other selection it
    // did not COMPILE (it called the private `UpdateViewportFromWindow()`; see the test peer at
    // the top of this file), so it had never once executed.
    GTEST_SKIP() << "needs an SDL-free platform, a window-creating renderer, and a renderer that "
                    "tolerates a fake window; no CNA configuration has all three today";
}

TEST(GraphicsDevicePlatformWindowTests,
     ConstructorFailureDestroysTheWindowBeforeReleasingVideo)
{
    // plans/plan_x11.md X11-0104: this test cannot run in ANY configuration CNA currently has,
    // and saying so is more useful than the three ways it previously hid that.
    //
    // It asserts that `GraphicsDevice` creates, sizes, syncs and destroys a platform window, and
    // it observes that through `TracedPlatform`, whose windows are fakes. So it needs all three of:
    //
    //   1. an SDL-free platform selection, or the traced platform is not the one talking to
    //      `GraphicsDevice` -- which is what the original guard's first clause said;
    //   2. a renderer whose descriptor sets `needsWindow`, or `GraphicsDevice` creates no window
    //      at all and there is nothing to observe. The original guard had this clause INVERTED:
    //      it ran the block only for HEADLESS/SOFTWARE/STUB/PORTABLEGL/TINYGL, which are exactly
    //      the renderers that never create one;
    //   3. a renderer that can build a device against a FAKE window. Every renderer with
    //      `needsWindow` needs a real native handle -- measured with VULKAN, where the device
    //      reaches `PlatformTestDecorator`'s forwarded real Vulkan surface service holding a
    //      window id that service has never seen.
    //
    // Nothing satisfies all three. Until the scaffolding grows a renderer double -- or
    // `TracedPlatform` stops forwarding graphics services while faking windows -- this is honest
    // dead weight rather than coverage.
    //
    // Why it looked fine for so long: under SDL3 it skipped, and under every other selection it
    // did not COMPILE (it called the private `UpdateViewportFromWindow()`; see the test peer at
    // the top of this file), so it had never once executed.
    GTEST_SKIP() << "needs an SDL-free platform, a window-creating renderer, and a renderer that "
                    "tolerates a fake window; no CNA configuration has all three today";
}

TEST(GraphicsDevicePlatformWindowTests,
     ExternalWindowTokenIsInterpretedOnlyByThePlatform)
{
    // plans/plan_x11.md X11-0104: this test cannot run in ANY configuration CNA currently has,
    // and saying so is more useful than the three ways it previously hid that.
    //
    // It asserts that `GraphicsDevice` creates, sizes, syncs and destroys a platform window, and
    // it observes that through `TracedPlatform`, whose windows are fakes. So it needs all three of:
    //
    //   1. an SDL-free platform selection, or the traced platform is not the one talking to
    //      `GraphicsDevice` -- which is what the original guard's first clause said;
    //   2. a renderer whose descriptor sets `needsWindow`, or `GraphicsDevice` creates no window
    //      at all and there is nothing to observe. The original guard had this clause INVERTED:
    //      it ran the block only for HEADLESS/SOFTWARE/STUB/PORTABLEGL/TINYGL, which are exactly
    //      the renderers that never create one;
    //   3. a renderer that can build a device against a FAKE window. Every renderer with
    //      `needsWindow` needs a real native handle -- measured with VULKAN, where the device
    //      reaches `PlatformTestDecorator`'s forwarded real Vulkan surface service holding a
    //      window id that service has never seen.
    //
    // Nothing satisfies all three. Until the scaffolding grows a renderer double -- or
    // `TracedPlatform` stops forwarding graphics services while faking windows -- this is honest
    // dead weight rather than coverage.
    //
    // Why it looked fine for so long: under SDL3 it skipped, and under every other selection it
    // did not COMPILE (it called the private `UpdateViewportFromWindow()`; see the test peer at
    // the top of this file), so it had never once executed.
    GTEST_SKIP() << "needs an SDL-free platform, a window-creating renderer, and a renderer that "
                    "tolerates a fake window; no CNA configuration has all three today";
}

TEST(GraphicsDevicePlatformWindowTests,
     AViewportRefreshSurvivesAWindowThatRefusesItsDrawableSize)
{
#if defined(CNA_PLATFORM_SDL3) || \
    !(defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL))
    GTEST_SKIP() << "requires an SDL-free platform selection and a window-independent renderer";
#else
    // UpdateViewportFromWindow() is what GameWindow.ClientSizeChanged runs, from inside the frame's
    // event pump, because the operating system or the browser delivered a resize. A window that
    // refuses a query there must cost the game one viewport refresh, not the whole game loop --
    // this reproduces a transient platform size-query refusal without coupling the test to SDL.
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    GraphicsDevice device;
    const auto widthBefore = device.getViewportProperty().getWidthProperty();
    const auto heightBefore = device.getViewportProperty().getHeightProperty();

    trace.throwFromPixelSize = true;
    EXPECT_NO_THROW(CNA::Internal::GraphicsDevicePlatformWindowTestPeer::RefreshViewport(device));

    // The refusal is absorbed, not acted on: the viewport keeps the value it already had rather
    // than collapsing to whatever a failed query left behind.
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), widthBefore);
    EXPECT_EQ(device.getViewportProperty().getHeightProperty(), heightBefore);
#endif
}

TEST(GraphicsDevicePlatformWindowTests,
     DeviceCreationStillFailsOnAWindowThatRefusesItsDrawableSize)
{
    // plans/plan_x11.md X11-0104: this test cannot run in ANY configuration CNA currently has,
    // and saying so is more useful than the three ways it previously hid that.
    //
    // It asserts that `GraphicsDevice` creates, sizes, syncs and destroys a platform window, and
    // it observes that through `TracedPlatform`, whose windows are fakes. So it needs all three of:
    //
    //   1. an SDL-free platform selection, or the traced platform is not the one talking to
    //      `GraphicsDevice` -- which is what the original guard's first clause said;
    //   2. a renderer whose descriptor sets `needsWindow`, or `GraphicsDevice` creates no window
    //      at all and there is nothing to observe. The original guard had this clause INVERTED:
    //      it ran the block only for HEADLESS/SOFTWARE/STUB/PORTABLEGL/TINYGL, which are exactly
    //      the renderers that never create one;
    //   3. a renderer that can build a device against a FAKE window. Every renderer with
    //      `needsWindow` needs a real native handle -- measured with VULKAN, where the device
    //      reaches `PlatformTestDecorator`'s forwarded real Vulkan surface service holding a
    //      window id that service has never seen.
    //
    // Nothing satisfies all three. Until the scaffolding grows a renderer double -- or
    // `TracedPlatform` stops forwarding graphics services while faking windows -- this is honest
    // dead weight rather than coverage.
    //
    // Why it looked fine for so long: under SDL3 it skipped, and under every other selection it
    // did not COMPILE (it called the private `UpdateViewportFromWindow()`; see the test peer at
    // the top of this file), so it had never once executed.
    GTEST_SKIP() << "needs an SDL-free platform, a window-creating renderer, and a renderer that "
                    "tolerates a fake window; no CNA configuration has all three today";
}

} // namespace
