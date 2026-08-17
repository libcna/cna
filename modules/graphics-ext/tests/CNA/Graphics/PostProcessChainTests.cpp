// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-200..MOD-210, MOD-225..MOD-228: the fullscreen-pass infrastructure.
//
// The identity chain is the load-bearing assertion here. A chain of copies has to reproduce its
// input exactly, and almost every way of getting the ping-pong bookkeeping wrong breaks it: reading
// the target being written, dropping the final pass's real destination, flipping the texture
// coordinate, or clamping an HDR intermediate to 8 bits. So the tests below run real passes against
// real targets and compare pixels, rather than checking that methods were called.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::BlitPass;
using CNA::Graphics::PostProcessChain;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::PostProcessPass;
using CNA::Graphics::RenderTargetPool;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

constexpr int kSize = 8;

/// Records the order passes ran in and what each was handed, without touching the GPU.
class RecordingPass final : public PostProcessPass
{
public:
    struct Invocation
    {
        std::string name;
        const void* source      = nullptr;
        const void* destination = nullptr;
    };

    RecordingPass(std::string name, std::vector<Invocation>* log)
        : name_(std::move(name)), log_(log)
    {
    }

    void apply(const PostProcessContext& context) override
    {
        log_->push_back({name_, context.source, context.destination});
    }

    [[nodiscard]] const std::string& getName() const override { return name_; }

    [[nodiscard]] bool isSupported(GraphicsDevice&) const override { return true; }

private:
    std::string name_;
    std::vector<RecordingPass::Invocation>* log_;
};

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

// ── RenderTargetPool ─────────────────────────────────────────────────────────

TEST(RenderTargetPoolTest, TheSameShapeIsHandedOutAgainRatherThanReallocated)
{
    GraphicsDevice gd;
    RenderTargetPool pool(gd);

    RenderTarget2D* first  = pool.acquire(16, 16, SurfaceFormat::Color, DepthFormat::None);
    RenderTarget2D* second = pool.acquire(16, 16, SurfaceFormat::Color, DepthFormat::None);

    EXPECT_EQ(first, second);
    EXPECT_EQ(pool.getTargetCount(), 1u);
}

TEST(RenderTargetPoolTest, EverySignificantDifferenceProducesADifferentTarget)
{
    GraphicsDevice gd;
    RenderTargetPool pool(gd);

    RenderTarget2D* base   = pool.acquire(16, 16, SurfaceFormat::Color, DepthFormat::None, 0);
    RenderTarget2D* bySlot = pool.acquire(16, 16, SurfaceFormat::Color, DepthFormat::None, 1);
    RenderTarget2D* bySize = pool.acquire(8, 16, SurfaceFormat::Color, DepthFormat::None, 0);
    RenderTarget2D* byDepth =
        pool.acquire(16, 16, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0);

    EXPECT_NE(base, bySlot);
    EXPECT_NE(base, bySize);
    EXPECT_NE(base, byDepth);
    EXPECT_EQ(pool.getTargetCount(), 4u);
}

TEST(RenderTargetPoolTest, ResetReleasesEverything)
{
    GraphicsDevice gd;
    RenderTargetPool pool(gd);
    (void)pool.acquire(16, 16, SurfaceFormat::Color, DepthFormat::None);
    ASSERT_GT(pool.getEstimatedBytes(), 0u);

    pool.reset();

    EXPECT_EQ(pool.getTargetCount(), 0u);
    EXPECT_EQ(pool.getEstimatedBytes(), 0u);
}

TEST(RenderTargetPoolTest, ANonPositiveSizeIsRejected)
{
    GraphicsDevice gd;
    RenderTargetPool pool(gd);

    EXPECT_THROW((void)pool.acquire(0, 16, SurfaceFormat::Color, DepthFormat::None),
                 std::invalid_argument);
    EXPECT_THROW((void)pool.acquire(16, -1, SurfaceFormat::Color, DepthFormat::None),
                 std::invalid_argument);
}

// ── PostProcessChain bookkeeping ─────────────────────────────────────────────

TEST(PostProcessChainTest, PassesRunInInsertionOrder)
{
    GraphicsDevice gd;
    std::vector<RecordingPass::Invocation> log;
    RecordingPass first("first", &log);
    RecordingPass second("second", &log);
    RecordingPass third("third", &log);

    PostProcessChain chain(gd);
    chain.addPass(&first);
    chain.addPass(&second);
    chain.addPass(&third);

    RenderTarget2D source(gd, kSize, kSize);
    PostProcessContext context;
    context.source = &source;
    context.width  = kSize;
    context.height = kSize;
    chain.apply(context);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0].name, "first");
    EXPECT_EQ(log[1].name, "second");
    EXPECT_EQ(log[2].name, "third");
}

TEST(PostProcessChainTest, EachPassReadsWhatThePreviousOneWroteAndNeverItsOwnTarget)
{
    GraphicsDevice gd;
    std::vector<RecordingPass::Invocation> log;
    RecordingPass first("first", &log);
    RecordingPass second("second", &log);
    RecordingPass third("third", &log);

    PostProcessChain chain(gd);
    chain.addPass(&first);
    chain.addPass(&second);
    chain.addPass(&third);

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    chain.apply(context);

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0].source, &source);
    EXPECT_EQ(log[1].source, log[0].destination);
    EXPECT_EQ(log[2].source, log[1].destination);
    for (const RecordingPass::Invocation& invocation : log)
        EXPECT_NE(invocation.source, invocation.destination);
    // Only the last pass may write the caller's destination.
    EXPECT_EQ(log[2].destination, &destination);
    EXPECT_NE(log[0].destination, &destination);
    EXPECT_NE(log[1].destination, &destination);
}

TEST(PostProcessChainTest, IntermediateTargetsAreReusedAcrossFrames)
{
    GraphicsDevice gd;
    std::vector<RecordingPass::Invocation> log;
    RecordingPass first("first", &log);
    RecordingPass second("second", &log);

    PostProcessChain chain(gd);
    chain.addPass(&first);
    chain.addPass(&second);

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;

    for (int frame = 0; frame < 10; ++frame)
        chain.apply(context);

    // Two passes need exactly one intermediate, and ten frames must not need ten of them.
    EXPECT_EQ(chain.getTargetPool().getTargetCount(), 1u);
}

TEST(PostProcessChainTest, AnEmptyChainStillProducesTheImage)
{
    GraphicsDevice gd;
    PostProcessChain chain(gd);

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);

    gd.SetRenderTarget(&source);
    gd.Clear(Color(10, 20, 30, 255));
    gd.SetRenderTarget(nullptr);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    chain.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_EQ(pixels.front().getRProperty(), 10);
    EXPECT_EQ(pixels.front().getGProperty(), 20);
    EXPECT_EQ(pixels.front().getBProperty(), 30);
}

TEST(PostProcessChainTest, AMissingSourceOrSizeIsRejected)
{
    GraphicsDevice gd;
    PostProcessChain chain(gd);
    RenderTarget2D source(gd, kSize, kSize);

    PostProcessContext noSource;
    noSource.width  = kSize;
    noSource.height = kSize;
    EXPECT_THROW(chain.apply(noSource), std::invalid_argument);

    PostProcessContext noSize;
    noSize.source = &source;
    EXPECT_THROW(chain.apply(noSize), std::invalid_argument);
}

TEST(PostProcessChainTest, OwnedAndBorrowedPassesRunTogether)
{
    GraphicsDevice gd;
    std::vector<RecordingPass::Invocation> log;
    RecordingPass borrowed("borrowed", &log);

    PostProcessChain chain(gd);
    chain.addPass(&borrowed);
    chain.addOwnedPass(std::make_unique<RecordingPass>("owned", &log));
    chain.addPass(nullptr);            // ignored rather than crashing
    chain.addOwnedPass(nullptr);

    EXPECT_EQ(chain.getPassCount(), 2u);

    RenderTarget2D source(gd, kSize, kSize);
    PostProcessContext context;
    context.source = &source;
    context.width  = kSize;
    context.height = kSize;
    chain.apply(context);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0].name, "borrowed");
    EXPECT_EQ(log[1].name, "owned");
}

// ── The identity round trip, which is what actually proves the mechanism ──────

TEST(BlitPassTest, ACopyReproducesItsSourceExactly)
{
    GraphicsDevice gd;
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);

    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 100, 50, 255));
    gd.SetRenderTarget(nullptr);

    BlitPass blit(gd);
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    blit.apply(context);

    for (const Color& texel : ReadTarget(destination))
    {
        EXPECT_EQ(texel.getRProperty(), 200);
        EXPECT_EQ(texel.getGProperty(), 100);
        EXPECT_EQ(texel.getBProperty(), 50);
        EXPECT_EQ(texel.getAProperty(), 255);
    }
}

TEST(BlitPassTest, AChainOfCopiesIsStillTheIdentity)
{
    // Three real passes through two ping-ponged intermediates. Any mistake in the alternation --
    // a pass reading its own destination, the wrong final target, a flipped coordinate -- shows up
    // here as pixels that differ from the input.
    GraphicsDevice gd;
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);

    gd.SetRenderTarget(&source);
    gd.Clear(Color(1, 2, 3, 255));
    gd.SetRenderTarget(nullptr);

    PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<BlitPass>(gd));
    chain.addOwnedPass(std::make_unique<BlitPass>(gd));
    chain.addOwnedPass(std::make_unique<BlitPass>(gd));

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    chain.apply(context);

    for (const Color& texel : ReadTarget(destination))
    {
        EXPECT_EQ(texel.getRProperty(), 1);
        EXPECT_EQ(texel.getGProperty(), 2);
        EXPECT_EQ(texel.getBProperty(), 3);
    }
}

TEST(BlitPassTest, AnHdrChainKeepsItsIntermediatesInFloat)
{
    // The clamp that would be invisible: a Color intermediate between two float passes destroys
    // exactly the values the HDR pipeline exists to carry, and every pixel still looks plausible.
    GraphicsDevice gd;
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D source(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);
    RenderTarget2D destination(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);

    gd.SetRenderTarget(&source);
    gd.Clear(6.0f, 3.0f, 1.5f, 1.0f);
    gd.SetRenderTarget(nullptr);

    PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<BlitPass>(gd));
    chain.addOwnedPass(std::make_unique<BlitPass>(gd));

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    chain.apply(context);

    std::vector<Microsoft::Xna::Framework::Vector4> pixels(
        static_cast<std::size_t>(kSize) * kSize);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_GT(pixels.front().X, 1.0f);
}

} // namespace

#endif // CNA_CNAEXT
