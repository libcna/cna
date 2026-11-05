// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/DrawableGameComponent.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "System/InvalidOperationException.hpp"
#include "RuntimePlatformTestSupport.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    struct ComponentDeviceService final : Graphics::IGraphicsDeviceService
    {
        Graphics::GraphicsDevice* device = nullptr;
        System::EventHandler<System::EventArgs> created, disposing, reset, resetting;
        Graphics::GraphicsDevice* getGraphicsDeviceProperty() const override { return device; }
        System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() override { return created; }
        System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() override { return disposing; }
        System::EventHandler<System::EventArgs>& getDeviceResetEvent() override { return reset; }
        System::EventHandler<System::EventArgs>& getDeviceResettingEvent() override { return resetting; }
    };

    class ContentProbe final : public DrawableGameComponent
    {
    public:
        using GameComponent::Dispose;
        explicit ContentProbe(Game& game) : DrawableGameComponent(game) {}
        std::vector<std::string> trace;
        bool failLoad = false;
        bool failUnload = false;
    protected:
        void LoadContent() override
        {
            trace.emplace_back("load");
            if (failLoad) throw std::runtime_error("load failed");
        }
        void UnloadContent() override
        {
            trace.emplace_back("unload");
            if (failUnload) throw std::runtime_error("unload failed");
        }
    };

    class DrawableContentContractTest : public ::testing::Test
    {
    protected:
        ComponentDeviceService service;
        std::unique_ptr<Game> game;
        void SetUp() override
        {
            if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
                GTEST_SKIP() << "Selected platform cannot create the hidden Game window";
            game = std::make_unique<Game>();
        }
        void Register() { game->getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&service); }
        void ExpectSubscriptions(std::size_t count)
        {
            EXPECT_EQ(service.created.Size(), count);
            EXPECT_EQ(service.disposing.Size(), count);
            EXPECT_EQ(service.reset.Size(), count);
            EXPECT_EQ(service.resetting.Size(), count);
        }
    };
}

TEST_F(DrawableContentContractTest, MissingServiceThrowsAndInitializationCanBeRetried)
{
    // XNA Refresh Initialize IL_0011..0048 throws before initialized is stored at IL_00ba.
    ContentProbe c(*game);
    EXPECT_THROW(c.Initialize(), System::InvalidOperationException);
    EXPECT_TRUE(c.trace.empty());
    Register();
    service.device = &game->getGraphicsDeviceProperty();
    c.Initialize();
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load"}));
    ExpectSubscriptions(1);
    c.Dispose();
}

TEST_F(DrawableContentContractTest, GraphicsDeviceRequiresInitializationAndKeepsTheCapturedService)
{
    // XNA Refresh get_GraphicsDevice IL_0000..001e uses the service captured by Initialize.
    Register();
    service.device = &game->getGraphicsDeviceProperty();
    ContentProbe c(*game);
    EXPECT_THROW((void)c.getGraphicsDeviceProperty(), System::InvalidOperationException);
    c.Initialize();
    EXPECT_EQ(&c.getGraphicsDeviceProperty(), service.device);
    game->getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
    ComponentDeviceService replacement;
    game->getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&replacement);
    EXPECT_EQ(&c.getGraphicsDeviceProperty(), service.device);
    c.Dispose();
    game->getServicesProperty().RemoveService<Graphics::IGraphicsDeviceService>();
}

TEST_F(DrawableContentContractTest, MissingDeviceDefersLoadAndResetEventsDoNotReload)
{
    // Refresh Initialize IL_0049..00b8 always hooks four events, loading only if device exists.
    // DeviceCreated/DeviceDisposing IL_0000..0006 call load/unload; both reset handlers IL_0000 return.
    Register();
    ContentProbe c(*game);
    c.Initialize();
    c.Initialize();
    EXPECT_TRUE(c.trace.empty());
    ExpectSubscriptions(1);
    service.device = &game->getGraphicsDeviceProperty();
    service.created.Raise(nullptr, System::EventArgs::Empty);
    service.resetting.Raise(nullptr, System::EventArgs::Empty);
    service.reset.Raise(nullptr, System::EventArgs::Empty);
    service.disposing.Raise(nullptr, System::EventArgs::Empty);
    service.created.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load", "unload", "load"}));
    c.Dispose();
    ExpectSubscriptions(0);
    service.created.Raise(nullptr, System::EventArgs::Empty);
    service.disposing.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load", "unload", "load", "unload"}));
}

TEST_F(DrawableContentContractTest, ExistingDeviceLoadsOnceButLaterCreationStillLoads)
{
    // Refresh Initialize IL_0049..00ba subscribes even when the first device already exists.
    Register();
    service.device = &game->getGraphicsDeviceProperty();
    ContentProbe c(*game);
    c.Initialize();
    c.Initialize();
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load"}));
    ExpectSubscriptions(1);
    service.disposing.Raise(nullptr, System::EventArgs::Empty);
    service.created.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load", "unload", "load"}));
    c.Dispose();
    ExpectSubscriptions(0);
}

TEST_F(DrawableContentContractTest, ThrowingLoadLeavesInitializationRetryableAndRepeatsSubscriptions)
{
    // Refresh Initialize IL_0049..00b3 hooks before LoadContent; IL_00ba is reached only on success.
    Register();
    service.device = &game->getGraphicsDeviceProperty();
    ContentProbe c(*game);
    c.failLoad = true;
    EXPECT_THROW(c.Initialize(), std::runtime_error);
    ExpectSubscriptions(1);
    c.failLoad = false;
    c.Initialize();
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load", "load"}));
    ExpectSubscriptions(2);
    // Dispose IL_0011..0068 removes one occurrence of each delegate per explicit call.
    c.Dispose();
    ExpectSubscriptions(1);
    c.Dispose();
    ExpectSubscriptions(0);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"load", "load", "unload", "unload"}));
}

TEST_F(DrawableContentContractTest, DestructionDisconnectsCallbacksWithoutCallingContentHooks)
{
    // C++ lifetime adaptation: a destroyed target cannot remain callable through a service event.
    // XNA's managed delegate keeps its target alive; explicit Dispose remains the unload path.
    Register();
    { ContentProbe c(*game); c.Initialize(); ExpectSubscriptions(1); }
    ExpectSubscriptions(0);
    service.created.Raise(nullptr, System::EventArgs::Empty);
    service.disposing.Raise(nullptr, System::EventArgs::Empty);
}

TEST_F(DrawableContentContractTest, ThrowingUnloadKeepsSubscriptionsUntilSuccessfulExplicitRetry)
{
    // Refresh Dispose IL_0003..0004 calls UnloadContent BEFORE IL_0011..0068 removes delegates.
    Register();
    ContentProbe c(*game);
    c.Initialize();
    c.failUnload = true;
    EXPECT_THROW(c.Dispose(), std::runtime_error);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"unload"}));
    ExpectSubscriptions(1);
    c.failUnload = false;
    c.Dispose();
    ExpectSubscriptions(0);
    service.disposing.Raise(nullptr, System::EventArgs::Empty);
    EXPECT_EQ(c.trace, (std::vector<std::string>{"unload", "unload"}));
}
