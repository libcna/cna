// SPDX-License-Identifier: MS-PL
// ISSUE-011: retain CNA's independent wrappers while enforcing manager ownership.
#include <gtest/gtest.h>
#include <utility>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class TextureOwnershipManager final : public ContentManager
    {
    public:
        using ContentManager::Dispose;
        using ContentManager::ReadAsset;
        explicit TextureOwnershipManager(GraphicsDevice& device)
            : ContentManager(nullptr, "tests/assets/xnb/monogame/windows/uncompressed")
        {
            setGraphicsDevice(device);
        }
    };

    class ContentTextureOwnershipTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device;

        static void ExpectWhite(Texture2D& texture)
        {
            ASSERT_FALSE(texture.getIsDisposedProperty());
            Color pixel;
            ASSERT_NO_THROW(texture.GetData(&pixel, 1));
            EXPECT_EQ(pixel, Color::White);
        }

        static void ExpectDisposed(Texture2D& texture)
        {
            EXPECT_TRUE(texture.getIsDisposedProperty());
            Color pixel;
            EXPECT_THROW(texture.GetData(&pixel, 1), System::ObjectDisposedException);
        }
    };
}

TEST_F(ContentTextureOwnershipTest, UnloadDisposesBothCachedResultsAndReleasesTheirRenderer)
{
    TextureOwnershipManager manager(device);
    auto first = manager.Load<Texture2D>("white-1");
    auto second = manager.Load<Texture2D>("white-1");
    ExpectWhite(first);
    ExpectWhite(second);
    const auto renderer = first.GetRendererWeak();
    ASSERT_FALSE(renderer.expired());
    EXPECT_EQ(renderer.lock(), second.GetRendererWeak().lock());

    manager.Unload();
    ExpectDisposed(first);
    ExpectDisposed(second);
    EXPECT_TRUE(renderer.expired());

    auto reloaded = manager.Load<Texture2D>("white-1");
    ExpectWhite(reloaded);
    manager.Unload();
    ExpectDisposed(reloaded);
}

TEST_F(ContentTextureOwnershipTest, UnloadingOneManagerDoesNotDisposeAnotherManagersTexture)
{
    TextureOwnershipManager firstManager(device);
    TextureOwnershipManager secondManager(device);
    auto first = firstManager.Load<Texture2D>("white-1");
    auto second = secondManager.Load<Texture2D>("white-1");
    EXPECT_NE(first.GetRendererWeak().lock(), second.GetRendererWeak().lock());

    firstManager.Unload();
    ExpectDisposed(first);
    ExpectWhite(second);
    secondManager.Unload();
    ExpectDisposed(second);
}

TEST_F(ContentTextureOwnershipTest, WrapperCopyAndMovePreserveTheManagersScopeNotExplicitDisposal)
{
    TextureOwnershipManager manager(device);
    auto original = manager.Load<Texture2D>("white-1");
    Texture2D copied(original);
    Texture2D moved(std::move(copied));
    Texture2D assigned;
    assigned = moved;
    Texture2D moveAssigned;
    moveAssigned = std::move(assigned);

    // CNA's documented value-copy extension: manually disposing one wrapper does
    // not dispose the others. Manager unload is the distinct owning operation.
    original.Dispose();
    ExpectDisposed(original);
    ExpectWhite(moved);
    ExpectWhite(moveAssigned);
    manager.Unload();
    ExpectDisposed(moved);
    ExpectDisposed(moveAssigned);
}

TEST_F(ContentTextureOwnershipTest, NonManagedDisposeDropsOwnershipWithoutDisposingTheTexture)
{
    TextureOwnershipManager manager(device);
    auto texture = manager.Load<Texture2D>("white-1");
    manager.Dispose(false);
    ExpectWhite(texture);
    manager.Dispose();
    ExpectWhite(texture);
}

TEST_F(ContentTextureOwnershipTest, UncachedReadRetainsTheResourceAfterTheCallerWrapperDies)
{
    TextureOwnershipManager manager(device);
    std::weak_ptr<CNA::Internal::Renderers::ITextureRenderer> renderer;
    {
        auto texture = manager.ReadAsset<Texture2D>("white-1", {});
        ExpectWhite(texture);
        renderer = texture.GetRendererWeak();
    }
    EXPECT_FALSE(renderer.expired());
    manager.Unload();
    EXPECT_TRUE(renderer.expired());
}

TEST_F(ContentTextureOwnershipTest, ExplicitRecorderReceivesConcreteTextureAndOwnsItsDisposal)
{
    TextureOwnershipManager manager(device);
    std::vector<std::shared_ptr<System::IDisposable>> records;
    auto texture = manager.ReadAsset<Texture2D>("white-1", [&](auto record) { records.push_back(record); });
    ASSERT_EQ(records.size(), 1U);
    const auto recordedTexture = std::dynamic_pointer_cast<Texture2D>(records[0]);
    ASSERT_NE(recordedTexture, nullptr);
    ExpectWhite(*recordedTexture);
    const auto renderer = texture.GetRendererWeak();
    manager.Unload();
    ExpectWhite(texture);
    records[0]->Dispose();
    ExpectDisposed(texture);
    ExpectDisposed(*recordedTexture);
    EXPECT_TRUE(renderer.expired());
}

TEST_F(ContentTextureOwnershipTest, ConstReturnedWrapperIsInvalidatedWithoutResurrectionByCopy)
{
    TextureOwnershipManager manager(device);
    const auto texture = manager.Load<Texture2D>("white-1");
    manager.Unload();
    EXPECT_TRUE(texture.getIsDisposedProperty());
    Color pixel;
    EXPECT_THROW(texture.GetData(&pixel, 1), System::ObjectDisposedException);
    Texture2D copied(texture);
    ExpectDisposed(copied);
}

TEST_F(ContentTextureOwnershipTest, DisposalCallbackMayDestroyAnotherTextureWrapper)
{
    TextureOwnershipManager manager(device);
    auto texture = manager.Load<Texture2D>("white-1");
    const auto renderer = texture.GetRendererWeak();
    auto victim = std::make_unique<Texture2D>(texture);
    Texture2D callbackOwner(texture);
    callbackOwner.Disposing += [&](System::Object*, const System::EventArgs&) { victim.reset(); };
    manager.Unload();
    EXPECT_EQ(victim, nullptr);
    ExpectDisposed(texture);
    ExpectDisposed(callbackOwner);
    EXPECT_TRUE(renderer.expired());
}

TEST_F(ContentTextureOwnershipTest, IndependentCStyleWrapperPreservesPixelsAcrossUnload)
{
    TextureOwnershipManager manager(device);
    auto native = manager.Load<Texture2D>("white-1");
    auto independent = manager.Load<Texture2D>("white-1");
    CNA::Internal::ContentOwnershipAccess::Detach(independent);
    const auto renderer = native.GetRendererWeak();
    manager.Unload();
    ExpectDisposed(native);
    ExpectWhite(independent);
    EXPECT_FALSE(renderer.expired());
    independent.Dispose();
    EXPECT_TRUE(renderer.expired());
}
