// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "RuntimePlatformTestSupport.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;

TEST(GraphicsDeviceManagerLifecycleContractTest, ExplicitDisposeReleasesGameOwnedDeviceBeforeDisposed)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    std::vector<std::string> trace;
    manager.DeviceDisposing += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &manager);
        EXPECT_TRUE(device->getIsDisposedProperty());
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
        trace.emplace_back("device");
    };
    manager.Disposed += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &manager);
        EXPECT_TRUE(device->getIsDisposedProperty());
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), nullptr);
        EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), nullptr);
        trace.emplace_back("manager");
    };
    manager.Dispose();
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"device", "manager", "manager"}));
}

TEST(GraphicsDeviceManagerLifecycleContractTest, ReentrantAndThrowingDisposedHandlersCanBeInvokedAgain)
{
    GraphicsDeviceManager manager;
    int notifications = 0;
    manager.Disposed += [&](System::Object*, const System::EventArgs&)
    {
        ++notifications;
        if (notifications == 1) manager.Dispose();
        if (notifications == 2) throw std::runtime_error("disposed callback refusal");
    };
    EXPECT_THROW(manager.Dispose(), std::runtime_error);
    EXPECT_EQ(notifications, 2);
    EXPECT_NO_THROW(manager.Dispose());
    EXPECT_EQ(notifications, 3);
}

TEST(GraphicsDeviceManagerLifecycleContractTest, ReentrantDeviceDisposalCompletesInnerAndOuterNotifications)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    std::vector<std::string> trace;
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&)
    {
        trace.emplace_back("device-enter");
        manager.Dispose();
        EXPECT_EQ(manager.getGraphicsDeviceProperty(), nullptr);
        EXPECT_TRUE(device->getIsDisposedProperty());
        trace.emplace_back("device-leave");
    };
    manager.Disposed += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("manager"); };
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"device-enter", "manager", "device-leave", "manager"}));
    trace.clear();
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"manager"}));
}

TEST(GraphicsDeviceManagerLifecycleContractTest, DestructorDoesNotRaiseManagedDisposedEvent)
{
    int notifications = 0;
    {
        GraphicsDeviceManager manager;
        manager.Disposed += [&](System::Object*, const System::EventArgs&) { ++notifications; };
    }
    EXPECT_EQ(notifications, 0);
}

TEST(GraphicsDeviceManagerLifecycleContractTest, UnmanagedHookPreservesDeviceAndServicesForExplicitDispose)
{
    class FinalizerProbe final : public GraphicsDeviceManager
    {
    public:
        using GraphicsDeviceManager::GraphicsDeviceManager;
        void FinalizerPath() { Dispose(false); }
    };
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    FinalizerProbe manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    std::vector<std::string> trace;
    manager.DeviceDisposing += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("device"); };
    manager.Disposed += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("manager"); };
    manager.FinalizerPath();
    EXPECT_TRUE(trace.empty());
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    EXPECT_FALSE(device->getIsDisposedProperty());
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &manager);
    EXPECT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
    manager.Dispose();
    EXPECT_EQ(trace, (std::vector<std::string>{"device", "manager"}));
}

TEST(GraphicsDeviceManagerLifecycleContractTest, DestructionDetachesWindowCallbackAndPreservesReplacementServices)
{
    Game game;
    EXPECT_TRUE(game.getWindowProperty().ClientSizeChanged.Empty());
    GraphicsDeviceManager replacement;
    {
        GraphicsDeviceManager manager(&game);
        EXPECT_FALSE(game.getWindowProperty().ClientSizeChanged.Empty());
        game.getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
        game.getServicesProperty().RemoveService<IGraphicsDeviceManager>();
        game.getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&replacement);
        game.getServicesProperty().AddService<IGraphicsDeviceManager>(&replacement);
    }
    EXPECT_TRUE(game.getWindowProperty().ClientSizeChanged.Empty());
    EXPECT_EQ(game.getServicesProperty().GetService<Graphics::IGraphicsDeviceService>(), &replacement);
    EXPECT_EQ(game.getServicesProperty().GetService<IGraphicsDeviceManager>(), &replacement);
}

TEST(GraphicsDeviceManagerLifecycleContractTest, ToggleRefusalKeepsPreferenceAndDeviceForRetry)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    manager.CreateDevice();
    auto* const device = manager.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);
    const bool initial = manager.getIsFullScreenProperty();
    std::vector<std::string> trace;
    bool refuse = true;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs&)
    {
        trace.emplace_back("prepare");
        if (refuse) throw std::runtime_error("toggle refusal");
    };
    manager.DeviceResetting += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("resetting"); };
    manager.DeviceReset += [&](System::Object*, const System::EventArgs&) { trace.emplace_back("reset"); };
    EXPECT_THROW(manager.ToggleFullScreen(), std::runtime_error);
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare"}));
    EXPECT_EQ(manager.getIsFullScreenProperty(), !initial);
    EXPECT_EQ(device->getPresentationParametersProperty().getIsFullScreenProperty(), initial);
    EXPECT_EQ(manager.getGraphicsDeviceProperty(), device);
    EXPECT_FALSE(device->getIsDisposedProperty());
    refuse = false;
    trace.clear();
    manager.ApplyChanges();
    EXPECT_EQ(trace, (std::vector<std::string>{"prepare", "resetting", "reset"}));
    EXPECT_EQ(device->getPresentationParametersProperty().getIsFullScreenProperty(), !initial);
    trace.clear();
    manager.ApplyChanges();
    EXPECT_TRUE(trace.empty());
    manager.Dispose();
}
