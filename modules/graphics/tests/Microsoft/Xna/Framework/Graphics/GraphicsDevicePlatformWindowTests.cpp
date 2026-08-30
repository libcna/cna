// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/DefaultWindowTitle.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

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

TEST(GraphicsDevicePlatformWindowTests,
     OwnsThePlatformWindowAndReleasesVideoAfterItsDestruction)
{
#if defined(CNA_PLATFORM_SDL3) || !(defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL))
    GTEST_SKIP() << "requires an SDL-free platform selection and a window-independent renderer";
#else
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    {
        GraphicsDevice device;
        // The window title is no longer a literal: it is the declared assembly title,
        // then the running executable's own name, then "Game". This test binary supplies
        // the middle one, so the assertion is against the same derivation the product
        // uses rather than against a hardcoded string.
        EXPECT_EQ(trace.description.title, CNA::Internal::GetDefaultWindowTitle());
        EXPECT_EQ(trace.description.width, 800);
        EXPECT_EQ(trace.description.height, 480);
        EXPECT_TRUE(trace.description.resizable);
        EXPECT_EQ(trace.events, (std::vector<std::string>{
            "video-acquired", "window-created", "fullscreen-applied",
            "size-requested", "window-synced"}));
    }

    EXPECT_EQ(trace.events, (std::vector<std::string>{
        "video-acquired", "window-created", "fullscreen-applied",
        "size-requested", "window-synced", "window-destroyed", "video-released"}));
#endif
}

TEST(GraphicsDevicePlatformWindowTests,
     ConstructorFailureDestroysTheWindowBeforeReleasingVideo)
{
#if defined(CNA_PLATFORM_SDL3) || !(defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL))
    GTEST_SKIP() << "requires an SDL-free platform selection and a window-independent renderer";
#else
    WindowOwnershipTrace trace;
    trace.throwFromSync = true;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    EXPECT_THROW((void)GraphicsDevice(), std::runtime_error);
    EXPECT_EQ(trace.events, (std::vector<std::string>{
        "video-acquired", "window-created", "fullscreen-applied",
        "size-requested", "window-synced", "window-destroyed", "video-released"}));
#endif
}

TEST(GraphicsDevicePlatformWindowTests,
     ExternalWindowTokenIsInterpretedOnlyByThePlatform)
{
#if defined(CNA_PLATFORM_SDL3) || !(defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL))
    GTEST_SKIP() << "requires an SDL-free platform selection and a window-independent renderer";
#else
    constexpr std::uintptr_t token = 0xCAFE1234u;
    WindowOwnershipTrace trace;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    Microsoft::Xna::Framework::Graphics::PresentationParameters parameters;
    parameters.setDeviceWindowHandleProperty(token);
    {
        GraphicsDevice device(
            Microsoft::Xna::Framework::Graphics::GraphicsAdapter::getDefaultAdapterProperty(),
            Microsoft::Xna::Framework::Graphics::GraphicsProfile::Reach,
            parameters);
        EXPECT_EQ(trace.adoptedHandle, token);
        EXPECT_EQ(
            device.getPresentationParametersProperty().getDeviceWindowHandleProperty(), token);
    }

    EXPECT_EQ(trace.events, (std::vector<std::string>{
        "video-acquired", "window-adopted", "fullscreen-applied",
        "size-requested", "window-synced", "window-destroyed", "video-released"}));
#endif
}

TEST(GraphicsDevicePlatformWindowTests,
     AViewportRefreshSurvivesAWindowThatRefusesItsDrawableSize)
{
#if defined(CNA_PLATFORM_SDL3) || !(defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL))
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
    EXPECT_NO_THROW(device.UpdateViewportFromWindow());

    // The refusal is absorbed, not acted on: the viewport keeps the value it already had rather
    // than collapsing to whatever a failed query left behind.
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), widthBefore);
    EXPECT_EQ(device.getViewportProperty().getHeightProperty(), heightBefore);
#endif
}

TEST(GraphicsDevicePlatformWindowTests,
     DeviceCreationStillFailsOnAWindowThatRefusesItsDrawableSize)
{
#if defined(CNA_PLATFORM_SDL3) || !(defined(CNA_RENDERER_HEADLESS) || defined(CNA_RENDERER_SOFTWARE) || defined(CNA_RENDERER_STUB) || defined(CNA_RENDERER_PORTABLEGL))
    GTEST_SKIP() << "requires an SDL-free platform selection and a window-independent renderer";
#else
    // The other half of the contract, and the reason the tolerance above is scoped to one block
    // rather than to the query itself: creating a renderer for a surface whose size cannot be
    // determined is a genuine failure the caller asked for and can act on. Only the unsolicited,
    // event-driven refresh absorbs it.
    WindowOwnershipTrace trace;
    trace.throwFromPixelSize = true;
    TracedPlatform platform(trace);
    ScopedCurrentPlatform current(platform);

    EXPECT_ANY_THROW((void)GraphicsDevice());
#endif
}

} // namespace
