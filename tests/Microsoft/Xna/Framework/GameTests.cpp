// SPDX-License-Identifier: MS-PL
// REMED-TEST-002: Game requires a live SDL window + graphics device, which is exactly what
// GraphicsDeviceCapabilityTests.cpp's bare `GraphicsDevice gd;` already constructs successfully in
// this same CnaTests binary -- Game::Game() unconditionally embeds one of those same GraphicsDevice
// objects (GraphicsDevice_). Following GameWindowTests.cpp's own skip-when-unavailable precedent,
// a cheap up-front SDL video probe still guards every TEST() here so a genuinely display-less
// environment skips rather than fails deep inside Game's own backend construction.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "System/EventArgs.hpp"

using namespace Microsoft::Xna::Framework;

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

// REMED-CORE-006: Game::UnloadContent() is a dead virtual lifecycle hook. FNA's Initialize()
// subscribes graphicsDeviceService.DeviceDisposing += (o,e) => UnloadContent(); CNA's Initialize()
// never performs that subscription, so UnloadContent() is never invoked under any circumstance.
//
// This test is EXPECTED TO FAIL until REMED-CORE-006 is fixed (CORE lane, not this task) -- it
// exists to prove the defect is real and reachable, and becomes the regression guard once fixed.
// Not fixed here per this task's strict scope (BUILD_TEST_CI/TEST-002 adds coverage; CORE owns the
// production fix).
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

    EXPECT_EQ(game.unloadContentCalls, 1)
        << "REMED-CORE-006: UnloadContent() was not invoked when the graphics device service was "
           "disposed -- Game::Initialize() does not subscribe to DeviceDisposing, unlike FNA.";
}
