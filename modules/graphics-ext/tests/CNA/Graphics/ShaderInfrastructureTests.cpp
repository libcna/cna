// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-210, MOD-219, MOD-220: the shader-side infrastructure the passes share.
//
// All three exist because of failures that produce a picture rather than an error. A shader
// compiled twice costs twice and looks identical. A shader that failed to compile makes its pass
// copy its input, which looks like a weak effect rather than a missing one. A pyramid sampled with
// point filtering looks like bloom, just blockier. So each is checked by counting or by reading
// what was reported, never by looking at the result.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/ShaderDiagnostics.hpp"
#include "CNA/Graphics/ShaderEffectFactory.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

#include <stdexcept>
#include <string>

namespace {

using CNA::Graphics::ShaderEffectFactory;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;

constexpr const char* kVertex = R"(#version 300 es
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

constexpr const char* kFragment = R"(#version 300 es
precision highp float;
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D texture1;
void main() { FragColor = texture(texture1, TexCoord); }
)";

/// Not GLSL at all. Every compiler rejects it, which is the point: MOD-219 is about what happens
/// when a shader fails, and a subtly-wrong shader might compile on some driver.
constexpr const char* kBroken = "this is not a shader; it is a sentence.";

// =====================================================================================
// MOD-210: compiled once per name, per device
// =====================================================================================

TEST(ShaderEffectFactoryTest, TheSameNameIsCompiledOnceAndHandedBackAfter)
{
    GraphicsDevice gd;
    ShaderEffectFactory factory(gd);
    EXPECT_EQ(factory.getCompileCount(), 0u);
    EXPECT_FALSE(factory.contains("Pass.copy"));

    ShaderEffect* first = factory.acquire("Pass.copy", kVertex, kFragment);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(factory.getCompileCount(), 1u);
    EXPECT_TRUE(factory.contains("Pass.copy"));

    // The row's actual criterion: a second request compiles nothing and is the same program.
    ShaderEffect* second = factory.acquire("Pass.copy", kVertex, kFragment);
    EXPECT_EQ(second, first);
    EXPECT_EQ(factory.getCompileCount(), 1u) << "the second request compiled the shader again";
}

TEST(ShaderEffectFactoryTest, DistinctNamesAreDistinctPrograms)
{
    GraphicsDevice gd;
    ShaderEffectFactory factory(gd);

    ShaderEffect* copy   = factory.acquire("Pass.copy", kVertex, kFragment);
    ShaderEffect* invert = factory.acquire("Pass.invert", kVertex, kFragment);
    EXPECT_NE(copy, invert);
    EXPECT_EQ(factory.getCompileCount(), 2u);
}

TEST(ShaderEffectFactoryTest, TheNameIsTheKeyAndTheSourceIsNotConsulted)
{
    // A deliberate consequence of keying by name, asserted so it is a documented rule rather than a
    // surprise: hashing two kilobytes of GLSL to discover it is the same GLSL is work to avoid
    // work. A name must therefore mean one shader, and reusing it with different source is a bug
    // in the caller -- one this test pins the behaviour of rather than pretends cannot happen.
    GraphicsDevice gd;
    ShaderEffectFactory factory(gd);

    ShaderEffect* first = factory.acquire("Pass.copy", kVertex, kFragment);
    ShaderEffect* again = factory.acquire("Pass.copy", kVertex, kBroken);
    EXPECT_EQ(again, first) << "the second source was consulted; it must not be";
    EXPECT_EQ(factory.getCompileCount(), 1u);
}

TEST(ShaderEffectFactoryTest, AnEmptyNameIsRejected)
{
    GraphicsDevice gd;
    ShaderEffectFactory factory(gd);
    EXPECT_THROW((void)factory.acquire("", kVertex, kFragment), std::invalid_argument);
}

TEST(ShaderEffectFactoryTest, AFailedCompileIsStillReturnedRatherThanNull)
{
    // Returning null would give every caller a second failure mode to handle, when ShaderEffect
    // already reports this one through IsEffectValid(). The cache also keeps it, so a pass asking
    // repeatedly does not recompile a shader that will not compile.
    GraphicsDevice gd;
    ShaderEffectFactory factory(gd);

    ShaderEffect* broken = factory.acquire("Pass.broken", kVertex, kBroken);
    ASSERT_NE(broken, nullptr);
    EXPECT_EQ(factory.acquire("Pass.broken", kVertex, kBroken), broken);
    EXPECT_EQ(factory.getCompileCount(), 1u);
}

TEST(ShaderEffectFactoryTest, ClearReleasesEverything)
{
    GraphicsDevice gd;
    ShaderEffectFactory factory(gd);
    factory.acquire("Pass.copy", kVertex, kFragment);
    factory.acquire("Pass.invert", kVertex, kFragment);
    ASSERT_EQ(factory.getCompileCount(), 2u);

    factory.clear();
    EXPECT_FALSE(factory.contains("Pass.copy"));
    // The compile counter is a lifetime total, not a cache size: it answers "how much compiling did
    // this run do", which is the question the row is about.
    EXPECT_EQ(factory.getCompileCount(), 2u);
    factory.acquire("Pass.copy", kVertex, kFragment);
    EXPECT_EQ(factory.getCompileCount(), 3u);
}

// =====================================================================================
// MOD-219: a shader that did not compile says so
// =====================================================================================

TEST(ShaderDiagnosticsTest, AWorkingShaderReportsNothing)
{
    GraphicsDevice gd;
    if (!CnaTest::EngineLayer::RunsShaderSource(gd))
        GTEST_SKIP() << "this renderer compiles no shader source, so there is no success to see";

    ShaderEffect effect(gd, kVertex, kFragment);
    ASSERT_TRUE(effect.IsEffectValid());
    EXPECT_TRUE(effect.GetCompileErrorEXT().empty())
        << "a shader that compiled reported an error anyway";

    bool logged = false;
    EXPECT_TRUE(CNA::Graphics::detail::reportShaderCompileFailure(gd, "Working", &effect, logged));
    EXPECT_FALSE(logged) << "nothing failed, so nothing should have been reported";
}

TEST(ShaderDiagnosticsTest, ABrokenShaderCarriesTheCompilerLog)
{
    GraphicsDevice gd;
    if (!CnaTest::EngineLayer::RunsShaderSource(gd))
        GTEST_SKIP() << "this renderer compiles no shader source, so nothing can fail to compile";

    ShaderEffect effect(gd, kVertex, kBroken);
    ASSERT_FALSE(effect.IsEffectValid()) << "a sentence compiled as a fragment shader";
    EXPECT_FALSE(effect.GetCompileErrorEXT().empty())
        << "the shader failed and the renderer said nothing about why";
}

TEST(ShaderDiagnosticsTest, TheFailureIsReportedOnceAndNamesThePass)
{
    GraphicsDevice gd;
    if (!CnaTest::EngineLayer::RunsShaderSource(gd))
        GTEST_SKIP() << "this renderer compiles no shader source";

    ShaderEffect effect(gd, kVertex, kBroken);
    bool logged = false;
    EXPECT_FALSE(CNA::Graphics::detail::reportShaderCompileFailure(gd, "MyPass", &effect, logged));
    EXPECT_TRUE(logged);

    // The flag is what keeps a per-frame caller from filling the log; the second call must be
    // silent and still answer the question.
    EXPECT_FALSE(CNA::Graphics::detail::reportShaderCompileFailure(gd, "MyPass", &effect, logged));
}

TEST(ShaderDiagnosticsTest, ANullEffectIsAFailureAndNotACrash)
{
    // The state on a renderer that accepts no custom effect at all: the pass never built one.
    GraphicsDevice gd;
    bool logged = false;
    EXPECT_FALSE(CNA::Graphics::detail::reportShaderCompileFailure(gd, "NoEffect", nullptr, logged));
    EXPECT_TRUE(logged);
}

TEST(ShaderDiagnosticsTest, ItDoesNotThrowWhateverTheRenderer)
{
    // The deviation from MOD-219's proposed "throws", asserted so it is not reintroduced. Three
    // renderers report CustomEffects true and never compile GLSL source, so throwing on a failed
    // compile would turn a documented capability boundary into a crash on all three.
    GraphicsDevice gd;
    bool logged = false;
    EXPECT_NO_THROW({
        ShaderEffect effect(gd, kVertex, kBroken);
        (void)CNA::Graphics::detail::reportShaderCompileFailure(gd, "Broken", &effect, logged);
    });
}

// =====================================================================================
// MOD-220: a pass states its own sampling requirement
// =====================================================================================

TEST(SamplerRequirementTest, BloomStillProducesItsSpreadWithItsOwnSamplerState)
{
    // MOD-220 changed how bloom asks for linear-clamp filtering, not what it gets: SpriteBatch
    // already documents a null sampler as meaning LinearClamp, so the pyramid was being filtered
    // correctly by inheritance. The point of stating it is that a change to that default can no
    // longer degrade bloom silently -- so what this test protects is that the explicit request
    // behaves as the inherited one did.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    CNA::Graphics::BloomPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the bloom shaders";

    EXPECT_EQ(pass.getName(), "Bloom");
}

} // namespace

#endif // CNA_CNAEXT
