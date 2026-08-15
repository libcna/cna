// SPDX-License-Identifier: MS-PL
// PLAT-56: GraphicsDeviceManager requires the selected platform/renderer combination to construct
// its ordinary hidden window. Probe that capability through IPlatform so this suite is unchanged
// across platform selections and never names a backend API.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/EventArgs.hpp"
#include "RuntimePlatformTestSupport.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
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
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
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
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
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

// REMED-CORE-007 (fixed): CreateDevice() now subscribes graphicsDevice_->DeviceResetting/
// DeviceReset, forwarding to this manager's own OnDeviceResetting()/OnDeviceReset(), matching FNA's
// IGraphicsDeviceManager.CreateDevice() (graphicsDevice.DeviceResetting += OnDeviceResetting;
// graphicsDevice.DeviceReset += OnDeviceReset;). A real renderer-detected device-lost/reset
// (GraphicsDevice's own deviceEventCallback seam, currently wired up only by the D3D9 renderer)
// raises GraphicsDevice::DeviceResetting/DeviceReset directly -- simulated here by raising those
// events directly on the managed GraphicsDevice instance, which is exactly what that callback does
// on a real device-lost, without requiring D3D9 hardware.
TEST(GraphicsDeviceManagerTest, RendererDetectedDeviceLostIsForwardedToManagerListeners)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
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

    // Simulates what GraphicsDevice's internal deviceEventCallback does on a real renderer-detected
    // device-lost/reset cycle -- raised directly on the device's own public events.
    device->DeviceResetting.Raise(device, System::EventArgs::Empty);
    device->DeviceReset.Raise(device, System::EventArgs::Empty);

    EXPECT_EQ(resettingCount, 1);
    EXPECT_EQ(resetCount, 1);
}

// FNA fidelity detail: GraphicsDeviceManager's OnDeviceResetting/OnDeviceReset/OnDeviceDisposing
// all re-send with `this` as sender rather than forwarding whatever sender they were called with
// (an asymmetry present in the real FNA source -- OnDeviceCreated is the one exception, which does
// forward its sender). This only became observable once forwarding from the managed GraphicsDevice
// was wired up (REMED-CORE-007) -- previously every call site already passed `this` regardless.
TEST(GraphicsDeviceManagerTest, ForwardedDeviceEventsReportTheManagerAsSender)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    Graphics::GraphicsDevice* device = gdm.getGraphicsDeviceProperty();
    ASSERT_NE(device, nullptr);

    System::Object* resettingSender = nullptr;
    System::Object* resetSender = nullptr;
    gdm.getDeviceResettingEvent() += [&](System::Object* sender, const System::EventArgs&) { resettingSender = sender; };
    gdm.getDeviceResetEvent() += [&](System::Object* sender, const System::EventArgs&) { resetSender = sender; };

    device->DeviceResetting.Raise(device, System::EventArgs::Empty);
    device->DeviceReset.Raise(device, System::EventArgs::Empty);

    EXPECT_EQ(resettingSender, static_cast<System::Object*>(&gdm));
    EXPECT_EQ(resetSender, static_cast<System::Object*>(&gdm));
}

// REMED-CORE-014 regression guard: a second explicit Dispose() call must not re-raise
// DeviceDisposing (GraphicsDeviceManager::Dispose(bool)'s disposed_ guard makes the whole method
// idempotent, independent of ownsGraphicsDevice_).
TEST(GraphicsDeviceManagerTest, RepeatedDisposeDoesNotReraiseDeviceDisposing)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
    {
        GTEST_SKIP() << "The selected platform cannot create a test window in this environment.";
    }

    OneFrameGame game;
    GraphicsDeviceManager gdm(&game);
    ASSERT_NO_THROW(game.Run());

    int disposingCount = 0;
    gdm.getDeviceDisposingEvent() += [&](System::Object*, const System::EventArgs&) { ++disposingCount; };

    gdm.Dispose();
    ASSERT_NO_THROW(gdm.Dispose());

    EXPECT_EQ(disposingCount, 1);
}
