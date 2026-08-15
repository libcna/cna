// SPDX-License-Identifier: MS-PL
// PLAT-56: Game requires the selected platform/renderer combination to construct its ordinary
// hidden window. Probe that capability through IPlatform so this suite is unchanged across
// platform selections and never names a backend API.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "System/EventArgs.hpp"
#include "RuntimePlatformTestSupport.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    // Records lifecycle call order/counts and exits after the first Draw().
    class LifecycleTestGame : public Game
    {
    public:
        int initializeCalls = 0;
        int loadContentCalls = 0;
        int updateCalls = 0;
        int drawCalls = 0;
        int unloadContentCalls = 0;

    protected:
        void Initialize() override
        {
            ++initializeCalls;
            Game::Initialize(); // XNA contract: base Initialize() invokes LoadContent().
        }

        void LoadContent() override
        {
            ++loadContentCalls;
        }

        void Update(GameTime& gameTime) override
        {
            ++updateCalls;
            Game::Update(gameTime);
        }

        void Draw(const GameTime& gameTime) override
        {
            ++drawCalls;
            Game::Draw(gameTime);
            Exit();
        }

        void UnloadContent() override
        {
            ++unloadContentCalls;
        }
    };

    // Minimal IGraphicsDeviceService test double, independent of GraphicsDeviceManager, used to
    // exercise Game::Initialize()'s deferred-LoadContent branch (a registered service whose device
    // is not yet available at Initialize() time) without needing a second real GraphicsDevice.
    class DeferredDeviceService : public Graphics::IGraphicsDeviceService
    {
    public:
        Graphics::GraphicsDevice* device = nullptr;
        System::EventHandler<System::EventArgs> DeviceCreatedEvt;
        System::EventHandler<System::EventArgs> DeviceDisposingEvt;
        System::EventHandler<System::EventArgs> DeviceResetEvt;
        System::EventHandler<System::EventArgs> DeviceResettingEvt;

        [[nodiscard]] Graphics::GraphicsDevice* getGraphicsDeviceProperty() const override { return device; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceCreatedEvent() override { return DeviceCreatedEvt; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceDisposingEvent() override { return DeviceDisposingEvt; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResetEvent() override { return DeviceResetEvt; }
        [[nodiscard]] System::EventHandler<System::EventArgs>& getDeviceResettingEvent() override { return DeviceResettingEvt; }
    };
}

TEST(GameTest, RunExecutesLifecycleInDocumentedOrder)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());

    EXPECT_EQ(game.initializeCalls, 1);
    EXPECT_EQ(game.loadContentCalls, 1);
    EXPECT_GE(game.updateCalls, 1);
    EXPECT_GE(game.drawCalls, 1);
}

// REMED-CORE-006 (fixed): Game::Initialize() now subscribes graphicsDeviceService.DeviceDisposing
// += (o,e) => UnloadContent(), matching FNA (Game.cs:649-662). Disposing the game's registered
// IGraphicsDeviceService (the GraphicsDeviceManager here) invokes UnloadContent() exactly once via
// the real public disposal path: Game::Dispose() -> Dispose(true) -> disposes
// graphicsDeviceService_ -> GraphicsDeviceManager::Dispose(true) -> OnDeviceDisposing()
// (REMED-CORE-014's fix: no longer gated on ownsGraphicsDevice_, which is always false for a
// Game-attached manager).
TEST(GameTest, DisposingDeviceInvokesUnloadContent)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());
    game.Dispose();

    EXPECT_EQ(game.unloadContentCalls, 1);
}

// Regression guard: a second explicit Dispose() call must not re-invoke UnloadContent().
// GraphicsDeviceManager::Dispose(bool)'s own disposed_ guard makes the underlying DeviceDisposing
// raise a one-time event even though Game::Dispose() itself unconditionally re-raises Disposed on
// every call (FNA-faithful; Game.cs:296-304 does the same).
TEST(GameTest, RepeatedDisposeDoesNotReinvokeUnloadContent)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());
    game.Dispose();
    ASSERT_NO_THROW(game.Dispose());

    EXPECT_EQ(game.unloadContentCalls, 1);
}

// Repeated Game/GraphicsDeviceManager construction and full lifecycle (including disposal) in one
// process must behave identically each time -- no leftover static/global state from one instance's
// DeviceDisposing subscription or disposal should affect the next.
TEST(GameTest, UnloadContentWorksAcrossRepeatedGameInstancesInOneProcess)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    for (int i = 0; i < 2; ++i)
    {
        LifecycleTestGame game;
        GraphicsDeviceManager gdm(&game);

        ASSERT_NO_THROW(game.Run()) << "iteration " << i;
        game.Dispose();

        EXPECT_EQ(game.unloadContentCalls, 1) << "iteration " << i;
    }
}

// REMED-CORE-006 full-fidelity branch: FNA's Initialize() also subscribes
// graphicsDeviceService.DeviceCreated += (o,e) => LoadContent(); when the registered service's
// device is not yet available at Initialize() time, deferring LoadContent() rather than skipping
// it. Exercised directly against a minimal IGraphicsDeviceService double (no GraphicsDeviceManager
// attached) since the Game+GraphicsDeviceManager combination always creates the device before
// Initialize() runs (DoInitialize() calls CreateDevice() first), making this branch otherwise
// unreachable through the normal production path.
TEST(GameTest, DeferredLoadContentFiresOnDeviceCreatedWhenServiceHasNoDeviceAtInitializeTime)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    LifecycleTestGame game;
    DeferredDeviceService fakeService;
    game.getServicesProperty().AddService<Graphics::IGraphicsDeviceService>(&fakeService);

    game.RunOneFrame();

    EXPECT_EQ(game.loadContentCalls, 0)
        << "LoadContent() must not fire immediately when the registered IGraphicsDeviceService has "
           "no device yet.";

    fakeService.DeviceCreatedEvt.Raise(nullptr, System::EventArgs::Empty);

    EXPECT_EQ(game.loadContentCalls, 1);
}
