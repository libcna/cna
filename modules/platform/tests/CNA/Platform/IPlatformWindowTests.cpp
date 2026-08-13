// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>

namespace {

using namespace CNA::Platform;

/// A minimal in-memory window. Its purpose is to prove the interface is implementable without
/// any windowing system at all -- which is the property that makes HEADLESS and TERMINAL
/// possible, and the property a contract accidentally shaped around SDL would not have.
class FakeWindow final : public IPlatformWindow
{
public:
    [[nodiscard]] WindowId GetId() const override { return 1; }

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

    [[nodiscard]] WindowSize GetPixelSize() const override { return WindowSize{width_, height_}; }

    void SetSize(const int width, const int height) override
    {
        pendingWidth_ = width;
        pendingHeight_ = height;
    }

    [[nodiscard]] float GetDisplayScale() const override { return 1.0f; }
    void SetResizable(const bool resizable) override { resizable_ = resizable; }
    void SetBorderless(const bool borderless) override { borderless_ = borderless; }
    void SetFullscreenMode(const WindowFullscreenMode mode) override { mode_ = mode; }
    [[nodiscard]] WindowFullscreenMode GetFullscreenMode() const override { return mode_; }
    void Show() override { visible_ = true; }
    void Hide() override { visible_ = false; }
    void Minimize() override { minimized_ = true; }
    void Maximize() override { minimized_ = false; }
    void Restore() override { minimized_ = false; }

    void Sync() override
    {
        // Models the real asynchrony: a size request only lands once Sync() runs.
        width_ = pendingWidth_;
        height_ = pendingHeight_;
    }

    [[nodiscard]] bool HasFocus() const override { return true; }
    [[nodiscard]] bool IsMinimized() const override { return minimized_; }
    [[nodiscard]] std::string GetDisplayName() const override { return {}; }

    [[nodiscard]] bool IsVisible() const { return visible_; }
    [[nodiscard]] bool IsResizable() const override { return resizable_; }
    [[nodiscard]] bool IsBorderless() const override { return borderless_; }

private:
    std::string title_;
    int width_ = 800, height_ = 480;
    int pendingWidth_ = 800, pendingHeight_ = 480;
    bool visible_ = true, resizable_ = true, borderless_ = false, minimized_ = false;
    WindowFullscreenMode mode_ = WindowFullscreenMode::Windowed;
};

TEST(IPlatformWindowTests, IsImplementableWithNoWindowingSystem)
{
    // The contract must not require a native window to be satisfiable at all.
    const std::unique_ptr<IPlatformWindow> window = std::make_unique<FakeWindow>();
    EXPECT_EQ(window->GetId(), 1u);
    EXPECT_EQ(window->GetNativeHandle().system, NativeWindowSystem::Headless);
    EXPECT_FALSE(HasNativeWindow(window->GetNativeHandle()));
}

TEST(IPlatformWindowTests, HasAVirtualDestructorSoOwnershipThroughTheInterfaceIsSafe)
{
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformWindow>);
}

TEST(IPlatformWindowTests, TitleRoundTrips)
{
    FakeWindow window;
    window.SetTitle("CNA");
    EXPECT_EQ(window.GetTitle(), "CNA");
}

TEST(IPlatformWindowTests, SizeChangeIsAsynchronousUntilSync)
{
    // The documented reason Sync() exists: window state changes do not land immediately, so
    // reading straight back after a set can report the old value.
    FakeWindow window;
    window.SetSize(1024, 768);
    EXPECT_EQ(window.GetClientBounds().width, 800);
    window.Sync();
    EXPECT_EQ(window.GetClientBounds().width, 1024);
    EXPECT_EQ(window.GetClientBounds().height, 768);
}

TEST(IPlatformWindowTests, PixelSizeAndClientBoundsAreSeparateQueries)
{
    // Under high DPI these differ; a renderer sizes its swapchain from the pixel size. Keeping
    // them separate is the entire reason both exist.
    FakeWindow window;
    window.Sync();
    EXPECT_EQ(window.GetPixelSize().width, window.GetClientBounds().width);
    EXPECT_GE(window.GetDisplayScale(), 1.0f);
}

TEST(IPlatformWindowTests, VisibilityAndStateTogglesRoundTrip)
{
    FakeWindow window;
    window.Hide();
    EXPECT_FALSE(window.IsVisible());
    window.Show();
    EXPECT_TRUE(window.IsVisible());

    window.Minimize();
    EXPECT_TRUE(window.IsMinimized());
    window.Restore();
    EXPECT_FALSE(window.IsMinimized());

    window.SetResizable(false);
    EXPECT_FALSE(window.IsResizable());
    window.SetBorderless(true);
    EXPECT_TRUE(window.IsBorderless());
}

TEST(IPlatformWindowTests, FullscreenModeRoundTrips)
{
    FakeWindow window;
    window.SetFullscreenMode(WindowFullscreenMode::ExclusiveFullscreen);
    EXPECT_EQ(window.GetFullscreenMode(), WindowFullscreenMode::ExclusiveFullscreen);
}

TEST(IPlatformSurfacePresenterTests, HasAVirtualDestructor)
{
    EXPECT_TRUE(std::has_virtual_destructor_v<IPlatformSurfacePresenter>);
}

TEST(IPlatformSurfacePresenterTests, FrameDefaultsAreAnInvalidFrameNotAnEmptyOne)
{
    // A default-constructed frame must not look presentable: null pixels and zero size mean an
    // implementation's validation rejects it rather than reading from nullptr.
    const SurfaceFrame frame;
    EXPECT_EQ(frame.pixels, nullptr);
    EXPECT_EQ(frame.width, 0);
    EXPECT_EQ(frame.height, 0);
    EXPECT_EQ(frame.strideBytes, 0);
}

} // namespace
