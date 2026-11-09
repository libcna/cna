// SPDX-License-Identifier: MS-PL
//
// HND-TQ-47 R2 / HND-TQ-49 D3: what a device's disposal does to the resources it created.
//
// Oracle: Microsoft XNA 4.0 Refresh, Microsoft.Xna.Framework.Graphics.dll 4.0.30901.0
// (SHA-256 560080fc39021c61..., ~/deps/xna-il-cache/Microsoft.Xna.Framework.Graphics.il).
//  - GraphicsDevice::'!GraphicsDevice' IL_000b-000d publishes isDisposed, then IL_0012-0020 calls
//    DeviceResourceManager::ReleaseAllDeviceResources, which (IL_009e-00a7) calls every tracked
//    child's IGraphicsResource::ReleaseNativeObject(false). '~GraphicsDevice' IL_0008-0021 raises
//    the device's Disposing only after that; the finalizer path Dispose(false) never raises it.
//  - ReleaseNativeObject(false) releases the native object only: Texture2D IL_0000-0048 releases
//    the state tracker and IDirect3DTexture9 and nulls pComPtr; ReleaseAllReferences(handle,false)
//    IL_0040-0041 skips FireDestroyedEvent. The child's managed isDisposed stays false and its own
//    Disposing ('~GraphicsResource' IL_0008-0021) is not raised.
//  - A later use reaches Helpers.CheckDisposed(this, pComPtr) (Microsoft.Xna.Framework.dll
//    IL_0000-001d), which throws ObjectDisposedException(GetType().Name) for the null pointer:
//    Texture2D/VertexBuffer/IndexBuffer::CopyData, EffectPass::Apply IL_0000-001c,
//    Effect::set_CurrentTechnique, Effect::.ctor(Effect), EffectParameter::SetValue(Texture)
//    IL_0005-0015 and TextureCollection::set_Item IL_0025-0039. OcclusionQuery has no such check:
//    Begin/End call through the cleared pointer (NullReferenceException, as after Dispose, TQ-035).
//  - A later Dispose: '!Texture2D' finds pComPtr null, so ReleaseAllReferences (and
//    ResourceDestroyed) is skipped; GraphicsResource::Dispose(true) then publishes IsDisposed and
//    raises Disposing once. Effect and OcclusionQuery still never raise it (TQ-035).
// FNA's GraphicsDevice.Dispose(bool) instead disposes every child; XNA wins.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ResourceDestroyedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/EventArgs.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NullReferenceException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::BufferUsage;
using Microsoft::Xna::Framework::Graphics::DynamicVertexBuffer;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectPass;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::GraphicsResource;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::IndexElementSize;
using Microsoft::Xna::Framework::Graphics::OcclusionQuery;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ResourceDestroyedEventArgs;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

namespace
{
    template <typename Use>
    void ExpectReleased(Use&& use, const char* objectName)
    {
        try
        {
            use();
            ADD_FAILURE() << "expected System::ObjectDisposedException for " << objectName;
        }
        catch (const System::ObjectDisposedException& exception)
        {
            EXPECT_EQ(typeid(exception), typeid(System::ObjectDisposedException));
            if (objectName != nullptr)
                EXPECT_EQ(exception.getObjectNameProperty(), objectName);
        }
        catch (const std::exception& exception)
        {
            ADD_FAILURE() << "unexpected " << typeid(exception).name() << ": " << exception.what();
        }
    }

    struct DisposingCounter
    {
        int count = 0;
        void Watch(GraphicsResource& resource)
        {
            resource.Disposing += [this](System::Object*, const System::EventArgs&) { ++count; };
        }
    };

    [[nodiscard]] bool Has3D(const GraphicsDevice& device)
    {
        return device.SupportsCapability(CNA::GraphicsCapability::ThreeD);
    }

    [[nodiscard]] EffectPass* FirstPass(Effect& effect)
    {
        return effect.getCurrentTechniqueProperty()->getPassesProperty()[0];
    }
}

TEST(DeviceTeardownRefreshTest, ChildrenStayUndisposedAndRaiseNoEvents)
{
    GraphicsDevice device;
    int destroyed = 0;
    device.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyed; };
    DisposingCounter disposing;

    Texture2D texture(device, 2, 2);
    disposing.Watch(texture);
    std::unique_ptr<VertexBuffer> vertices;
    std::unique_ptr<DynamicVertexBuffer> dynamicVertices;
    std::unique_ptr<IndexBuffer> indices;
    std::unique_ptr<BasicEffect> effect;
    if (Has3D(device))
    {
        vertices = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        dynamicVertices = std::make_unique<DynamicVertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        indices = std::make_unique<IndexBuffer>(
            device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
        effect = std::make_unique<BasicEffect>(device);
        disposing.Watch(*vertices);
        disposing.Watch(*dynamicVertices);
        disposing.Watch(*indices);
        disposing.Watch(*effect);
    }

    device.Dispose();

    EXPECT_TRUE(device.getIsDisposedProperty());
    EXPECT_FALSE(texture.getIsDisposedProperty());
    EXPECT_EQ(texture.getGraphicsDeviceProperty(), &device);
    if (vertices)
    {
        EXPECT_FALSE(vertices->getIsDisposedProperty());
        EXPECT_FALSE(dynamicVertices->getIsDisposedProperty());
        EXPECT_FALSE(indices->getIsDisposedProperty());
        EXPECT_FALSE(effect->getIsDisposedProperty());
        EXPECT_EQ(vertices->getGraphicsDeviceProperty(), &device);
        EXPECT_EQ(effect->getGraphicsDeviceProperty(), &device);
    }
    EXPECT_EQ(disposing.count, 0) << "ReleaseNativeObject(false) raises no child Disposing";
    EXPECT_EQ(destroyed, 0) << "ReleaseAllReferences(handle, false) skips FireDestroyedEvent";
}

TEST(DeviceTeardownRefreshTest, NativeObjectsAreReleasedBeforeTheDeviceDisposingEvent)
{
    GraphicsDevice device;
    Texture2D texture(device, 2, 2);
    const std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> nativeTexture =
        texture.GetRendererWeak();
    ASSERT_FALSE(nativeTexture.expired());
    std::unique_ptr<VertexBuffer> vertices;
    std::unique_ptr<IndexBuffer> indices;
    if (Has3D(device))
    {
        vertices = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        indices = std::make_unique<IndexBuffer>(
            device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
        ASSERT_TRUE(vertices->HasRenderer());
        ASSERT_TRUE(indices->HasRenderer());
    }

    int notifications = 0;
    bool releasedInsideHandler = false;
    bool undisposedInsideHandler = false;
    device.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        ++notifications;
        releasedInsideHandler = nativeTexture.expired() && !texture.HasRenderer() &&
            (!vertices || (!vertices->HasRenderer() && !indices->HasRenderer()));
        undisposedInsideHandler = !texture.getIsDisposedProperty();
    };

    device.Dispose();

    EXPECT_EQ(notifications, 1);
    EXPECT_TRUE(releasedInsideHandler)
        << "'~GraphicsDevice' raises Disposing after ReleaseAllDeviceResources";
    EXPECT_TRUE(undisposedInsideHandler);
    EXPECT_TRUE(nativeTexture.expired());
    EXPECT_FALSE(texture.HasRenderer());
    EXPECT_FALSE(texture.getIsDisposedProperty());
}

TEST(DeviceTeardownRefreshTest, LaterTransfersRefuseLikeAReleasedNativePointer)
{
    GraphicsDevice device;
    std::array<Color, 4> pixels{Color(1, 2, 3, 4), Color(5, 6, 7, 8), Color(9, 10, 11, 12),
                                Color(13, 14, 15, 16)};
    Texture2D texture(device, 2, 2);
    texture.SetData(pixels.data(), 4);
    texture.GetData(pixels.data(), 4);

    std::array<VertexPositionColor, 3> colored{};
    std::array<std::uint16_t, 3> indexData{0, 1, 2};
    std::unique_ptr<VertexBuffer> vertices;
    std::unique_ptr<DynamicVertexBuffer> dynamicVertices;
    std::unique_ptr<IndexBuffer> indices;
    if (Has3D(device))
    {
        vertices = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        dynamicVertices = std::make_unique<DynamicVertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        indices = std::make_unique<IndexBuffer>(
            device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
        vertices->SetData(colored.data(), 3);
        dynamicVertices->SetData(colored.data(), 3);
        indices->SetData(indexData.data(), 3);
    }

    device.Dispose();

    ExpectReleased([&] { texture.SetData(pixels.data(), 4); }, "Texture2D");
    ExpectReleased([&] { texture.GetData(pixels.data(), 4); }, "Texture2D");
    ExpectReleased([&] { texture.GetData(0, nullptr, pixels.data(), 0, 4); }, "Texture2D");
    if (vertices)
    {
        ExpectReleased([&] { vertices->SetData(colored.data(), 3); }, "VertexBuffer");
        ExpectReleased([&] { vertices->GetData(colored.data(), 0, 3); }, "VertexBuffer");
        ExpectReleased([&] { dynamicVertices->SetData(colored.data(), 3); }, "DynamicVertexBuffer");
        ExpectReleased([&] { indices->SetData(indexData.data(), 3); }, "IndexBuffer");
        ExpectReleased([&] { indices->GetData(indexData.data(), 3); }, "IndexBuffer");
    }
    // Releasing is not disposing: none of the refusals published IsDisposed.
    EXPECT_FALSE(texture.getIsDisposedProperty());
    if (vertices)
        EXPECT_FALSE(vertices->getIsDisposedProperty());
}

TEST(DeviceTeardownRefreshTest, ARenderTargetIsReleasedAndRefusedUnderItsOwnName)
{
    GraphicsDevice device;
    RenderTarget2D target(device, 2, 2);
    std::array<Color, 4> pixels{};
    DisposingCounter disposing;
    disposing.Watch(target);

    device.Dispose();

    EXPECT_FALSE(target.getIsDisposedProperty());
    EXPECT_FALSE(target.HasRenderer());
    EXPECT_EQ(disposing.count, 0);
    ExpectReleased([&] { target.GetData(pixels.data(), 4); }, "RenderTarget2D");
    target.Dispose();
    EXPECT_TRUE(target.getIsDisposedProperty());
    EXPECT_EQ(disposing.count, 1);
}

TEST(DeviceTeardownRefreshTest, AReleasedEffectRefusesApplyCloneAndTechniqueChanges)
{
    GraphicsDevice device;
    if (!Has3D(device))
        GTEST_SKIP() << "renderer has no stock effects";
    BasicEffect effect(device);
    EffectPass* const pass = FirstPass(effect);
    ASSERT_NE(pass, nullptr);
    auto* const technique = effect.getCurrentTechniqueProperty();

    device.Dispose();

    EXPECT_FALSE(effect.getIsDisposedProperty());
    ExpectReleased([&] { pass->Apply(); }, nullptr);
    ExpectReleased([&] { effect.setCurrentTechniqueProperty(technique); }, nullptr);
    ExpectReleased([&] { std::unique_ptr<Effect> clone(effect.Clone()); }, nullptr);
    // Reflection stays: Effect::ReleaseNativeObject releases only the ID3DXEffect.
    EXPECT_EQ(effect.getCurrentTechniqueProperty(), technique);
    EXPECT_NE(effect.getParametersProperty()["World"], nullptr);
    EXPECT_FALSE(effect.getIsDisposedProperty());
}

TEST(DeviceTeardownRefreshTest, ALiveDeviceRefusesAReleasedTextureLikeADisposedOne)
{
    GraphicsDevice released;
    Texture2D texture(released, 1, 1);
    released.Dispose();
    ASSERT_FALSE(texture.getIsDisposedProperty());

    GraphicsDevice live;
    // TextureCollection::set_Item IL_0025-0039: CheckDisposed(texture, texture.GetComPtr()).
    // EffectParameter::SetValue(Texture) is pinned by EffectReflectedApiTest's compiled table.
    ExpectReleased([&] { live.getTexturesProperty()(0, &texture); }, "Texture2D");
}

TEST(DeviceTeardownRefreshTest, AReleasedQueryBehavesAsAfterItsOwnDisposal)
{
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
        GTEST_SKIP() << "renderer has no occlusion queries";
    device.SetGraphicsProfileEXT(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
    OcclusionQuery query(device);
    ASSERT_TRUE(query.HasRenderer());

    device.Dispose();

    // OcclusionQuery::ReleaseNativeObject IL_0024-0032 clears _isAvailable and pComPtr only.
    EXPECT_FALSE(query.getIsDisposedProperty());
    EXPECT_FALSE(query.HasRenderer());
    EXPECT_FALSE(query.getIsCompleteProperty());
    EXPECT_THROW((void)query.getPixelCountProperty(), System::InvalidOperationException);
    EXPECT_THROW(query.Begin(), System::NullReferenceException);
    query.Dispose();
    EXPECT_TRUE(query.getIsDisposedProperty());
}

TEST(DeviceTeardownRefreshTest, ALaterDisposeRaisesOnlyTheResourcesOwnDisposingOnce)
{
    GraphicsDevice device;
    int destroyed = 0;
    device.ResourceDestroyed += [&](System::Object*, const ResourceDestroyedEventArgs&) { ++destroyed; };
    Texture2D texture(device, 1, 1);
    DisposingCounter textureDisposing;
    textureDisposing.Watch(texture);
    std::unique_ptr<VertexBuffer> vertices;
    std::unique_ptr<BasicEffect> effect;
    DisposingCounter otherDisposing;
    if (Has3D(device))
    {
        vertices = std::make_unique<VertexBuffer>(
            device, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        effect = std::make_unique<BasicEffect>(device);
        otherDisposing.Watch(*vertices);
        otherDisposing.Watch(*effect);
    }

    device.Dispose();
    texture.Dispose();
    texture.Dispose();
    EXPECT_TRUE(texture.getIsDisposedProperty());
    EXPECT_EQ(textureDisposing.count, 1);
    if (vertices)
    {
        vertices->Dispose();
        effect->Dispose();
        EXPECT_TRUE(vertices->getIsDisposedProperty());
        EXPECT_TRUE(effect->getIsDisposedProperty());
        // The buffer raises its own; Effect never does (Effect::Dispose(bool) IL_0003-000d).
        EXPECT_EQ(otherDisposing.count, 1);
    }
    EXPECT_EQ(destroyed, 0) << "a released native object is not destroyed a second time";
}

TEST(DeviceTeardownRefreshTest, ReleasedChildrenMayOutliveTheDeviceObject)
{
    auto device = std::make_unique<GraphicsDevice>();
    auto texture = std::make_unique<Texture2D>(*device, 1, 1);
    auto target = std::make_unique<RenderTarget2D>(*device, 1, 1);
    auto destroyedWithoutDispose = std::make_unique<Texture2D>(*device, 1, 1);
    std::unique_ptr<BasicEffect> effect;
    std::unique_ptr<IndexBuffer> indices;
    if (Has3D(*device))
    {
        effect = std::make_unique<BasicEffect>(*device);
        indices = std::make_unique<IndexBuffer>(
            *device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
    }
    device->getTexturesProperty()(0, texture.get());

    device->Dispose();
    device.reset();

    // Every path that used to find these children disposed by the cascade now runs with the
    // device object gone; none may touch it (sanitizer witness).
    EXPECT_FALSE(texture->getIsDisposedProperty());
    ExpectReleased([&] { std::array<Color, 1> pixel{}; texture->GetData(pixel.data(), 1); },
                   "Texture2D");
    texture->Dispose();
    target->Dispose();
    EXPECT_TRUE(texture->getIsDisposedProperty());
    EXPECT_TRUE(target->getIsDisposedProperty());
    if (effect)
    {
        effect->Dispose();
        EXPECT_TRUE(effect->getIsDisposedProperty());
    }
    destroyedWithoutDispose.reset();
    indices.reset();
    texture.reset();
    target.reset();
    effect.reset();
}

TEST(DeviceTeardownRefreshTest, DestroyingTheDeviceReleasesWithoutDisposingToo)
{
    auto device = std::make_unique<GraphicsDevice>();
    Texture2D texture(*device, 1, 1);
    const auto nativeTexture = texture.GetRendererWeak();
    DisposingCounter disposing;
    disposing.Watch(texture);

    device.reset();

    EXPECT_TRUE(nativeTexture.expired());
    EXPECT_FALSE(texture.getIsDisposedProperty());
    EXPECT_EQ(disposing.count, 0);
}

TEST(DeviceTeardownRefreshTest, ACopyOfAReleasedTextureIsReleasedToo)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1);
    device.Dispose();

    const Texture2D copy(texture);
    EXPECT_FALSE(copy.getIsDisposedProperty());
    EXPECT_FALSE(copy.HasRenderer());
    std::array<Color, 1> pixel{};
    ExpectReleased([&] { copy.GetData(pixel.data(), 1); }, "Texture2D");
}

TEST(DeviceTeardownRefreshTest, AThrowingDisposingHandlerStillLeavesTheDeviceTornDown)
{
    GraphicsDevice device;
    Texture2D texture(device, 1, 1);
    const auto nativeTexture = texture.GetRendererWeak();
    device.Disposing += [](System::Object*, const System::EventArgs&)
    {
        throw std::runtime_error("handler failure");
    };

    EXPECT_THROW(device.Dispose(), std::runtime_error);
    EXPECT_TRUE(device.getIsDisposedProperty());
    EXPECT_TRUE(nativeTexture.expired());
    EXPECT_FALSE(texture.getIsDisposedProperty());
    // The renderer went although the handler threw.
    EXPECT_THROW((void)device.GetRenderer(), System::InvalidOperationException);
    // The device stays disposed; a second call has nothing left to do and raises nothing.
    EXPECT_NO_THROW(device.Dispose());
}

TEST(DeviceTeardownRefreshTest, AResourceCreatedByADisposingHandlerIsReleasedToo)
{
    // CNA destroys the renderer after the event, so a handler can still create a resource; it
    // must not keep a renderer object past the renderer's destruction.
    GraphicsDevice device;
    std::unique_ptr<Texture2D> late;
    device.Disposing += [&](System::Object*, const System::EventArgs&)
    {
        late = std::make_unique<Texture2D>(device, 1, 1);
    };
    device.Dispose();
    ASSERT_NE(late, nullptr);
    EXPECT_FALSE(late->HasRenderer());
    EXPECT_FALSE(late->getIsDisposedProperty());
    late.reset();
}
