// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2073: the thin-film term, checked against what interference actually does.
//
// A thin film is the one shading term whose *correctness* is easy to state and easy to get subtly
// wrong: the colour has to move with thickness and with angle, and a film of no thickness has to be
// no film at all. A model that returned a fixed rainbow would look plausible in a screenshot and
// fail every test here.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/ThinFilmIridescence.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::FullscreenPass;
using CNA::Graphics::ThinFilmIridescence;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;

/// The dielectric base every test starts from: 4% reflectance, the same in all three channels, so
/// any colour in the result came from the film.
const Vector3 kDielectric(0.04f, 0.04f, 0.04f);

float Spread(const Vector3& v)
{
    const float highest = std::max(v.X, std::max(v.Y, v.Z));
    const float lowest = std::min(v.X, std::min(v.Y, v.Z));
    return highest - lowest;
}

TEST(ThinFilmIridescenceTest, AFilmOfNoThicknessIsNoFilmAtAll)
{
    // The property that makes the extension safe to leave switched on with a zero thickness: it has
    // to return the ordinary Fresnel reflectance of the base, not something near it.
    for (const float cosTheta : {1.0f, 0.7f, 0.3f})
    {
        const Vector3 film = ThinFilmIridescence::evaluate(1.0f, 1.3f, cosTheta, 0.0f, kDielectric);
        const float schlick = 0.04f + 0.96f * std::pow(1.0f - cosTheta, 5.0f);
        EXPECT_NEAR(film.X, schlick, 2e-3f) << "at N.V " << cosTheta;
        EXPECT_NEAR(film.Y, schlick, 2e-3f) << "at N.V " << cosTheta;
        EXPECT_NEAR(film.Z, schlick, 2e-3f) << "at N.V " << cosTheta;
        EXPECT_LT(Spread(film), 1e-3f) << "a film of no thickness produced a colour";
    }
}

TEST(ThinFilmIridescenceTest, AFilmOfSomeThicknessProducesAColour)
{
    // The base is grey, so every channel would agree if the film did nothing. Interference makes
    // them disagree, and that disagreement *is* the effect.
    const Vector3 film = ThinFilmIridescence::evaluate(1.0f, 1.3f, 1.0f, 400.0f, kDielectric);
    EXPECT_GT(Spread(film), 0.01f) << "a 400 nm film left the reflectance grey";
    EXPECT_GE(film.X, 0.0f);
    EXPECT_GE(film.Y, 0.0f);
    EXPECT_GE(film.Z, 0.0f);
}

TEST(ThinFilmIridescenceTest, TheColourMovesWithThickness)
{
    // The defining behaviour: which wavelengths cancel depends on how far the two reflections are
    // out of phase, so a different thickness is a different colour -- not a brighter one.
    std::vector<Vector3> colours;
    for (const float thickness : {200.0f, 300.0f, 400.0f, 500.0f, 600.0f})
        colours.push_back(ThinFilmIridescence::evaluate(1.0f, 1.3f, 1.0f, thickness, kDielectric));

    int distinct = 0;
    for (std::size_t i = 1; i < colours.size(); ++i)
    {
        const float dr = colours[i].X - colours[i - 1].X;
        const float dg = colours[i].Y - colours[i - 1].Y;
        const float db = colours[i].Z - colours[i - 1].Z;
        if (std::sqrt(dr * dr + dg * dg + db * db) > 5e-3f) ++distinct;
    }
    EXPECT_GE(distinct, 3) << "the colour barely changed across a 400 nm sweep of thickness";

    // And the *hue* moves, not just the brightness: at least one pair swaps which channel leads.
    bool swapped = false;
    for (std::size_t i = 1; i < colours.size(); ++i)
        if ((colours[i - 1].X > colours[i - 1].Z) != (colours[i].X > colours[i].Z)) swapped = true;
    EXPECT_TRUE(swapped) << "red never overtook blue, so the film is changing brightness, not hue";
}

TEST(ThinFilmIridescenceTest, TheColourMovesWithAngle)
{
    // The other half of what makes iridescence recognisable: it shifts as the surface turns,
    // because the path through the film lengthens.
    const Vector3 headOn = ThinFilmIridescence::evaluate(1.0f, 1.3f, 1.0f, 500.0f, kDielectric);
    const Vector3 tilted = ThinFilmIridescence::evaluate(1.0f, 1.3f, 0.5f, 500.0f, kDielectric);

    const float dr = tilted.X - headOn.X;
    const float dg = tilted.Y - headOn.Y;
    const float db = tilted.Z - headOn.Z;
    EXPECT_GT(std::sqrt(dr * dr + dg * dg + db * db), 1e-2f)
        << "the film's colour did not move when the surface turned";
}

TEST(ThinFilmIridescenceTest, TheReflectanceStaysPhysical)
{
    // Interference redistributes light between wavelengths; it does not create it. A term above 1
    // would make a surface brighter than the light falling on it.
    for (int thickness = 0; thickness <= 1200; thickness += 50)
        for (const float cosTheta : {1.0f, 0.8f, 0.5f, 0.2f, 0.05f})
        {
            const Vector3 film = ThinFilmIridescence::evaluate(
                1.0f, 1.3f, cosTheta, static_cast<float>(thickness), kDielectric);
            for (const float channel : {film.X, film.Y, film.Z})
            {
                ASSERT_GE(channel, 0.0f) << thickness << " nm at N.V " << cosTheta;
                ASSERT_LE(channel, 1.0f) << thickness << " nm at N.V " << cosTheta;
                ASSERT_EQ(channel, channel) << "NaN at " << thickness << " nm";
            }
        }
}

TEST(ThinFilmIridescenceTest, AMetallicBaseKeepsItsOwnColourUnderTheFilm)
{
    // The film modulates what is beneath it rather than replacing it, so a gold base has to stay
    // recognisably gold: its red must still lead its blue.
    const Vector3 gold(1.0f, 0.77f, 0.34f);
    for (const float thickness : {200.0f, 400.0f, 600.0f})
    {
        const Vector3 film = ThinFilmIridescence::evaluate(1.0f, 1.3f, 1.0f, thickness, gold);
        EXPECT_GT(film.X, film.Z) << "gold stopped looking like gold at " << thickness << " nm";
    }
}

// ── The shader against the CPU reference ─────────────────────────────────────

namespace {

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

/// Outputs the film's reflectance directly, scaled up so an 8-bit frame can carry a term whose
/// values live between 0.04 and 0.15 -- without the scale a whole channel would be six steps wide.
std::string MakeProbeSource()
{
    std::string source = "#version 300 es\nprecision highp float;\n";
    source += ThinFilmIridescence::getGlsl();
    source += R"(
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
uniform float uCosTheta;
uniform float uThickness;
uniform float uScale;
void main() {
    vec3 film = cnaThinFilmIridescence(1.0, 1.3, uCosTheta, uThickness, vec3(0.04));
    FragColor = vec4(clamp(film * uScale, 0.0, 1.0), 1.0);
}
)";
    return source;
}

}  // namespace

TEST(ThinFilmIridescenceTest, TheShaderMatchesTheCpuReference)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    ShaderEffect effect(gd, kVertexSource, MakeProbeSource());
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();

    constexpr int kSize = 8;
    constexpr float kScale = 3.0f;
    RenderTarget2D target(gd, kSize, kSize);
    FullscreenPass fullscreen(gd);
    Texture2D white(gd, 1, 1);
    const Color whitePixel = Color::White;
    white.SetData(&whitePixel, 1);

    // Not down to grazing: the scale that makes the term readable in eight bits also makes a
    // grazing sample clip at 1.0, and a comparison against a clipped value measures the clamp
    // rather than the model. The assertion below keeps that honest rather than assumed.
    for (const float cosTheta : {1.0f, 0.7f, 0.5f})
        for (const float thickness : {0.0f, 150.0f, 320.0f, 480.0f, 750.0f})
        {
            effect.Apply();
            effect.SetUniformFloat("uCosTheta", cosTheta);
            effect.SetUniformFloat("uThickness", thickness);
            effect.SetUniformFloat("uScale", kScale);

            gd.SetRenderTarget(&target);
            gd.Clear(Color::Black);
            fullscreen.drawOverCurrentTarget(&white, &effect, kSize, kSize);
            gd.SetRenderTarget(nullptr);

            std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            const Color measured = pixels[static_cast<std::size_t>(kSize) * kSize / 2];

            const Vector3 expected =
                ThinFilmIridescence::evaluate(1.0f, 1.3f, cosTheta, thickness, kDielectric);
            const float highest = std::max(expected.X, std::max(expected.Y, expected.Z));
            ASSERT_LT(highest * kScale, 0.95f)
                << "the probe saturates at " << thickness << " nm, N.V " << cosTheta
                << ", so this sample would be comparing against the clamp";

            const float tolerance = 3.0f / 255.0f / kScale;
            EXPECT_NEAR(static_cast<float>(measured.getRProperty()) / 255.0f / kScale, expected.X,
                        tolerance) << "red at " << thickness << " nm, N.V " << cosTheta;
            EXPECT_NEAR(static_cast<float>(measured.getGProperty()) / 255.0f / kScale, expected.Y,
                        tolerance) << "green at " << thickness << " nm, N.V " << cosTheta;
            EXPECT_NEAR(static_cast<float>(measured.getBProperty()) / 255.0f / kScale, expected.Z,
                        tolerance) << "blue at " << thickness << " nm, N.V " << cosTheta;
        }
}

} // namespace

#endif // CNA_CNAEXT
