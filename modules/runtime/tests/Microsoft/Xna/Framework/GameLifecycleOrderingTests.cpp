// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameComponent.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "RuntimePlatformTestSupport.hpp"
#include "System/ObjectDisposedException.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    class LifecycleOrderingGame final : public Game
    {
    public:
        explicit LifecycleOrderingGame(std::vector<std::string>& trace) : trace_(trace)
        {
            setIsFixedTimeStepProperty(false);
        }

    protected:
        void Initialize() override
        {
            trace_.emplace_back("initialize");
            Game::Initialize();
        }
        void LoadContent() override { trace_.emplace_back("load"); }
        void BeginRun() override { trace_.emplace_back("begin"); }
        void Update(GameTime& time) override
        {
            trace_.emplace_back("update");
            Game::Update(time);
            Exit();
        }
        void Draw(const GameTime&) override { trace_.emplace_back("draw"); }
        void EndRun() override { trace_.emplace_back("end"); }
        void UnloadContent() override { trace_.emplace_back("unload"); }

    private:
        std::vector<std::string>& trace_;
    };
}

TEST(GameLifecycleOrderingTest, RunOrdersDeviceCreationInitializationAndExitHooks)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    std::vector<std::string> trace;
    LifecycleOrderingGame game(trace);
    GraphicsDeviceManager manager(&game);
    manager.PreparingDeviceSettings += [&](System::Object* sender, const PreparingDeviceSettingsEventArgs&)
    {
        EXPECT_EQ(sender, &manager);
        trace.emplace_back("prepare");
    };
    manager.DeviceCreated += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &manager);
        EXPECT_NE(manager.getGraphicsDeviceProperty(), nullptr);
        trace.emplace_back("created");
    };
    game.Exiting += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &game);
        trace.emplace_back("exiting");
    };

    game.Run();

    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "created", "initialize", "load",
                                            "begin", "update", "exiting", "end"}));
    game.Dispose();
}

TEST(GameLifecycleOrderingTest, ComponentsDisposeBeforeContentAndDeviceBeforeGameDisposedEvent)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    std::vector<std::string> trace;
    LifecycleOrderingGame game(trace);
    GraphicsDeviceManager manager(&game);
    auto first = std::make_shared<GameComponent>(game);
    auto second = std::make_shared<GameComponent>(game);
    first->Disposed += [&](System::Object*, const System::EventArgs&)
    {
        EXPECT_NO_THROW(game.getContentProperty().Unload());
        trace.emplace_back("first");
    };
    second->Disposed += [&](System::Object*, const System::EventArgs&)
    {
        EXPECT_NO_THROW(game.getContentProperty().Unload());
        trace.emplace_back("second");
    };
    game.getComponentsProperty().Add(first);
    game.getComponentsProperty().Add(second);
    game.Run();
    // Subscribe after Initialize so the game's UnloadContent hook runs first.
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&)
    {
        EXPECT_THROW(game.getContentProperty().Unload(), System::ObjectDisposedException);
        trace.emplace_back("device");
    };
    game.Disposed += [&](System::Object*, const System::EventArgs&)
    {
        EXPECT_THROW(game.Run(), System::ObjectDisposedException);
        trace.emplace_back("disposed");
    };
    trace.clear();

    game.Dispose();

    EXPECT_EQ(trace, (std::vector<std::string>{"first", "second", "unload", "device", "disposed"}));
    EXPECT_EQ(game.getComponentsProperty().getCountProperty(), 0U);
}

TEST(GameLifecycleOrderingTest, PreparingSettingsPrecedesResetAndNoChangeRaisesNoEvents)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    std::vector<std::string> trace;
    LifecycleOrderingGame game(trace);
    GraphicsDeviceManager manager(&game);
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args)
    {
        trace.emplace_back("prepare");
        args.getGraphicsDeviceInformationEXT().getPresentationParametersProperty()
            .setBackBufferWidthProperty(320);
    };
    manager.DeviceCreated += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("created"); };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    manager.CreateDevice();
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "created"}));
    ASSERT_NE(manager.getGraphicsDeviceProperty(), nullptr);
    EXPECT_EQ(manager.getGraphicsDeviceProperty()->getPresentationParametersProperty().getBackBufferWidthProperty(), 320);
    trace.clear();
    manager.ApplyChanges();
    EXPECT_TRUE(trace.empty());

    manager.setPreferredBackBufferWidthProperty(640);
    manager.ApplyChanges();

    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "resetting", "reset"}));
    EXPECT_EQ(manager.getGraphicsDeviceProperty()->getPresentationParametersProperty().getBackBufferWidthProperty(), 320);
    EXPECT_EQ(manager.getPreferredBackBufferWidthProperty(), 640);
    trace.clear();
    manager.ApplyChanges();
    EXPECT_TRUE(trace.empty());
}

TEST(GameLifecycleOrderingTest, RejectedPreparingSettingsLeavesDeviceUntouchedAndAllowsRetry)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    std::vector<std::string> trace;
    LifecycleOrderingGame game(trace);
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    const auto oldWidth = device->getPresentationParametersProperty().getBackBufferWidthProperty();
    bool reject = true;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args)
    {
        trace.emplace_back("prepare");
        args.getGraphicsDeviceInformationEXT().getPresentationParametersProperty()
            .setBackBufferWidthProperty(oldWidth + 32);
        if (reject) throw std::runtime_error("settings rejected");
    };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    manager.setPreferredBackBufferWidthProperty(oldWidth + 16);

    EXPECT_THROW(manager.ApplyChanges(), std::runtime_error);

    EXPECT_EQ(trace, (std::vector<std::string>{"prepare"}));
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    EXPECT_FALSE(device->getIsDisposedProperty());
    EXPECT_EQ(device->getPresentationParametersProperty().getBackBufferWidthProperty(), oldWidth);
    reject = false;
    trace.clear();
    manager.ApplyChanges();
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "resetting", "reset"}));
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    EXPECT_EQ(device->getPresentationParametersProperty().getBackBufferWidthProperty(), oldWidth + 32);
}
