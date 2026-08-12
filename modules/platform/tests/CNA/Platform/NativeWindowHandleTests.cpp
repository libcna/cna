// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/NativeWindowHandle.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace {

using namespace CNA::Platform;

/// Stand-in addresses. Never dereferenced -- the accessors only ever move these values around.
int g_displayObject = 0;
int g_windowObject = 0;
int g_surfaceObject = 0;

void* FakeDisplay() { return &g_displayObject; }
void* FakeWindow() { return &g_windowObject; }
void* FakeSurface() { return &g_surfaceObject; }

NativeWindowHandle MakeWin32()
{
    NativeWindowHandle handle;
    handle.system = NativeWindowSystem::Win32;
    handle.window = FakeWindow();
    return handle;
}

NativeWindowHandle MakeX11()
{
    NativeWindowHandle handle;
    handle.system = NativeWindowSystem::X11;
    handle.display = FakeDisplay();
    handle.windowId = 0x1a00007u;
    return handle;
}

NativeWindowHandle MakeWayland()
{
    NativeWindowHandle handle;
    handle.system = NativeWindowSystem::Wayland;
    handle.display = FakeDisplay();
    handle.surface = FakeSurface();
    return handle;
}

NativeWindowHandle MakeCocoa()
{
    NativeWindowHandle handle;
    handle.system = NativeWindowSystem::Cocoa;
    handle.window = FakeWindow();
    return handle;
}

NativeWindowHandle MakeAndroid()
{
    NativeWindowHandle handle;
    handle.system = NativeWindowSystem::Android;
    handle.window = FakeWindow();
    return handle;
}

// --- struct contract -------------------------------------------------------------------------

TEST(NativeWindowHandleTests, IsTriviallyCopyableAndOwnsNothing)
{
    // Documented as trivially copyable: renderers store it by value at initialization.
    EXPECT_TRUE(std::is_trivially_copyable_v<NativeWindowHandle>);
}

TEST(NativeWindowHandleTests, DefaultConstructedHandleIsUnknownAndEmpty)
{
    const NativeWindowHandle handle;
    EXPECT_EQ(handle.system, NativeWindowSystem::Unknown);
    EXPECT_EQ(handle.display, nullptr);
    EXPECT_EQ(handle.window, nullptr);
    EXPECT_EQ(handle.surface, nullptr);
    EXPECT_EQ(handle.windowId, 0u);
}

TEST(NativeWindowHandleTests, X11WindowIdIsAtLeast32BitsWide)
{
    // An X11 Window is a 32-bit XID. Storing it in a pointer field is the classic bug this
    // separate integer field exists to prevent, so its width is part of the contract.
    EXPECT_GE(sizeof(NativeWindowHandle::windowId), 4u);
    EXPECT_TRUE(std::is_unsigned_v<decltype(NativeWindowHandle::windowId)>);
}

// --- successful retrieval --------------------------------------------------------------------

TEST(NativeWindowHandleTests, TryGetWin32ReturnsTheWindow)
{
    Win32NativeWindow out;
    ASSERT_TRUE(TryGetWin32(MakeWin32(), out));
    EXPECT_EQ(out.hwnd, FakeWindow());
}

TEST(NativeWindowHandleTests, TryGetX11ReturnsDisplayAndXid)
{
    X11NativeWindow out;
    ASSERT_TRUE(TryGetX11(MakeX11(), out));
    EXPECT_EQ(out.display, FakeDisplay());
    EXPECT_EQ(out.window, 0x1a00007u);
}

TEST(NativeWindowHandleTests, TryGetWaylandReturnsDisplayAndSurface)
{
    WaylandNativeWindow out;
    ASSERT_TRUE(TryGetWayland(MakeWayland(), out));
    EXPECT_EQ(out.display, FakeDisplay());
    EXPECT_EQ(out.surface, FakeSurface());
}

TEST(NativeWindowHandleTests, TryGetCocoaReturnsTheWindow)
{
    CocoaNativeWindow out;
    ASSERT_TRUE(TryGetCocoa(MakeCocoa(), out));
    EXPECT_EQ(out.window, FakeWindow());
}

TEST(NativeWindowHandleTests, TryGetAndroidReturnsTheWindow)
{
    AndroidNativeWindow out;
    ASSERT_TRUE(TryGetAndroid(MakeAndroid(), out));
    EXPECT_EQ(out.window, FakeWindow());
}

// --- the reason the accessors exist: cross-system rejection -----------------------------------

TEST(NativeWindowHandleTests, EveryAccessorRejectsEveryOtherSystem)
{
    // The point of the typed accessors: a renderer handed a window from a different system gets
    // a false it must handle, not a plausible-looking pointer that crashes on first use.
    const NativeWindowHandle handles[] = {MakeWin32(), MakeX11(), MakeWayland(), MakeCocoa(), MakeAndroid()};

    for (const NativeWindowHandle& handle : handles)
    {
        Win32NativeWindow win32;
        X11NativeWindow x11;
        WaylandNativeWindow wayland;
        CocoaNativeWindow cocoa;
        AndroidNativeWindow android;

        EXPECT_EQ(TryGetWin32(handle, win32), handle.system == NativeWindowSystem::Win32);
        EXPECT_EQ(TryGetX11(handle, x11), handle.system == NativeWindowSystem::X11);
        EXPECT_EQ(TryGetWayland(handle, wayland), handle.system == NativeWindowSystem::Wayland);
        EXPECT_EQ(TryGetCocoa(handle, cocoa), handle.system == NativeWindowSystem::Cocoa);
        EXPECT_EQ(TryGetAndroid(handle, android), handle.system == NativeWindowSystem::Android);
    }
}

TEST(NativeWindowHandleTests, FailedRetrievalLeavesTheOutputUntouched)
{
    // Documented behaviour: `out` is untouched on false. A caller that ignores the return value
    // must not find a half-populated struct that looks usable.
    Win32NativeWindow out;
    out.hwnd = FakeSurface();
    EXPECT_FALSE(TryGetWin32(MakeX11(), out));
    EXPECT_EQ(out.hwnd, FakeSurface());
}

// --- malformed handles ------------------------------------------------------------------------

TEST(NativeWindowHandleTests, RightSystemButMissingFieldsIsRejected)
{
    NativeWindowHandle win32 = MakeWin32();
    win32.window = nullptr;
    Win32NativeWindow win32Out;
    EXPECT_FALSE(TryGetWin32(win32, win32Out));

    NativeWindowHandle wayland = MakeWayland();
    wayland.surface = nullptr;
    WaylandNativeWindow waylandOut;
    EXPECT_FALSE(TryGetWayland(wayland, waylandOut));
}

TEST(NativeWindowHandleTests, X11XidZeroIsRejectedEvenWithADisplay)
{
    // XID 0 is None -- never a valid drawable. A pointer-style null check on `window` would not
    // have caught this, which is exactly why the XID lives in its own integer field.
    NativeWindowHandle handle = MakeX11();
    handle.windowId = 0;
    X11NativeWindow out;
    EXPECT_FALSE(TryGetX11(handle, out));
}

// --- HasNativeWindow --------------------------------------------------------------------------

TEST(NativeWindowHandleTests, HasNativeWindowIsTrueForEveryPopulatedRealSystem)
{
    EXPECT_TRUE(HasNativeWindow(MakeWin32()));
    EXPECT_TRUE(HasNativeWindow(MakeX11()));
    EXPECT_TRUE(HasNativeWindow(MakeWayland()));
    EXPECT_TRUE(HasNativeWindow(MakeCocoa()));
    EXPECT_TRUE(HasNativeWindow(MakeAndroid()));
}

TEST(NativeWindowHandleTests, HasNativeWindowIsFalseForWindowlessSystems)
{
    for (const NativeWindowSystem system :
         {NativeWindowSystem::Unknown, NativeWindowSystem::Web, NativeWindowSystem::Headless,
          NativeWindowSystem::Terminal})
    {
        NativeWindowHandle handle;
        handle.system = system;
        // Even with pointers set, these systems expose no consumable native window. A renderer
        // must refuse rather than reach for one.
        handle.display = FakeDisplay();
        handle.window = FakeWindow();
        EXPECT_FALSE(HasNativeWindow(handle)) << "system: " << ToString(system);
    }
}

TEST(NativeWindowHandleTests, HasNativeWindowIsFalseForAnIncompleteHandle)
{
    NativeWindowHandle handle = MakeWayland();
    handle.display = nullptr;
    EXPECT_FALSE(HasNativeWindow(handle));
}

// --- Describe ---------------------------------------------------------------------------------

TEST(NativeWindowHandleTests, DescribeNamesTheSystemAndPopulatedFields)
{
    EXPECT_EQ(Describe(MakeX11()), "X11(display=set, windowId=set)");
    EXPECT_EQ(Describe(MakeWayland()), "Wayland(display=set, surface=set)");
    EXPECT_EQ(Describe(MakeWin32()), "Win32(window=set)");
}

TEST(NativeWindowHandleTests, DescribeOmitsFieldListWhenNothingIsSet)
{
    NativeWindowHandle handle;
    handle.system = NativeWindowSystem::Terminal;
    EXPECT_EQ(Describe(handle), "Terminal");
}

TEST(NativeWindowHandleTests, DescribeNeverPrintsPointerValues)
{
    // Pointer values are meaningless across processes and invite treating a log line as a usable
    // handle. Two handles differing only in address must describe identically.
    NativeWindowHandle first = MakeWin32();
    NativeWindowHandle second = MakeWin32();
    second.window = FakeSurface();
    EXPECT_EQ(Describe(first), Describe(second));
}

} // namespace
