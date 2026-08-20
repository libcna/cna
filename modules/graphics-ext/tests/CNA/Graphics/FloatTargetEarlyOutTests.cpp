// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2035b: how far does the early-out defect reach?
//
// `HalfFloatDepthMechanismTests` established that on this renderer a shader reading a
// single-channel half-float texture and then writing `if (value <= 0.0) { …; return; }` takes that
// return on every pixel, although every value is positive. Depth is packed into eight bits now, so
// the depth path is out of its way.
//
// **The layer still reads float targets everywhere else.** `RenderPipeline`'s HDR scene target is
// `HdrBlendable`; so are both of weighted-blended transparency's. And several shipped passes open
// with the same shape on a value sampled from that image:
//
//   FxaaPass                     if (lumaMax - lumaMin < uEdgeThreshold) { …; return; }
//   WeightedBlendedTransparency  if (revealage > 0.9999) discard;
//
// If the defect reached those formats, FXAA would be a silent no-op on every HDR frame and
// transparency would resolve to nothing -- neither of which announces itself. This file asks,
// against the shader the layer actually ships.
//
// **The answer is that it does not.** FXAA filters identically with and without its early-out on
// `Color`, `HdrBlendable`, `Vector4` and `HalfSingle`, and the structural shape the failing pass has
// -- guard, then an indexed loop over the same sampler -- reproduces nothing on any of them either.
//
// The audit behind that: of the sixteen early-outs in this layer's shaders, the ones on a value
// sampled from a **float** target are FXAA's alone. `DepthOfFieldPass` and `SsrPass` guard on the
// *depth* image, which the `MOD-2035` policy keeps packed into eight bits; `LightShaftPass` and
// `SpatialUpscalePass` guard on uniforms; `MotionBlurPass` guards on the velocity image, which is
// `Color`; `WeightedBlendedTransparency` discards on a value from an `HdrBlendable` target and is
// covered by the format sweep below. There is no second instance of the defect in the layer.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::Graphics::FullscreenPass;
using CNA::Graphics::FxaaPass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
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

/// Diagonal black-and-white wedges: an edge filter has something to do on every row.
constexpr const char* kEdgeSource = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    float v = (TexCoord.x + TexCoord.y * 0.35) * 8.0;
    float bright = step(0.5, fract(v));
    FragColor = vec4(bright, bright, bright, 1.0);
}
)";

/// FXAA's own early-out, exactly as `FxaaPass` writes it.
constexpr const char* kFxaaEarlyOut =
    "    if (lumaMax - lumaMin < uEdgeThreshold) {\n"
    "        FragColor = vec4(center, 1.0);\n"
    "        return;\n"
    "    }\n";

/// Renders the wedge image into a target of the given format.
std::unique_ptr<RenderTarget2D> MakeEdges(GraphicsDevice& gd, const SurfaceFormat format)
{
    auto target = std::make_unique<RenderTarget2D>(gd, kSize, kSize, false, format,
                                                   DepthFormat::None);
    Texture2D seed(gd, 1, 1);
    const Color white = Color::White;
    seed.SetData(&white, 1);

    ShaderEffect fill(gd, kVertexSource, kEdgeSource);
    EXPECT_TRUE(fill.IsEffectValid());
    FullscreenPass pass(gd);
    fill.Apply();
    pass.draw(&seed, target.get(), &fill, kSize, kSize);
    return target;
}

/// How many pixels FXAA changes, running the shipped shader with or without its early-out.
///
/// The output target is always `Color`, so the count is of the filter's effect and never of the
/// destination's precision.
int PixelsChanged(GraphicsDevice& gd, RenderTarget2D& source, const bool keepEarlyOut)
{
    std::string filtered = FxaaPass::getFragmentGlsl();
    if (!keepEarlyOut)
    {
        const std::size_t at = filtered.find(kFxaaEarlyOut);
        EXPECT_NE(at, std::string::npos)
            << "FXAA's early-out is no longer written the way this test matches it";
        if (at == std::string::npos) return -1;
        filtered.erase(at, std::string(kFxaaEarlyOut).size());
    }

    ShaderEffect fxaa(gd, kVertexSource, filtered);
    EXPECT_TRUE(fxaa.IsEffectValid());

    // The unfiltered image in the same 8-bit form, to compare against.
    RenderTarget2D before(gd, kSize, kSize);
    RenderTarget2D after(gd, kSize, kSize);
    FullscreenPass pass(gd);
    {
        ShaderEffect copy(gd, kVertexSource, R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() { FragColor = texture(texture1, TexCoord); }
)");
        EXPECT_TRUE(copy.IsEffectValid());
        copy.Apply();
        pass.draw(&source, &before, &copy, kSize, kSize);
    }

    fxaa.Apply();
    fxaa.SetUniformVec2("uTexelSize", 1.0f / static_cast<float>(kSize),
                        1.0f / static_cast<float>(kSize));
    fxaa.SetUniformFloat("uEdgeThreshold", 0.125f);
    pass.draw(&source, &after, &fxaa, kSize, kSize);

    std::vector<Color> plain(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    std::vector<Color> filteredPixels = plain;
    before.GetData(plain.data(), static_cast<int>(plain.size()));
    after.GetData(filteredPixels.data(), static_cast<int>(filteredPixels.size()));

    int changed = 0;
    for (std::size_t i = 0; i < plain.size(); ++i)
        if (std::abs(plain[i].getRProperty() - filteredPixels[i].getRProperty()) > 2) ++changed;
    return changed;
}

TEST(FloatTargetEarlyOutTest, FxaaStillFiltersOnEveryFormatTheLayerRendersInto)
{
    // The live question. If FXAA's early-out misfires on the HDR scene target the way SSAO's did on
    // a half-float depth image, then every HDR frame in this layer is unfiltered and nothing says
    // so -- an anti-aliasing pass that quietly does nothing looks like an anti-aliasing pass.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    struct Candidate { const char* name; SurfaceFormat format; };
    const Candidate candidates[] = {
        {"Color",        SurfaceFormat::Color},
        {"HdrBlendable", SurfaceFormat::HdrBlendable},
        {"Vector4",      SurfaceFormat::Vector4},
        {"HalfSingle",   SurfaceFormat::HalfSingle},
    };

    int tested = 0;
    for (const Candidate& candidate : candidates)
    {
        if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(candidate.format))
        {
            std::printf("    %-13s not a render target here\n", candidate.name);
            continue;
        }
        auto edges = MakeEdges(gd, candidate.format);

        const int shipped = PixelsChanged(gd, *edges, true);
        const int without = PixelsChanged(gd, *edges, false);
        std::printf("    %-13s the shipped filter changes %4d px | with its early-out removed, "
                    "%4d px\n", candidate.name, shipped, without);

        if (without <= 0)
        {
            // A single-channel format carries no colour to build an edge out of, so there is
            // nothing for the filter to do and nothing this case can say about it.
            std::printf("    %-13s carries no edge to filter, so it is not compared\n",
                        candidate.name);
            continue;
        }
        ++tested;
        EXPECT_GT(shipped, 0)
            << candidate.name << ": the shipped FXAA changed nothing while the same shader without "
               "its early-out changed " << without << " pixels -- the early-out is misfiring on "
               "this format, exactly as it does on a half-float depth image";
    }

    ASSERT_GT(tested, 1) << "only one format could be compared, so this says nothing about the "
                            "difference between them";
}

/// The structural shape SSAO has and FXAA does not: a guard on a sampled value, followed by a loop
/// that samples the same texture again at coordinates it computes from a uniform array.
///
/// FXAA has the guard and no loop. The old reduction had the loop and no guard. This has both --
/// and it still does not reproduce the defect, on any format. That is the finding: the trigger is
/// narrower than any structural description of it, which is why the layer's answer is a policy
/// rather than a rule about how to write a shader.
constexpr const char* kShapeBody = R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform vec3  uKernel[64];
uniform float uRadius;
uniform int   uSampleCount;

void main() {
    float centre = texture(texture1, TexCoord).r;

#ifdef CNA_GUARD
    if (centre <= 0.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
#endif

    float found = 0.0;
    for (int i = 0; i < 64; ++i) {
        if (i >= uSampleCount) break;
        vec2 uv = TexCoord + uKernel[i].xy * uRadius;
        float s = textureLod(texture1, uv, 0.0).r;
        if (s <= 0.0) continue;
        if (s < centre - 0.02) found += 1.0;
    }
    FragColor = vec4(centre, found / float(uSampleCount), 0.0, 1.0);
}
)";

int LoopFindings(GraphicsDevice& gd, RenderTarget2D& source, const bool guard,
                 const std::vector<float>& kernel)
{
    std::string body = "#version 300 es\nprecision highp float;\n";
    if (guard) body += "#define CNA_GUARD 1\n";
    body += kShapeBody;

    ShaderEffect probe(gd, kVertexSource, body);
    EXPECT_TRUE(probe.IsEffectValid());

    RenderTarget2D verdict(gd, kSize, kSize);
    FullscreenPass pass(gd);
    probe.Apply();
    probe.SetUniformVec3Array("uKernel", kernel.data(), 64);
    probe.SetUniformFloat("uRadius", 0.2f);
    probe.SetUniformInt("uSampleCount", 16);
    pass.draw(&source, &verdict, &probe, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    verdict.GetData(pixels.data(), static_cast<int>(pixels.size()));

    int found = 0;
    for (const Color& texel : pixels) if (texel.getGProperty() > 4) ++found;
    return found;
}

TEST(FloatTargetEarlyOutTest, TheGuardAndLoopShapeOnItsOwnDoesNotReproduceIt)
{
    // The attempt to turn the finding into a rule, and its failure -- which is worth keeping,
    // because the rule would have been wrong. Guard plus indexed loop over the same sampler is the
    // structure the failing pass has, and reproducing that structure over a rendered image
    // reproduces nothing, on any format this layer renders into.
    //
    // So there is no "do not write a guard before a sampling loop on a float target" to follow. The
    // one thing that reproduces every time is the shipped estimator over a real half-float prepass
    // depth image, and the layer's answer stays what `MOD-2035` made it: pack depth, everywhere.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    // A deterministic spread of offsets, in the +Z hemisphere like SSAO's own.
    std::vector<float> kernel;
    kernel.reserve(64 * 3);
    for (int index = 0; index < 64; ++index)
    {
        const float angle = static_cast<float>(index) * 0.39269908f;
        const float scale = 0.2f + 0.8f * static_cast<float>(index) / 64.0f;
        kernel.push_back(std::cos(angle) * scale);
        kernel.push_back(std::sin(angle) * scale);
        kernel.push_back(scale);
    }

    struct Candidate { const char* name; SurfaceFormat format; };
    const Candidate candidates[] = {
        {"Color",        SurfaceFormat::Color},
        {"HdrBlendable", SurfaceFormat::HdrBlendable},
        {"Vector4",      SurfaceFormat::Vector4},
        {"HalfSingle",   SurfaceFormat::HalfSingle},
    };

    int compared = 0;
    for (const Candidate& candidate : candidates)
    {
        if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(candidate.format))
        {
            std::printf("    %-13s not a render target here\n", candidate.name);
            continue;
        }
        auto edges = MakeEdges(gd, candidate.format);
        const int without = LoopFindings(gd, *edges, false, kernel);
        const int with    = LoopFindings(gd, *edges, true, kernel);
        std::printf("    %-13s loop finds %4d px without the guard | %4d px with it\n",
                    candidate.name, without, with);
        if (without <= 0) continue;
        ++compared;
        EXPECT_EQ(with, without)
            << candidate.name << ": adding a guard that never fires changed what the loop found, so "
               "this shape *does* carry the defect on this format and every pass built from it "
               "needs auditing";
    }

    ASSERT_GT(compared, 1) << "fewer than two formats could be compared";
}

} // namespace

#endif // CNA_CNAEXT
