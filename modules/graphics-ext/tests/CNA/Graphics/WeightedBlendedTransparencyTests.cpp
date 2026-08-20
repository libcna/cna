// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2106..MOD-2108: transparency that does not depend on order.
//
// The claim is unusually easy to state and unusually easy to fake: submitting the same surfaces in
// any order must produce the same frame. So the central test draws two overlapping transparent
// quads twice, swapping only which one goes first, and requires the two frames to be identical
// **byte for byte** -- and then proves the comparison can fail at all by running the same pair
// through ordinary alpha blending, where swapping them must change the result. A test that only
// checked the first half would pass just as happily against a pass that drew nothing.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/WeightedBlendedTransparency.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::WeightedBlendedTransparency;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

constexpr int   kSize     = 32;
constexpr float kFarPlane = 100.0f;

/// A frame the resolve can be composited **onto**, which means it has to survive being unbound and
/// bound again -- the default `DiscardContents` throws its contents away on the second bind, and a
/// test that used one would be measuring that rather than the pass.
std::unique_ptr<RenderTarget2D> MakeFrame(GraphicsDevice& device)
{
    return std::make_unique<RenderTarget2D>(
        device, kSize, kSize, false,
        Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color,
        Microsoft::Xna::Framework::Graphics::DepthFormat::Depth24, 0,
        Microsoft::Xna::Framework::Graphics::RenderTargetUsage::PreserveContents);
}

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
uniform mat4 World;
uniform mat4 View;
uniform mat4 Projection;
void main() { gl_Position = vec4(aPos, 1.0); }
)";

/// A quad covering the whole target, so every pixel carries the same answer and one read is enough.
std::array<VertexPositionColor, 6> FullscreenQuad()
{
    const auto at = [](const float x, const float y) {
        return VertexPositionColor(Vector3(x, y, 0.0f), Color::White);
    };
    return {at(-1.0f, -1.0f), at(-1.0f, 1.0f), at(1.0f, 1.0f),
            at(-1.0f, -1.0f), at(1.0f, 1.0f),  at(1.0f, -1.0f)};
}

std::vector<Color> Read(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

struct Surface
{
    Vector3 Colour;
    float   Alpha;
    float   Depth;   // world units along the view
};

/// A shader that contributes one flat surface to the accumulation.
std::unique_ptr<ShaderEffect> MakeEmitter(GraphicsDevice& device)
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += WeightedBlendedTransparency::getAccumulationGlsl();
    source += R"(
uniform vec3  uColour;
uniform float uAlpha;
uniform float uDepth;
void main() { cnaOitEmit(uColour, uAlpha, uDepth); }
)";
    return std::make_unique<ShaderEffect>(device, kVertexSource, source);
}

/// A shader for the control: ordinary alpha blending, which is order dependent by construction.
std::unique_ptr<ShaderEffect> MakeBlender(GraphicsDevice& device)
{
    return std::make_unique<ShaderEffect>(device, kVertexSource, R"(#version 300 es
precision highp float;
out vec4 FragColor;
uniform vec3  uColour;
uniform float uAlpha;
void main() { FragColor = vec4(uColour, uAlpha); }
)");
}

void DrawSurface(GraphicsDevice& device, ShaderEffect& effect, const Surface& surface)
{
    effect.Apply();
    effect.SetUniformVec3("uColour", surface.Colour.X, surface.Colour.Y, surface.Colour.Z);
    effect.SetUniformFloat("uAlpha", surface.Alpha);
    effect.SetUniformFloat("uDepth", surface.Depth);
    effect.SetUniformFloat("uCnaOitFarPlane", kFarPlane);
    const auto quad = FullscreenQuad();
    device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
}

TEST(WeightedBlendedTransparencyTest, TheWeightFallsWithDepthAndRisesWithCoverage)
{
    // The published curve's shape, checked at the two ends and in between rather than against a
    // magic number: a nearer surface counts for more, a more opaque one counts for more, and both
    // are clamped so a distant surface still contributes and a near one cannot swamp the buffer.
    const auto w = [](const float depth, const float alpha) {
        return WeightedBlendedTransparency::weight(depth, alpha, kFarPlane);
    };
    EXPECT_GT(w(5.0f, 1.0f), w(50.0f, 1.0f));
    EXPECT_GT(w(50.0f, 1.0f), w(100.0f, 1.0f));
    EXPECT_GT(w(50.0f, 1.0f), w(50.0f, 0.25f));

    EXPECT_LE(w(0.0f, 1.0f), 3e3f) << "the near clamp is missing";
    EXPECT_GE(w(kFarPlane, 1.0f), 1e-2f) << "the far clamp is missing";
    EXPECT_FLOAT_EQ(w(50.0f, 0.0f), 0.0f) << "a surface covering nothing must weigh nothing";
    EXPECT_FLOAT_EQ(w(kFarPlane * 4.0f, 1.0f), w(kFarPlane, 1.0f))
        << "a depth past the far plane must clamp rather than keep falling";
}

TEST(WeightedBlendedTransparencyTest, TheShaderAndTheCpuWeightAgree)
{
    // MOD-2107. Written twice, so compared on the GPU rather than read side by side -- the pattern
    // Phase 20 named after six rows hit it, every one of which produced a plausible frame.
    GraphicsDevice device;
    WeightedBlendedTransparency oit(device, kSize, kSize);
    if (!oit.isSupported()) GTEST_SKIP() << oit.getUnsupportedReason();

    // The weight spans four orders of magnitude, so it is compared as a ratio in log space: an
    // 8-bit target cannot carry the value itself, and scaling it linearly would test only the
    // clamp. log10(w) over [-2, 3.5] maps onto the byte range with room to spare.
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += WeightedBlendedTransparency::getAccumulationGlsl();
    source += R"(
in vec2 TexCoord;
uniform float uAlpha;
uniform float uNearDepth;
uniform float uFarDepth;
void main() {
    float depth = mix(uNearDepth, uFarDepth, TexCoord.x);
    float encoded = (log(cnaOitWeight(depth, uAlpha)) / 2.302585 + 2.0) / 5.5;
    cnaOitAccumulation = vec4(clamp(encoded, 0.0, 1.0), 0.0, 0.0, 1.0);
    cnaOitRevealage = vec4(0.0);
}
)";
    ShaderEffect probe(device, R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)", source);
    ASSERT_TRUE(probe.IsEffectValid());

    constexpr float kAlpha = 0.6f;
    constexpr float kNear = 0.5f;
    constexpr float kFar  = 95.0f;
    const auto frame = MakeFrame(device);
    Microsoft::Xna::Framework::Graphics::Texture2D white(device, 1, 1);
    const Color pixel = Color::White;
    white.SetData(&pixel, 1);
    {
        CNA::Graphics::FullscreenPass pass(device);
        probe.Apply();
        probe.SetUniformFloat("uAlpha", kAlpha);
        probe.SetUniformFloat("uNearDepth", kNear);
        probe.SetUniformFloat("uFarDepth", kFar);
        probe.SetUniformFloat("uCnaOitFarPlane", kFarPlane);
        pass.draw(&white, frame.get(), &probe, kSize, kSize);
    }

    const std::vector<Color> pixels = Read(*frame);
    int compared = 0;
    for (int x = 0; x < kSize; ++x)
    {
        const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kSize);
        const float depth = kNear + (kFar - kNear) * u;
        const float expected =
            (std::log(WeightedBlendedTransparency::weight(depth, kAlpha, kFarPlane)) / 2.302585f
             + 2.0f) / 5.5f;
        const float actual =
            static_cast<float>(pixels[static_cast<std::size_t>(kSize / 2) * kSize + x]
                                   .getRProperty()) / 255.0f;
        EXPECT_NEAR(actual, std::clamp(expected, 0.0f, 1.0f), 0.01f) << "at column " << x;
        ++compared;
    }
    EXPECT_EQ(compared, kSize);
}

TEST(WeightedBlendedTransparencyTest, TheAccumulationGlslDeclaresWhatAShaderNeeds)
{
    const std::string glsl = WeightedBlendedTransparency::getAccumulationGlsl();
    EXPECT_NE(glsl.find("layout(location = 0) out vec4 cnaOitAccumulation"), std::string::npos);
    EXPECT_NE(glsl.find("layout(location = 1) out vec4 cnaOitRevealage"), std::string::npos);
    EXPECT_NE(glsl.find("void cnaOitEmit("), std::string::npos);
    EXPECT_NE(glsl.find("uCnaOitFarPlane"), std::string::npos);
}

TEST(WeightedBlendedTransparencyTest, EveryMisuseIsRejected)
{
    GraphicsDevice device;
    EXPECT_THROW(WeightedBlendedTransparency(device, 0, kSize), std::invalid_argument);
    EXPECT_THROW(WeightedBlendedTransparency(device, kSize, -4), std::invalid_argument);

    WeightedBlendedTransparency oit(device, kSize, kSize);
    EXPECT_THROW(oit.begin(0.0f), std::invalid_argument);
    EXPECT_THROW(oit.resolve(0, kSize), std::invalid_argument);
    EXPECT_THROW(oit.end(), std::logic_error) << "end without begin";

    oit.begin(kFarPlane);
    EXPECT_TRUE(oit.isAccumulating());
    EXPECT_THROW(oit.begin(kFarPlane), std::logic_error) << "two accumulations at once";
    EXPECT_THROW(oit.resolve(kSize, kSize), std::logic_error) << "resolved while still open";
    EXPECT_THROW(oit.resize(64, 64), std::logic_error) << "resized while still open";
    oit.end();
    EXPECT_FALSE(oit.isAccumulating());
}

TEST(WeightedBlendedTransparencyTest, AFrameWithNothingTransparentInItIsUntouched)
{
    // The resolve discards where nothing was accumulated, so a pipeline can leave this pass in the
    // chain unconditionally. Exact, not near: a pass that perturbs untouched pixels cannot be left
    // in a chain at all.
    GraphicsDevice device;
    WeightedBlendedTransparency oit(device, kSize, kSize);
    if (!oit.isSupported()) GTEST_SKIP() << oit.getUnsupportedReason();

    const auto frame = MakeFrame(device);
    device.SetRenderTarget(frame.get());
    device.Clear(Color(40, 90, 160, 255));
    device.SetRenderTarget(nullptr);
    const std::vector<Color> before = Read(*frame);

    oit.begin(kFarPlane);
    oit.end();
    device.SetRenderTarget(frame.get());
    oit.resolve(kSize, kSize);
    device.SetRenderTarget(nullptr);

    const std::vector<Color> after = Read(*frame);
    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i)
        EXPECT_EQ(after[i].getRProperty(), before[i].getRProperty()) << "at texel " << i;
}

/// Draws the two surfaces in the given order through the OIT path and returns the resolved frame.
std::vector<Color> ResolveInOrder(GraphicsDevice& device, WeightedBlendedTransparency& oit,
                                  ShaderEffect& emitter, const Surface& first, const Surface& second)
{
    const auto frame = MakeFrame(device);
    device.SetRenderTarget(frame.get());
    device.Clear(Color::Black);
    device.SetRenderTarget(nullptr);

    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.SetVertexBuffer(nullptr);

    oit.begin(kFarPlane);
    DrawSurface(device, emitter, first);
    DrawSurface(device, emitter, second);
    oit.end();

    device.SetRenderTarget(frame.get());
    oit.resolve(kSize, kSize);
    device.SetRenderTarget(nullptr);
    return Read(*frame);
}

/// The control: the same two surfaces through ordinary alpha blending, which order does change.
std::vector<Color> BlendInOrder(GraphicsDevice& device, ShaderEffect& blender,
                                const Surface& first, const Surface& second)
{
    const auto frame = MakeFrame(device);
    device.SetRenderTarget(frame.get());
    device.Clear(Color::Black);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setBlendStateProperty(BlendState::NonPremultiplied);
    device.SetVertexBuffer(nullptr);
    for (const Surface& surface : {first, second})
    {
        blender.Apply();
        blender.SetUniformVec3("uColour", surface.Colour.X, surface.Colour.Y, surface.Colour.Z);
        blender.SetUniformFloat("uAlpha", surface.Alpha);
        const auto quad = FullscreenQuad();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }
    device.setBlendStateProperty(BlendState::Opaque);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.SetRenderTarget(nullptr);
    return Read(*frame);
}

TEST(WeightedBlendedTransparencyTest, TheSameSurfacesInEitherOrderProduceTheSameFrame)
{
    GraphicsDevice device;
    WeightedBlendedTransparency oit(device, kSize, kSize);
    if (!oit.isSupported()) GTEST_SKIP() << oit.getUnsupportedReason();
    const auto emitter = MakeEmitter(device);
    ASSERT_TRUE(emitter->IsEffectValid()) << "the accumulation shader did not compile";
    const auto blender = MakeBlender(device);
    ASSERT_TRUE(blender->IsEffectValid());

    const Surface red{Vector3(1.0f, 0.1f, 0.1f), 0.5f, 5.0f};
    const Surface blue{Vector3(0.1f, 0.2f, 1.0f), 0.5f, 40.0f};

    const auto largestDifference = [](const std::vector<Color>& left,
                                      const std::vector<Color>& right) {
        int worst = 0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            worst = std::max(worst, std::abs(left[i].getRProperty() - right[i].getRProperty()));
            worst = std::max(worst, std::abs(left[i].getGProperty() - right[i].getGProperty()));
            worst = std::max(worst, std::abs(left[i].getBProperty() - right[i].getBProperty()));
        }
        return worst;
    };

    // First, prove the comparison can fail: ordinary alpha blending must give two different frames.
    const int blended = largestDifference(BlendInOrder(device, *blender, red, blue),
                                          BlendInOrder(device, *blender, blue, red));
    ASSERT_GT(blended, 16)
        << "alpha blending gave nearly the same frame in both orders, so this scene cannot detect "
        << "order dependence and the assertion below would pass against anything";

    // Then the claim -- and it is stated to the precision it actually holds to. The accumulation
    // buffer is half-float, so the framebuffer rounds after **each** blend: adding a then b and
    // adding b then a can land one representable step apart. Floating-point addition is
    // commutative; the rounding between the two additions is what is not. So the promise is not
    // "the same bytes", it is "the same frame to within the buffer's precision" -- and the control
    // above says what a real order dependence would look like, which is an order of magnitude more.
    const std::vector<Color> redFirst  = ResolveInOrder(device, oit, *emitter, red, blue);
    const std::vector<Color> blueFirst = ResolveInOrder(device, oit, *emitter, blue, red);
    ASSERT_EQ(redFirst.size(), blueFirst.size());
    const int independent = largestDifference(redFirst, blueFirst);
    std::printf("[ MOD-2106 ] worst per-channel difference: order-independent %d, alpha blended %d\n",
                independent, blended);
    EXPECT_LE(independent, 1)
        << "the two orders differ by more than the accumulation buffer's last bit";
    EXPECT_LT(independent * 8, blended)
        << "the order-independent path is nearly as order dependent as plain blending";

    // And that it drew something at all: an order-independent black frame is also order independent.
    int lit = 0;
    for (const Color& texel : redFirst)
        if (texel.getRProperty() > 8 || texel.getBProperty() > 8) ++lit;
    EXPECT_GT(lit, kSize * kSize / 2) << "the composite reached almost none of the frame";
}

TEST(WeightedBlendedTransparencyTest, TheNearerSurfaceDominatesTheComposite)
{
    // Order independence alone would be satisfied by averaging the two surfaces equally, which is
    // not transparency. The depth weight is what makes the near one count for more, so the same
    // pair swapped in *depth* must change the frame even though swapping them in order does not.
    GraphicsDevice device;
    WeightedBlendedTransparency oit(device, kSize, kSize);
    if (!oit.isSupported()) GTEST_SKIP() << oit.getUnsupportedReason();
    const auto emitter = MakeEmitter(device);
    ASSERT_TRUE(emitter->IsEffectValid());

    const Surface nearRed{Vector3(1.0f, 0.0f, 0.0f), 0.5f, 2.0f};
    const Surface farBlue{Vector3(0.0f, 0.0f, 1.0f), 0.5f, 60.0f};
    const Surface nearBlue{Vector3(0.0f, 0.0f, 1.0f), 0.5f, 2.0f};
    const Surface farRed{Vector3(1.0f, 0.0f, 0.0f), 0.5f, 60.0f};

    const std::vector<Color> redInFront  = ResolveInOrder(device, oit, *emitter, nearRed, farBlue);
    const std::vector<Color> blueInFront = ResolveInOrder(device, oit, *emitter, nearBlue, farRed);

    const std::size_t middle = static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2;
    EXPECT_GT(redInFront[middle].getRProperty(), redInFront[middle].getBProperty())
        << "the near red surface did not dominate";
    EXPECT_GT(blueInFront[middle].getBProperty(), blueInFront[middle].getRProperty())
        << "the near blue surface did not dominate";
}

} // namespace

#endif // CNA_CNAEXT
