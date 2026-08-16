// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-223. Texture2D::ReconstructFromCache() builds through the protected constructor
// RenderTarget2D owns, which raises gpuOnlyContent_ -- the flag that declares "the live renderer
// is the sole authority for this resource's pixels, trust no CPU shadow". A cache hit is not a
// render target: it is an ordinary content texture whose shadow IS authoritative and whose
// renderer is shared with every other wrapper reconstructed from the same cache entry.
//
// These tests pin both halves of that contract, which no single-renderer suite exercised:
//   * a reconstructed wrapper reads its own cached pixels rather than delegating to a renderer
//     that cannot serve them;
//   * a full-level SetData on any ordinary texture detaches by building a fresh renderer, so an
//     upload can never be published through a shared cached renderer into an unrelated wrapper
//     (the CNB-33 aliasing CnjCacheIsolationTests pins, reached here from the Texture2D side);
//   * a real RenderTarget2D keeps the opposite semantics unchanged -- in-place renderer update,
//     no retained shadow.

#include <gtest/gtest.h>

#include "CNA/RendererTestGate.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "System/NotSupportedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    constexpr int kW = 2;
    constexpr int kH = 2;

    std::vector<Color> Pattern(int base)
    {
        return {
            Color(base, 0, 0, 255), Color(0, base, 0, 255),
            Color(0, 0, base, 255), Color(base, base, base, 255),
        };
    }

    /// Reproduces what ContentManager's weak texture cache does on a hit: hand the cached renderer
    /// and cached CPU pixel buffer to ReconstructFromCache and wrap them in a new Texture2D.
    Texture2D ReconstructLike(GraphicsDevice& device, const Texture2D& cached)
    {
        return Texture2D::ReconstructFromCache(
            device, cached.getWidthProperty(), cached.getHeightProperty(),
            cached.getFormatProperty(), cached.getLevelCountProperty(),
            cached.GetRendererWeak().lock(), cached.GetCpuPixelsWeak().lock());
    }
}

class Texture2DCacheReconstructionTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

// (1) ordinary texture create -> SetData -> GetData.
TEST_F(Texture2DCacheReconstructionTest, OrdinaryTextureRoundTripsThroughItsCpuShadow)
{
    Texture2D tex(gd, kW, kH);
    const auto in = Pattern(10);
    tex.SetData(in.data(), static_cast<int>(in.size()));

    std::vector<Color> out(in.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(tex.GetData(out.data(), static_cast<int>(out.size())));
    EXPECT_EQ(out, in);
}

// (2) ordinary texture cache reconstruction -> GetData. Before the fix this delegated to the
// renderer and threw on every renderer without plain-texture readback.
TEST_F(Texture2DCacheReconstructionTest, ReconstructedTextureReadsTheCachedPixels)
{
    Texture2D original(gd, kW, kH);
    const auto in = Pattern(20);
    original.SetData(in.data(), static_cast<int>(in.size()));

    Texture2D reconstructed = ReconstructLike(gd, original);

    std::vector<Color> out(in.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(reconstructed.GetData(out.data(), static_cast<int>(out.size())));
    EXPECT_EQ(out, in);
}

// (3) cache reconstruction -> SetData -> GetData, and (10) the isolation that upload owes the
// wrapper it was cached from: a shared renderer must not be written through.
TEST_F(Texture2DCacheReconstructionTest, SetDataOnAReconstructedTextureDetachesFromTheCachedRenderer)
{
    Texture2D original(gd, kW, kH);
    const auto in = Pattern(30);
    original.SetData(in.data(), static_cast<int>(in.size()));

    Texture2D reconstructed = ReconstructLike(gd, original);
    const auto sharedRenderer = original.GetRendererWeak().lock();
    ASSERT_NE(sharedRenderer, nullptr);
    ASSERT_EQ(sharedRenderer, reconstructed.GetRendererWeak().lock())
        << "a cache hit must start out sharing the cached renderer";

    const auto patch = Pattern(40);
    reconstructed.SetData(patch.data(), static_cast<int>(patch.size()));

    EXPECT_NE(sharedRenderer, reconstructed.GetRendererWeak().lock())
        << "a full-level upload must detach rather than mutate the shared cached renderer";
    EXPECT_EQ(sharedRenderer, original.GetRendererWeak().lock())
        << "the wrapper the cache entry came from must keep its own renderer";

    std::vector<Color> out(patch.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(reconstructed.GetData(out.data(), static_cast<int>(out.size())));
    EXPECT_EQ(out, patch);

    std::vector<Color> untouched(in.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(original.GetData(untouched.data(), static_cast<int>(untouched.size())));
    EXPECT_EQ(untouched, in) << "the upload must not have leaked into the cached wrapper";
}

// (4) cache hit after the source wrapper is destroyed: identity and content survive as long as
// the cache's own strong references do.
TEST_F(Texture2DCacheReconstructionTest, ReconstructionSurvivesSourceWrapperDestruction)
{
    const auto in = Pattern(50);
    std::shared_ptr<CNA::Internal::Renderers::ITextureRenderer> renderer;
    std::shared_ptr<std::vector<std::uint8_t>> pixels;
    SurfaceFormat fmt = SurfaceFormat::Color;
    int levels = 1;

    {
        Texture2D original(gd, kW, kH);
        original.SetData(in.data(), static_cast<int>(in.size()));
        renderer = original.GetRendererWeak().lock();
        pixels = original.GetCpuPixelsWeak().lock();
        fmt = original.getFormatProperty();
        levels = original.getLevelCountProperty();
    }
    ASSERT_NE(renderer, nullptr);
    ASSERT_NE(pixels, nullptr);

    Texture2D reconstructed =
        Texture2D::ReconstructFromCache(gd, kW, kH, fmt, levels, renderer, pixels);

    std::vector<Color> out(in.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(reconstructed.GetData(out.data(), static_cast<int>(out.size())));
    EXPECT_EQ(out, in);
}

// (5) multiple reconstructed wrappers stay isolated from each other.
TEST_F(Texture2DCacheReconstructionTest, IndependentReconstructedWrappersDoNotAliasOnUpload)
{
    Texture2D original(gd, kW, kH);
    const auto in = Pattern(60);
    original.SetData(in.data(), static_cast<int>(in.size()));

    Texture2D first = ReconstructLike(gd, original);
    Texture2D second = ReconstructLike(gd, original);

    const auto patch = Pattern(70);
    first.SetData(patch.data(), static_cast<int>(patch.size()));

    std::vector<Color> firstOut(patch.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(first.GetData(firstOut.data(), static_cast<int>(firstOut.size())));
    EXPECT_EQ(firstOut, patch);

    std::vector<Color> secondOut(in.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(second.GetData(secondOut.data(), static_cast<int>(secondOut.size())));
    EXPECT_EQ(secondOut, in) << "a sibling cache wrapper must not observe another's upload";
}

// (8)/(9) repeated hit/miss cycles through the same cache entry, reusing the wrapper storage.
TEST_F(Texture2DCacheReconstructionTest, RepeatedReconstructionCyclesStayCorrect)
{
    Texture2D original(gd, kW, kH);
    const auto in = Pattern(80);
    original.SetData(in.data(), static_cast<int>(in.size()));

    for (int cycle = 0; cycle < 8; ++cycle)
    {
        Texture2D reconstructed = ReconstructLike(gd, original);
        std::vector<Color> out(in.size(), Color(0, 0, 0, 0));
        ASSERT_NO_THROW(reconstructed.GetData(out.data(), static_cast<int>(out.size())))
            << "cycle " << cycle;
        EXPECT_EQ(out, in) << "cycle " << cycle;

        // A miss-shaped cycle: upload through the wrapper, then drop it. The cached wrapper must
        // be unaffected on the next iteration.
        const auto patch = Pattern(90 + cycle);
        reconstructed.SetData(patch.data(), static_cast<int>(patch.size()));
    }

    std::vector<Color> out(in.size(), Color(0, 0, 0, 0));
    ASSERT_NO_THROW(original.GetData(out.data(), static_cast<int>(out.size())));
    EXPECT_EQ(out, in);
}

// OPENVG: ShivaVG's non-EGL context extension (vgCreateContextSH/vgResizeSurfaceSH) only ever
// binds the OpenVG pipeline to the one real window surface -- there is no pbuffer/FBO-backed
// off-screen VGImage surface to bind as a draw target, so CreateRenderTarget2D keeps the shared
// IGraphicsRenderer default (returns nullptr) and RenderTarget2D silently falls back to ordinary
// CPU-shadowed Texture2D behaviour. Same "no genuine render-target storage" shape as this file's
// pre-existing TextureCube/RenderTargetCube gates elsewhere use, just for the 2D case; no other
// current renderer lacks RenderTarget2D, so this is the first gate of its kind here.
/// plan_runtimerenderer.md RTR-P9-8: asked of the ACTIVE renderer, so a multi-renderer build gets
/// the right answer per run instead of the build default's.
[[nodiscard]] inline bool RenderTarget2DSupported()
{
    return !CNA_RENDERER_IS(CNA::GraphicsRendererType::OpenVg);
}

// (6) a real RenderTarget2D keeps the opposite semantics: its renderer is updated in place -- it
// must not be swapped for an ordinary texture renderer -- and it retains no CPU shadow.
TEST_F(Texture2DCacheReconstructionTest, RenderTargetKeepsItsRendererAndDropsTheShadowOnUpload)
{
    if (!RenderTarget2DSupported())
    {
        GTEST_SKIP() << "this renderer has no genuine RenderTarget2D storage to keep in place";
    }

    RenderTarget2D rt(gd, kW, kH);
    // A renderer that creates no render-target resource at all (its CreateRenderTarget2D keeps
    // IGraphicsRenderer's nullptr default) has no IRenderTargetRenderer for this contract to be
    // about; GraphicsDeviceValidationTest covers its deterministic refusal of a binding instead.
    if (rt.GetRenderTargetRenderer() == nullptr)
        GTEST_SKIP() << "this renderer creates no RenderTarget2D resource";

    const auto before = rt.GetRendererWeak().lock();
    ASSERT_NE(before, nullptr);

    const auto in = Pattern(100);
    rt.SetData(in.data(), static_cast<int>(in.size()));

    EXPECT_EQ(before, rt.GetRendererWeak().lock())
        << "a render target must keep the renderer its IRenderTargetRenderer view points at";
    EXPECT_TRUE(rt.GetCpuPixelsWeak().expired())
        << "a render target's renderer is authoritative; no upload shadow may be retained";
}

// (7) render-target readback is served by the target's own surface, never by a retained upload
// shadow, and a renderer that cannot read its colour attachment back refuses without fabricating
// pixels (REMED-GFX-127's contract).
//
// Deliberately NOT asserted: that the readback equals the bytes just uploaded. ITextureRenderer
// declares UpdatePixels with an empty default body, and a render-target renderer that does not
// override it drops the upload silently -- EasyGL's does not override it, so the honest answer
// there is the target's cleared surface. That gap is recorded separately as REMED-GFX-224; it is
// not what this test is for, and pinning the upload here would pin the gap instead of the
// contract.
TEST_F(Texture2DCacheReconstructionTest, RenderTargetReadbackComesFromTheSurfaceNotAnUploadShadow)
{
    if (!RenderTarget2DSupported())
    {
        GTEST_SKIP() << "this renderer has no genuine RenderTarget2D storage to read back from";
    }

    RenderTarget2D rt(gd, kW, kH);
    // A renderer that creates no render-target resource at all (its CreateRenderTarget2D keeps
    // IGraphicsRenderer's nullptr default) has no IRenderTargetRenderer for this contract to be
    // about; GraphicsDeviceValidationTest covers its deterministic refusal of a binding instead.
    if (rt.GetRenderTargetRenderer() == nullptr)
        GTEST_SKIP() << "this renderer creates no RenderTarget2D resource";

    const auto in = Pattern(110);
    rt.SetData(in.data(), static_cast<int>(in.size()));
    ASSERT_TRUE(rt.GetCpuPixelsWeak().expired())
        << "the upload shadow must be gone before the readback is asked for";

    const Color sentinel(1, 2, 3, 4);
    std::vector<Color> out(in.size(), sentinel);
    try
    {
        rt.GetData(out.data(), static_cast<int>(out.size()));
    }
    catch (const System::NotSupportedException&)
    {
        for (const auto& pixel : out)
        {
            EXPECT_EQ(pixel, sentinel)
                << "a refused readback must leave the caller's buffer byte-for-byte untouched";
        }
        return;
    }

    const bool servedTheUpload = out == in;
    const bool servedTheClearedSurface =
        std::all_of(out.begin(), out.end(),
                    [](const Color& pixel) { return pixel == Color(0, 0, 0, 0); });
    EXPECT_TRUE(servedTheUpload || servedTheClearedSurface)
        << "a completed readback must return the target's real surface, not arbitrary bytes";
}
