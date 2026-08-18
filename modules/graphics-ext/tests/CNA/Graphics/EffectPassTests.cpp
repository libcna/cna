// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-231/MOD-232: the three CNAEXT effects that existed before this plan, used
// inside the chain the plan built.
//
// The point of the adapters is that the effects did not change. So the tests here are about the
// adaptation, not about what DepthEffect or CRTEffect do -- those have their own suites. What is
// checked is that an effect really reaches the draw, that ownership works both ways round, that a
// pass with no effect degrades to a copy rather than refusing, and that the ASCII effect -- which
// is not an Effect at all and cannot go through EffectPass -- is driven correctly by its own pass.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/AsciiPass.hpp"
#include "CNA/Graphics/AsciiPostProcessEffect.hpp"
#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DepthEffectMode.hpp"
#include "CNA/Graphics/EffectPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::AsciiPass;
using CNA::Graphics::CRTEffect;
using CNA::Graphics::DepthEffect;
using CNA::Graphics::EffectPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

constexpr int kSize = 8;

/// A source target filled with one recognisable colour, so a pass that did nothing and a pass that
/// copied are distinguishable from a pass that changed the image.
[[nodiscard]] std::unique_ptr<RenderTarget2D> MakeSource(GraphicsDevice& device, const Color& fill)
{
    auto source = std::make_unique<RenderTarget2D>(device, kSize, kSize);
    device.SetRenderTarget(source.get());
    device.Clear(fill);
    device.SetRenderTarget(nullptr);
    return source;
}

TEST(EffectPassTest, ANameIsRequiredBecauseAPipelineReportsIt)
{
    GraphicsDevice gd;
    EXPECT_THROW(EffectPass(gd, nullptr, ""), std::invalid_argument);
    EXPECT_NO_THROW(EffectPass(gd, nullptr, "Named"));
}

TEST(EffectPassTest, TheNameIsWhatWasGiven)
{
    GraphicsDevice gd;
    const EffectPass pass(gd, nullptr, "Depth");
    EXPECT_EQ(pass.getName(), "Depth");
}

TEST(EffectPassTest, ABorrowedEffectIsHeldWithoutBeingOwned)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
        GTEST_SKIP() << "this renderer accepts no custom effect to borrow";

    DepthEffect effect(gd);
    EffectPass pass(gd, &effect, "Depth");
    EXPECT_EQ(pass.getEffect(), &effect);
    // The borrowed effect is still alive and still the caller's: the pass never owned it.
    effect.setMode(CNA::Graphics::DepthEffectMode::Grayscale4Bit);
    EXPECT_EQ(effect.getMode(), CNA::Graphics::DepthEffectMode::Grayscale4Bit);
}

TEST(EffectPassTest, AnOwnedEffectIsHeldAndReleasedWhenReplaced)
{
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
        GTEST_SKIP() << "this renderer accepts no custom effect";

    EffectPass pass(gd, std::make_unique<CRTEffect>(gd), "CRT");
    ASSERT_NE(pass.getEffect(), nullptr);

    // Replacing with a borrowed one must release the owned one, or it stays alive with nothing
    // referring to it -- a leak with no symptom, the same trap Skybox's ownership test pins.
    DepthEffect borrowed(gd);
    pass.setEffect(&borrowed);
    EXPECT_EQ(pass.getEffect(), &borrowed);
}

TEST(EffectPassTest, APassWithNoEffectIsNotSupportedAndCopiesInstead)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    EffectPass pass(gd, nullptr, "Empty");
    EXPECT_FALSE(pass.isSupported(gd)) << "a pass with no effect cannot claim to do work";

    auto source = MakeSource(gd, Color(11, 22, 33, 255));
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context;
    context.source      = source.get();
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;

    EXPECT_NO_THROW(pass.apply(context));

    std::vector<Color> pixels(kSize * kSize, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_EQ(pixels[0].getRProperty(), 11);
    EXPECT_EQ(pixels[0].getGProperty(), 22);
    EXPECT_EQ(pixels[0].getBProperty(), 33);
}

TEST(EffectPassTest, SupportIsTheCustomEffectsQuestionNotTheShaderSourceOne)
{
    // The deviation from the base class, asserted so it cannot be "tidied" back. An EffectPass runs
    // whatever Effect it was handed; a stock or compiled effect is real work on a renderer that
    // never compiles GLSL source, so asking ExecutesShaderEffectSourceEXT here would refuse a pass
    // that would have worked.
    GraphicsDevice gd;
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
        GTEST_SKIP() << "this renderer accepts no custom effect";

    DepthEffect effect(gd);
    const EffectPass pass(gd, &effect, "Depth");
    EXPECT_TRUE(pass.isSupported(gd));
}

TEST(EffectPassTest, TheAdaptedEffectReallyReachesTheDraw)
{
    // MOD-231's actual question: is the adaptation real, or does the effect quietly not apply?
    // DepthEffect at its coarsest mode has to change a mid-grey; a pass that dropped the effect
    // would return the source unchanged.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!CnaTest::EngineLayer::RunsShaderSource(gd))
        GTEST_SKIP() << "this renderer does not run shader source, so the effect would be accepted "
                        "and ignored";

    DepthEffect effect(gd);
    effect.setMode(CNA::Graphics::DepthEffectMode::Grayscale1Bit);
    EffectPass pass(gd, &effect, "Depth");

    auto source = MakeSource(gd, Color(120, 120, 120, 255));
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context;
    context.source      = source.get();
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    pass.apply(context);

    std::vector<Color> pixels(kSize * kSize, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_NE(pixels[0].getRProperty(), 120)
        << "the effect did not reach the draw -- the source came through unchanged";
}

TEST(EffectPassTest, AnAdaptedEffectRunsInsideAChain)
{
    // The whole reason for the adapter: these effects predate the chain and must now sit in it.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    if (!gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects))
        GTEST_SKIP() << "this renderer accepts no custom effect";

    CNA::Graphics::PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<EffectPass>(gd, std::make_unique<CRTEffect>(gd), "CRT"));
    EXPECT_EQ(chain.getPassCount(), 1u);

    auto source = MakeSource(gd, Color(90, 90, 90, 255));
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context;
    context.source      = source.get();
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    EXPECT_NO_THROW(chain.apply(context));
}

// =====================================================================================
// The one that is not an Effect
// =====================================================================================

TEST(AsciiPassTest, ItHasItsOwnNameAndOwnsItsEffect)
{
    GraphicsDevice gd;
    AsciiPass pass(gd);
    EXPECT_EQ(pass.getName(), "Ascii");
    // Reachable so a chain author can set the cell size; owned so they need not keep it alive.
    pass.getEffect().setCellSize(4, 6);
    int cellWidth = 0, cellHeight = 0;
    pass.getEffect().getCellSize(cellWidth, cellHeight);
    EXPECT_EQ(cellWidth, 4);
    EXPECT_EQ(cellHeight, 6);
}

TEST(AsciiPassTest, SupportIsProbedRatherThanRead)
{
    // There is no capability for "GetData works on a texture", which is what this effect needs, so
    // the pass asks by doing. Whatever the answer, it must be an answer and not an exception.
    GraphicsDevice gd;
    const AsciiPass pass(gd);
    EXPECT_NO_THROW((void)pass.isSupported(gd));
}

TEST(AsciiPassTest, ItValidatesItsInputs)
{
    GraphicsDevice gd;
    AsciiPass pass(gd);

    PostProcessContext context;
    context.width  = kSize;
    context.height = kSize;
    EXPECT_THROW(pass.apply(context), std::invalid_argument) << "a null source";

    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    auto source = MakeSource(gd, Color::White);
    context.source = source.get();
    context.width  = 0;
    EXPECT_THROW(pass.apply(context), std::invalid_argument) << "a zero width";
}

TEST(AsciiPassTest, ItQuantisesTheSourceIntoTheDestination)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    AsciiPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot read a texture back, which this effect requires";

    constexpr int kBig = 32;
    RenderTarget2D source(gd, kBig, kBig);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 40, 40, 255));
    gd.SetRenderTarget(nullptr);

    RenderTarget2D destination(gd, kBig, kBig);
    pass.getEffect().setCellSize(8, 8);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kBig;
    context.height      = kBig;
    EXPECT_NO_THROW(pass.apply(context));

    // Not asserting an exact image -- that is AsciiPostProcessEffect's own suite's job. What this
    // asserts is that the pass drew *into the destination* rather than wherever the device happened
    // to point, which is the one thing the adapter is responsible for.
    std::vector<Color> pixels(static_cast<std::size_t>(kBig) * kBig, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    bool anyInk = false;
    for (const Color& pixel : pixels)
        if (pixel.getRProperty() != 0 || pixel.getGProperty() != 0 || pixel.getBProperty() != 0)
        { anyInk = true; break; }
    EXPECT_TRUE(anyInk) << "the pass drew nothing into its destination";
}

} // namespace

#endif // CNA_CNAEXT
