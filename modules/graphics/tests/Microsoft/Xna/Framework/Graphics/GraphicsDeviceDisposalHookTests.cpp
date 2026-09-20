// SPDX-License-Identifier: MS-PL
//
// XNA-MISSING-009: the documented protected GraphicsDevice.Dispose(Boolean).
//
// XNA's public Dispose() is `Dispose(true); GC.SuppressFinalize(this);` and its finalizer is
// `Dispose(false)`; the hook itself is protected and virtual
// (xna4-decomp/.../Microsoft.Xna.Framework.Graphics/GraphicsDevice.cs:2769). CNA had the public
// disposer only, so a subclass had no seam. What matters here is that the hook is reached however
// disposal is requested, that the order the public disposer established is unchanged, and that the
// Disposing notification stays tied to an explicit disposal.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/EventArgs.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"

using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    /// Observes the hook: whether it runs, with which flag, and what the device's state is when it
    /// does. The base is entered afterwards, so a subclass can release its own state first.
    class DeviceDisposeProbe final : public GraphicsDevice
    {
    public:
        using GraphicsDevice::Dispose;

        int hookCalls = 0;
        bool lastDisposing = false;
        bool disposedWhenHookRan = false;

        void DisposeWithoutNotifying() { GraphicsDevice::Dispose(false); }

    protected:
        void Dispose(bool disposing) override
        {
            ++hookCalls;
            lastDisposing = disposing;
            disposedWhenHookRan = getIsDisposedProperty();
            GraphicsDevice::Dispose(disposing);
        }
    };
}

TEST(GraphicsDeviceDisposalHookTest, PublicDisposeRoutesThroughTheProtectedHook)
{
    DeviceDisposeProbe probe;
    ASSERT_EQ(probe.hookCalls, 0);
    ASSERT_FALSE(probe.getIsDisposedProperty());

    probe.Dispose();
    EXPECT_EQ(probe.hookCalls, 1);
    EXPECT_TRUE(probe.lastDisposing);
    EXPECT_FALSE(probe.disposedWhenHookRan)
        << "the subclass runs before the base marks the device disposed";
    EXPECT_TRUE(probe.getIsDisposedProperty());
}

TEST(GraphicsDeviceDisposalHookTest, DisposalThroughTheDisposableInterfaceReachesTheOverride)
{
    DeviceDisposeProbe probe;
    System::IDisposable& asDisposable = probe;
    asDisposable.Dispose();

    EXPECT_EQ(probe.hookCalls, 1);
    EXPECT_TRUE(probe.getIsDisposedProperty());
}

TEST(GraphicsDeviceDisposalHookTest, RepeatedDisposalChangesNothingAfterTheFirst)
{
    DeviceDisposeProbe probe;
    probe.Dispose();
    probe.Dispose();
    probe.Dispose();

    // Every call reaches the hook, because the public method is a plain forwarder, but the base's
    // own guard means only the first does any work.
    EXPECT_EQ(probe.hookCalls, 3);
    EXPECT_TRUE(probe.getIsDisposedProperty());
}

TEST(GraphicsDeviceDisposalHookTest, TheDeviceIsAlreadyDisposedWhenDisposingIsRaised)
{
    // The existing lifecycle boundary: isDisposed_ is set before the event, so a handler cannot
    // issue work against a device that is being torn down. Routing through the hook must not move
    // that boundary.
    GraphicsDevice device;
    bool disposedInsideHandler = false;
    int notifications = 0;
    device.Disposing += [&](System::Object* sender, const System::EventArgs&) {
        ++notifications;
        disposedInsideHandler =
            static_cast<GraphicsDevice*>(sender)->getIsDisposedProperty();
    };

    device.Dispose();
    EXPECT_EQ(notifications, 1);
    EXPECT_TRUE(disposedInsideHandler);

    // A second disposal does not notify again.
    device.Dispose();
    EXPECT_EQ(notifications, 1);
}

TEST(GraphicsDeviceDisposalHookTest, TheHookRaisesDisposingOnlyForAnExplicitDisposal)
{
    DeviceDisposeProbe probe;
    int notifications = 0;
    probe.Disposing += [&notifications](System::Object*, const System::EventArgs&) {
        ++notifications;
    };

    // XNA's finalizer path is Dispose(false), which tears the device down without notifying.
    probe.DisposeWithoutNotifying();
    EXPECT_EQ(notifications, 0);
    EXPECT_TRUE(probe.getIsDisposedProperty());
    EXPECT_FALSE(probe.lastDisposing);
}

TEST(GraphicsDeviceDisposalHookTest, OwnedResourcesAreStillDisposedThroughTheHook)
{
    // The resource cascade is the part of disposal a caller depends on most, so it is asserted
    // through the subclass seam rather than only through the public method.
    DeviceDisposeProbe probe;
    Texture2D texture(probe, 4, 4);
    ASSERT_FALSE(texture.getIsDisposedProperty());

    probe.Dispose();
    EXPECT_EQ(probe.hookCalls, 1);
    EXPECT_TRUE(probe.getIsDisposedProperty());
    EXPECT_TRUE(texture.getIsDisposedProperty())
        << "a device disposal must still cascade to the resources it owns";
}
