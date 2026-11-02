// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "RuntimePlatformTestSupport.hpp"

#include <string>
#include <stdexcept>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    class DisposalDispatchGame final : public Game
    {
    public:
        using Game::Dispose;
        std::vector<std::string> trace;
        bool callBase = true;
        int unloads = 0;
        void FinalizerPath() { Dispose(false); }

    protected:
        void Update(GameTime&) override { Exit(); }
        void UnloadContent() override { ++unloads; }
        void Dispose(bool disposing) override
        {
            trace.emplace_back("hook-enter");
            if (callBase) Game::Dispose(disposing);
            trace.emplace_back("hook-leave");
        }
    };

    struct DisposableComponent final : IGameComponent, System::IDisposable
    {
        int disposals = 0;
        bool refuse = false;
        void Initialize() override {}
        void Dispose() override
        {
            ++disposals;
            if (refuse) throw std::runtime_error("component disposal refusal");
        }
    };
}

TEST(GameDisposalDispatchContractTest, BaseHookRaisesDisposedBeforeReturningToOverride)
{
    DisposalDispatchGame game;
    game.Exiting += [&](System::Object*, const System::EventArgs&) { game.trace.emplace_back("exiting"); };
    game.Disposed += [&](System::Object* sender, const System::EventArgs&)
    {
        EXPECT_EQ(sender, &game);
        game.trace.emplace_back("disposed");
    };
    game.Dispose();
    EXPECT_EQ(game.trace, (std::vector<std::string>{"hook-enter", "disposed", "hook-leave"}));
    game.trace.clear();
    game.Dispose();
    EXPECT_EQ(game.trace, (std::vector<std::string>{"hook-enter", "disposed", "hook-leave"}));
}

TEST(GameDisposalDispatchContractTest, OverrideWithoutBaseCallSuppressesDisposed)
{
    DisposalDispatchGame game;
    game.callBase = false;
    game.Disposed += [&](System::Object*, const System::EventArgs&) { game.trace.emplace_back("disposed"); };
    game.Dispose();
    EXPECT_EQ(game.trace, (std::vector<std::string>{"hook-enter", "hook-leave"}));
}

TEST(GameDisposalDispatchContractTest, FinalizerPathDoesNotRaiseDisposed)
{
    DisposalDispatchGame game;
    game.Disposed += [&](System::Object*, const System::EventArgs&) { game.trace.emplace_back("disposed"); };
    game.FinalizerPath();
    EXPECT_EQ(game.trace, (std::vector<std::string>{"hook-enter", "hook-leave"}));
}

TEST(GameDisposalDispatchContractTest, RepeatedDisposeRevisitsComponentsAndLeavesContentUsable)
{
    DisposableComponent component;
    DisposalDispatchGame game;
    game.getComponentsProperty().Add(&component);
    game.Dispose();
    EXPECT_EQ(component.disposals, 1);
    EXPECT_NO_THROW(game.getContentProperty().Unload());
    game.Dispose();
    EXPECT_EQ(component.disposals, 2);
    EXPECT_NO_THROW(game.getContentProperty().Unload());
}

TEST(GameDisposalDispatchContractTest, UnmanagedHookLeavesExplicitComponentDisposalPending)
{
    DisposableComponent component;
    DisposalDispatchGame game;
    game.getComponentsProperty().Add(&component);
    game.FinalizerPath();
    EXPECT_EQ(component.disposals, 0);
    game.Dispose();
    EXPECT_EQ(component.disposals, 1);
}

TEST(GameDisposalDispatchContractTest, GameUnhooksContentEventsAfterManagerDisposal)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    DisposalDispatchGame game;
    GraphicsDeviceManager manager(&game);
    game.Run();
    game.Dispose();
    EXPECT_EQ(game.unloads, 1);
    manager.DeviceDisposing.Raise(&manager, System::EventArgs::Empty);
    EXPECT_EQ(game.unloads, 1);
    game.Dispose();
    EXPECT_EQ(game.unloads, 1);
}

TEST(GameDisposalDispatchContractTest, ComponentRefusalStopsBeforeLaterComponentsAndGameEventThenRetries)
{
    DisposableComponent first, second;
    DisposalDispatchGame game;
    game.getComponentsProperty().Add(&first);
    game.getComponentsProperty().Add(&second);
    int notifications = 0;
    game.Disposed += [&](System::Object*, const System::EventArgs&) { ++notifications; };
    first.refuse = true;
    EXPECT_THROW(game.Dispose(), std::runtime_error);
    EXPECT_EQ(first.disposals, 1);
    EXPECT_EQ(second.disposals, 0);
    EXPECT_EQ(notifications, 0);
    EXPECT_NO_THROW(game.getContentProperty().Unload());
    first.refuse = false;
    game.Dispose();
    EXPECT_EQ(first.disposals, 2);
    EXPECT_EQ(second.disposals, 1);
    EXPECT_EQ(notifications, 1);
}
