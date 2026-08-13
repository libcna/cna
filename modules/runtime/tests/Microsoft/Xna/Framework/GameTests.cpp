// SPDX-License-Identifier: MS-PL
// REMED-TEST-002: Game requires a live SDL window + graphics device, which is exactly what
// GraphicsDeviceCapabilityTests.cpp's bare `GraphicsDevice gd;` already constructs successfully in
// this same CnaTests binary -- Game::Game() unconditionally embeds one of those same GraphicsDevice
// objects (GraphicsDevice_). Following GameWindowTests.cpp's own skip-when-unavailable precedent,
// a cheap up-front SDL video probe still guards every TEST() here so a genuinely display-less
// environment skips rather than fails deep inside Game's own renderer construction.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "System/EventArgs.hpp"

using namespace Microsoft::Xna::Framework;

namespace CNA::Internal
{
    class GameTestPeer
    {
    public:
        static bool IsSuspended(const Microsoft::Xna::Framework::Game& game) { return game.isSuspended_; }
        static void PollEvents(Microsoft::Xna::Framework::Game& game) { game.PollEvents(); }
    };
}

namespace
{
    // Matches GameWindowTests.cpp's own probe-then-skip idiom.
    bool VideoSubsystemAvailable()
    {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        {
            return false;
        }
        SDL_Window* probe = SDL_CreateWindow("cna-gametests-probe", 64, 64, SDL_WINDOW_HIDDEN);
        if (probe == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return false;
        }
        SDL_DestroyWindow(probe);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return true;
    }

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
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
    }

    LifecycleTestGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());

    EXPECT_EQ(game.initializeCalls, 1);
    EXPECT_EQ(game.loadContentCalls, 1);
    EXPECT_GE(game.updateCalls, 1);
    EXPECT_GE(game.drawCalls, 1);
}

TEST(GameTest, MobileLifecycleEventsSuspendResumeAndTerminateTheLoop)
{
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
    }

    LifecycleTestGame game;
    ASSERT_FALSE(CNA::Internal::GameTestPeer::IsSuspended(game));

    SDL_Event event{};
    event.type = SDL_EVENT_DID_ENTER_BACKGROUND;
    ASSERT_TRUE(SDL_PushEvent(&event));
    CNA::Internal::GameTestPeer::PollEvents(game);
    EXPECT_TRUE(CNA::Internal::GameTestPeer::IsSuspended(game));

    event = {};
    event.type = SDL_EVENT_WILL_ENTER_FOREGROUND;
    ASSERT_TRUE(SDL_PushEvent(&event));
    CNA::Internal::GameTestPeer::PollEvents(game);
    EXPECT_FALSE(CNA::Internal::GameTestPeer::IsSuspended(game));

    event = {};
    event.type = SDL_EVENT_DID_ENTER_BACKGROUND;
    ASSERT_TRUE(SDL_PushEvent(&event));
    event = {};
    event.type = SDL_EVENT_TERMINATING;
    ASSERT_TRUE(SDL_PushEvent(&event));
    CNA::Internal::GameTestPeer::PollEvents(game);
    EXPECT_TRUE(CNA::Internal::GameTestPeer::IsSuspended(game));
    EXPECT_FALSE(game.RunApplication);
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
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
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
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
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
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
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
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
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
