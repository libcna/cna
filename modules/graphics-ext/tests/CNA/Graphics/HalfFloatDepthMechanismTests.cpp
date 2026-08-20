// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2035, the mechanism: *why* did a half-float depth target defeat every
// screen-space effect in this layer?
//
// The ticket was closed by changing the format. The reason stayed open, and the previous attempt --
// `HalfFloatDepthSamplingTests` -- built a reduction *upwards* from a single fullscreen fill and
// never reproduced the failure, so it could not bisect it. It ruled out three variables and wrote
// down what it had not reached.
//
// This file works the other way round: build the shape that actually failed -- the real
// `DepthNormalPrepass` over real perspective geometry -- and take things away one at a time. The
// answer is one line, and it is not in this project's code:
//
//   **A shader that reads a single-channel half-float texture and then writes
//   `if (value <= 0.0) { …; return; }` takes that early return for every pixel, although the value
//   is positive everywhere.**
//
// The same comparison, on the same texture, in a shader without the rest of the body, evaluates
// correctly. The same shader with the early return removed -- one preprocessor line, everything
// else byte-identical -- works. It is a shader-compiler defect on this renderer, and every
// screen-space pass in this layer opens with exactly that early-out on exactly that read, which is
// why all of them went blank at once.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthEncoding.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::Graphics::DepthEncoding;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::FullscreenPass;
using CNA::Graphics::SsaoPass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kSize      = 128;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 60.0f;

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)";

Matrix View()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 4.0f, 12.0f), Vector3::Zero, Vector3::Up);
}
Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, kNearPlane, kFarPlane);
}

std::array<VertexPositionNormalTexture, 6> Ground()
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    const auto vertex = [&](const float x, const float z) {
        return VertexPositionNormalTexture(Vector3(x, 0.0f, z), up, Vector2(0.0f, 0.0f));
    };
    return {vertex(-8.0f, -8.0f), vertex(8.0f, -8.0f), vertex(8.0f, 8.0f),
            vertex(-8.0f, -8.0f), vertex(8.0f, 8.0f),  vertex(-8.0f, 8.0f)};
}

/// A slab standing on the ground: its base is the depth step SSAO reacts to.
std::array<VertexPositionNormalTexture, 6> Slab()
{
    const Vector3 towards(0.0f, 0.0f, 1.0f);
    const auto vertex = [&](const float x, const float y) {
        return VertexPositionNormalTexture(Vector3(x, y, 0.0f), towards, Vector2(0.0f, 0.0f));
    };
    return {vertex(-2.0f, 0.0f), vertex(2.0f, 0.0f), vertex(2.0f, 3.0f),
            vertex(-2.0f, 0.0f), vertex(2.0f, 3.0f), vertex(-2.0f, 3.0f)};
}

void RunPrepass(GraphicsDevice& device, DepthNormalPrepass& prepass)
{
    const auto ground = Ground();
    const auto slab   = Slab();

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetVertexBuffer(nullptr);

    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
    {
        prepass.begin(pass, View(), Projection(), kNearPlane, kFarPlane);
        ShaderEffect* effect = prepass.getPrepassEffect();
        ASSERT_NE(effect, nullptr);
        effect->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, slab.data(), 0, 2);
        prepass.end();
    }
}

/// The rotation texture `SsaoPass` builds for itself, so the estimator sees what it normally sees.
std::unique_ptr<Texture2D> RotationNoise(GraphicsDevice& gd)
{
    auto noise = std::make_unique<Texture2D>(gd, 4, 4);
    std::vector<Color> texels;
    texels.reserve(16);
    for (int index = 0; index < 16; ++index)
    {
        const float angle = static_cast<float>(index) * 0.39269908f;
        texels.emplace_back(static_cast<int>((std::cos(angle) * 0.5f + 0.5f) * 255.0f),
                            static_cast<int>((std::sin(angle) * 0.5f + 0.5f) * 255.0f), 0, 255);
    }
    noise->SetData(texels.data(), static_cast<int>(texels.size()));
    return noise;
}

void BindEstimator(ShaderEffect& effect, const SsaoPass& reference, Texture2D& normals,
                   Texture2D& noise)
{
    effect.Apply();
    effect.SetUniformInt("uNormalSampler", 1);
    effect.SetTexture(1, normals);
    effect.SetUniformInt("uNoiseSampler", 2);
    effect.SetTexture(2, noise);
    effect.SetUniformVec3Array("uKernel", &reference.getKernel()[0].X, 64);
    effect.SetUniformVec2("uNoiseScale", static_cast<float>(kSize) / 4.0f,
                          static_cast<float>(kSize) / 4.0f);
    effect.SetUniformFloat("uRadius", 0.25f);
    effect.SetUniformFloat("uBias", 0.005f);
    effect.SetUniformFloat("uDepthRange", 0.0625f);
    effect.SetUniformInt("uSampleCount", 16);
}

int CountBelow(const std::vector<Color>& pixels, const int threshold)
{
    int count = 0;
    for (const Color& texel : pixels) if (texel.getRProperty() < threshold) ++count;
    return count;
}

/// How many pixels the real estimator darkens, over a depth image in the given layout.
int OccludedPixels(GraphicsDevice& gd, Texture2D* depth, const bool packed, Texture2D& normals,
                   Texture2D& noise, const SsaoPass& reference)
{
    ShaderEffect occlusion(gd, kVertexSource, SsaoPass::getOcclusionGlsl(packed));
    EXPECT_TRUE(occlusion.IsEffectValid());

    RenderTarget2D verdict(gd, kSize, kSize);
    FullscreenPass pass(gd);
    BindEstimator(occlusion, reference, normals, noise);
    pass.draw(depth, &verdict, &occlusion, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return CountBelow(pixels, 254);
}

/// A straight copy of the decoded depth, for asking what an image holds.
constexpr const char* kDirectRead = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    float d = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    FragColor = vec4(d, d, d, 1.0);
}
)";

struct Spread
{
    int distinct = 0;
    int lowest   = 255;
    int highest  = 0;
};

Spread ReadDepth(GraphicsDevice& gd, DepthNormalPrepass& prepass)
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += DepthNormalPrepass::getDepthDecodeGlsl(prepass.isDepthPacked());
    source += kDirectRead;

    ShaderEffect copy(gd, kVertexSource, source);
    EXPECT_TRUE(copy.IsEffectValid());

    RenderTarget2D verdict(gd, kSize, kSize);
    FullscreenPass pass(gd);
    copy.Apply();
    pass.draw(prepass.getDepthTexture(), &verdict, &copy, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));

    std::set<int> values;
    Spread out;
    for (const Color& texel : pixels)
    {
        values.insert(texel.getRProperty());
        out.lowest  = std::min(out.lowest, static_cast<int>(texel.getRProperty()));
        out.highest = std::max(out.highest, static_cast<int>(texel.getRProperty()));
    }
    out.distinct = static_cast<int>(values.size());
    return out;
}

bool CanBuildTheFailingShape(GraphicsDevice& gd)
{
    return CnaTest::EngineLayer::CanReadRenderTargets(gd)
        && CnaTest::EngineLayer::RunsShaderSource(gd)
        && gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HalfSingle);
}

// ── 1. The reproduction ─────────────────────────────────────────────────────

TEST(HalfFloatDepthMechanismTest, TheSameSceneOccludesInOneEncodingAndNotTheOther)
{
    // The measurement that drove the format change, rebuilt so it can be taken apart. Both prepasses
    // see the same geometry through the same camera; only the storage differs.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    if (!CanBuildTheFailingShape(gd))
        GTEST_SKIP() << "the failing shape needs a half-float render target, a readable one, and a "
                        "renderer that runs shader source";

    DepthNormalPrepass packed(gd, kSize, kSize, DepthEncoding::Packed);
    DepthNormalPrepass halfFloat(gd, kSize, kSize, DepthEncoding::HalfFloat);
    if (!packed.isSupported(gd) || !halfFloat.isSupported(gd))
        GTEST_SKIP() << "no prepass on this renderer";
    RunPrepass(gd, packed);
    RunPrepass(gd, halfFloat);

    const Spread packedDepth = ReadDepth(gd, packed);
    const Spread halfDepth   = ReadDepth(gd, halfFloat);
    std::printf("    depth images: packed %d distinct %d..%d | half-float %d distinct %d..%d\n",
                packedDepth.distinct, packedDepth.lowest, packedDepth.highest,
                halfDepth.distinct, halfDepth.lowest, halfDepth.highest);

    // First: the two images describe the same scene. Everything after this is about what a shader
    // does with them, not about what they hold.
    ASSERT_GT(packedDepth.distinct, 8) << "the packed prepass wrote no scene, so nothing below "
                                          "means anything";
    EXPECT_EQ(packedDepth.distinct, halfDepth.distinct);
    EXPECT_EQ(packedDepth.lowest, halfDepth.lowest);
    EXPECT_EQ(packedDepth.highest, halfDepth.highest);

    auto noise = RotationNoise(gd);
    SsaoPass reference(gd);
    const int fromPacked = OccludedPixels(gd, packed.getDepthTexture(), true,
                                          *packed.getNormalTexture(), *noise, reference);
    const int fromHalf   = OccludedPixels(gd, halfFloat.getDepthTexture(), false,
                                          *halfFloat.getNormalTexture(), *noise, reference);
    std::printf("    the estimator darkens %d pixels from the packed image and %d from the "
                "half-float one\n", fromPacked, fromHalf);

    ASSERT_GT(fromPacked, 100) << "the scene produced no occlusion at all, so there is no failure "
                                  "to explain here";
    // No assertion that the half-float path *must* fail: on a renderer without the defect it will
    // not, and that is the outcome this whole investigation would like. The guard is in the last
    // case in this file.
}

// ── 2. The values are not the problem ───────────────────────────────────────

/// Repacks a half-float depth image into the packed layout, losing nothing: a 16-bit float carries
/// fewer bits than the packed layout holds.
constexpr const char* kRepackSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    float d = texture(texture1, TexCoord).r;
    const vec4 shift = vec4(16777216.0, 65536.0, 256.0, 1.0);
    const vec4 mask  = vec4(0.0, 1.0 / 256.0, 1.0 / 256.0, 1.0 / 256.0);
    vec4 channels = fract(clamp(d, 0.0, 0.99999994) * shift);
    channels -= channels.xxyz * mask;
    FragColor = channels;
}
)";

TEST(HalfFloatDepthMechanismTest, TheSameValuesInAnotherFormatOccludeNormally)
{
    // The bisection that separates *values* from *format*. Take the half-float image, repack it into
    // the packed layout without touching a number, and run the estimator on the result. Occlusion
    // appears, so the half-float image held usable depth all along.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    if (!CanBuildTheFailingShape(gd)) GTEST_SKIP() << "the failing shape cannot be built here";

    DepthNormalPrepass halfFloat(gd, kSize, kSize, DepthEncoding::HalfFloat);
    if (!halfFloat.isSupported(gd)) GTEST_SKIP() << "no prepass on this renderer";
    RunPrepass(gd, halfFloat);

    ShaderEffect repack(gd, kVertexSource, kRepackSource);
    ASSERT_TRUE(repack.IsEffectValid());
    RenderTarget2D repacked(gd, kSize, kSize);
    {
        FullscreenPass copy(gd);
        repack.Apply();
        copy.draw(halfFloat.getDepthTexture(), &repacked, &repack, kSize, kSize);
    }

    auto noise = RotationNoise(gd);
    SsaoPass reference(gd);
    const int fromRepacked = OccludedPixels(gd, &repacked, true, *halfFloat.getNormalTexture(),
                                            *noise, reference);
    std::printf("    the half-float image's own values, repacked: %d pixels darkened\n",
                fromRepacked);
    EXPECT_GT(fromRepacked, 100)
        << "the values themselves carry no occlusion, so the format is not the variable after all";
}

// ── 3. The mechanism ────────────────────────────────────────────────────────

/// The estimator's core, with its sky early-out selected by the preprocessor.
///
/// Everything outside the `#ifdef` is identical between the two programs this builds. That is the
/// whole experiment: one line, and it decides whether a half-float depth image produces occlusion.
constexpr const char* kProbeBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform sampler2D uNormalSampler;
uniform sampler2D uNoiseSampler;
uniform vec3  uKernel[64];
uniform vec2  uNoiseScale;
uniform float uRadius;
uniform float uBias;
uniform float uDepthRange;
uniform int   uSampleCount;

void main() {
    float centerDepth = cnaDecodeLinearDepth(texture(texture1, TexCoord));

#ifdef CNA_SKY_EARLY_RETURN
    if (centerDepth <= 0.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
#endif

    vec3 rawNormal = texture(uNormalSampler, TexCoord).xyz * 2.0 - 1.0;
    vec3 normal = length(rawNormal) > 1e-4 ? normalize(rawNormal) : vec3(0.0, 0.0, 1.0);
    vec3 rawRandom = vec3(texture(uNoiseSampler, TexCoord * uNoiseScale).xy * 2.0 - 1.0, 0.0);
    vec3 randomVector = length(rawRandom) > 1e-4 ? normalize(rawRandom) : vec3(1.0, 0.0, 0.0);
    vec3 rawTangent = randomVector - normal * dot(randomVector, normal);
    vec3 tangent = length(rawTangent) > 1e-4
                     ? normalize(rawTangent)
                     : normalize(cross(normal, vec3(0.0, 1.0, 0.0)) + vec3(1e-3, 0.0, 0.0));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float nearer = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSampleCount) break;
        vec3 samplePosition = tbn * uKernel[i];
        vec2 uv = TexCoord + samplePosition.xy * uRadius;
        float s = cnaDecodeLinearDepth(textureLod(texture1, uv, 0.0));
        if (s <= 0.0) continue;
        if (s < centerDepth - uBias) nearer += 1.0;
    }

    FragColor = vec4(centerDepth, nearer / float(uSampleCount), 0.0, 1.0);
}
)";

int SamplesFound(GraphicsDevice& gd, DepthNormalPrepass& prepass, const bool earlyReturn,
                 Texture2D& noise, const SsaoPass& reference)
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    if (earlyReturn) source += "#define CNA_SKY_EARLY_RETURN 1\n";
    source += DepthNormalPrepass::getDepthDecodeGlsl(prepass.isDepthPacked());
    source += kProbeBody;

    ShaderEffect probe(gd, kVertexSource, source);
    EXPECT_TRUE(probe.IsEffectValid());

    RenderTarget2D verdict(gd, kSize, kSize);
    FullscreenPass pass(gd);
    BindEstimator(probe, reference, *prepass.getNormalTexture(), noise);
    pass.draw(prepass.getDepthTexture(), &verdict, &probe, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));

    int found = 0;
    for (const Color& texel : pixels) if (texel.getGProperty() > 4) ++found;
    return found;
}

/// The estimator's sky early-out, exactly as `SsaoPass` writes it.
///
/// Matched against the emitted source rather than retyped into a replica: a replica of this shader
/// changed its answer when arithmetic elsewhere in it was simplified, which is itself evidence
/// about what kind of defect this is -- and a reason to do the experiment on the shader the layer
/// actually ships.
constexpr const char* kSkyEarlyOut =
    "    if (centerDepth <= 0.0) {\n"
    "        FragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
    "        return;\n"
    "    }\n";

/// The real estimator with its sky early-out cut out of the emitted string, and nothing else
/// touched.
int OccludedWithoutTheEarlyOut(GraphicsDevice& gd, DepthNormalPrepass& prepass, Texture2D& noise,
                               const SsaoPass& reference)
{
    std::string source = SsaoPass::getOcclusionGlsl(prepass.isDepthPacked());
    const std::size_t at = source.find(kSkyEarlyOut);
    EXPECT_NE(at, std::string::npos) << "the early-out is no longer written the way this test "
                                        "matches it, so this measurement is of nothing";
    if (at == std::string::npos) return -1;
    source.erase(at, std::string(kSkyEarlyOut).size());

    ShaderEffect occlusion(gd, kVertexSource, source);
    EXPECT_TRUE(occlusion.IsEffectValid());

    RenderTarget2D verdict(gd, kSize, kSize);
    FullscreenPass pass(gd);
    BindEstimator(occlusion, reference, *prepass.getNormalTexture(), noise);
    pass.draw(prepass.getDepthTexture(), &verdict, &occlusion, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return CountBelow(pixels, 254);
}

TEST(HalfFloatDepthMechanismTest, TheSkyEarlyReturnIsWhatBreaksTheHalfFloatPath)
{
    // The answer, taken on the shader the layer actually ships rather than on a replica of it. The
    // only difference between the two programs is that one has `SsaoPass`'s sky early-out and the
    // other has it cut out of the emitted string.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    if (!CanBuildTheFailingShape(gd)) GTEST_SKIP() << "the failing shape cannot be built here";

    auto noise = RotationNoise(gd);
    SsaoPass reference(gd);

    bool defectPresent = false;
    for (const auto& variant : {std::make_pair("packed", DepthEncoding::Packed),
                                std::make_pair("half-float", DepthEncoding::HalfFloat)})
    {
        DepthNormalPrepass prepass(gd, kSize, kSize, variant.second);
        if (!prepass.isSupported(gd)) GTEST_SKIP() << "no prepass on this renderer";
        RunPrepass(gd, prepass);

        const int with    = OccludedPixels(gd, prepass.getDepthTexture(), prepass.isDepthPacked(),
                                           *prepass.getNormalTexture(), *noise, reference);
        const int without = OccludedWithoutTheEarlyOut(gd, prepass, *noise, reference);
        std::printf("    %-10s the shipped estimator darkens %5d px | with its sky early-out "
                    "removed, %5d px\n", variant.first, with, without);

        ASSERT_GT(without, 100)
            << variant.first << ": the estimator finds nothing even without the early-out, so this "
               "comparison has nothing in it";
        if (variant.second == DepthEncoding::Packed)
        {
            EXPECT_NEAR(with, without, std::max(4, without / 40))
                << "removing an early-out that never fires changed the packed result, which would "
                   "make the early-out the variable rather than the format";
        }
        else if (with == 0)
        {
            defectPresent = true;
        }
    }

    if (defectPresent)
    {
        std::printf("    this renderer takes the sky early-out on every pixel of a half-float "
                    "depth image, though every value in it is positive\n");
        // The guard. The layer's packing policy exists because of this, so where the defect is
        // present the policy must be in force -- otherwise the layer is one flag away from a blank
        // frame with nothing to point at.
        EXPECT_TRUE(DepthNormalPrepass::usesPackedDepthEXT(gd))
            << "this renderer has the defect and the layer is not packing depth";
    }
    else
    {
        std::printf("    this renderer does not have the defect: the early-out costs nothing on a "
                    "half-float depth image\n");
    }
}

// ── 4. What it is not ───────────────────────────────────────────────────────

/// Reports which texture the shader is actually looking at, and what a comparison on it says.
constexpr const char* kIdentifySource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    ivec2 size = textureSize(texture1, 0);
    float d = texture(texture1, TexCoord).r;
    float guard = 1.0;
    if (d <= 0.0) guard = 0.0;
    FragColor = vec4(float(size.x) / 255.0, d, d > 0.0 ? 1.0 : 0.0, guard);
}
)";

TEST(HalfFloatDepthMechanismTest, TheBindingAndTheComparisonAreBothSound)
{
    // Two things the answer above has to survive. The half-float texture really does reach the
    // shader -- `textureSize` names it, the two prepasses being different sizes on purpose -- and
    // the very same `<= 0.0` test, written the very same way, passes on every pixel when the rest
    // of the estimator is not around it. A silently-failed bind would look exactly like a format
    // that samples wrongly, and a comparison that failed here would be a simpler explanation than
    // the one this file arrived at.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    if (!CanBuildTheFailingShape(gd)) GTEST_SKIP() << "the failing shape cannot be built here";

    DepthNormalPrepass packed(gd, 128, 128, DepthEncoding::Packed);
    DepthNormalPrepass halfFloat(gd, 64, 64, DepthEncoding::HalfFloat);
    if (!packed.isSupported(gd) || !halfFloat.isSupported(gd))
        GTEST_SKIP() << "no prepass on this renderer";
    RunPrepass(gd, packed);
    RunPrepass(gd, halfFloat);

    ShaderEffect identify(gd, kVertexSource, kIdentifySource);
    ASSERT_TRUE(identify.IsEffectValid());

    const auto look = [&](const char* label, Texture2D* source, const int expectedWidth) {
        RenderTarget2D verdict(gd, 32, 32);
        FullscreenPass pass(gd);
        identify.Apply();
        pass.draw(source, &verdict, &identify, 32, 32);

        std::vector<Color> pixels(32 * 32, Color(0, 0, 0, 0));
        verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));
        int positive = 0, guarded = 0;
        for (const Color& texel : pixels)
        {
            if (texel.getBProperty() > 128) ++positive;
            if (texel.getAProperty() > 128) ++guarded;
        }
        std::printf("    %-12s shader sees a %d-wide texture | above zero %d/%d | "
                    "the same guard lets through %d/%d\n",
                    label, pixels[0].getRProperty(), positive, static_cast<int>(pixels.size()),
                    guarded, static_cast<int>(pixels.size()));
        EXPECT_EQ(pixels[0].getRProperty(), expectedWidth)
            << label << ": the shader is looking at a different texture than it was given";
        return positive;
    };

    look("packed 128", packed.getDepthTexture(), 128);
    const int halfPositive = look("half-float 64", halfFloat.getDepthTexture(), 64);
    look("packed again", packed.getDepthTexture(), 128);

    EXPECT_EQ(halfPositive, 32 * 32)
        << "the half-float image does hold non-positive depths, which would explain the early-out "
           "without any defect";
}

} // namespace

#endif // CNA_CNAEXT
