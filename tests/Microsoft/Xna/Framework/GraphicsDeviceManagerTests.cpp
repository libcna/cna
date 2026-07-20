// SPDX-License-Identifier: MS-PL
// REMED-TEST-002: GraphicsDeviceManager requires a live Game/SDL window/graphics backend, which is
// exactly what GraphicsDeviceCapabilityTests.cpp's bare `GraphicsDevice gd;` already constructs
// successfully in this same CnaTests binary. Following GameWindowTests.cpp's own skip-when-
// unavailable precedent, a cheap up-front SDL video probe still guards every TEST() here so a
// genuinely display-less environment skips rather than fails deep inside backend construction.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
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
        SDL_Window* probe = SDL_CreateWindow("cna-gdmtests-probe", 64, 64, SDL_WINDOW_HIDDEN);
        if (probe == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return false;
        }
        SDL_DestroyWindow(probe);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return true;
    }

    // Runs exactly one frame then exits, giving CreateDevice() a chance to run via
    // Game::DoInitialize() without blocking in the loop.
    class OneFrameGame : public Game
    {
    protected:
        void Draw(const GameTime& gameTime) override
        {
            Game::Draw(gameTime);
            Exit();
        }
    };
}

TEST(GraphicsDeviceManagerTest, CreateDeviceIsReachableAfterRun)
{
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);

    ASSERT_NO_THROW(game.Run());

    EXPECT_NE(gdm.getGraphicsDeviceProperty(), nullptr);
}

// Regression guard: GraphicsDeviceManager's own self-initiated ApplyChanges() path already raises
// DeviceResetting/DeviceReset correctly (unconditionally, not gated on device ownership) -- this
// must keep working and must not double-raise.
TEST(GraphicsDeviceManagerTest, ApplyChangesRaisesResettingAndResetExactlyOnce)
{
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    int resettingCount = 0;
    int resetCount = 0;
    gdm.getDeviceResettingEvent() += [&](System::Object*, const System::EventArgs&) { ++resettingCount; };
    gdm.getDeviceResetEvent() += [&](System::Object*, const System::EventArgs&) { ++resetCount; };

    gdm.setPreferredBackBufferWidthProperty(gdm.getPreferredBackBufferWidthProperty() + 16);
    gdm.ApplyChanges();

    EXPECT_EQ(resettingCount, 1);
    EXPECT_EQ(resetCount, 1);
}

// REMED-CORE-007: GraphicsDeviceManager never subscribes to GraphicsDevice's own DeviceResetting/
// DeviceReset events. FNA's IGraphicsDeviceManager.CreateDevice() wires
// graphicsDevice.DeviceResetting += OnDeviceResetting; graphicsDevice.DeviceReset += OnDeviceReset;
// -- CNA instead raises its own separate copies manually, only around its own
// applyToExistingBackend() call (see ApplyChangesRaisesResettingAndResetExactlyOnce above). A real
// backend-detected device-lost/reset (GraphicsDevice's own deviceEventCallback seam, currently wired
// up only by the D3D9 backend) raises GraphicsDevice::DeviceResetting/DeviceReset directly and is
// never forwarded to GraphicsDeviceManager's listeners at all. Simulated here by raising those
// events directly on the managed GraphicsDevice instance, which is exactly what that callback does
// on a real device-lost -- this does not require D3D9 hardware to reproduce the forwarding gap.
//
// This test is EXPECTED TO FAIL until REMED-CORE-007 is fixed (CORE lane, not this task) -- it
// exists to prove the defect is real and reachable, and becomes the regression guard once fixed.
// Not fixed here per this task's strict scope.
TEST(GraphicsDeviceManagerTest, BackendDetectedDeviceLostIsForwardedToManagerListeners)
{
    if (!VideoSubsystemAvailable())
    {
        GTEST_SKIP() << "No usable SDL video subsystem in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    Graphics::GraphicsDevice* device = gdm.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);

    int resettingCount = 0;
    int resetCount = 0;
    gdm.getDeviceResettingEvent() += [&](System::Object*, const System::EventArgs&) { ++resettingCount; };
    gdm.getDeviceResetEvent() += [&](System::Object*, const System::EventArgs&) { ++resetCount; };

    // Simulates what GraphicsDevice's internal deviceEventCallback does on a real backend-detected
    // device-lost/reset cycle -- raised directly on the device's own public events.
    device->DeviceResetting.Raise(device, System::EventArgs::Empty);
    device->DeviceReset.Raise(device, System::EventArgs::Empty);

    EXPECT_EQ(resettingCount, 1)
        << "REMED-CORE-007: GraphicsDeviceManager never subscribed to GraphicsDevice::DeviceResetting, "
           "so a backend-detected device-lost never reaches IGraphicsDeviceService listeners.";
    EXPECT_EQ(resetCount, 1)
        << "REMED-CORE-007: GraphicsDeviceManager never subscribed to GraphicsDevice::DeviceReset, "
           "so a backend-detected device reset never reaches IGraphicsDeviceService listeners.";
}
