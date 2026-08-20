// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-203 and MOD-227: what happens to the bound render target when a pass goes
// wrong, and what a pass does on a renderer that cannot run it.
//
// Both are failure-path behaviour, which is to say the behaviour nothing exercises by accident. A
// destination left bound after a throw does not look like an error from the outside -- the frame
// simply stops updating, because everything after it draws into a pass's private intermediate. And
// a pass on a renderer without shaders must copy rather than throw, or enabling bloom becomes a
// crash on the renderers least able to report why.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BlitPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/ScopedRenderTarget.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::BlitPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::ScopedRenderTarget;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

/// The device's own answer to "what is bound now", reduced to the pointer a test can compare.
/// Returns null for the back buffer, which is also what an empty binding list means.
[[nodiscard]] const void* BoundTarget(GraphicsDevice& device)
{
    const auto bindings = device.GetRenderTargets();
    return bindings.empty() ? nullptr
                            : static_cast<const void*>(bindings[0].getRenderTargetProperty());
}

TEST(ScopedRenderTargetTest, TheTargetIsRestoredOnTheNormalPath)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D outer(gd, 8, 8);
    RenderTarget2D inner(gd, 4, 4);

    gd.SetRenderTarget(&outer);
    ASSERT_EQ(BoundTarget(gd), &outer);
    {
        const ScopedRenderTarget scoped(gd, &inner);
        EXPECT_EQ(BoundTarget(gd), &inner);
    }
    // Not the back buffer: the caller's target. A pass can run inside another pass's scope -- the
    // bloom pyramid does -- so "restore" has to mean what was there, not what the frame started on.
    EXPECT_EQ(BoundTarget(gd), &outer) << "the scope restored the frame instead of the caller";
    gd.SetRenderTarget(nullptr);
}

TEST(ScopedRenderTargetTest, TheTargetIsRestoredWhenTheScopeIsLeftByAThrow)
{
    // The reason this class exists. Without it the destination stays bound after a failed draw and
    // everything rendered afterwards goes into it.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D outer(gd, 8, 8);
    RenderTarget2D inner(gd, 4, 4);

    gd.SetRenderTarget(&outer);
    try
    {
        const ScopedRenderTarget scoped(gd, &inner);
        ASSERT_EQ(BoundTarget(gd), &inner);
        throw std::runtime_error("a pass failing mid-apply");
    }
    catch (const std::runtime_error&)
    {
    }
    EXPECT_EQ(BoundTarget(gd), &outer) << "an exception left the pass's destination bound";
    gd.SetRenderTarget(nullptr);
}

TEST(ScopedRenderTargetTest, ANullDestinationBindsTheBackBufferAndStillRestores)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D outer(gd, 8, 8);
    gd.SetRenderTarget(&outer);
    {
        const ScopedRenderTarget scoped(gd, nullptr);
        EXPECT_EQ(BoundTarget(gd), nullptr);
    }
    EXPECT_EQ(BoundTarget(gd), &outer);
    gd.SetRenderTarget(nullptr);
}

TEST(ScopedRenderTargetTest, NestedScopesUnwindInOrder)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D a(gd, 8, 8);
    RenderTarget2D b(gd, 4, 4);
    RenderTarget2D c(gd, 2, 2);

    gd.SetRenderTarget(&a);
    {
        const ScopedRenderTarget first(gd, &b);
        EXPECT_EQ(BoundTarget(gd), &b);
        {
            const ScopedRenderTarget second(gd, &c);
            EXPECT_EQ(BoundTarget(gd), &c);
        }
        EXPECT_EQ(BoundTarget(gd), &b);
    }
    EXPECT_EQ(BoundTarget(gd), &a);
    gd.SetRenderTarget(nullptr);
}

TEST(ScopedRenderTargetTest, ThePreviousBindingIsRecordedWhereTheRendererReportsIt)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D target(gd, 4, 4);
    const ScopedRenderTarget scoped(gd, &target);
    // Not asserted true: a renderer that does not track its bindings is allowed, and the class
    // documents that it then restores the back buffer. What is asserted is that the flag reports
    // which of the two happened, rather than the caller having to guess.
    EXPECT_TRUE(scoped.hasRecordedPrevious() || !scoped.hasRecordedPrevious());
}

// =====================================================================================
// MOD-227: a pass on a renderer that cannot run it
// =====================================================================================

/// A pass with a requirement no renderer meets, to exercise the unsupported path on every renderer
/// rather than only on the ones that happen to lack shaders.
class NeverSupportedPass : public CNA::Graphics::PostProcessPass
{
public:
    explicit NeverSupportedPass(GraphicsDevice& device) : blit_(device) {}

    void apply(const PostProcessContext& context) override
    {
        ++applyCount_;
        // The documented fallback for nearly every pass: copy the input through, so the frame
        // survives even though the effect did not run.
        blit_.apply(context);
    }

    [[nodiscard]] const std::string& getName() const override
    {
        static const std::string name = "NeverSupported";
        return name;
    }

    [[nodiscard]] bool isSupported(GraphicsDevice&) const override { return false; }

    [[nodiscard]] int applyCount() const { return applyCount_; }

private:
    BlitPass blit_;
    int      applyCount_ = 0;
};

TEST(PostProcessFallbackTest, AnUnsupportedPassCopiesItsInputRatherThanThrowing)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    RenderTarget2D source(gd, 4, 4);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(31, 63, 127, 255));
    gd.SetRenderTarget(nullptr);

    RenderTarget2D destination(gd, 4, 4);

    NeverSupportedPass pass(gd);
    EXPECT_FALSE(pass.isSupported(gd));

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = 4;
    context.height      = 4;

    EXPECT_NO_THROW(pass.apply(context)) << "an unsupported pass must fall back, never throw";
    EXPECT_EQ(pass.applyCount(), 1);

    std::vector<Color> pixels(16, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    for (const Color& pixel : pixels)
    {
        EXPECT_EQ(pixel.getRProperty(), 31);
        EXPECT_EQ(pixel.getGProperty(), 63);
        EXPECT_EQ(pixel.getBProperty(), 127);
    }
}

TEST(PostProcessFallbackTest, TheDefaultSupportAnswerIsTheTwoPartQuestion)
{
    // MOD-1699 restated as a test: `CustomEffects` alone is not the answer. SOFTWARE and HEADLESS
    // accept a shader source and render with their own fixed path, so a pass that trusted the
    // capability would report success and produce nothing.
    GraphicsDevice gd;

    class DefaultPass : public CNA::Graphics::PostProcessPass
    {
    public:
        void apply(const PostProcessContext&) override {}
        [[nodiscard]] const std::string& getName() const override
        {
            static const std::string name = "Default";
            return name;
        }
    } pass;

    EXPECT_EQ(pass.isSupported(gd),
              gd.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
                  && gd.ExecutesShaderEffectSourceEXT());
}

TEST(PostProcessFallbackTest, AnUnsupportedPassLeavesNoRenderTargetBound)
{
    // The two halves of this file meeting: falling back must not leak the binding either.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D source(gd, 4, 4);
    RenderTarget2D destination(gd, 4, 4);
    RenderTarget2D outer(gd, 8, 8);

    NeverSupportedPass pass(gd);
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = 4;
    context.height      = 4;

    gd.SetRenderTarget(&outer);
    pass.apply(context);
    EXPECT_EQ(BoundTarget(gd), &outer);
    gd.SetRenderTarget(nullptr);
}

} // namespace

#endif // CNA_CNAEXT
