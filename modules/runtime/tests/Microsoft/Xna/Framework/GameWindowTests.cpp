// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/DefaultWindowTitle.hpp"

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/WindowDescription.hpp"
#include "CNA/TargetPlatform.hpp"
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

// plans/plan_apple.md APPLE-15: SupportedOrientations is the framework's own bookkeeping on a desktop
// and the channel into the operating system on iOS/Android. These cases pin down both halves --
// the fallback rule that is platform-independent, and the forwarding to the platform window that
// is what makes the declaration mean anything on a mobile target.
namespace
{
    // SetSupportedOrientations is protected (XNA declares it protected internal on GameWindow);
    // GraphicsDeviceManager reaches it as a friend, a test reaches it by deriving.
    class TestableGameWindow : public GameWindow
    {
    public:
        using GameWindow::GameWindow;
        using GameWindow::SetSupportedOrientations;
    };

    // Records what GameWindow forwards, so the mapping can be asserted without a real display and
    // without naming the native hint the platform ultimately sets.
    class OrientationRecordingWindow final : public CNA::Platform::IPlatformWindow
    {
    public:
        explicit OrientationRecordingWindow(CNA::Platform::IPlatformWindow* inner) : inner_(inner) {}

        [[nodiscard]] CNA::Platform::WindowId GetId() const override { return inner_->GetId(); }
        [[nodiscard]] CNA::Platform::NativeWindowHandle GetNativeHandle() const override
        {
            return inner_->GetNativeHandle();
        }
        [[nodiscard]] std::string GetTitle() const override { return inner_->GetTitle(); }
        void SetTitle(const std::string& title) override { inner_->SetTitle(title); }
        [[nodiscard]] CNA::Platform::WindowBounds GetClientBounds() const override
        {
            return inner_->GetClientBounds();
        }
        [[nodiscard]] CNA::Platform::WindowSize GetPixelSize() const override
        {
            return inner_->GetPixelSize();
        }
        void SetSize(const int width, const int height) override { inner_->SetSize(width, height); }
        [[nodiscard]] float GetDisplayScale() const override { return inner_->GetDisplayScale(); }
        [[nodiscard]] bool IsResizable() const override { return inner_->IsResizable(); }
        void SetResizable(const bool resizable) override { inner_->SetResizable(resizable); }
        [[nodiscard]] bool IsBorderless() const override { return inner_->IsBorderless(); }
        void SetBorderless(const bool borderless) override { inner_->SetBorderless(borderless); }
        void SetFullscreenMode(const CNA::Platform::WindowFullscreenMode mode) override
        {
            inner_->SetFullscreenMode(mode);
        }
        [[nodiscard]] CNA::Platform::WindowFullscreenMode GetFullscreenMode() const override
        {
            return inner_->GetFullscreenMode();
        }
        void Show() override { inner_->Show(); }
        void Hide() override { inner_->Hide(); }
        void Minimize() override { inner_->Minimize(); }
        void Maximize() override { inner_->Maximize(); }
        void Restore() override { inner_->Restore(); }
        void Sync() override { inner_->Sync(); }
        [[nodiscard]] bool HasFocus() const override { return inner_->HasFocus(); }
        [[nodiscard]] bool IsMinimized() const override { return inner_->IsMinimized(); }
        [[nodiscard]] std::string GetDisplayName() const override { return inner_->GetDisplayName(); }

        void SetSupportedOrientations(const CNA::Platform::ScreenOrientation orientations) override
        {
            received = orientations;
            ++callCount;
        }

        CNA::Platform::ScreenOrientation received = CNA::Platform::ScreenOrientation::None;
        int callCount = 0;

    private:
        CNA::Platform::IPlatformWindow* inner_;
    };
}

TEST(GameWindowTest, SetSupportedOrientations_LeavesTheDefaultOrientationAlone)
{
    // DisplayOrientation::Default means "no orientation asserted" and counts as supported
    // whatever the supported set is, so narrowing the set does not force a concrete orientation
    // onto a window that never had one.
    TestableGameWindow window;

    window.SetSupportedOrientations(DisplayOrientation::Portrait);
    EXPECT_EQ(DisplayOrientation::Default, window.getCurrentOrientationProperty());
}

TEST(GameWindowTest, SetSupportedOrientations_FallsBackWhenTheCurrentOneStopsBeingSupported)
{
    // A concrete current orientation only comes from real window bounds, so this case needs a
    // window: 128x64 is landscape, which is in GameWindow's default supported set.
    std::unique_ptr<CNA::Platform::IPlatform> platform =
        CNA::Platform::PlatformFactory::Create("Headless");
    CNA::Platform::WindowDescription description = MakeDescription("orientation");
    description.width = 128;
    description.height = 64;
    std::unique_ptr<CNA::Platform::IPlatformWindow> platformWindow =
        platform->CreateWindow(description);

    TestableGameWindow window(platformWindow.get());
    ASSERT_EQ(DisplayOrientation::LandscapeLeft, window.getCurrentOrientationProperty());

    window.SetSupportedOrientations(DisplayOrientation::Portrait);
    EXPECT_EQ(DisplayOrientation::Portrait, window.getCurrentOrientationProperty());
}

TEST(GameWindowTest, SetSupportedOrientations_ForwardsTheRequestedSetToThePlatformWindow)
{
    std::unique_ptr<CNA::Platform::IPlatform> platform =
        CNA::Platform::PlatformFactory::Create("Headless");
    std::unique_ptr<CNA::Platform::IPlatformWindow> inner =
        platform->CreateWindow(MakeDescription("orientation-forwarding"));
    OrientationRecordingWindow recording(inner.get());

    TestableGameWindow window(&recording);
    window.SetSupportedOrientations(
        DisplayOrientation::Portrait | DisplayOrientation::LandscapeLeft);

    EXPECT_EQ(1, recording.callCount);
    EXPECT_TRUE(HasOrientation(recording.received, CNA::Platform::ScreenOrientation::Portrait));
    EXPECT_TRUE(
        HasOrientation(recording.received, CNA::Platform::ScreenOrientation::LandscapeLeft));
    EXPECT_FALSE(
        HasOrientation(recording.received, CNA::Platform::ScreenOrientation::LandscapeRight));
}

TEST(GameWindowTest, SetSupportedOrientations_DefaultClearsThePlatformPreference)
{
    std::unique_ptr<CNA::Platform::IPlatform> platform =
        CNA::Platform::PlatformFactory::Create("Headless");
    std::unique_ptr<CNA::Platform::IPlatformWindow> inner =
        platform->CreateWindow(MakeDescription("orientation-default"));
    OrientationRecordingWindow recording(inner.get());

    TestableGameWindow window(&recording);
    window.SetSupportedOrientations(DisplayOrientation::Default);

    EXPECT_EQ(1, recording.callCount);
    EXPECT_EQ(CNA::Platform::ScreenOrientation::None, recording.received);
}


TEST(GameWindowPlatformTest, DelegatesStateAndGeometryToTheSelectedPlatformWindow)
{
    // plans/plan_platform.md PLAT-SDL2-6: constructing a Game constructs a GraphicsDevice, and the
    // selected platform may refuse to make a window this build's renderer can use. That is a
    // legitimate refusal, not a GameWindow defect -- the same distinction PLAT-118's golden suite
    // already draws -- and there is nothing to delegate to when it happens, so it skips.
    //
    // Found for real: this ctest entry pins the headless dummy video driver, which the SDL3
    // backend accepts for an OpenGL window and the SDL2 backend does not -- it refuses, saying
    // OpenGL is not available in the current video driver. Under CNA_PLATFORM=SDL2 with any GL
    // renderer the refusal escaped the test body and reported as a failure, while the same binary
    // on a real display skipped correctly.
    std::unique_ptr<Game> owner;
    try
    {
        owner = std::make_unique<Game>();
    }
    catch (const CNA::Platform::PlatformException& refusal)
    {
        GTEST_SKIP() << "the selected platform cannot back this build's renderer: " << refusal.what();
    }
    Game& game = *owner;

    GameWindow& window = game.getWindowProperty();
    if (window.GetNativeWindowHandleEXT().system == CNA::Platform::NativeWindowSystem::Unknown)
    {
        GTEST_SKIP() << "The selected renderer intentionally creates no platform window.";
    }

    EXPECT_EQ(window.getTitleProperty(), CNA::Internal::GetDefaultWindowTitle());
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
}
