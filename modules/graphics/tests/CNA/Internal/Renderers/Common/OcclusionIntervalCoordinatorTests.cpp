// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/OcclusionSampleCount.hpp"

namespace
{
using namespace CNA::Internal::Renderers;

TEST(OcclusionSampleCountTest, NativeCountsPreserveXnaSignedDwordBits)
{
    EXPECT_EQ(OcclusionSampleCount(0).Value(), 0);
    EXPECT_EQ(OcclusionSampleCount(1).Value(), 1);
    EXPECT_EQ(OcclusionSampleCount(0x7fffffffu).Value(), 2147483647);
    EXPECT_EQ(OcclusionSampleCount(0x80000000u).Value(), (-2147483647 - 1));
    EXPECT_EQ(OcclusionSampleCount(0xffffffffu).Value(), -1);
}

TEST(OcclusionSampleCountTest, WideNativeResultsWrapLikeTheXnaGpuReference)
{
    EXPECT_EQ(OcclusionSampleCount(UINT64_C(0x100000000)).Value(), 0);
    EXPECT_EQ(OcclusionSampleCount(UINT64_C(0x100100000)).Value(), 1048576);
}

TEST(OcclusionSampleCountTest, PassingSamplesCrossBothBoundariesWithoutSaturation)
{
    OcclusionSampleCount count(0x7fffffffu);
    count.Add(1);
    EXPECT_EQ(count.Value(), (-2147483647 - 1));
    count.Add(0x7fffffffu);
    EXPECT_EQ(count.Value(), -1);
    count.Add(1);
    EXPECT_EQ(count.Value(), 0);
}

class QueryProbeRenderer final : public IGraphicsRenderer
{
public:
    struct Native final : IOcclusionQueryRenderer
    {
        QueryProbeRenderer& owner;
        std::uint32_t count = 0;
        bool ended = false;
        bool begun = false, precise = true;
        explicit Native(QueryProbeRenderer& owner) : owner(owner) { CheckLease(); ++owner.live; }
        ~Native() override { CheckLease(); if (owner.active == this) owner.active = nullptr; --owner.live; }
        void CheckLease() const { EXPECT_GT(owner.leases, 0); }
        void Begin() override
        {
            CheckLease();
            if (owner.active) throw std::logic_error("overlapping native queries");
            owner.active = this; count = 0; ended = false;
            begun = true; precise = owner.precise;
        }
        void End() override { CheckLease(); EXPECT_EQ(owner.active, this); owner.active = nullptr; ended = true; }
        bool IsComplete() const override { CheckLease(); return ended && owner.ready; }
        int PixelCount() const override { CheckLease(); return precise ? std::bit_cast<std::int32_t>(count) : count != 0; }
        bool PixelCountIsPreciseEXT() const noexcept override { return begun ? precise : owner.precise; }
    };
    struct Lease final : IRendererThreadContextLease
    {
        QueryProbeRenderer& owner;
        explicit Lease(QueryProbeRenderer& owner) : owner(owner) { ++owner.leases; }
        ~Lease() override { --owner.leases; }
    };
    Native* active = nullptr;
    int leases = 0, live = 0;
    int capacity = std::numeric_limits<int>::max();
    bool ready = true, precise = true, failFactory = false;
    std::unique_ptr<IRendererThreadContextLease> AcquireThreadContextLeaseEXT(
        RendererThreadContextLeaseRelease = RendererThreadContextLeaseRelease::RestorePreviousBinding) override
    { return std::make_unique<Lease>(*this); }
    std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override
    {
        if (failFactory || live >= capacity) throw std::bad_alloc();
        return std::make_unique<Native>(*this);
    }
    void Samples(std::uint32_t count) { if (active) active->count += count; }
    void Clear(float, float, float, float) override {}
    void Present() override {}
    void GetViewportSize(int& w, int& h) override { w = h = 0; }
    void SetVirtualResolution(int, int) override {}
    void SetPresentationMode(int) override {}
    std::unique_ptr<ITextureRenderer> CreateTexture(const CNA::Internal::Graphics::ImageData&) override { return nullptr; }
    std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override { return nullptr; }
    void SetRenderTargets(const RenderTargetBindingDescriptor*, int) override {}
    void ClearColorAndDepth(float, float, float, float, float) override {}
    void ClearDepth(float) override {}
    void ClearStencil(int) override {}
    void ClearDepthAndStencil(float, int) override {}
    void ClearColorAndStencil(float, float, float, float, int) override {}
    void ClearColorDepthAndStencil(float, float, float, float, float, int) override {}
    void SetDepthTestEnabled(bool) override {}
    void SetBlendEnabled(bool) override {}
    void SetDepthWriteEnabled(bool) override {}
    std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int) override { return nullptr; }
    std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int) override { return nullptr; }
    void DrawColoredPrimitives(const IVertexBufferRenderer&, const Microsoft::Xna::Framework::Matrix&,
        const Microsoft::Xna::Framework::Matrix&, const Microsoft::Xna::Framework::Matrix&,
        Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}
    void DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
        const Microsoft::Xna::Framework::Matrix&, const Microsoft::Xna::Framework::Matrix&,
        const Microsoft::Xna::Framework::Matrix&, Microsoft::Xna::Framework::Graphics::PrimitiveType, int) override {}
};

TEST(OcclusionIntervalCoordinatorTest, NativeTargetNeverOverlapsAndPendingSegmentsStayIndependent)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    renderer.ready = false;
    a->Begin(); renderer.Samples(1); b->Begin(); renderer.Samples(2);
    a->End(); renderer.Samples(4); b->End();
    EXPECT_FALSE(a->IsComplete()); EXPECT_FALSE(b->IsComplete());
    renderer.ready = true;
    EXPECT_TRUE(a->IsComplete()); EXPECT_EQ(a->PixelCount(), 3);
    EXPECT_TRUE(b->IsComplete()); EXPECT_EQ(b->PixelCount(), 6);
    a.reset(); b.reset(); EXPECT_EQ(renderer.live, 0); EXPECT_EQ(renderer.leases, 0);
}

TEST(OcclusionIntervalCoordinatorTest, BooleanSegmentsAreOrCombinedNotAdded)
{
    QueryProbeRenderer renderer; renderer.precise = false;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(10); b->Begin(); renderer.Samples(20);
    b->End(); renderer.Samples(40); a->End();
    EXPECT_EQ(a->PixelCount(), 1); EXPECT_EQ(b->PixelCount(), 1);
    EXPECT_FALSE(a->PixelCountIsPreciseEXT());
}

TEST(OcclusionIntervalCoordinatorTest, ReuseDoesNotDiscardASharedPendingResult)
{
    QueryProbeRenderer renderer; renderer.ready = false;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(1); b->Begin(); renderer.Samples(2); b->End();
    EXPECT_FALSE(b->IsComplete()); b->Begin(); renderer.Samples(4); a->End(); b->End();
    renderer.ready = true;
    EXPECT_EQ(a->PixelCount(), 7); EXPECT_EQ(b->PixelCount(), 4);
}

TEST(OcclusionIntervalCoordinatorTest, DisposalOfEitherActiveMemberPreservesSurvivor)
{
    for (bool disposeOuter : {false, true})
    {
        QueryProbeRenderer renderer;
        auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
        a->Begin(); renderer.Samples(1); b->Begin(); renderer.Samples(2);
        (disposeOuter ? a : b).reset(); renderer.Samples(4);
        auto& survivor = disposeOuter ? b : a; survivor->End();
        EXPECT_EQ(survivor->PixelCount(), disposeOuter ? 6 : 7);
        survivor.reset(); EXPECT_EQ(renderer.live, 0);
    }
}

TEST(OcclusionIntervalCoordinatorTest, CompletedSegmentsAreRetiredDuringLongLivedIntervals)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin();
    for (int i = 0; i < 200; ++i)
    {
        b->Begin(); renderer.Samples(1); b->End();
        EXPECT_TRUE(b->IsComplete()); EXPECT_EQ(b->PixelCount(), 1);
        EXPECT_LE(renderer.live, 4);
    }
    a->End(); EXPECT_EQ(a->PixelCount(), 200);
}

TEST(OcclusionIntervalCoordinatorTest, SummedCountsMatchXnaSignedDwordBoundary)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(std::numeric_limits<int>::max());
    b->Begin(); renderer.Samples(1); b->End(); a->End();
    EXPECT_EQ(a->PixelCount(), std::numeric_limits<std::int32_t>::min()); EXPECT_EQ(b->PixelCount(), 1);
}

TEST(OcclusionIntervalCoordinatorTest, UnsignedCounterWrapIsDefinedAcrossSharedSegments)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(0xffffffffu);
    b->Begin(); renderer.Samples(1); b->End(); a->End();
    EXPECT_EQ(a->PixelCount(), 0); EXPECT_EQ(b->PixelCount(), 1);
}

TEST(OcclusionIntervalCoordinatorTest, NativeHighBitCountsDoNotOverflowSignedArithmetic)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(0x80000000u);
    b->Begin(); renderer.Samples(1); b->End();
    renderer.Samples(0x7fffffffu); a->End();
    EXPECT_EQ(a->PixelCount(), 0); EXPECT_EQ(b->PixelCount(), 1);
}

TEST(OcclusionIntervalCoordinatorTest, BooleanFallbackKeepsVisibilityAfterPreciseSumWrap)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(0x80000000u);
    b->Begin(); renderer.Samples(0x80000000u); b->End();
    renderer.precise = false;
    b->Begin(); b->End(); a->End();
    EXPECT_FALSE(a->PixelCountIsPreciseEXT());
    EXPECT_EQ(a->PixelCount(), 1); EXPECT_EQ(b->PixelCount(), 0);
    a->Begin(); a->End(); EXPECT_EQ(a->PixelCount(), 0);
}

TEST(OcclusionIntervalCoordinatorTest, FailedSegmentAllocationPreservesActiveIntervals)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(1); b->Begin(); renderer.Samples(2);
    renderer.failFactory = true; EXPECT_THROW(a->End(), std::bad_alloc);
    renderer.failFactory = false; renderer.Samples(4); a->End(); b->End();
    EXPECT_EQ(a->PixelCount(), 7); EXPECT_EQ(b->PixelCount(), 6);
}

TEST(OcclusionIntervalCoordinatorTest, RetiresCompletedSegmentsBeforeRequestingReplacement)
{
    QueryProbeRenderer renderer; renderer.capacity = 2;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(1); b->Begin(); renderer.Samples(2);
    ASSERT_NO_THROW(b->End());
    renderer.Samples(4); a->End();
    EXPECT_EQ(a->PixelCount(), 7); EXPECT_EQ(b->PixelCount(), 2);
}

TEST(OcclusionIntervalCoordinatorTest, UnstartedLogicalQueriesDoNotExhaustNativeSlots)
{
    QueryProbeRenderer renderer; renderer.capacity = 2;
    std::vector<std::unique_ptr<IOcclusionQueryRenderer>> queries;
    ASSERT_NO_THROW(for (int i = 0; i < 40; ++i)
        queries.push_back(renderer.CreateLogicalOcclusionQueryEXT()));
    queries.front()->Begin(); renderer.Samples(3);
    queries.back()->Begin(); renderer.Samples(5);
    queries.back()->End(); renderer.Samples(7); queries.front()->End();
    EXPECT_EQ(queries.front()->PixelCount(), 15);
    EXPECT_EQ(queries.back()->PixelCount(), 5);
    queries.clear(); EXPECT_EQ(renderer.live, 0); EXPECT_EQ(renderer.leases, 0);
}

TEST(OcclusionIntervalCoordinatorTest, CachedSharedResultDoesNotRetainANativeSlot)
{
    QueryProbeRenderer renderer; renderer.capacity = 2;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a->Begin(); renderer.Samples(1); b->Begin(); renderer.Samples(2); b->End();
    // b has not polled its shared segment. a can retire its native resource while preserving
    // the cached value for b, then start another interval without exhausting the two slots.
    auto c = renderer.CreateLogicalOcclusionQueryEXT();
    renderer.Samples(4); ASSERT_NO_THROW(c->Begin()); renderer.Samples(8);
    ASSERT_NO_THROW(c->End()); renderer.Samples(16); a->End();
    EXPECT_EQ(a->PixelCount(), 31); EXPECT_EQ(b->PixelCount(), 2); EXPECT_EQ(c->PixelCount(), 8);
}

TEST(OcclusionIntervalCoordinatorTest, UnusedConstructionProbeIsReleasedUnderItsOwnerLease)
{
    QueryProbeRenderer renderer;
    auto a = renderer.CreateLogicalOcclusionQueryEXT(), b = renderer.CreateLogicalOcclusionQueryEXT();
    a.reset(); b.reset();
    EXPECT_EQ(renderer.live, 0); EXPECT_EQ(renderer.leases, 0);
}
}
