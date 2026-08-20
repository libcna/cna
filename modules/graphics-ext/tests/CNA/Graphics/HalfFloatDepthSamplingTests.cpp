// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2035: does this renderer sample a half-float image in a loop the way it
// samples an 8-bit one?
//
// The question matters because `DepthNormalPrepass` stopped storing depth in a half-float target.
// With one, SSAO driven from the real prepass occluded **nothing**; with a packed 8-bit one it
// occludes, and `CNAEXT_Showcase`'s check E went from 0 strongly-occluded pixels to 1022. Something
// about the half-float path defeated every screen-space effect in this layer, all of which sample
// depth in a loop.
//
// This file is the attempt to reduce that to one shader: two textures built to hold **the same
// values** -- proven, not assumed, by reading both directly in the same shader -- and one loop
// inlined over each. It runs the comparison twice, once with no depth attachment and once with the
// `Depth24` the prepass actually allocates, because a reduction that does not build the failing
// shape is not a reduction.
//
// **On this project's reference renderer both shapes agree**, so the reduction still does not
// capture whatever the real pass hit, and this test says so rather than pretending otherwise.
// Three attributes have now been ruled out as the variable -- the format alone, the depth
// attachment, and the loop itself -- which leaves the difference somewhere in what the real pass
// does that this does not: perspective geometry through an MRT prepass, a sample coordinate
// computed from two other texture reads, and a 64-entry uniform array. That is written down so the
// next attempt starts where this one stopped instead of repeating it.
//
// The test is kept for what it can still do: if a renderer ever *does* fail this comparison, the
// layer must not be storing depth in the format that fails it.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <utility>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::FullscreenPass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 64;

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)";

/// A near square on a far field: two distinct values, which is all a comparison loop needs.
constexpr const char* kFillSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    bool inner = all(greaterThan(TexCoord, vec2(0.25))) && all(lessThan(TexCoord, vec2(0.75)));
    float d = inner ? 0.2 : 1.0;
    FragColor = vec4(d, d, d, 1.0);
}
)";

/// Red and green: the same loop over two samplers. Blue and alpha: a direct read of each, so a run
/// can prove the two textures hold the same values rather than assuming it.
constexpr const char* kCompareSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uOther;

float loopOver(sampler2D image) {
    float centre = texture(image, TexCoord).r;
    float nearer = 0.0;
    for (int i = 0; i < 32; ++i) {
        float a = float(i) * 0.19634954;
        vec2 uv = TexCoord + vec2(cos(a), sin(a)) * 0.25;
        float s = textureLod(image, uv, 0.0).r;
        if (s < centre - 0.005) nearer += 1.0;
    }
    return nearer / 32.0;
}

void main() {
    FragColor = vec4(loopOver(texture1), loopOver(uOther),
                     texture(texture1, TexCoord).r, texture(uOther, TexCoord).r);
}
)";

/// One comparison, for a half-float target built with the given depth attachment.
///
/// The attachment is a parameter because it is the one attribute that differed between the shape
/// this reduction was first written with (none) and the shape the prepass actually allocates
/// (`Depth24`) -- and a reduction that does not build the failing shape is not a reduction.
struct Verdict
{
    int readsDiffer   = 0;
    int loopFromHalf  = 0;
    int loopFromEight = 0;
};

Verdict Compare(GraphicsDevice& gd, const Microsoft::Xna::Framework::Graphics::DepthFormat depth)
{
    Texture2D seed(gd, kSize, kSize);
    const std::vector<Color> white(static_cast<std::size_t>(kSize) * kSize, Color::White);
    seed.SetData(white.data(), static_cast<int>(white.size()));

    RenderTarget2D halfFloat(gd, kSize, kSize, false, SurfaceFormat::HalfSingle, depth);
    RenderTarget2D eightBit(gd, kSize, kSize, false, SurfaceFormat::Color, depth);
    {
        ShaderEffect fill(gd, kVertexSource, kFillSource);
        EXPECT_TRUE(fill.IsEffectValid());
        FullscreenPass pass(gd);
        fill.Apply();
        pass.draw(&seed, &halfFloat, &fill, kSize, kSize);
        fill.Apply();
        pass.draw(&seed, &eightBit, &fill, kSize, kSize);
    }

    ShaderEffect compare(gd, kVertexSource, kCompareSource);
    EXPECT_TRUE(compare.IsEffectValid());
    RenderTarget2D verdict(gd, kSize, kSize);
    {
        FullscreenPass pass(gd);
        compare.Apply();
        compare.SetUniformInt("uOther", 1);
        compare.SetTexture(1, eightBit);
        pass.draw(&halfFloat, &verdict, &compare, kSize, kSize);
    }

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));

    Verdict out;
    for (const Color& texel : pixels)
    {
        if (std::abs(texel.getBProperty() - texel.getAProperty()) > 2) ++out.readsDiffer;
        if (texel.getRProperty() > 8) ++out.loopFromHalf;
        if (texel.getGProperty() > 8) ++out.loopFromEight;
    }
    return out;
}

TEST(HalfFloatDepthSamplingTest, TheLayerDoesNotDependOnSamplingHalfFloatDepthInALoop)
{
    GraphicsDevice gd;
    if (!CnaTest::EngineLayer::RunsShaderSource(gd))
        GTEST_SKIP() << "this renderer does not execute effect source";
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle))
        GTEST_SKIP() << "no half-float render target to compare against";

    bool anyDisagreement = false;
    for (const auto& variant : {
             std::pair<const char*, Microsoft::Xna::Framework::Graphics::DepthFormat>{
                 "no depth attachment", Microsoft::Xna::Framework::Graphics::DepthFormat::None},
             std::pair<const char*, Microsoft::Xna::Framework::Graphics::DepthFormat>{
                 "Depth24 (the prepass's own shape)",
                 Microsoft::Xna::Framework::Graphics::DepthFormat::Depth24}})
    {
        const Verdict verdict = Compare(gd, variant.second);

        // Everything else is worthless unless the two images really are the same image.
        ASSERT_EQ(verdict.readsDiffer, 0)
            << variant.first << ": the two textures do not hold the same values, so the loops "
            << "compare nothing";
        ASSERT_GT(verdict.loopFromEight, 0)
            << variant.first << ": the loop finds nothing even in the 8-bit image, so it is not "
            << "testing what it claims";

        const bool disagrees = verdict.loopFromHalf * 4 < verdict.loopFromEight;
        anyDisagreement = anyDisagreement || disagrees;
        std::printf("[ MOD-2035 ] %-34s identical data, same loop: half-float %d, 8-bit %d (of %d)%s\n",
                    variant.first, verdict.loopFromHalf, verdict.loopFromEight, kSize * kSize,
                    disagrees ? "   <-- DISAGREES" : "");
    }

    if (anyDisagreement)
    {
        // The assertion is on CNA, never on the driver: where a loop cannot read a half-float
        // depth image, the layer must not be storing one.
        EXPECT_TRUE(DepthNormalPrepass::usesPackedDepthEXT(gd))
            << "this renderer mis-samples a half-float texture in a loop and the prepass is still "
               "storing depth in one -- every screen-space pass will silently produce nothing";
    }
    else
    {
        // What this renderer actually reports today. Recorded rather than asserted: the reduction
        // agreeing does not make the real pass's failure go away, it only says this shader is not
        // small enough to see it.
        std::printf("[ MOD-2035 ] neither shape reproduces it -- the reduction is still too small\n");
        SUCCEED();
    }
}

} // namespace

#endif // CNA_CNAEXT
