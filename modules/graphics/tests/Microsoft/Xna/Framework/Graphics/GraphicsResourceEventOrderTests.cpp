// SPDX-License-Identifier: MS-PL
// ISSUE-082 follow-up: genuine XNA's non-shared GraphicsResource disposal ordering.
// Prepared independently of inherited content-owned alias work. The first four
// cases need no GPU; the last two exercise actual device registration lifetime.
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsResource.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/NotSupportedException.hpp"
#include <memory>
#include <vector>

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

TEST(GraphicsResourceEventOrderTest, StandaloneMetadataRemainsAccessibleAfterDisposal)
{
    ResourceEventProbe resource;
    ResourceEventProbe tag;
    resource.setNameProperty("before");
    resource.setTagProperty(&tag);
    resource.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        EXPECT_TRUE(resource.getIsDisposedProperty());
        EXPECT_EQ(resource.getNameProperty(), "before");
        EXPECT_EQ(resource.getTagProperty(), &tag);
        resource.setNameProperty("during");
    };
    resource.Dispose();
    EXPECT_EQ(resource.getNameProperty(), "during");
    EXPECT_EQ(resource.ToString(), "during");
    EXPECT_EQ(resource.getTagProperty(), &tag);
    EXPECT_EQ(resource.getGraphicsDeviceProperty(), nullptr);
    resource.setNameProperty("after");
    resource.setTagProperty(nullptr);
    resource.Dispose();
    EXPECT_EQ(resource.getNameProperty(), "after");
    EXPECT_EQ(resource.getTagProperty(), nullptr);
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

TEST(GraphicsResourceFailureCleanupTest, ThrowingCreatedHandlerRetiresIncompleteRegistrationAndAllowsRetry)
{
    GraphicsDevice device;
    const auto baseline = device.GetTrackedResourceCount();
    ResourceEventProbe survivor(&device);
    GraphicsResource* incomplete = nullptr;
    bool refuse = true;
    int created = 0;
    device.ResourceCreated += [&](System::Object* sender, const auto& args)
    {
        EXPECT_EQ(sender, &device);
        ++created;
        if (refuse)
        {
            incomplete = dynamic_cast<GraphicsResource*>(args.getResourceProperty());
            EXPECT_NE(incomplete, nullptr);
            throw std::runtime_error("creation callback refusal");
        }
    };
    std::unique_ptr<ResourceEventProbe> result;
    try
    {
        result = std::make_unique<ResourceEventProbe>(&device);
        ADD_FAILURE() << "ResourceCreated refusal was swallowed";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_STREQ(error.what(), "creation callback refusal");
    }
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    // Keep a regressed implementation's stale pointer out of device teardown after reporting it.
    if (device.GetTrackedResourceCount() != baseline + 1)
        device.RemoveResourceReference(incomplete);
    EXPECT_FALSE(survivor.getIsDisposedProperty());
    refuse = false;
    ASSERT_NO_THROW(result = std::make_unique<ResourceEventProbe>(&device));
    EXPECT_EQ(created, 2);
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 2);
    result.reset();
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline + 1);
    survivor.Dispose();
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
    EXPECT_NO_THROW(device.Dispose());
}

#if defined(CNA_RENDERER_SOFTWARE)
TEST(GraphicsResourceFailureCleanupTest, RefusedSoftwareTextureFormatsPreserveLivePixelsAndRegistrations)
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    GraphicsDevice device;
    device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
    const auto baseline = device.GetTrackedResourceCount();
    {
        Texture2D survivor(device, 2, 2);
        const std::vector<Color> pixels(4, Color::CornflowerBlue);
        survivor.SetData(pixels.data(), 4);
        const auto liveCount = device.GetTrackedResourceCount();
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            EXPECT_THROW((TextureCube(device, 2, false, SurfaceFormat::NormalizedByte2)),
                         System::NotSupportedException);
            EXPECT_EQ(device.GetTrackedResourceCount(), liveCount);
            EXPECT_THROW((Texture3D(device, 2, 2, 2, false, SurfaceFormat::Dxt1)),
                         System::NotSupportedException);
            EXPECT_EQ(device.GetTrackedResourceCount(), liveCount);
            std::vector<Color> actual(4);
            survivor.GetData(actual.data(), 4);
            EXPECT_EQ(actual, pixels);
            {
                TextureCube retry(device, 2, false, SurfaceFormat::Color);
                EXPECT_EQ(device.GetTrackedResourceCount(), liveCount + 1);
                EXPECT_FALSE(retry.getIsDisposedProperty());
            }
            EXPECT_EQ(device.GetTrackedResourceCount(), liveCount);
        }
    }
    EXPECT_EQ(device.GetTrackedResourceCount(), baseline);
}
#endif
