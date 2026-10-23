// SPDX-License-Identifier: MS-PL
// ISSUE-082 follow-up: genuine XNA's non-shared GraphicsResource disposal ordering.
// Prepared independently of inherited content-owned alias work. The first four
// cases need no GPU; the last two exercise actual device registration lifetime.
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"

namespace
{
using Microsoft::Xna::Framework::Graphics::GraphicsResource;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
class ResourceEventProbe final : public GraphicsResource
{
public:
    explicit ResourceEventProbe(GraphicsDevice* device = nullptr) : GraphicsResource(device) {}
    const std::string& GetTypeName() const override
    {
        static const std::string name = "ResourceEventProbe";
        return name;
    }
    void FinalizerPath() { GraphicsResource::Dispose(false); }
};

TEST(GraphicsResourceEventOrderTest, DisposingObservesPublishedStateAndOriginalSender)
{
    ResourceEventProbe resource;
    int calls = 0;
    bool sawDisposed = false;
    bool senderMatches = false;
    resource.Disposing += [&](System::Object* sender, const System::EventArgs&)
    {
        ++calls;
        sawDisposed = resource.getIsDisposedProperty();
        senderMatches = sender == &resource;
    };
    resource.Dispose();
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(sawDisposed);
    EXPECT_TRUE(senderMatches);
    EXPECT_TRUE(resource.getIsDisposedProperty());
}

TEST(GraphicsResourceEventOrderTest, ReentrantDisposeDoesNotRaiseAnotherEvent)
{
    ResourceEventProbe resource;
    int calls = 0;
    resource.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        ++calls;
        // Bound the pre-fix witness: expose a second callback without stack overflow.
        if (calls == 1)
            resource.Dispose();
    };
    resource.Dispose();
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(resource.getIsDisposedProperty());
}

TEST(GraphicsResourceEventOrderTest, ThrowingCallbackDoesNotUndoDisposalOrRepeatTheEvent)
{
    ResourceEventProbe resource;
    int calls = 0;
    resource.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        ++calls;
        throw std::runtime_error("original callback failure");
    };
    EXPECT_THROW(resource.Dispose(), std::runtime_error);
    EXPECT_TRUE(resource.getIsDisposedProperty());
    EXPECT_NO_THROW(resource.Dispose());
    EXPECT_EQ(calls, 1);
}

TEST(GraphicsResourceEventOrderTest, FinalizerPathPublishesStateWithoutManagedEvent)
{
    ResourceEventProbe resource;
    int calls = 0;
    resource.Disposing += [&](System::Object*, const System::EventArgs&) { ++calls; };
    resource.FinalizerPath();
    EXPECT_TRUE(resource.getIsDisposedProperty());
    resource.Dispose();
    EXPECT_EQ(calls, 0);
}

TEST(GraphicsResourceEventOrderTest, ThrowingDisposingCallbackStillRetiresDeviceRegistration)
{
    GraphicsDevice device;
    RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    const auto originalCount = device.GetTrackedResourceCount();
    {
        ResourceEventProbe resource(&device);
        ASSERT_EQ(device.GetTrackedResourceCount(), originalCount + 1);
        resource.Disposing += [](System::Object*, const System::EventArgs&)
        {
            throw std::runtime_error("attached resource callback failure");
        };
        EXPECT_THROW(resource.Dispose(), std::runtime_error);
        EXPECT_TRUE(resource.getIsDisposedProperty());
        EXPECT_EQ(device.GetTrackedResourceCount(), originalCount);
    }
    EXPECT_EQ(device.GetTrackedResourceCount(), originalCount);
    EXPECT_NO_THROW(device.Dispose());
}

TEST(GraphicsResourceEventOrderTest, ThrowingDestroyedCallbackStillRetiresRegistrationOnce)
{
    GraphicsDevice device;
    RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    const auto originalCount = device.GetTrackedResourceCount();
    int notifications = 0;
    device.ResourceDestroyed += [&](System::Object*, const auto&)
    {
        ++notifications;
        // Bound the old destructor path: it must not terminate on a second throw.
        if (notifications == 1)
            throw std::runtime_error("device resource-destroyed callback failure");
    };
    {
        ResourceEventProbe resource(&device);
        ASSERT_EQ(device.GetTrackedResourceCount(), originalCount + 1);
        EXPECT_THROW(resource.Dispose(), std::runtime_error);
        EXPECT_TRUE(resource.getIsDisposedProperty());
        EXPECT_EQ(device.GetTrackedResourceCount(), originalCount);
    }
    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(device.GetTrackedResourceCount(), originalCount);
    EXPECT_NO_THROW(device.Dispose());
}
} // namespace
