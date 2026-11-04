// SPDX-License-Identifier: MS-PL
// TQ-021: GraphicsDevice.ResourceCreated announces a complete resource.
//
// Oracle: Microsoft XNA 4.0 Graphics assembly. Every tracked resource constructor validates its
// arguments and creates its native object first, and only then registers itself through
// DeviceResourceManager::AddTrackedObject, whose IL_00dc-00e3 raises ResourceCreated:
//  - VertexBuffer::.ctor(GraphicsDevice, VertexDeclaration, int, BufferUsage): vertexCount check
//    IL_0019-002c, CreateBuffer IL_003f, AddTrackedObject IL_005e;
//  - OcclusionQuery::.ctor(GraphicsDevice): profile check IL_0019-0040, CreateQuery IL_006c,
//    AddTrackedObject IL_009e.
// A refused construction therefore raises neither ResourceCreated nor ResourceDestroyed (the
// fault handlers call GraphicsResource::Dispose(true) on an untracked object), and a handler sees
// the finished, most-derived object. FNA raises the event from its GraphicsDevice setter at the
// start of construction; XNA wins (docs/xna-fna-behavior-decisions.md).
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::OcclusionQuery;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    class ResourceCreatedTimingTest : public ::testing::Test
    {
    protected:
        int created = 0;
        int destroyed = 0;
        bool sawVertexBuffer = false;
        int seenVertexCount = -1;
        GraphicsDevice device;

        void SetUp() override
        {
            device.SetGraphicsProfileEXT(GraphicsProfile::HiDef);
            if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
                GTEST_SKIP() << "renderer has no vertex buffers";
            device.ResourceCreated +=
                [this](System::Object*, const ResourceCreatedEventArgs& args)
            {
                ++created;
                // The static type is System::Object, a base of the class under construction, so
                // this cast is well defined even when the event is raised from a base constructor.
                const auto* buffer = dynamic_cast<const VertexBuffer*>(args.getResourceProperty());
                sawVertexBuffer = buffer != nullptr;
                seenVertexCount = buffer != nullptr ? buffer->getVertexCountProperty() : -1;
            };
            device.ResourceDestroyed +=
                [this](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyed; };
        }
    };
}

TEST_F(ResourceCreatedTimingTest, HandlerSeesTheFinishedVertexBuffer)
{
    VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 3,
                        BufferUsage::None);
    EXPECT_EQ(created, 1);
    EXPECT_TRUE(sawVertexBuffer);
    EXPECT_EQ(seenVertexCount, 3);
}

TEST_F(ResourceCreatedTimingTest, RefusedVertexBufferIsNeverAnnounced)
{
    EXPECT_THROW((VertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(), 0,
                               BufferUsage::None)),
                 System::ArgumentOutOfRangeException);
    EXPECT_EQ(created, 0);
    EXPECT_EQ(destroyed, 0);
}

TEST_F(ResourceCreatedTimingTest, RefusedReachOcclusionQueryIsNeverAnnounced)
{
    device.SetGraphicsProfileEXT(GraphicsProfile::Reach);
    EXPECT_THROW((OcclusionQuery(device)), System::NotSupportedException);
    EXPECT_EQ(created, 0);
    EXPECT_EQ(destroyed, 0);
}
