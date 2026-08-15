// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Metal/MetalCommandFailure.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

using CNA::Internal::Renderers::ITextureRenderer;
using CNA::Internal::Renderers::ITextureCubeRenderer;
using namespace CNA::Internal::Renderers::Metal;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTargetCube;
using Microsoft::Xna::Framework::Graphics::RenderTargetUsage;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    struct CommonCleanupOwner
    {
        std::shared_ptr<MetalCommandFailureLatch> latch;
        int commonCleanupCalls=0;
        int abandonCalls=0;

        void ThrowPendingCommandFailure()
        {
            ++commonCleanupCalls;
            if(latch->ConsumeFailure()) {
                ++abandonCalls;
                throw std::runtime_error("portable common Metal command cleanup");
            }
        }
    };

    class HealthBoundTextureRenderer final : public ITextureRenderer
    {
    public:
        HealthBoundTextureRenderer(std::shared_ptr<MetalResourceHealth> health,
                                  std::weak_ptr<CommonCleanupOwner> owner,
                                  std::shared_ptr<int> nativeTouches)
            : health_(std::move(health)), owner_(std::move(owner)),
              nativeTouches_(std::move(nativeTouches))
        {
        }

        int GetWidth() const override { return 1; }
        int GetHeight() const override { return 1; }

        void UpdatePixels(const std::uint8_t* /*rgba*/, int /*stride*/) override
        {
            CheckOwner();
            ++*nativeTouches_;
        }

        bool GetData(int /*level*/, int /*x*/, int /*y*/, int /*w*/, int /*h*/, void* /*data*/,
                     int /*dataLength*/) const override
        {
            CheckOwner();
            ++*nativeTouches_;
            return true;
        }

    private:
        void CheckOwner() const
        {
            auto owner=RequireMetalResourceOwner(health_,owner_);
            owner->ThrowPendingCommandFailure();
        }

        std::shared_ptr<MetalResourceHealth> health_;
        std::weak_ptr<CommonCleanupOwner> owner_;
        std::shared_ptr<int> nativeTouches_;
    };
}

TEST(MetalResourceHealth, ObservationDoesNotStealLiveOwnerCleanup)
{
    auto latch=std::make_shared<MetalCommandFailureLatch>();
    auto health=std::make_shared<MetalResourceHealth>(latch);

    EXPECT_EQ(health->ObserveState(),MetalResourceHealthState::Active);
    EXPECT_NO_THROW(health->RequireUsable());

    latch->RecordFailure();
    EXPECT_EQ(health->ObserveState(),MetalResourceHealthState::PendingCommandFailure);
    EXPECT_THROW(health->RequireUsable(),std::runtime_error);
    EXPECT_TRUE(latch->HasFailure())
        << "resource observation must leave cleanup ownership with the live renderer";
}

TEST(MetalResourceHealth, InactiveOwnerWinsOverLaterCommandCompletion)
{
    auto latch=std::make_shared<MetalCommandFailureLatch>();
    auto health=std::make_shared<MetalResourceHealth>(latch);

    health->MarkOwnerInactive();
    latch->RecordFailure();

    EXPECT_EQ(health->ObserveState(),MetalResourceHealthState::OwnerInactive);
    EXPECT_THROW(health->RequireUsable(),std::runtime_error);
    EXPECT_TRUE(latch->HasFailure())
        << "an inactive resource must not consume an asynchronously completed command failure";
}

TEST(MetalResourceHealth, WeakOwnerLockRetainsOnlyOneOperation)
{
    auto latch=std::make_shared<MetalCommandFailureLatch>();
    auto health=std::make_shared<MetalResourceHealth>(latch);
    auto owner=std::make_shared<int>(42);
    std::weak_ptr<int> weakOwner=owner;

    auto retained=RequireMetalResourceOwner(health,weakOwner);
    owner.reset();
    EXPECT_EQ(*retained,42);
    EXPECT_FALSE(weakOwner.expired());

    retained.reset();
    EXPECT_TRUE(weakOwner.expired());
    EXPECT_THROW((void)RequireMetalResourceOwner(health,weakOwner),std::runtime_error);
}

TEST(MetalResourceHealth, LiveOwnerResourceFailureUsesCommonCleanupThenRetrySucceeds)
{
    auto latch=std::make_shared<MetalCommandFailureLatch>();
    auto health=std::make_shared<MetalResourceHealth>(latch);
    auto owner=std::make_shared<CommonCleanupOwner>();
    owner->latch=latch;
    auto nativeTouches=std::make_shared<int>(0);
    auto renderer=std::make_shared<HealthBoundTextureRenderer>(health,owner,nativeTouches);

    latch->RecordFailure();
    EXPECT_THROW(renderer->UpdatePixels(nullptr,4),std::runtime_error);
    EXPECT_FALSE(latch->HasFailure());
    EXPECT_EQ(owner->abandonCalls,1);
    EXPECT_EQ(*nativeTouches,0);

    EXPECT_NO_THROW(renderer->UpdatePixels(nullptr,4));
    EXPECT_EQ(owner->commonCleanupCalls,2);
    EXPECT_EQ(owner->abandonCalls,1);
    EXPECT_EQ(*nativeTouches,1);
}

TEST(MetalResourceHealth, OwnerlessRenderTarget2DAndCubeCleanupSkipsOwnerAccess)
{
    auto latch=std::make_shared<MetalCommandFailureLatch>();
    auto health=std::make_shared<MetalResourceHealth>(latch);
    auto owner=std::make_shared<CommonCleanupOwner>();
    owner->latch=latch;
    std::weak_ptr<CommonCleanupOwner> rt2dOwner=owner;
    std::weak_ptr<CommonCleanupOwner> rtCubeOwner=owner;

    health->MarkOwnerInactive();
    owner.reset();

    EXPECT_FALSE(LockMetalResourceOwnerForCleanup(rt2dOwner));
    EXPECT_FALSE(LockMetalResourceOwnerForCleanup(rtCubeOwner));
    EXPECT_THROW((void)RequireMetalResourceOwner(health,rt2dOwner),std::runtime_error);
    EXPECT_THROW((void)RequireMetalResourceOwner(health,rtCubeOwner),std::runtime_error);
}

TEST(MetalResourceHealth, CopiedMovedAndWeakLockedTextureRendererRejectAfterDeviceDeath)
{
    auto latch=std::make_shared<MetalCommandFailureLatch>();
    auto health=std::make_shared<MetalResourceHealth>(latch);
    std::weak_ptr<MetalResourceHealth> weakHealth=health;
    auto nativeTouches=std::make_shared<int>(0);
    auto owner=std::make_shared<CommonCleanupOwner>();
    owner->latch=latch;
    auto renderer=std::make_shared<HealthBoundTextureRenderer>(health,owner,nativeTouches);

    Texture2D original=Texture2D::CreateWithRendererForTests(1,1,renderer);
    Texture2D copied=original;
    Texture2D moved=std::move(copied);
    auto escapedRenderer=moved.GetRendererWeak().lock();
    ASSERT_EQ(escapedRenderer,original.GetRendererWeak().lock());

    health->MarkOwnerInactive();
    owner.reset();
    health.reset();
    ASSERT_FALSE(weakHealth.expired())
        << "retained texture renderers must keep only the inactive health sentinel alive";

    const std::array<std::uint8_t,4> pixel{1,2,3,4};
    EXPECT_THROW(original.SetDataRGBA(pixel.data(),1),std::runtime_error);
    EXPECT_THROW(moved.SetDataRGBA(pixel.data(),1),std::runtime_error);
    EXPECT_THROW(escapedRenderer->UpdatePixels(pixel.data(),4),std::runtime_error);
    std::array<std::uint8_t,4> readback{};
    EXPECT_THROW((void)escapedRenderer->GetData(0,0,0,1,1,readback.data(),4),
                 std::runtime_error);
    EXPECT_EQ(*nativeTouches,0)
        << "post-device calls must reject before touching any retained native resource";
}

TEST(MetalResourceHealth, RenderTargetCubeRendererEscapesThroughTextureCubeBaseMove)
{
    std::shared_ptr<ITextureCubeRenderer> escapedRenderer;
    ITextureCubeRenderer* rendererIdentity=nullptr;

    {
        GraphicsDevice device;
        RenderTargetCube renderTarget(device,1,false,SurfaceFormat::Color,DepthFormat::None,0,
                                      RenderTargetUsage::DiscardContents);
        rendererIdentity=renderTarget.GetRenderTargetCubeRenderer();
        // Renderer-neutral runtime gate (not a renderer-name list, matching this "host-portable"
        // test's own compiled-into-every-renderer's-CnaTests placement): a renderer with no
        // genuine RenderTargetCube storage leaves this pointer null by design (RenderTargetCube.cpp
        // silently accepts a null renderer rather than throwing at construction; TextureCube::
        // SetData is what later refuses -- same shape TextureCubeTests.cpp's own
        // kCubeStorageSupported list documents for several other 2D-only renderers). Nothing in
        // this test's own subject (does a live cube renderer identity survive a TextureCube
        // base-class move) is observable without one.
        if (!rendererIdentity)
        {
            GTEST_SKIP() << "this renderer has no genuine RenderTargetCube storage to move";
        }

        TextureCube escapedTexture(std::move(renderTarget));
        ASSERT_EQ(&escapedTexture.GetRenderer(),rendererIdentity);
        escapedRenderer=escapedTexture.GetRenderer().shared_from_this();
    }

    ASSERT_NE(escapedRenderer,nullptr);
    EXPECT_EQ(escapedRenderer.get(),rendererIdentity)
        << "the base-moved TextureCube can publish a renderer that outlives its device";
}
