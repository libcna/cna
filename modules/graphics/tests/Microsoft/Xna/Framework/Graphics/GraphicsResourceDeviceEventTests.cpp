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

#include <memory>
#include <string>
#include <vector>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectMaterial.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceCreatedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/EventArgs.hpp"
#include "System/NotSupportedException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

#include "RecordingSpriteBatchRenderer.hpp"

using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::EffectMaterial;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
using Microsoft::Xna::Framework::Graphics::GraphicsResource;
using Microsoft::Xna::Framework::Graphics::OcclusionQuery;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ResourceCreatedEventArgs;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::SpriteEffect;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    // Models a renderer-side child whose destruction needs its live renderer owner, as the
    // DirectX9 buffers do when unregistering from the owner's default-pool resource list.
    class OwnerCheckingSpriteBatchRenderer final
        : public CNA::Internal::Renderers::RecordingSpriteBatchRenderer
    {
    public:
        OwnerCheckingSpriteBatchRenderer(
            CNA::Internal::Renderers::IGraphicsRenderer& owner, int& released)
            : owner_(owner), released_(released) {}

        ~OwnerCheckingSpriteBatchRenderer() override
        {
            (void)owner_.SupportsCapability(CNA::GraphicsCapability::ThreeD);
            ++released_;
        }

    private:
        CNA::Internal::Renderers::IGraphicsRenderer& owner_;
        int& released_;
    };

    class DeviceBoundTestSpriteBatch final : public SpriteBatch
    {
    public:
        DeviceBoundTestSpriteBatch(GraphicsDevice& device, int& released)
            : SpriteBatch(std::make_unique<OwnerCheckingSpriteBatchRenderer>(
                  device.GetRenderer(), released))
        {
            BindSharedResourceIdentityToDevice(&device);
        }
    };

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

// Refresh SpriteBatch::.ctor IL_0021/0039-003b initializes GraphicsResource and assigns its
// parent without AddTrackedObject. Its internal Effect/buffers are tracked, not the batch.
TEST(GraphicsResourceSpriteBatchEventTest, BatchHasAParentButNoDeviceResourceEvents)
{
    std::vector<System::Object*> created;
    std::vector<std::string> destroyedNames;
    int disposing = 0;
    GraphicsDevice device;
    device.ResourceCreated += [&](System::Object*, const ResourceCreatedEventArgs& args)
    {
        created.push_back(args.getResourceProperty());
    };
    device.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs& args)
    {
        destroyedNames.push_back(args.getNameProperty());
    };
    SpriteBatch batch(device);
    batch.setNameProperty("hnd-tq-12-batch");
    EXPECT_EQ(batch.getGraphicsDeviceProperty(), &device);
    for (System::Object* resource : created)
        EXPECT_NE(resource, static_cast<System::Object*>(&batch));

    batch.Disposing += [&](System::Object* sender, const System::EventArgs& args)
    {
        EXPECT_EQ(sender, &batch);
        EXPECT_EQ(&args, &System::EventArgs::Empty);
        EXPECT_TRUE(batch.getIsDisposedProperty());
        ++disposing;
    };
    batch.Dispose();
    batch.Dispose();
    EXPECT_EQ(disposing, 1);
    for (const std::string& name : destroyedNames)
        EXPECT_NE(name, "hnd-tq-12-batch");

    // A tracked resource still announces creation and destruction on this same device.
    Texture2D texture(device, 1, 1);
    texture.setNameProperty("hnd-tq-12-texture");
    ASSERT_FALSE(created.empty());
    EXPECT_EQ(created.back(), static_cast<System::Object*>(&texture));
    texture.Dispose();
    ASSERT_FALSE(destroyedNames.empty());
    EXPECT_EQ(destroyedNames.back(), "hnd-tq-12-texture");
}

TEST(GraphicsResourceSpriteBatchEventTest, DeviceDisposalDoesNotDisposeTheUntrackedBatch)
{
    int disposing = 0;
    GraphicsDevice device;
    SpriteBatch batch(device);
    batch.Disposing += [&](System::Object*, const System::EventArgs&) { ++disposing; };
    // ReleaseAllDeviceResources IL_009e-00a7 visits tracked objects only. SpriteBatch's
    // managed flag changes through its own Dispose(bool) IL_0026-0028, not its parent's.
    device.Dispose();
    EXPECT_FALSE(batch.getIsDisposedProperty());
    EXPECT_EQ(batch.getGraphicsDeviceProperty(), &device);
    EXPECT_EQ(disposing, 0);
    batch.Dispose();
    EXPECT_TRUE(batch.getIsDisposedProperty());
    EXPECT_EQ(disposing, 1);
}

TEST(GraphicsResourceSpriteBatchEventTest, NativeStorageIsReleasedBeforeItsRendererOwner)
{
    int released = 0;
    int disposing = 0;
    auto device = std::make_unique<GraphicsDevice>();
    auto first = std::make_unique<DeviceBoundTestSpriteBatch>(*device, released);
    auto second = std::make_unique<DeviceBoundTestSpriteBatch>(*device, released);
    first->Disposing += [&](System::Object*, const System::EventArgs&) { ++disposing; };
    second->Disposing += [&](System::Object*, const System::EventArgs&) { ++disposing; };
    {
        DeviceBoundTestSpriteBatch expiredIdentity(*device, released);
    }
    EXPECT_EQ(released, 1);
    device->Dispose();
    // XNA's untracked batch survives, while ReleaseAllDeviceResources releases its tracked
    // internal effect/buffers. This is a native ownership invariant, independent of renderer.
    EXPECT_EQ(released, 3);
    EXPECT_EQ(disposing, 0);
    EXPECT_FALSE(first->getIsDisposedProperty());
    EXPECT_FALSE(second->getIsDisposedProperty());
    device.reset();
    first->Dispose();
    first->Dispose();
    first.reset();
    second.reset(); // ASan catches the owner call here if teardown omitted native release.
    EXPECT_EQ(released, 3);
    EXPECT_EQ(disposing, 1);
}

TEST(GraphicsResourceSpriteBatchEventTest, BatchDestructionAfterDeviceDisposalIsSafe)
{
    int disposing = 0;
    GraphicsDevice device;
    auto batch = std::make_unique<SpriteBatch>(device);
    batch->Disposing += [&](System::Object*, const System::EventArgs&) { ++disposing; };
    device.Dispose();
    EXPECT_FALSE(batch->getIsDisposedProperty());
    batch.reset();
    // XNA finalization does not raise the managed Disposing event.
    EXPECT_EQ(disposing, 0);
}

TEST(GraphicsResourceSpriteBatchEventTest, BatchesCanBeDisposedOrDestroyedAfterDeviceDestruction)
{
    int disposing = 0;
    auto device = std::make_unique<GraphicsDevice>();
    auto first = std::make_unique<SpriteBatch>(*device);
    auto second = std::make_unique<SpriteBatch>(*device);
    first->Disposing += [&](System::Object*, const System::EventArgs&) { ++disposing; };
    second->Disposing += [&](System::Object*, const System::EventArgs&) { ++disposing; };
    device.reset();
    EXPECT_FALSE(first->getIsDisposedProperty());
    EXPECT_FALSE(second->getIsDisposedProperty());
    EXPECT_EQ(disposing, 0);
    first->Dispose();
    first->Dispose();
    EXPECT_TRUE(first->getIsDisposedProperty());
    first.reset();
    second.reset();
    EXPECT_EQ(disposing, 1);
}

TEST(GraphicsResourceSpriteBatchEventTest, QueuedTextureStorageIsReleasedWithTheDevice)
{
    GraphicsDevice device;
    SpriteBatch batch(device);
    std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> nativeTexture;
    batch.Begin();
    {
        Texture2D texture(device, 1, 1);
        nativeTexture = texture.GetRendererWeak();
        batch.Draw(texture, 0.0f, 0.0f);
    }
    EXPECT_FALSE(nativeTexture.expired());
    device.Dispose();
    // XNA retains the queued managed reference, but ReleaseAllDeviceResources releases its
    // native texture. CNA's queue must likewise release native ownership before renderer death.
    EXPECT_TRUE(nativeTexture.expired());
    EXPECT_FALSE(batch.getIsDisposedProperty());
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
}

// Refresh Begin IL_0081-00b5 only reads/writes fields for Deferred. End IL_001c-001d
// calls SetRenderState, whose viewport read IL_00b8-00be reaches get_Viewport's
// Helpers::CheckDisposed (IL_0000-0012), even when no sprites were queued.
TEST(GraphicsResourceSpriteBatchEventTest, DeferredBeginOnDisposedDeviceFailsAtEnd)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1);
    SpriteBatch batch(device);
    device.Dispose();
    EXPECT_THROW(batch.End(), System::InvalidOperationException);
    EXPECT_THROW(batch.Draw(texture, 0.0f, 0.0f), System::InvalidOperationException);
    EXPECT_NO_THROW(batch.Begin());
    EXPECT_THROW(batch.Begin(), System::InvalidOperationException);
    // InternalDraw IL_01e9-0232 retains even a disposed texture without touching native storage.
    EXPECT_NO_THROW(batch.Draw(texture, 0.0f, 0.0f));
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
    EXPECT_THROW(batch.Begin(), System::InvalidOperationException);
    EXPECT_FALSE(batch.getIsDisposedProperty());
}

// Refresh Begin IL_0065-0066 applies state before publishing the pair IL_00b0.
TEST(GraphicsResourceSpriteBatchEventTest, ImmediateBeginOnDisposedDeviceDoesNotOpenPair)
{
    GraphicsDevice device;
    SpriteBatch batch(device);
    device.Dispose();
    EXPECT_THROW(batch.Begin(SpriteSortMode::Immediate, nullptr, nullptr, nullptr, nullptr), System::ObjectDisposedException);
    EXPECT_THROW(batch.End(), System::InvalidOperationException);
    EXPECT_NO_THROW(batch.Begin());
    EXPECT_THROW(batch.End(), System::ObjectDisposedException);
    EXPECT_FALSE(batch.getIsDisposedProperty());
}

// Refresh End IL_0024-0033/0047-0062 only decrements counters for an empty Immediate pair.
TEST(GraphicsResourceSpriteBatchEventTest, OpenImmediateBatchCanEndAfterDeviceDisposal)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate, nullptr, nullptr, nullptr, nullptr);
    device.Dispose();
    EXPECT_THROW(batch.Draw(texture, 0.0f, 0.0f), System::ObjectDisposedException);
    EXPECT_NO_THROW(batch.End());
    EXPECT_THROW(batch.End(), System::InvalidOperationException);
    EXPECT_FALSE(batch.getIsDisposedProperty());
}

TEST(GraphicsResourceSpriteBatchEventTest, OperationsAfterParentDestructionNeverReadTheDeadDevice)
{
    auto device = std::make_unique<GraphicsDevice>();
    auto deferred = std::make_unique<SpriteBatch>(*device);
    auto closed = std::make_unique<SpriteBatch>(*device);
    deferred->Begin();
    device.reset();
    // C++ cannot keep a destroyed parent object alive as a managed XNA reference would. Refuse
    // at the same disposed-device boundary before touching its freed counters/state.
    EXPECT_THROW(closed->Begin(), System::ObjectDisposedException);
    EXPECT_THROW(closed->Begin(SpriteSortMode::Immediate, nullptr, nullptr, nullptr, nullptr), System::ObjectDisposedException);
    EXPECT_THROW(deferred->End(), System::ObjectDisposedException);
    EXPECT_THROW(deferred->Begin(), System::InvalidOperationException);
    Texture2D texture;
    EXPECT_THROW(deferred->Draw(texture, 0.0f, 0.0f), System::ObjectDisposedException);
}

TEST(GraphicsResourceSpriteBatchEventTest, OpenImmediateBatchCanEndAfterParentDestruction)
{
    auto device = std::make_unique<GraphicsDevice>();
    auto batch = std::make_unique<SpriteBatch>(*device);
    batch->Begin(SpriteSortMode::Immediate, nullptr, nullptr, nullptr, nullptr);
    device.reset();
    EXPECT_NO_THROW(batch->End());
    EXPECT_THROW(batch->End(), System::InvalidOperationException);
    EXPECT_THROW(batch->Begin(), System::ObjectDisposedException);
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

// TQ-035 (owner decision 2026-11-04): XNA never raises Disposing for an Effect or an
// OcclusionQuery. '!Effect' IL_0008-000a and '~OcclusionQuery' IL_0008-000a set
// GraphicsResource.isDisposed before Effect/OcclusionQuery::Dispose(bool) call
// GraphicsResource::Dispose(true) from their finally blocks (IL_000b-000d); '~GraphicsResource'
// IL_0000-0006 then returns before it raises Disposing (IL_000e-0021). Their native release still
// reaches DeviceResourceManager::ReleaseAllReferences (Effect::ReleaseNativeObject IL_0018-002d,
// OcclusionQuery::ReleaseNativeObject IL_000a-001f), so ResourceDestroyed is raised exactly once.
// The stock effects and EffectMaterial override no Dispose(bool), so they inherit Effect's.
TEST_F(GraphicsResourceDeviceEventTest, EffectsAndOcclusionQueriesNeverRaiseDisposing)
{
    const std::vector<std::string> destroyedOnly{"ResourceDestroyed"};
    {
        BasicEffect effect(device);
        EXPECT_EQ(DisposeAndRecord(effect), destroyedOnly) << "BasicEffect";
        EXPECT_TRUE(effect.getIsDisposedProperty());
    }
    {
        SpriteEffect effect(device);
        EXPECT_EQ(DisposeAndRecord(effect), destroyedOnly) << "SpriteEffect";
        EXPECT_TRUE(effect.getIsDisposedProperty());
    }
    {
        BasicEffect source(device);
        EffectMaterial material(source);
        EXPECT_EQ(DisposeAndRecord(material), destroyedOnly) << "EffectMaterial";
        EXPECT_TRUE(material.getIsDisposedProperty());
        EXPECT_FALSE(source.getIsDisposedProperty());
    }
    if (device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
    {
        OcclusionQuery query(device);
        EXPECT_EQ(DisposeAndRecord(query), destroyedOnly) << "OcclusionQuery";
        EXPECT_TRUE(query.getIsDisposedProperty());
    }

    // Control, in the same fixture: an ordinary resource still raises both, in XNA's order.
    Texture2D texture(device, 1, 1);
    EXPECT_EQ(DisposeAndRecord(texture),
              (std::vector<std::string>{"ResourceDestroyed", "Disposing"}));
}
