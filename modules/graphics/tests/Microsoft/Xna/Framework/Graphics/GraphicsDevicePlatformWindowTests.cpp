// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <gtest/gtest.h>

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
    bool throwFromSync = false;
};

class TracedWindow final : public IPlatformWindow
{
public:
    explicit TracedWindow(WindowOwnershipTrace& trace)
        : trace_(trace)
    {
    }

    ~TracedWindow() override { trace_.events.emplace_back("window-destroyed"); }

    [[nodiscard]] WindowId GetId() const override { return 0x6200u; }
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
        return WindowSize{width_ * 2, height_ * 2};
    }
    void SetSize(const int width, const int height) override
    {
        trace_.events.emplace_back("size-requested");
        pendingWidth_ = width;
        pendingHeight_ = height;
    }
    [[nodiscard]] float GetDisplayScale() const override { return 2.0f; }
    void SetResizable(bool) override {}
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
        EXPECT_EQ(trace.description.title, "Game");
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

} // namespace
