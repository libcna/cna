// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using namespace Microsoft::Xna::Framework;

namespace
{
    CNA::Platform::WindowDescription MakeDescription(const std::string& title)
    {
        CNA::Platform::WindowDescription description;
        description.title = title;
        description.width = 64;
        description.height = 64;
        description.visible = false;
        return description;
    }

    struct BorrowedHeadlessGameWindow
    {
        explicit BorrowedHeadlessGameWindow(const std::string& title)
            : platform(CNA::Platform::PlatformFactory::Create("Headless"))
            , platformWindow(platform->CreateWindow(MakeDescription(title)))
            , window(platformWindow.get())
        {
        }

        std::unique_ptr<CNA::Platform::IPlatform> platform;
        std::unique_ptr<CNA::Platform::IPlatformWindow> platformWindow;
        GameWindow window;
    };
}

TEST(GameWindowTest, SetAndGetTitleUsingPlatformWindow)
{
    BorrowedHeadlessGameWindow fixture("initial-title");

    fixture.window.setTitleProperty("new-title");
    EXPECT_EQ(fixture.window.getTitleProperty(), "new-title");
    EXPECT_EQ(fixture.platformWindow->GetTitle(), "new-title");

    fixture.window.setTitleProperty("");
    EXPECT_EQ(fixture.window.getTitleProperty(), "");
    EXPECT_EQ(fixture.platformWindow->GetTitle(), "");
}

TEST(GameWindowTest, NullWindow_IsSafeAndReturnsEmptyTitle)
{
    GameWindow window;

    EXPECT_EQ(window.getTitleProperty(), "");
    EXPECT_NO_THROW(window.setTitleProperty("ignored"));
}

TEST(GameTest, ExposesWindowProperty)
{
    using WindowGetterReturnType = decltype(std::declval<Game&>().getWindowProperty());
    EXPECT_TRUE((std::is_same_v<WindowGetterReturnType, GameWindow&>));
}

TEST(GameWindowTest, NullWindow_DefaultOrientationIsDefault)
{
    GameWindow window;
    EXPECT_EQ(window.getCurrentOrientationProperty(), DisplayOrientation::Default);
}

TEST(GameWindowTest, NullWindow_HandleIsNullptr)
{
    GameWindow window;
    EXPECT_EQ(window.getHandleProperty(), 0);
}

TEST(GameWindowTest, NullWindow_NativeHandleIsUnknownAndEmpty)
{
    const GameWindow window;
    const CNA::Platform::NativeWindowHandle handle = window.GetNativeWindowHandleEXT();

    EXPECT_EQ(handle.system, CNA::Platform::NativeWindowSystem::Unknown);
    EXPECT_EQ(handle.display, nullptr);
    EXPECT_EQ(handle.window, nullptr);
    EXPECT_EQ(handle.surface, nullptr);
    EXPECT_EQ(handle.windowId, 0u);
}

TEST(GameWindowTest, NativeHandleComesFromTheBorrowedPlatformWindow)
{
    BorrowedHeadlessGameWindow fixture("native-handle");

    const CNA::Platform::NativeWindowHandle handle = fixture.window.GetNativeWindowHandleEXT();
    EXPECT_EQ(handle.system, CNA::Platform::NativeWindowSystem::Headless);
}

TEST(GameWindowTest, DestroyingTheWrapperDoesNotDestroyTheBorrowedPlatformWindow)
{
    const std::unique_ptr<CNA::Platform::IPlatform> platform =
        CNA::Platform::PlatformFactory::Create("Headless");
    const std::unique_ptr<CNA::Platform::IPlatformWindow> platformWindow =
        platform->CreateWindow(MakeDescription("caller-owned"));

    {
        const GameWindow window(platformWindow.get());
        EXPECT_EQ(window.getTitleProperty(), "caller-owned");
    }

    EXPECT_NO_THROW(platformWindow->SetTitle("still-alive"));
    EXPECT_EQ(platformWindow->GetTitle(), "still-alive");
}

TEST(GameWindowTest, NullWindow_ScreenDeviceNameIsEmpty)
{
    GameWindow window;
    EXPECT_EQ(window.getScreenDeviceNameProperty(), "");
}

TEST(GameWindowTest, NullWindow_AllowUserResizingDefaultFalse)
{
    GameWindow window;
    EXPECT_FALSE(window.getAllowUserResizingProperty());
}

TEST(GameWindowTest, NullWindow_SetAllowUserResizingCaches)
{
    GameWindow window;
    window.setAllowUserResizingProperty(true);
    EXPECT_TRUE(window.getAllowUserResizingProperty());
}

TEST(GameWindowTest, NullWindow_IsBorderlessDefaultFalse)
{
    GameWindow window;
    EXPECT_FALSE(window.getIsBorderlessEXTProperty());
}

TEST(GameWindowTest, NullWindow_SetBorderlessCaches)
{
    GameWindow window;
    window.setIsBorderlessEXTProperty(true);
    EXPECT_TRUE(window.getIsBorderlessEXTProperty());
}

TEST(GameWindowTest, NullWindow_ClientSizeChangedEventFires)
{
    GameWindow window;
    int fired = 0;
    window.ClientSizeChanged += [&](System::Object*, const System::EventArgs&) { ++fired; };
    // EndScreenDeviceChange on null window: bounds stay (0,0,0,0) — no size change fires.
    // BeginScreenDeviceChange + EndScreenDeviceChange on null window is safe.
    window.BeginScreenDeviceChange(false);
    window.EndScreenDeviceChange("test", 0, 0);
    EXPECT_EQ(fired, 0);
}

TEST(GameWindowTest, NullWindow_MinimizeEXTIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.MinimizeEXT());
}

TEST(GameWindowTest, NullWindow_RestoreEXTIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.RestoreEXT());
}

TEST(GameWindowTest, MinimizeAndRestoreEXTUsingPlatformWindow)
{
    BorrowedHeadlessGameWindow fixture("minimize-restore-test");

    EXPECT_NO_THROW(fixture.window.MinimizeEXT());
    EXPECT_TRUE(fixture.platformWindow->IsMinimized());

    EXPECT_NO_THROW(fixture.window.RestoreEXT());
    EXPECT_FALSE(fixture.platformWindow->IsMinimized());
}

TEST(GameWindowTest, NullWindow_EndScreenDeviceChangeOneArgIsSafe)
{
    GameWindow window;
    EXPECT_NO_THROW(window.EndScreenDeviceChange("test"));
}

TEST(GameWindowPlatformTest, DelegatesStateAndGeometryToTheSelectedPlatformWindow)
{
#if defined(CNA_PLATFORM_SDL3)
    GTEST_SKIP() << "the HEADLESS renderer intentionally creates no window under the SDL3 selection";
#else
    Game game;
    GameWindow& window = game.getWindowProperty();

    EXPECT_EQ(window.getTitleProperty(), "Game");
    EXPECT_TRUE(window.getAllowUserResizingProperty());
    EXPECT_EQ(window.getClientBoundsProperty(), Rectangle(0, 0, 800, 480));

    window.setTitleProperty("platform-window");
    EXPECT_EQ(window.getTitleProperty(), "platform-window");
    window.setAllowUserResizingProperty(false);
    EXPECT_FALSE(window.getAllowUserResizingProperty());
    window.setIsBorderlessEXTProperty(true);
    EXPECT_TRUE(window.getIsBorderlessEXTProperty());

    window.BeginScreenDeviceChange(false);
    window.EndScreenDeviceChange("virtual-display", 320, 240);
    EXPECT_EQ(window.getClientBoundsProperty(), Rectangle(0, 0, 320, 240));
    EXPECT_NO_THROW(window.MinimizeEXT());
    EXPECT_NO_THROW(window.RestoreEXT());
#endif
}
