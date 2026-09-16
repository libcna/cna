// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0062/WIN32-0063: the platform-to-renderer bridge.
//
// `DirectX11Renderer` and `DirectX12Renderer` both begin the same way:
//
//     CNA::Platform::Win32NativeWindow nativeWindow;
//     if (!CNA::Platform::TryGetWin32(surface_.GetNativeHandle(), nativeWindow))
//         throw std::runtime_error("... requires a Win32 native window.");
//     hwnd_ = static_cast<HWND>(nativeWindow.hwnd);
//
// That is the entire contract between this platform and those renderers, and it is what this file
// exercises -- deliberately without linking either of them. A renderer is a separate axis: the
// platform's obligation is to produce a handle that satisfies the generic accessor, and coupling
// the platform's own tests to d3d11.dll would make `platform != renderer` a little less true with
// every such edge.
//
// Real device and swapchain creation on top of this handle is proved separately, by
// spikes/win32-directx-spike/, which is where a GPU dependency belongs.

#include "CNA/Platform/PlatformFactory.hpp"

#include "Win32/Win32Common.hpp"

#include "Win32TestDesktop.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace {

using namespace CNA::Platform;
using CNA::Platform::Testing::SizeThatFitsTheWorkArea;

/// Exactly what a D3D renderer does with the handle it is given, reproduced here so the assertion
/// is about the call the renderer actually makes rather than about a paraphrase of it.
bool RendererWouldAccept(const NativeWindowHandle& handle, HWND& hwnd)
{
    Win32NativeWindow nativeWindow;
    if (!TryGetWin32(handle, nativeWindow))
        return false;
    hwnd = static_cast<HWND>(nativeWindow.hwnd);
    return true;
}

class Win32RendererBridge : public ::testing::Test
{
protected:
    void SetUp() override
    {
        platform_ = PlatformFactory::Create("Win32");
        ASSERT_NE(platform_, nullptr);
        platform_->AcquireSubsystem(PlatformSubsystem::Video);

        // Same reason as SizeThatFitsTheWorkArea's: an absolute 800x600 here would make the whole
        // fixture assert that the suite is running on a desktop at least that big. It is not, on
        // the Wine virtual desktop this suite's cross-build runs under, nor in a Windows session
        // whose window station reports a small display.
        created_ = SizeThatFitsTheWorkArea(800, 600);

        WindowDescription description;
        description.title = "renderer bridge";
        description.width = created_.width;
        description.height = created_.height;
        description.visible = false;
        window_ = platform_->CreateWindow(description);
        ASSERT_NE(window_, nullptr);
        window_->Sync();
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    WindowSize created_{};
};

TEST_F(Win32RendererBridge, TheCapabilityPromisesAHandleAndTheWindowDelivers)
{
    // A renderer checks the capability before it asks. A platform that claimed the capability and
    // then produced an unusable handle would send it straight into a null dereference.
    ASSERT_TRUE(platform_->GetCapabilities().nativeWindowHandle);

    HWND hwnd = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), hwnd))
        << Describe(window_->GetNativeHandle());
    EXPECT_NE(hwnd, nullptr);
    EXPECT_NE(IsWindow(hwnd), FALSE) << "the handle must name a live window, not merely be non-null";
}

TEST_F(Win32RendererBridge, TheHandleDescribesItselfWithoutLeakingAddresses)
{
    // Describe() is what a renderer puts in its refusal message. Printing a pointer there is
    // meaningless across processes and invites treating a log line as a usable handle.
    const std::string description = Describe(window_->GetNativeHandle());
    EXPECT_NE(description.find("Win32"), std::string::npos) << description;
    EXPECT_EQ(description.find("0x"), std::string::npos) << description;
}

TEST_F(Win32RendererBridge, TheSwapchainSizeComesFromTheDrawableSizeNotTheLogicalOne)
{
    // Both D3D renderers size their swapchain from surface_.GetDrawableSize(), falling back to the
    // virtual size only when it is non-positive. A platform reporting zero here would send the
    // renderer to its fallback and produce a swapchain unrelated to the window.
    const WindowSize drawable = window_->GetPixelSize();
    EXPECT_GT(drawable.width, 0);
    EXPECT_GT(drawable.height, 0);
    EXPECT_EQ(drawable.width, created_.width);
    EXPECT_EQ(drawable.height, created_.height);
}

TEST_F(Win32RendererBridge, TheHandleSurvivesAResizeUnchanged)
{
    // PlatformRendererSurfaceState re-validates handle identity whenever it refreshes, and treats
    // a change as a different window. A resize must therefore move the drawable size and leave the
    // HWND exactly where it was.
    HWND before = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), before));

    // The target is derived from the work area rather than written down as 1024x768. Windows will
    // not give a window a client area its monitor cannot hold, so a hardcoded size turns this test
    // into an assertion about the screen the suite happens to be running on: measured on Windows 10
    // build 19045, the same binary passed on a 1920x1080 desktop and failed on a 1024x768 one --
    // for a reason that has nothing to do with the handle identity this test is about.
    const WindowSize target = SizeThatFitsTheWorkArea(1024, 768);

    window_->SetSize(target.width, target.height);
    window_->Sync();

    HWND after = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), after));
    EXPECT_EQ(before, after) << "a resize must not look like a new window to a renderer";
    EXPECT_EQ(window_->GetPixelSize().width, target.width);
    EXPECT_EQ(window_->GetPixelSize().height, target.height);
}

TEST_F(Win32RendererBridge, TheHandleSurvivesAFullscreenTransitionUnchanged)
{
    HWND before = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), before));

    window_->SetFullscreenMode(WindowFullscreenMode::BorderlessFullscreen);
    window_->Sync();
    HWND duringFullscreen = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), duringFullscreen));
    EXPECT_EQ(before, duringFullscreen);

    window_->SetFullscreenMode(WindowFullscreenMode::Windowed);
    window_->Sync();
    HWND after = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), after));
    EXPECT_EQ(before, after);
}

TEST_F(Win32RendererBridge, EachWindowHandsItsOwnRendererADistinctHandle)
{
    // Two renderers on two windows is the case the registry exists for. Handing both the same
    // HWND would have the second swapchain present into the first window.
    WindowDescription description;
    description.title = "second renderer bridge";
    description.width = 320;
    description.height = 240;
    description.visible = false;
    const std::unique_ptr<IPlatformWindow> second = platform_->CreateWindow(description);
    ASSERT_NE(second, nullptr);

    HWND first = nullptr;
    HWND other = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), first));
    ASSERT_TRUE(RendererWouldAccept(second->GetNativeHandle(), other));
    EXPECT_NE(first, other);
}

TEST_F(Win32RendererBridge, ARendererRequiringWin32RefusesAHeadlessWindowRatherThanCrashing)
{
    // The other half of the contract, and the reason the typed accessors exist at all: a renderer
    // initialised against a platform that has no native window gets a false it must handle.
    const std::unique_ptr<IPlatform> headless = PlatformFactory::Create("Headless");
    WindowDescription description;
    description.title = "no native window";
    description.visible = false;
    const std::unique_ptr<IPlatformWindow> headlessWindow = headless->CreateWindow(description);
    ASSERT_NE(headlessWindow, nullptr);

    HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0xBADC0DE));
    EXPECT_FALSE(RendererWouldAccept(headlessWindow->GetNativeHandle(), hwnd));
    EXPECT_EQ(hwnd, reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0xBADC0DE)))
        << "a failed retrieval must leave the caller's output untouched";
    EXPECT_FALSE(HasNativeWindow(headlessWindow->GetNativeHandle()));
}

TEST_F(Win32RendererBridge, AnAdoptedWindowPresentsTheSameHandleToARenderer)
{
    // A host that owns its own window and hands it to CNA must reach the same renderer path as one
    // CNA created -- otherwise "adopt an existing window" would be a second, untested route.
    const std::unique_ptr<IPlatformWindow> adopted = platform_->AdoptWindow(window_->GetId());
    ASSERT_NE(adopted, nullptr);

    HWND owned = nullptr;
    HWND borrowed = nullptr;
    ASSERT_TRUE(RendererWouldAccept(window_->GetNativeHandle(), owned));
    ASSERT_TRUE(RendererWouldAccept(adopted->GetNativeHandle(), borrowed));
    EXPECT_EQ(owned, borrowed);
}

} // namespace
