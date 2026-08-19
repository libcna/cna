// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2044: the light list as three textures, and a GPU round-trip that proves the
// bytes survive.
//
// The interesting risk here is not the C++ -- it is whether a float written as four bytes of an
// 8-bit texture comes back out of a sampler as the same float. Nothing on the CPU can answer that,
// so the tests that matter run a real shader, decode what was uploaded, compare it against
// uniforms holding the same numbers, and paint the result.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ClusteredLightAssignment.hpp"
#include "CNA/Graphics/ClusteredLightBuffer.hpp"
#include "CNA/Graphics/ClusteredLightGrid.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/ClusteredLightSetEXT.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::ClusteredLightAssignment;
using CNA::Graphics::ClusteredLightBuffer;
using CNA::Graphics::ClusteredLightGrid;
using CNA::Graphics::FullscreenPass;
using CNA::Graphics::ClusteredLightEXT;
using CNA::Graphics::ClusteredLightSetEXT;
using CNA::Graphics::ClusteredLightType;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 16;
constexpr float kNear = 0.5f;
constexpr float kFar  = 120.0f;

ClusteredLightGrid MakeGrid()
{
    ClusteredLightGrid grid;
    grid.setProjection(Matrix::CreatePerspectiveFieldOfView(1.0471975512f, 16.0f / 9.0f, kNear,
                                                            kFar),
                       kNear, kFar);
    return grid;
}

ClusteredLightEXT MakePoint(const Vector3& position, const float range)
{
    ClusteredLightEXT light;
    light.Type = ClusteredLightType::Point;
    light.Position = position;
    light.Range = range;
    return light;
}

ClusteredLightEXT MakeSpot(const Vector3& position, const Vector3& direction)
{
    ClusteredLightEXT light;
    light.Type = ClusteredLightType::Spot;
    light.Position = position;
    light.Direction = direction;
    light.Range = 12.0f;
    light.InnerAngle = 0.25f;
    light.OuterAngle = 0.5f;
    return light;
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

/// A shader that decodes one light and paints white when every field matches the uniforms holding
/// the same values, black otherwise. Whole-frame, so a single pixel read answers the question.
std::string MakeProbeSource()
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += ClusteredLightBuffer::getLightLookupGlsl();
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform int   uProbeIndex;
uniform vec3  uProbePosition;
uniform float uProbeRange;
uniform vec3  uProbeColour;
uniform float uProbeIsSpot;
uniform vec3  uProbeDirection;
uniform float uProbeCosOuter;

bool cnaClose(float a, float b) { return abs(a - b) < 1e-5; }
bool cnaClose3(vec3 a, vec3 b) {
    return cnaClose(a.x, b.x) && cnaClose(a.y, b.y) && cnaClose(a.z, b.z);
}

void main() {
    CnaClusteredLight light = cnaLoadLight(uProbeIndex);
    bool ok = cnaClose3(light.position, uProbePosition)
           && cnaClose(light.range, uProbeRange)
           && cnaClose3(light.colour, uProbeColour)
           && cnaClose(light.isSpot, uProbeIsSpot)
           && cnaClose3(light.direction, uProbeDirection)
           && cnaClose(light.cosOuter, uProbeCosOuter);
    FragColor = ok ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
)";
    return source;
}

/// A shader that paints white when the cluster's light list matches the one handed in as uniforms.
std::string MakeListSource()
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += ClusteredLightBuffer::getLightLookupGlsl();
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform int uProbeCluster;
uniform int uProbeCount;
uniform int uProbeFirst;
uniform int uProbeLast;

void main() {
    int count = cnaClusterLightCount(uProbeCluster);
    bool ok = count == uProbeCount;
    if (ok && count > 0) {
        ok = ok && cnaClusterLightIndex(uProbeCluster, 0) == uProbeFirst;
        ok = ok && cnaClusterLightIndex(uProbeCluster, count - 1) == uProbeLast;
    }
    FragColor = ok ? vec4(1.0) : vec4(0.0, 0.0, 0.0, 1.0);
}
)";
    return source;
}

Color RenderProbe(GraphicsDevice& gd, ShaderEffect& effect, const ClusteredLightBuffer& buffer)
{
    RenderTarget2D target(gd, kSize, kSize);
    FullscreenPass fullscreen(gd);

    // FullscreenPass draws a textured quad, so it needs something to sample even when the shader
    // ignores it entirely.
    Texture2D white(gd, 1, 1);
    const Color whitePixel = Color::White;
    white.SetData(&whitePixel, 1);

    effect.Apply();
    buffer.bind(effect, 1);

    gd.SetRenderTarget(&target);
    gd.Clear(Color(40, 0, 0, 255));
    fullscreen.drawOverCurrentTarget(&white, &effect, kSize, kSize);
    gd.SetRenderTarget(nullptr);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels[static_cast<std::size_t>(kSize) * kSize / 2 + kSize / 2];
}

// ── The CPU side ─────────────────────────────────────────────────────────────

TEST(ClusteredLightBufferTest, NothingIsBoundBeforeAnUpload)
{
    GraphicsDevice gd;
    ClusteredLightBuffer buffer(gd);
    EXPECT_FALSE(buffer.isUploaded());
    EXPECT_EQ(buffer.getLightCount(), 0);

    ShaderEffect effect(gd, kVertexSource, MakeProbeSource());
    EXPECT_THROW(buffer.bind(effect, 1), std::runtime_error);
}

TEST(ClusteredLightBufferTest, AMismatchedTrioIsRefused)
{
    // The three inputs describe one frame between them, and nothing in their types says so. An
    // assignment made from a different light set would light the wrong objects with the wrong
    // lamps and never fail, so the count agreement is checked where they meet.
    GraphicsDevice gd;
    ClusteredLightBuffer buffer(gd);
    const ClusteredLightGrid grid = MakeGrid();

    ClusteredLightSetEXT twoLights;
    twoLights.add(MakePoint(Vector3(0.0f, 0.0f, -10.0f), 5.0f));
    twoLights.add(MakePoint(Vector3(3.0f, 0.0f, -10.0f), 5.0f));

    ClusteredLightSetEXT oneLight;
    oneLight.add(MakePoint(Vector3(0.0f, 0.0f, -10.0f), 5.0f));

    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(), oneLight.collectBounds());
    EXPECT_THROW(buffer.upload(twoLights, grid, assignment), std::invalid_argument);

    const ClusteredLightGrid otherGrid(8, 4, 6);
    ClusteredLightGrid usableOtherGrid = otherGrid;
    usableOtherGrid.setProjection(Matrix::CreatePerspectiveFieldOfView(1.0f, 1.0f, kNear, kFar),
                                  kNear, kFar);
    EXPECT_THROW(buffer.upload(oneLight, usableOtherGrid, assignment), std::invalid_argument);
}

TEST(ClusteredLightBufferTest, TheCountsSurviveTheUpload)
{
    GraphicsDevice gd;
    ClusteredLightBuffer buffer(gd);
    const ClusteredLightGrid grid = MakeGrid();

    ClusteredLightSetEXT lights;
    lights.add(MakePoint(Vector3(0.0f, 0.0f, -10.0f), 6.0f));
    lights.add(MakeSpot(Vector3(2.0f, 1.0f, -20.0f), Vector3(0.0f, 0.0f, -1.0f)));

    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(), lights.collectBounds());

    buffer.upload(lights, grid, assignment);
    EXPECT_TRUE(buffer.isUploaded());
    EXPECT_EQ(buffer.getLightCount(), 2);
    EXPECT_EQ(buffer.getClusterCount(), grid.getClusterCount());
    EXPECT_EQ(buffer.getReferenceCount(), assignment.getTotalReferenceCount());
    EXPECT_GT(buffer.getReferenceCount(), 0) << "two lights in the frustum reached no cluster";
}

TEST(ClusteredLightBufferTest, AnEmptySetUploadsWithoutAZeroSizedTexture)
{
    // A frame with no lights is ordinary, and a zero-by-zero texture is not creatable. The buffer
    // has to round up to one row rather than refuse the frame.
    GraphicsDevice gd;
    ClusteredLightBuffer buffer(gd);
    const ClusteredLightGrid grid = MakeGrid();
    const ClusteredLightSetEXT lights;

    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(), lights.collectBounds());
    EXPECT_NO_THROW(buffer.upload(lights, grid, assignment));
    EXPECT_EQ(buffer.getLightCount(), 0);
    EXPECT_EQ(buffer.getReferenceCount(), 0);
}

// ── The GPU round-trip ───────────────────────────────────────────────────────

TEST(ClusteredLightBufferTest, TheShaderReadsBackEveryFieldOfEveryLight)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    ShaderEffect effect(gd, kVertexSource, MakeProbeSource());
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightSetEXT lights;

    // Values chosen to be awkward rather than round: negatives, a large magnitude, a small one,
    // and an intensity that has to be folded into the colour on the way in.
    ClusteredLightEXT first = MakePoint(Vector3(-3.25f, 1234.5f, -0.0009765625f), 17.5f);
    first.Color = Vector3(0.125f, 0.75f, 0.5f);
    first.Intensity = 2.5f;
    lights.add(first);

    ClusteredLightEXT second = MakeSpot(Vector3(7.5f, -2.25f, -30.0f), Vector3(0.0f, 0.0f, -1.0f));
    second.Color = Vector3(1.0f, 0.25f, 0.0625f);
    second.Intensity = 0.5f;
    lights.add(second);

    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(), lights.collectBounds());
    ClusteredLightBuffer buffer(gd);
    buffer.upload(lights, grid, assignment);

    for (int index = 0; index < lights.getCount(); ++index)
    {
        const ClusteredLightEXT& light = lights.getAt(index);
        effect.Apply();
        effect.SetUniformInt("uProbeIndex", index);
        effect.SetUniformVec3("uProbePosition", light.Position.X, light.Position.Y,
                              light.Position.Z);
        effect.SetUniformFloat("uProbeRange", light.Range);
        effect.SetUniformVec3("uProbeColour", light.Color.X * light.Intensity,
                              light.Color.Y * light.Intensity, light.Color.Z * light.Intensity);
        effect.SetUniformFloat("uProbeIsSpot",
                               light.Type == ClusteredLightType::Spot ? 1.0f : 0.0f);
        effect.SetUniformVec3("uProbeDirection", light.Direction.X, light.Direction.Y,
                              light.Direction.Z);
        effect.SetUniformFloat("uProbeCosOuter", std::cos(light.OuterAngle));

        const Color result = RenderProbe(gd, effect, buffer);
        EXPECT_GT(static_cast<int>(result.getRProperty()), 200)
            << "light " << index << " did not survive the byte encoding";
    }
}

TEST(ClusteredLightBufferTest, TheShaderDisagreesWhenItShould)
{
    // The probe shader paints white on agreement, so a test that only ever asks for agreement
    // cannot tell "it matched" from "the shader always paints white". This asks for the wrong
    // answer on purpose.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    ShaderEffect effect(gd, kVertexSource, MakeProbeSource());
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightSetEXT lights;
    lights.add(MakePoint(Vector3(1.0f, 2.0f, -10.0f), 5.0f));

    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(), lights.collectBounds());
    ClusteredLightBuffer buffer(gd);
    buffer.upload(lights, grid, assignment);

    effect.Apply();
    effect.SetUniformInt("uProbeIndex", 0);
    effect.SetUniformVec3("uProbePosition", 1.0f, 2.0f, -10.5f);   // wrong by half a unit
    effect.SetUniformFloat("uProbeRange", 5.0f);
    effect.SetUniformVec3("uProbeColour", 1.0f, 1.0f, 1.0f);
    effect.SetUniformFloat("uProbeIsSpot", 0.0f);
    effect.SetUniformVec3("uProbeDirection", 0.0f, -1.0f, 0.0f);
    effect.SetUniformFloat("uProbeCosOuter", std::cos(0.5f));

    const Color result = RenderProbe(gd, effect, buffer);
    EXPECT_LT(static_cast<int>(result.getRProperty()), 50)
        << "the probe agreed with a light it was given the wrong position for";
}

TEST(ClusteredLightBufferTest, TheShaderWalksTheSameClusterListTheCpuBuilt)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    ShaderEffect effect(gd, kVertexSource, MakeListSource());
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    const ClusteredLightGrid grid = MakeGrid();
    ClusteredLightSetEXT lights;
    for (int i = 0; i < 8; ++i)
        lights.add(MakePoint(Vector3(static_cast<float>(i) * 2.0f - 7.0f, 0.0f, -14.0f), 6.0f));

    ClusteredLightAssignment assignment;
    assignment.assign(grid, Matrix::getIdentityProperty(), lights.collectBounds());
    ClusteredLightBuffer buffer(gd);
    buffer.upload(lights, grid, assignment);

    // Three clusters that between them cover a busy one, an empty one, and the very last index --
    // the last is where an off-by-one in the offset arithmetic surfaces.
    int busiest = 0;
    int empty = -1;
    for (int cluster = 0; cluster < grid.getClusterCount(); ++cluster)
    {
        const std::size_t size = assignment.lightsInCluster(cluster).size();
        if (size > assignment.lightsInCluster(busiest).size()) busiest = cluster;
        if (size == 0 && empty < 0) empty = cluster;
    }
    ASSERT_GT(assignment.lightsInCluster(busiest).size(), 1u) << "no cluster holds several lights";
    ASSERT_GE(empty, 0) << "every cluster holds a light, so the empty case is untested";

    for (const int cluster : {busiest, empty, grid.getClusterCount() - 1})
    {
        const std::span<const int> list = assignment.lightsInCluster(cluster);
        effect.Apply();
        effect.SetUniformInt("uProbeCluster", cluster);
        effect.SetUniformInt("uProbeCount", static_cast<int>(list.size()));
        effect.SetUniformInt("uProbeFirst", list.empty() ? -1 : list.front());
        effect.SetUniformInt("uProbeLast", list.empty() ? -1 : list.back());

        const Color result = RenderProbe(gd, effect, buffer);
        EXPECT_GT(static_cast<int>(result.getRProperty()), 200)
            << "cluster " << cluster << " reads back differently on the GPU";
    }
}

} // namespace

#endif // CNA_CNAEXT
