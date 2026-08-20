// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2035: does SSAO produce occlusion from a *real* prepass?
//
// Every SSAO test until now fed the pass synthetic 8-bit images built by hand. That covers the
// estimator and nothing else, and it is why `CNAEXT_Showcase`'s check E could sit at zero
// contribution without a single unit test noticing: the failure is not in the estimator, it is
// somewhere between a real `DepthNormalPrepass` and the pass reading it.
//
// The scene is the smallest one that must produce occlusion: a ground plane with a box standing on
// it. Where the box meets the ground there is a depth discontinuity, and a depth discontinuity is
// the only thing SSAO reacts to.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::FullscreenPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
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
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr const char* kCopyVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)";

constexpr int   kSize      = 128;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 60.0f;

Matrix View()
{
    return Matrix::CreateLookAt(Vector3(0.0f, 4.0f, 12.0f), Vector3::Zero, Vector3::Up);
}
Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, kNearPlane, kFarPlane);
}

std::array<VertexPositionNormalTexture, 6> Quad(const float y, const float halfExtent)
{
    const Vector3 up(0.0f, 1.0f, 0.0f);
    const float e = halfExtent;
    const auto vertex = [&](const float x, const float z) {
        return VertexPositionNormalTexture(Vector3(x, y, z), up, Vector2(0.0f, 0.0f));
    };
    return {vertex(-e, -e), vertex(e, -e), vertex(e, e),
            vertex(-e, -e), vertex(e, e),  vertex(-e, e)};
}

/// A slab standing on the ground, seen from in front: its base is a depth step against the floor.
std::array<VertexPositionNormalTexture, 6> UprightSlab()
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
    const auto ground = Quad(0.0f, 8.0f);
    const auto slab   = UprightSlab();

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

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

TEST(SsaoFromRealPrepassTest, ThePrepassWritesARangeOfDepthsRatherThanOneValue)
{
    // Before asking what SSAO does with the image, establish that the image describes a scene. A
    // depth buffer that is one value everywhere -- all far, all near, all cleared -- makes every
    // later question meaningless, and it is the first thing to rule out.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    if (!prepass.isSupported(gd)) GTEST_SKIP() << "no prepass on this renderer";
    RunPrepass(gd, prepass);

    // The depth target may be half-float, which GetData cannot read, so the depth is inspected
    // through a shader that copies it into a Color target instead.
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += DepthNormalPrepass::getDepthDecodeGlsl(prepass.isDepthPacked());
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    float d = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    FragColor = vec4(d, d, d, 1.0);
}
)";
    ShaderEffect copy(gd, R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() { gl_Position = projection * vec4(aPos, 0.0, 1.0); TexCoord = aTexCoord; }
)", source);
    ASSERT_TRUE(copy.IsEffectValid());

    RenderTarget2D readable(gd, kSize, kSize);
    copy.Apply();
    FullscreenPass fullscreen(gd);
    fullscreen.draw(prepass.getDepthTexture(), &readable, &copy, kSize, kSize);

    const std::vector<Color> depth = ReadTarget(readable);
    int lowest = 255, highest = 0, distinct = 0;
    std::vector<int> seen(256, 0);
    for (const Color& pixel : depth)
    {
        const int v = pixel.getRProperty();
        lowest = std::min(lowest, v);
        highest = std::max(highest, v);
        if (seen[v]++ == 0) ++distinct;
    }
    std::printf("[ prepass ] depth spans %d..%d over %d distinct values\n", lowest, highest, distinct);

    // And again with two extra sampler units bound first, which is what SsaoPass does before its
    // own draw. If the float source stops reading when other units are occupied, that interaction
    // is the fault rather than the format.
    {
        std::vector<Color> dummy(static_cast<std::size_t>(kSize) * kSize, Color(1, 2, 3, 255));
        auto extra = std::make_unique<Texture2D>(gd, kSize, kSize);
        extra->SetData(dummy.data(), static_cast<int>(dummy.size()));
        RenderTarget2D again(gd, kSize, kSize);
        copy.Apply();
        copy.SetUniformInt("uUnusedOne", 1);
        copy.SetTexture(1, *extra);
        copy.SetUniformInt("uUnusedTwo", 2);
        copy.SetTexture(2, *extra);
        FullscreenPass second(gd);
        second.draw(prepass.getDepthTexture(), &again, &copy, kSize, kSize);
        int low = 255, high = 0;
        for (const Color& p : ReadTarget(again))
        { low = std::min(low, static_cast<int>(p.getRProperty()));
          high = std::max(high, static_cast<int>(p.getRProperty())); }
        std::printf("[ prepass ] with units 1 and 2 also bound: depth spans %d..%d\n", low, high);
    }

    EXPECT_LT(lowest, 250) << "nothing was drawn: the whole buffer is at the far plane";
    EXPECT_GT(distinct, 8) << "the depth buffer holds no range of distances";
}

TEST(SsaoFromRealPrepassTest, TheSameStepOccludesFromATextureAndFromARenderTarget)
{
    // The bisection. The synthetic SSAO tests feed a `Texture2D`; a pipeline feeds a
    // `RenderTarget2D`. If the identical *values* occlude through one and not the other, the fault
    // is in how the pass reads a render target rather than in the estimator.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    // Near on the left, far on the right -- the step SsaoPassTests uses and knows to occlude.
    std::vector<Color> step;
    step.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const int v = x < kSize / 2 ? 60 : 200;
            step.emplace_back(v, v, v, 255);
        }
    auto asTexture = std::make_unique<Texture2D>(gd, kSize, kSize);
    asTexture->SetData(step.data(), static_cast<int>(step.size()));

    auto asTarget = std::make_unique<RenderTarget2D>(gd, kSize, kSize);
    { FullscreenPass blit(gd); blit.draw(asTexture.get(), asTarget.get(), nullptr, kSize, kSize); }

    std::vector<Color> facing(static_cast<std::size_t>(kSize) * kSize, Color(128, 128, 255, 255));
    auto normalTexture = std::make_unique<Texture2D>(gd, kSize, kSize);
    normalTexture->SetData(facing.data(), static_cast<int>(facing.size()));
    auto normalTarget = std::make_unique<RenderTarget2D>(gd, kSize, kSize);
    { FullscreenPass blit(gd); blit.draw(normalTexture.get(), normalTarget.get(), nullptr, kSize, kSize); }

    SsaoPass pass(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 200, 200, 255));
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setSSAORadius(0.25f);
    settings.setSSAOIntensity(2.0f);
    settings.setSSAOSampleCount(32);

    const auto darkenedWith = [&](Texture2D* depth, Texture2D* normals) {
        PostProcessContext context;
        context.source = &source; context.destination = &destination;
        context.width = kSize; context.height = kSize; context.settings = &settings;
        context.sourceDepth = depth; context.sourceNormals = normals;
        context.nearPlane = kNearPlane; context.farPlane = kFarPlane;
        pass.apply(context);
        int n = 0;
        for (const Color& p : ReadTarget(destination))
            if (p.getRProperty() < 196) ++n;
        return n;
    };

    const int fromTextures = darkenedWith(asTexture.get(), normalTexture.get());
    const int fromTargets  = darkenedWith(asTarget.get(), normalTarget.get());
    std::printf("[ bisect ] textures %d darkened, render targets %d darkened\n",
                fromTextures, fromTargets);

    EXPECT_GT(fromTextures, 0) << "the estimator does not occlude even from a plain texture";
    EXPECT_GT(fromTargets, 0)
        << "the identical values occlude from a texture and not from a render target";
}

TEST(SsaoFromRealPrepassTest, EveryDepthImageFormatOccludesTheSame)
{
    // plan_modern.md MOD-2035, rewritten when the answer arrived.
    //
    // This test was built to pin a bisection: SSAO appeared to produce no occlusion at all from a
    // half-float depth target while producing plenty from an 8-bit one, and the surface format was
    // the only variable that separated them. **That no longer reproduces.** The same four targets
    // now occlude within 12% of each other, and the difference that remains is the 8-bit target's
    // quantisation, which is the difference one would expect.
    //
    //   Color      + no depth attachment -> 2052 darkened
    //   Color      + Depth24             -> 2052
    //   HalfSingle + Depth24             -> 2304
    //   HalfSingle + no depth attachment -> 2304
    //
    // What check E of `CNAEXT_Showcase` was actually measuring was not a format at all: the example
    // drove the prepass with `drawScene()`, and that lambda calls `Apply()` on the scene's own
    // effects -- so every draw replaced the prepass program `begin()` had just selected, and the
    // "depth" target held the shaded frame's red channel. SSAO then compared shading against
    // shading, which yields a weak plausible term everywhere instead of occlusion at contacts.
    // Driving the prepass with the prepass effect takes check E from 2 strongly-occluded pixels to
    // 1 021, and the gate from 7/8 to 8/8.
    //
    // The test is kept, and now asserts what it once could not: that the format does **not** decide
    // this. It is the cheapest guard against the original symptom returning.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    if (!prepass.isSupported(gd)) GTEST_SKIP() << "no prepass on this renderer";
    RunPrepass(gd, prepass);

    SsaoPass pass(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 200, 200, 255));
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setSSAORadius(0.25f);
    settings.setSSAOIntensity(1.5f);
    settings.setSSAOSampleCount(32);

    const auto darkenedFrom = [&](Texture2D* depth) {
        PostProcessContext context;
        context.source = &source; context.destination = &destination;
        context.width = kSize; context.height = kSize; context.settings = &settings;
        context.sourceDepth = depth; context.sourceNormals = prepass.getNormalTexture();
        context.nearPlane = kNearPlane; context.farPlane = kFarPlane;
        pass.apply(context);
        int n = 0;
        for (const Color& p : ReadTarget(destination)) if (p.getRProperty() < 196) ++n;
        return n;
    };

    std::string copySource = "#version 300 es\nprecision highp float;\n";
    copySource += DepthNormalPrepass::getDepthDecodeGlsl(prepass.isDepthPacked());
    copySource += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() { float d = cnaDecodeLinearDepth(texture(texture1, TexCoord)); FragColor = vec4(d, d, d, 1.0); }
)";
    ShaderEffect copy(gd, kCopyVertexSource, copySource);
    ASSERT_TRUE(copy.IsEffectValid());

    RenderTarget2D eightBit(gd, kSize, kSize);
    copy.Apply();
    FullscreenPass fullscreen(gd);
    fullscreen.draw(prepass.getDepthTexture(), &eightBit, &copy, kSize, kSize);

    // The same copy into a target built exactly like the prepass's own -- half-float, with a depth
    // attachment. If this behaves like the prepass target rather than like the 8-bit one, the
    // variable is how the target was made, not which object it is.
    const bool floatTargets =
        gd.SupportsSurfaceFormatAsRenderTargetEXT(
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::HalfSingle);
    namespace G = Microsoft::Xna::Framework::Graphics;
    int fromLikeThePrepass = -1, fromFloatNoDepth = -1, fromColorWithDepth = -1;
    const auto copyInto = [&](RenderTarget2D& target) {
        copy.Apply();
        fullscreen.draw(prepass.getDepthTexture(), &target, &copy, kSize, kSize);
        return darkenedFrom(&target);
    };
    if (floatTargets)
    {
        RenderTarget2D sameShape(gd, kSize, kSize, false, G::SurfaceFormat::HalfSingle,
                                 G::DepthFormat::Depth24);
        fromLikeThePrepass = copyInto(sameShape);
        RenderTarget2D floatOnly(gd, kSize, kSize, false, G::SurfaceFormat::HalfSingle,
                                 G::DepthFormat::None);
        fromFloatNoDepth = copyInto(floatOnly);
    }
    {
        RenderTarget2D colorWithDepth(gd, kSize, kSize, false, G::SurfaceFormat::Color,
                                      G::DepthFormat::Depth24);
        fromColorWithDepth = copyInto(colorWithDepth);
    }

    const int fromEightBit  = darkenedFrom(&eightBit);
    const int fromPrepass   = darkenedFrom(prepass.getDepthTexture());
    // The order was never varied, and an order effect would look exactly like a texture effect.
    const int prepassFirst  = darkenedFrom(prepass.getDepthTexture());
    const int eightBitAfter = darkenedFrom(&eightBit);
    std::printf("[ MOD-2035 ] 8bit %d | prepass %d | float+depth %d | float+nodepth %d | "
                "color+depth %d\n",
                fromEightBit, fromPrepass, fromLikeThePrepass, fromFloatNoDepth, fromColorWithDepth);

    EXPECT_GT(fromEightBit, 0)
        << "the scene produces no occlusion even from an 8-bit depth image, so this test's premise "
        << "no longer holds and MOD-2035 needs re-establishing from the start";
    EXPECT_EQ(eightBitAfter, fromEightBit)
        << "the same 8-bit image gave a different answer depending on when it was measured, so the "
        << "comparison is confounded by order rather than by the format";
    EXPECT_GT(fromColorWithDepth, 0)
        << "a depth attachment on an otherwise working target broke it, which would move the "
        << "variable from the format to the attachment";
    EXPECT_GT(fromPrepass, 0)
        << "the prepass's own depth target occludes nothing, which is the original MOD-2035 symptom";
    if (floatTargets)
    {
        // The two assertions this test was originally unable to make.
        EXPECT_GT(fromLikeThePrepass, 0)
            << "a half-float depth target occludes nothing while an 8-bit one does, which is the "
            << "format-dependent behaviour MOD-2035 was opened for";
        EXPECT_GT(fromFloatNoDepth, 0);
        EXPECT_NEAR(static_cast<double>(fromLikeThePrepass), static_cast<double>(fromEightBit),
                    static_cast<double>(fromEightBit) * 0.25)
            << "the two formats no longer agree to within quantisation, so something has started "
            << "to depend on the depth image's format again";
    }
}

} // namespace

#endif // CNA_CNAEXT
