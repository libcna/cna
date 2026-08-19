// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2009: does the view-position reconstruction agree with the camera the prepass
// was driven with?
//
// `cnaViewPositionFromDepth` has shipped since MOD-503 and has never been exercised end to end.
// `SsaoPass`, its only consumer until now, works in UV and depth alone and never reconstructs a
// position, and no test drove real geometry through `DepthNormalPrepass` at all -- every prepass
// test so far checks sizes, formats, misuse and the clear. So the one question that matters for
// every screen-space effect after SSAO has been open the whole time: the prepass writes its depth
// into a *render target*, sampling a render target is vertically flipped (MOD-2000), and the
// reconstruction maps a UV straight through the camera's own NDC. If those disagree, every
// reconstructed position is mirrored and every such effect is subtly wrong in a way that looks
// plausible.
//
// The test answers it by construction rather than by inspection: a quad is drawn in one known
// quadrant of the view, and the shader asks, at each pixel the prepass says the quad occupies,
// whether the reconstructed position is in that same quadrant. Both halves are read in shader
// space, so the answer does not depend on knowing which way round any render target's rows run.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::FullscreenPass;
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
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kSize      = 64;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 100.0f;
constexpr float kQuadZ     = -20.0f;   // in front of the camera
constexpr float kQuadLow   = 2.0f;     // the quadrant: x and y both well clear of the axis
constexpr float kQuadHigh  = 7.0f;

Matrix View() { return Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f), Vector3::Up); }
Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(0.7853982f, 1.0f, kNearPlane, kFarPlane);
}

/// A quad facing the camera, in the +X +Y quadrant of the view.
std::array<VertexPositionNormalTexture, 6> QuadInPositiveQuadrant()
{
    const Vector3 facing(0.0f, 0.0f, 1.0f);
    const auto vertex = [&](const float x, const float y, const float u, const float v) {
        return VertexPositionNormalTexture(Vector3(x, y, kQuadZ), facing, Vector2(u, v));
    };
    return {
        vertex(kQuadLow, kQuadLow, 0.0f, 0.0f),
        vertex(kQuadHigh, kQuadLow, 1.0f, 0.0f),
        vertex(kQuadHigh, kQuadHigh, 1.0f, 1.0f),
        vertex(kQuadLow, kQuadLow, 0.0f, 0.0f),
        vertex(kQuadHigh, kQuadHigh, 1.0f, 1.0f),
        vertex(kQuadLow, kQuadHigh, 0.0f, 1.0f),
    };
}

void RunPrepassOverTheQuad(GraphicsDevice& device, DepthNormalPrepass& prepass)
{
    const auto quad = QuadInPositiveQuadrant();
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetVertexBuffer(nullptr);

    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
    {
        prepass.begin(pass, View(), Projection(), kNearPlane, kFarPlane);
        // begin() applies both prepass programs to set their uniforms, so the one left current is
        // the skinned one. A caller drawing rigid geometry has to select its own, which is what the
        // documented usage does and what this reproduces.
        ShaderEffect* effect = prepass.getPrepassEffect();
        ASSERT_NE(effect, nullptr);
        effect->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        prepass.end();
    }
}

constexpr const char* kVertexSource = R"(#version 300 es
precision highp float;
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;
out vec2 TexCoord;
uniform mat4 projection;
void main() {
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)";

/// Classifies every pixel the prepass says the quad occupies.
///
/// R: the reconstruction put it in the +X +Y quadrant, where the quad was drawn.
/// G: the reconstruction put it somewhere else -- the failure this test exists to detect.
/// B: the pixel is not the quad at all, and is ignored.
std::string MakeVerdictSource(const bool packed)
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += DepthNormalPrepass::getDepthDecodeGlsl(packed);
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform mat4  uInverseProjection;
uniform float uQuadDepth;

void main() {
    float d = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    if (abs(d - uQuadDepth) > 0.02) {
        FragColor = vec4(0.0, 0.0, 1.0, 1.0);
        return;
    }
    vec3 p = cnaViewPositionFromDepth(TexCoord, d, uInverseProjection);
    bool agrees = p.x > 0.0 && p.y > 0.0;
    FragColor = vec4(agrees ? 1.0 : 0.0, agrees ? 0.0 : 1.0, 0.0, 1.0);
}
)";
    return source;
}

TEST(PrepassReconstructionTest, TheReconstructedPositionIsWhereTheGeometryWasDrawn)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    if (!prepass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the depth/normal prepass";
    RunPrepassOverTheQuad(gd, prepass);

    ShaderEffect verdict(gd, kVertexSource, MakeVerdictSource(prepass.isDepthPacked()));
    ASSERT_TRUE(verdict.IsEffectValid());

    RenderTarget2D destination(gd, kSize, kSize);
    const Matrix inverseProjection = Matrix::Invert(Projection());
    verdict.Apply();
    verdict.SetUniformMat4("uInverseProjection", &inverseProjection.M11);
    verdict.SetUniformFloat("uQuadDepth", -kQuadZ / kFarPlane);

    FullscreenPass fullscreen(gd);
    fullscreen.draw(prepass.getDepthTexture(), &destination, &verdict, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));

    int agreed = 0;
    int disagreed = 0;
    for (const Color& pixel : pixels)
    {
        if (pixel.getRProperty() > 128) ++agreed;
        if (pixel.getGProperty() > 128) ++disagreed;
    }

    // Anti-vacuity first: a test that found no quad at all would report perfect agreement.
    ASSERT_GT(agreed + disagreed, 100)
        << "the prepass wrote no depth for the quad, so nothing was actually compared";
    EXPECT_EQ(disagreed, 0)
        << agreed << " pixels agreed and " << disagreed << " did not: the reconstruction does not "
        << "put the geometry where the camera put it";
}

TEST(PrepassReconstructionTest, TheUnwrittenSkyReadsAsTheFarPlaneAndNotAsTheEye)
{
    // The clear convention, restated as the number a consumer actually sees. `DepthNormalPrepass`
    // clears depth to **white**, so an empty pixel decodes to 1.0 -- the far plane. A pass that
    // tests for "nothing here" by comparing against zero therefore treats the sky as a surface at
    // the eye and marches from it. `SsrPass` had exactly that bug until this test was written.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    DepthNormalPrepass prepass(gd, kSize, kSize);
    if (!prepass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the depth/normal prepass";
    RunPrepassOverTheQuad(gd, prepass);

    std::string source = "#version 300 es\nprecision highp float;\n";
    source += DepthNormalPrepass::getDepthDecodeGlsl(prepass.isDepthPacked());
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() {
    float d = cnaDecodeLinearDepth(texture(texture1, TexCoord));
    // R: at the far plane. G: at the eye. Nothing the prepass writes should ever be the second.
    FragColor = vec4(d >= 0.99 ? 1.0 : 0.0, d <= 0.01 ? 1.0 : 0.0, 0.0, 1.0);
}
)";
    ShaderEffect verdict(gd, kVertexSource, source);
    ASSERT_TRUE(verdict.IsEffectValid());

    RenderTarget2D destination(gd, kSize, kSize);
    verdict.Apply();
    FullscreenPass fullscreen(gd);
    fullscreen.draw(prepass.getDepthTexture(), &destination, &verdict, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));

    int atFarPlane = 0;
    int atTheEye   = 0;
    for (const Color& pixel : pixels)
    {
        if (pixel.getRProperty() > 128) ++atFarPlane;
        if (pixel.getGProperty() > 128) ++atTheEye;
    }

    EXPECT_GT(atFarPlane, 1000) << "the empty part of the frame did not read as the far plane";
    EXPECT_EQ(atTheEye, 0) << "some part of the frame reads as a surface at the camera";
}

} // namespace

#endif // CNA_CNAEXT
