// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-501..MOD-507: the depth/normal prepass SSAO reads.
//
// Two encodings have to agree with their inverses here, and neither failure is visible: a depth
// packing whose halves drift reads back as noise, and a normal encoding off by a sign produces
// occlusion on the wrong side of every surface. So the packing is checked against its CPU twin
// numerically, and the rendered images are checked for what they must contain rather than for how
// they look.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <vector>
#include <stdexcept>
#include <string>

namespace {

using CNA::Graphics::DepthNormalPrepass;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr int kSize = 32;

[[nodiscard]] Matrix View()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 5.0f), Vector3::Zero,
                                Vector3(0.0f, 1.0f, 0.0f));
}

[[nodiscard]] Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(1.0f, 1.0f, 0.1f, 100.0f);
}

// =====================================================================================
// MOD-507: the packed-depth encoding, against its own inverse
// =====================================================================================

TEST(DepthPackingTest, EveryDepthRoundTripsThroughThePacking)
{
    // The failure this catches is the one a packed format always has: the encoder and the decoder
    // agreeing on the shifts but not on the mask, so mid-range values come back subtly wrong while
    // 0 and 1 look fine. Sweeping the range is the only way to see it.
    for (int step = 0; step <= 512; ++step)
    {
        // Stops one step short of 1.0: that value is clamped by design, and its own assertion is
        // in TheEndsOfTheRangeArePinned rather than hidden inside a sweep.
        const float value = static_cast<float>(step) / 513.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        DepthNormalPrepass::packDepth(value, r, g, b, a);
        const float recovered = DepthNormalPrepass::unpackDepth(r, g, b, a);
        EXPECT_NEAR(recovered, value, 1e-5f) << "at " << value;
    }
}

TEST(DepthPackingTest, EveryChannelStaysInRange)
{
    // A channel outside 0..1 is silently clamped by the texture write, which turns a packing bug
    // into a depth that is merely wrong rather than obviously broken.
    for (int step = 0; step <= 256; ++step)
    {
        const float value = static_cast<float>(step) / 256.0f;
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        DepthNormalPrepass::packDepth(value, r, g, b, a);
        for (const float channel : {r, g, b, a})
        {
            EXPECT_GE(channel, 0.0f) << "at " << value;
            EXPECT_LE(channel, 1.0f) << "at " << value;
        }
    }
}

TEST(DepthPackingTest, TheEndsOfTheRangeArePinned)
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    DepthNormalPrepass::packDepth(0.0f, r, g, b, a);
    EXPECT_NEAR(DepthNormalPrepass::unpackDepth(r, g, b, a), 0.0f, 1e-6f);

    // 1.0 is representable only as "one texel short of the far plane", and deliberately so:
    // fract(1.0) is zero, so an unclamped 1.0 would pack to all zeroes and read back as the
    // *nearest* possible surface. Depth is normalised by the far plane, which makes 1.0 the most
    // common value in the buffer -- inverting it would put the whole background in front.
    DepthNormalPrepass::packDepth(1.0f, r, g, b, a);
    EXPECT_NEAR(DepthNormalPrepass::unpackDepth(r, g, b, a), 1.0f, 1e-6f);
    EXPECT_LT(DepthNormalPrepass::unpackDepth(r, g, b, a), 1.0f);
}

TEST(DepthPackingTest, OutOfRangeInputIsClamped)
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
    DepthNormalPrepass::packDepth(-1.0f, r, g, b, a);
    EXPECT_NEAR(DepthNormalPrepass::unpackDepth(r, g, b, a), 0.0f, 1e-6f);
    DepthNormalPrepass::packDepth(2.0f, r, g, b, a);
    EXPECT_NEAR(DepthNormalPrepass::unpackDepth(r, g, b, a), 1.0f, 1e-6f);
}

TEST(DepthPackingTest, TheSharedGlslDeclaresWhatConsumersInclude)
{
    // MOD-504. Not a compile test -- the renderers do that when a consumer uses it -- but the two
    // functions must be present under both encodings, because a consumer picks the variant from
    // isDepthPacked() and cannot afford the other one to be missing.
    for (const bool packed : {false, true})
    {
        const std::string glsl = DepthNormalPrepass::getDepthDecodeGlsl(packed);
        EXPECT_NE(glsl.find("cnaDecodeLinearDepth"), std::string::npos);
        EXPECT_NE(glsl.find("cnaViewPositionFromDepth"), std::string::npos);
        if (packed)
            EXPECT_NE(glsl.find("cnaUnpackDepth"), std::string::npos)
                << "the packed variant decodes without its unpacker";
    }
}

// =====================================================================================
// MOD-501/502/503: the prepass itself
// =====================================================================================

TEST(DepthNormalPrepassTest, ItValidatesItsSizeAndItsCameraRange)
{
    GraphicsDevice gd;
    EXPECT_THROW(DepthNormalPrepass(gd, 0, 8), std::invalid_argument);
    EXPECT_THROW(DepthNormalPrepass(gd, 8, -1), std::invalid_argument);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    // A far plane at or behind the near plane makes the normalising divide meaningless, and the
    // buffer fills with NaNs rather than with a wrong image -- worth refusing loudly.
    EXPECT_THROW(prepass.begin(0, View(), Projection(), 0.0f, 100.0f), std::invalid_argument);
    EXPECT_THROW(prepass.begin(0, View(), Projection(), 10.0f, 10.0f), std::invalid_argument);
    EXPECT_THROW(prepass.begin(0, View(), Projection(), 10.0f, 1.0f), std::invalid_argument);
}

TEST(DepthNormalPrepassTest, ThePassCountFollowsTheRenderersMrtSupport)
{
    GraphicsDevice gd;
    const DepthNormalPrepass prepass(gd, kSize, kSize);
    const bool mrt = gd.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets);
    EXPECT_EQ(prepass.isUsingMultipleRenderTargets(), mrt);
    EXPECT_EQ(prepass.getPassCount(), mrt ? 1 : 2)
        << "the loop an app writes must match what the renderer can do in one pass";
}

TEST(DepthNormalPrepassTest, BothTexturesExistWhateverTheRenderer)
{
    // They exist even where the effect cannot compile: a consumer holding null textures would have
    // to handle a second failure mode on top of isSupported().
    GraphicsDevice gd;
    const DepthNormalPrepass prepass(gd, kSize, kSize);
    EXPECT_NE(prepass.getDepthTexture(), nullptr);
    EXPECT_NE(prepass.getNormalTexture(), nullptr);
    EXPECT_NE(prepass.getDepthTexture(), prepass.getNormalTexture());
}

TEST(DepthNormalPrepassTest, TheDepthFormatFollowsTheRenderersFloatSupport)
{
    GraphicsDevice gd;
    const DepthNormalPrepass prepass(gd, kSize, kSize);
    const bool hasHalfFloat = gd.SupportsSurfaceFormatAsRenderTargetEXT(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfSingle);
    EXPECT_EQ(prepass.isDepthPacked(), !hasHalfFloat);
}

TEST(DepthNormalPrepassTest, EveryMisuseIsRejected)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    EXPECT_THROW(prepass.end(), std::logic_error) << "end without begin";
    EXPECT_THROW(prepass.begin(prepass.getPassCount(), View(), Projection(), 0.1f, 100.0f),
                 std::out_of_range);
    EXPECT_THROW(prepass.begin(-1, View(), Projection(), 0.1f, 100.0f), std::out_of_range);

    prepass.begin(0, View(), Projection(), 0.1f, 100.0f);
    EXPECT_THROW(prepass.begin(0, View(), Projection(), 0.1f, 100.0f), std::logic_error)
        << "two passes at once";
    EXPECT_THROW(prepass.resize(16, 16), std::logic_error) << "resized mid-pass";
    prepass.end();
}

TEST(DepthNormalPrepassTest, EveryPassCanBeOpenedAndClosed)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
        EXPECT_NO_THROW({
            prepass.begin(pass, View(), Projection(), 0.1f, 100.0f);
            prepass.end();
        }) << "pass " << pass;
}

TEST(DepthNormalPrepassTest, ResizingIsANoOpWhenTheSizeIsUnchanged)
{
    GraphicsDevice gd;
    DepthNormalPrepass prepass(gd, kSize, kSize);
    const void* before = prepass.getDepthTexture();
    prepass.resize(kSize, kSize);
    EXPECT_EQ(prepass.getDepthTexture(), before)
        << "an unchanged size reallocated the targets anyway";

    prepass.resize(kSize * 2, kSize);
    EXPECT_NE(prepass.getDepthTexture(), nullptr);
    EXPECT_THROW(prepass.resize(0, kSize), std::invalid_argument);
}

TEST(DepthNormalPrepassTest, TheSkinnedEffectIsASecondProgramNotTheSameOne)
{
    // MOD-503. A skinned mesh drawn with the rigid effect lands in the depth buffer in its bind
    // pose, so it occludes the wrong part of the screen -- which looks like the AO being wrong.
    GraphicsDevice gd;
    const DepthNormalPrepass prepass(gd, kSize, kSize);
    if (!prepass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the prepass shaders";

    ASSERT_NE(prepass.getPrepassEffect(), nullptr);
    ASSERT_NE(prepass.getSkinnedPrepassEffect(), nullptr);
    EXPECT_NE(prepass.getPrepassEffect(), prepass.getSkinnedPrepassEffect());
    EXPECT_TRUE(prepass.getPrepassEffect()->IsEffectValid());
    EXPECT_TRUE(prepass.getSkinnedPrepassEffect()->IsEffectValid());
}

TEST(DepthNormalPrepassTest, AnUnsupportedRendererIsReportedRatherThanFailing)
{
    GraphicsDevice gd;
    const DepthNormalPrepass prepass(gd, kSize, kSize);
    if (prepass.isSupported(gd))
    {
        EXPECT_NE(prepass.getPrepassEffect(), nullptr);
    }
    else
    {
        // Both effects null, both textures present: the caller sees one failure mode, not two.
        EXPECT_EQ(prepass.getPrepassEffect(), nullptr);
        EXPECT_EQ(prepass.getSkinnedPrepassEffect(), nullptr);
        EXPECT_NE(prepass.getDepthTexture(), nullptr);
    }
}

TEST(DepthNormalPrepassTest, AnEmptyPrepassLeavesDepthAtTheFarPlane)
{
    // The clear convention, and it is the one that matters: white means "nothing here, infinitely
    // far". Clearing to black would make every empty pixel the nearest possible occluder and SSAO
    // would darken the whole frame.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
    {
        prepass.begin(pass, View(), Projection(), 0.1f, 100.0f);
        prepass.end();
    }

    std::vector<Microsoft::Xna::Framework::Color> depth(
        static_cast<std::size_t>(kSize) * kSize, Microsoft::Xna::Framework::Color::Black);
    if (prepass.isDepthPacked())
    {
        prepass.getDepthTexture()->GetData(depth.data(), static_cast<int>(depth.size()));
        for (const auto& texel : depth)
            EXPECT_EQ(texel.getAProperty(), 255)
                << "an unwritten texel did not read as the far plane";
    }
    else
    {
        // A half-float target is not readable as Color; that it cleared without throwing is what
        // this branch can honestly assert.
        SUCCEED() << "float depth target: the clear is verified through SSAO's own suite";
    }
}

} // namespace

#endif // CNA_CNAEXT
