// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Platform/CannedKeyboard.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "RuntimePlatformTestSupport.hpp"

using namespace Microsoft::Xna::Framework;
using CNA::Platform::KeyCode;
namespace
{
    constexpr auto allOrientations = DisplayOrientation::Portrait | DisplayOrientation::LandscapeLeft | DisplayOrientation::LandscapeRight;
    class OrientationGame final : public Game
    {
    public:
        OrientationGame(std::unique_ptr<CNA::Platform::Testing::CannedKeyboardPlatform> platform, CNA::Platform::Testing::CannedKeyboard* fixture)
            : Game(std::move(platform)), manager(this)
        {
            keyboard = fixture;
            EXPECT_FALSE(getWindowProperty().getKeyboardOrientationEmulationEnabledEXT());
            manager.setSupportedOrientationsProperty(allOrientations);
            getWindowProperty().setKeyboardOrientationEmulationEnabledEXT(true);
            getWindowProperty().OrientationChanged += [&](System::Object*, const System::EventArgs&) { ++events; };
            setIsFixedTimeStepProperty(false);
        }
        GraphicsDeviceManager manager;
        CNA::Platform::Testing::CannedKeyboard* keyboard = nullptr;
        int phase = 0, events = 0;
        void Check(DisplayOrientation orientation, int width, int height)
        {
            EXPECT_EQ(getWindowProperty().getCurrentOrientationProperty(), orientation) << phase;
            EXPECT_EQ(getGraphicsDeviceProperty().getPresentationParametersProperty().getDisplayOrientationProperty(), orientation) << phase;
            EXPECT_EQ(getGraphicsDeviceProperty().getViewportProperty().getWidthProperty(), width) << phase;
            EXPECT_EQ(getGraphicsDeviceProperty().getViewportProperty().getHeightProperty(), height) << phase;
        }
    protected:
        void Draw(const GameTime&) override
        {
            switch (phase++)
            {
            case 0: Check(DisplayOrientation::LandscapeLeft, 800, 480); events = 0; keyboard->SetPending({KeyCode::Up}); break;
            case 1: Check(DisplayOrientation::Portrait, 480, 800); EXPECT_EQ(events, 1); keyboard->SetPending({}); break;
            case 2: Check(DisplayOrientation::Portrait, 480, 800);
                manager.setSupportedOrientationsProperty(DisplayOrientation::Portrait);
                manager.setPreferredBackBufferWidthProperty(480); manager.setPreferredBackBufferHeightProperty(800);
                manager.ApplyChanges(); keyboard->SetPending({KeyCode::Right}); break;
            case 3: Check(DisplayOrientation::Portrait, 480, 800); EXPECT_EQ(events, 1);
                manager.setSupportedOrientationsProperty(allOrientations); manager.ApplyChanges();
                Check(DisplayOrientation::LandscapeRight, 800, 480); EXPECT_EQ(events, 2); keyboard->SetPending({}); break;
            case 4: Check(DisplayOrientation::LandscapeRight, 800, 480); keyboard->SetPending({KeyCode::Left}); break;
            case 5: Check(DisplayOrientation::LandscapeLeft, 800, 480); EXPECT_EQ(events, 3); keyboard->SetPending({KeyCode::Up, KeyCode::Right}); break;
            case 6: Check(DisplayOrientation::LandscapeLeft, 800, 480); EXPECT_EQ(events, 3);
                getWindowProperty().setKeyboardOrientationEmulationEnabledEXT(false);
                EXPECT_FALSE(getWindowProperty().getKeyboardOrientationEmulationEnabledEXT());
                EXPECT_EQ(getGraphicsDeviceProperty().getViewportProperty().getWidthProperty(), 480);
                EXPECT_EQ(getGraphicsDeviceProperty().getViewportProperty().getHeightProperty(), 800);
                keyboard->SetPending({KeyCode::Right}); break;
            case 7: EXPECT_EQ(getWindowProperty().getCurrentOrientationProperty(), DisplayOrientation::Portrait);
                EXPECT_EQ(events, 4); Exit(); break;
            default: FAIL() << "Orientation game did not exit"; Exit();
            }
        }
    };
}
TEST(KeyboardOrientationTest, GamePumpResizesAndHonorsLockPendingUnlockAndDisable)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    auto platform = std::make_unique<CNA::Platform::Testing::CannedKeyboardPlatform>();
    auto* keyboard = &platform->Canned();
    OrientationGame game(std::move(platform), keyboard);
    game.Run();
    EXPECT_EQ(game.phase, 8);
}

TEST(KeyboardOrientationTest, LateEnableHonorsDefaultLandscapeAndPreparingParameters)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow()) GTEST_SKIP();
    auto platform = std::make_unique<CNA::Platform::Testing::CannedKeyboardPlatform>();
    auto* keyboard = &platform->Canned();
    class LateGame final : public Game
    {
    public:
        LateGame(std::unique_ptr<CNA::Platform::IPlatform> platform,
                 CNA::Platform::Testing::CannedKeyboard* keyboard)
            : Game(std::move(platform)), manager(this), keyboard(keyboard)
        { setIsFixedTimeStepProperty(false); }
        GraphicsDeviceManager manager;
        CNA::Platform::Testing::CannedKeyboard* keyboard;
        int phase = 0;
    protected:
        void Draw(const GameTime&) override
        {
            auto& window = getWindowProperty();
            switch (phase++)
            {
            case 0:
                EXPECT_FALSE(window.getKeyboardOrientationEmulationEnabledEXT());
                window.setKeyboardOrientationEmulationEnabledEXT(true);
                window.setKeyboardOrientationEmulationEnabledEXT(true);
                EXPECT_EQ(getGraphicsDeviceProperty().getPresentationParametersProperty().getDisplayOrientationProperty(), DisplayOrientation::LandscapeLeft);
                keyboard->SetPending({KeyCode::Up}); break;
            case 1:
                EXPECT_EQ(window.getCurrentOrientationProperty(), DisplayOrientation::LandscapeLeft);
                keyboard->SetPending({KeyCode::Right}); break;
            case 2:
                EXPECT_EQ(window.getCurrentOrientationProperty(), DisplayOrientation::LandscapeRight);
                window.setKeyboardOrientationEmulationEnabledEXT(false);
                EXPECT_EQ(getGraphicsDeviceProperty().getPresentationParametersProperty().getDisplayOrientationProperty(), DisplayOrientation::Default);
                manager.setSupportedOrientationsProperty(DisplayOrientation::Portrait);
                window.setKeyboardOrientationEmulationEnabledEXT(true);
                EXPECT_EQ(window.getCurrentOrientationProperty(), DisplayOrientation::Portrait);
                EXPECT_EQ(getGraphicsDeviceProperty().getViewportProperty().getWidthProperty(), 480);
                keyboard->SetPending({KeyCode::Down}); break;
            case 3:
                EXPECT_EQ(window.getCurrentOrientationProperty(), DisplayOrientation::Portrait);
                Exit(); break;
            default: ADD_FAILURE(); Exit();
            }
        }
    } game(std::move(platform), keyboard);
    game.Run();
    EXPECT_EQ(game.phase, 4);
}
