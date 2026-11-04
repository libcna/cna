// SPDX-License-Identifier: MS-PL
// TQ-021: GraphicsDevice.ResourceCreated / ResourceDestroyed and their order against the
// resource's own Disposing event.
//
// Oracle: Microsoft XNA 4.0 Graphics assembly.
//  - DeviceResourceManager::AddTrackedObject IL_00dc-00e3 raises ResourceCreated once, at the end
//    of the public constructor (VertexBuffer::.ctor IL_0044-005e, Texture2D likewise).
//  - DeviceResourceManager::ReleaseAllReferences IL_0040-0057 raises ResourceDestroyed with the
//    resource's cached Name and Tag when the native object is released.
//  - VertexBuffer::Dispose(bool) and Texture2D::Dispose(bool) IL_0003-000d release the native
//    object inside a try block ('~VertexBuffer' -> ReleaseNativeObject -> ReleaseAllReferences)
//    and call GraphicsResource::Dispose(true) -- which raises Disposing -- from its finally block.
//    ResourceDestroyed therefore precedes Disposing.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/EventArgs.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::GraphicsResource;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    class EventTag final : public System::Object
    {
    public:
        [[nodiscard]] const std::string& GetTypeName() const override
        {
            static const std::string name = "TQ021.EventTag";
            return name;
        }
    };

    struct DestroyedRecord
    {
        System::Object* sender = nullptr;
        std::string name;
        System::Object* tag = nullptr;
    };

    class GraphicsResourceDeviceEventTest : public ::testing::Test
    {
    protected:
        // Declared before the device so they outlive it: device teardown may still dispose
        // resources and raise ResourceDestroyed into these.
        std::vector<std::string> order;
        std::vector<DestroyedRecord> destroyed;
        GraphicsDevice device;

        void SetUp() override
        {
            device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "renderer has no vertex buffers";
            device.ResourceDestroyed +=
                [this](System::Object* sender, const ResourceDestroyedEventArgs& args)
            {
                order.emplace_back("ResourceDestroyed");
                destroyed.push_back({sender, args.getNameProperty(), args.getTagProperty()});
            };
        }

        // Disposes @p resource twice and returns what the device and the resource reported.
        std::vector<std::string> DisposeAndRecord(GraphicsResource& resource)
        {
            order.clear();
            destroyed.clear();
            resource.Disposing += [this](System::Object*, const System::EventArgs&)
            {
                order.emplace_back("Disposing");
            };
            resource.Dispose();
            resource.Dispose();
            return order;
        }
    };
}

TEST_F(GraphicsResourceDeviceEventTest, VertexBufferDestroyedCarriesNameTagAndSenderOnce)
{
    EventTag tag;
    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                        BufferUsage::None);
    buffer.setNameProperty("tq021-vertices");
    buffer.setTagProperty(&tag);

    (void)DisposeAndRecord(buffer);

    // ReleaseAllReferences reads the cached Name/Tag at release time; a second Dispose finds the
    // resource already released and raises nothing.
    ASSERT_EQ(destroyed.size(), 1u);
    EXPECT_EQ(destroyed[0].sender, &device);
    EXPECT_EQ(destroyed[0].name, "tq021-vertices");
    EXPECT_EQ(destroyed[0].tag, &tag);
}

TEST_F(GraphicsResourceDeviceEventTest, Texture2DDestroyedCarriesTheLatestNameAndTag)
{
    EventTag first;
    EventTag second;
    Texture2D texture(device, 2, 2);
    texture.setNameProperty("before");
    texture.setTagProperty(&first);
    texture.setNameProperty("after");
    texture.setTagProperty(&second);

    (void)DisposeAndRecord(texture);

    ASSERT_EQ(destroyed.size(), 1u);
    EXPECT_EQ(destroyed[0].sender, &device);
    EXPECT_EQ(destroyed[0].name, "after");
    EXPECT_EQ(destroyed[0].tag, &second);
}

TEST_F(GraphicsResourceDeviceEventTest, ResourceDestroyedPrecedesDisposingForBuffersAndTextures)
{
    {
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                            BufferUsage::None);
        EXPECT_EQ(DisposeAndRecord(buffer),
                  (std::vector<std::string>{"ResourceDestroyed", "Disposing"}));
    }
    {
        Texture2D texture(device, 2, 2);
        EXPECT_EQ(DisposeAndRecord(texture),
                  (std::vector<std::string>{"ResourceDestroyed", "Disposing"}));
    }
}

// RenderTarget2D is one tracked Texture2D: XNA's RenderTarget2D constructor reaches
// AddTrackedObject once through the Texture2D creation path. A refactor that publishes creation
// from each constructor in the hierarchy must not announce the same object twice.
TEST_F(GraphicsResourceDeviceEventTest, RenderTarget2DIsAnnouncedExactlyOnce)
{
    int created = 0;
    System::Object* announced = nullptr;
    device.ResourceCreated += [&](System::Object* sender, const ResourceCreatedEventArgs& args)
    {
        EXPECT_EQ(sender, &device);
        ++created;
        announced = args.getResourceProperty();
    };
    try
    {
        RenderTarget2D target(device, 4, 4);
        EXPECT_EQ(created, 1);
        EXPECT_EQ(dynamic_cast<GraphicsResource*>(announced),
                  static_cast<GraphicsResource*>(&target));
    }
    catch (const System::NotSupportedException&)
    {
        GTEST_SKIP() << "renderer has no RenderTarget2D";
    }
}
