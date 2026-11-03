// SPDX-License-Identifier: MS-PL
// Apple M4 AM4-116: the GLSL ES-to-desktop header rewrite parses the #version directive
// instead of matching the exact text "#version 300 es\n", which missed CRLF sources (MojoShader's
// output in a Windows build) and blanks inside the directive.

#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/EasyGL/GlStockShaderSources.hpp"

#include <algorithm>
#include <string>

#include "CNA/Internal/Renderers/EasyGL/GlslVersionDirective.hpp"

using CNA::Internal::Renderers::EasyGL::FindGlslEsVersionLine;
using CNA::Internal::Renderers::EasyGL::RewriteGlslEsVersionToDesktop;

namespace
{
    std::size_t Lines(const std::string& text) { return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')); }
}

TEST(GlslVersionDirective, AnLfSourceIsRewrittenExactlyAsBefore)
{
    const std::string es = "#version 300 es\nprecision mediump float;\nout vec4 c;\nvoid main(){c=vec4(1);}\n";
    EXPECT_EQ(RewriteGlslEsVersionToDesktop(es, "300", "#version 330 core"),
              "#version 330 core\n\nout vec4 c;\nvoid main(){c=vec4(1);}\n");
}

TEST(GlslVersionDirective, ACrlfSourceIsRewrittenAndKeepsItsLineEndings)
{
    const std::string es = "#version 300 es\r\nprecision highp float;\r\nout vec4 c;\r\n";
    const std::string desktop = RewriteGlslEsVersionToDesktop(es, "300", "#version 330 core");
    EXPECT_EQ(desktop, "#version 330 core\r\n\r\nout vec4 c;\r\n");
    EXPECT_EQ(Lines(desktop), Lines(es)) << "a diagnostic must still name the author's line";
}

TEST(GlslVersionDirective, BlanksInsideAndAroundTheDirectiveAreAccepted)
{
    EXPECT_TRUE(FindGlslEsVersionLine("  #  version   300   es  \nvoid main(){}\n", "300").Found());
    EXPECT_TRUE(FindGlslEsVersionLine("\t#version 300 es\t\r\n", "300").Found());
    EXPECT_TRUE(FindGlslEsVersionLine("// header\n#version 300 es", "300").Found()) << "no final newline";
}

TEST(GlslVersionDirective, OtherVersionsAndNonEsSourcesAreLeftAlone)
{
    const std::string desktop = "#version 330 core\nvoid main(){}\n";
    EXPECT_FALSE(FindGlslEsVersionLine(desktop, "300").Found());
    EXPECT_EQ(RewriteGlslEsVersionToDesktop(desktop, "300", "#version 330 core"), desktop);
    EXPECT_FALSE(FindGlslEsVersionLine("#version 300 esx\n", "300").Found());
    EXPECT_FALSE(FindGlslEsVersionLine("#version 3000 es\n", "300").Found());
    EXPECT_FALSE(FindGlslEsVersionLine("#version 310 es\n", "300").Found());
    EXPECT_TRUE(FindGlslEsVersionLine("#version 310 es\n", "310").Found());
    EXPECT_EQ(RewriteGlslEsVersionToDesktop("#version 320 es\r\nlayout(location=0) out vec4 c;\r\n", "320",
                                            "#version 410 core"),
              "#version 410 core\r\nlayout(location=0) out vec4 c;\r\n")
        << "a non-precision second line is kept";
}

TEST(GlslVersionDirective, ReportsExactSpanWithoutEatingAdjacentLines)
{
    // Parser contract: offsets include indentation and the directive's terminator only.
    const std::string source = "// preface\n\t# version 300 es\r\nvoid main(){}\n";
    const auto line = FindGlslEsVersionLine(source, "300");
    ASSERT_TRUE(line.Found());
    EXPECT_EQ(line.begin, 11u);
    EXPECT_EQ(line.end, 30u);
    EXPECT_EQ(line.terminator, "\r\n");
    EXPECT_EQ(RewriteGlslEsVersionToDesktop(source, "300", "#version 330 core"),
              "// preface\n#version 330 core\r\nvoid main(){}\n");
}

TEST(GlslVersionDirective, FirstVersionControlsRewritingAndTokensCannotRunTogether)
{
    // GLSL ES 3.00 section 3.3: #version is a tokenized directive. This helper's
    // contract gives the first version priority, even in malformed multi-version input.
    for (const std::string source : {"#version 310 es\n#version 300 es\n",
         "#version 330 core\n#version 300 es\n", "#version300 es\n", "#version 300es\n",
         "#version 300 esExtra\n", "// #version 300 es\n", "", "\n\t\n"}) {
        SCOPED_TRACE(source);
        EXPECT_FALSE(FindGlslEsVersionLine(source, "300").Found());
        EXPECT_EQ(RewriteGlslEsVersionToDesktop(source, "300", "#version 330 core"), source);
    }
}

TEST(GlslVersionDirective, EofAndIndentedPrecisionKeepTheRemainingSourceExactly)
{
    // Rewriter contract: blank only the immediately following precision line,
    // preserve diagnostic line numbers and every byte of the remaining body.
    EXPECT_EQ(RewriteGlslEsVersionToDesktop("#version 300 es", "300", "#version 330 core"),
              "#version 330 core");
    EXPECT_EQ(RewriteGlslEsVersionToDesktop("#version 300 es\n  precision highp float;", "300", "#version 330 core"),
              "#version 330 core\n");
    EXPECT_EQ(RewriteGlslEsVersionToDesktop("#version 300 es\n\tprecision mediump float;\n// body\n", "300", "#version 330 core"),
              "#version 330 core\n\n// body\n");
    EXPECT_EQ(RewriteGlslEsVersionToDesktop("#version 300 es\n\nprecision highp float;\n", "300", "#version 330 core"),
              "#version 330 core\n\nprecision highp float;\n");
}


TEST(EasyGLStockShader, EveryProgramHasEsVersionPrecisionAndDesktopAdaptation)
{
    using namespace CNA::Internal::Renderers::GlStockShaders;
    // The shared shader contract is GLSL ES 3.00 for GLES3/WebGL2 and GLSL 3.30
    // core for GL33. The profile rewrite must not alter uniforms, varyings or bodies.
    const GlStockProgramSource programs[] = {
        Colored3DSource(), Textured3DSource(), ColoredTextured3DSource(), Lit3DSource(),
        Lit3DVertexLitSource(), DualTextured3DSource(), DualTexturedColored3DSource(),
        EnvMapped3DSource(), SkinnedSource(), SkinnedVertexLitSource(), PbrSource(false),
        PbrSource(true), PbrSkinnedSource(false), PbrSkinnedSource(true), SpriteSource()};
    for (std::size_t i = 0; i < std::size(programs); ++i) {
        SCOPED_TRACE(i);
        for (const auto& source : {programs[i].vertex, programs[i].fragment}) {
            ASSERT_TRUE(source.starts_with("#version 300 es\nprecision "));
            const auto body = source.find('\n', source.find('\n') + 1);
            ASSERT_NE(body, std::string::npos);
            const auto desktop = RewriteGlslEsVersionToDesktop(source, "300", "#version 330 core");
            EXPECT_EQ(desktop, std::string("#version 330 core\n") + source.substr(body));
            EXPECT_EQ(Lines(desktop), Lines(source));
        }
    }
}

TEST(EasyGLStockShader, BasicAndSkinnedInputsMatchTheBinderContract)
{
    using namespace CNA::Internal::Renderers::GlStockShaders;
    // Renderer binder ABI: position 0, color 1, float bone registers and 72 matrices.
    // FNA Effect/Resources/SkinnedEffect.fx also fixes MaxBones to 72.
    const auto colored = Colored3DSource();
    for (const auto token : {"layout(location=0) in vec3 aPos;", "layout(location=1) in vec4 aColor;",
                            "uniform mat4 uWVP;", "uniform vec4 uDiffuseColor;", "out vec4 vColor;"})
        EXPECT_NE(colored.vertex.find(token), std::string::npos) << token;
    EXPECT_NE(colored.fragment.find("in vec4 vColor;"), std::string::npos);
    for (const auto& source : {SkinnedSource().vertex, SkinnedVertexLitSource().vertex}) {
        EXPECT_NE(source.find("in vec4 aBoneIndices;"), std::string::npos);
        EXPECT_NE(source.find("in vec4 aBoneWeights;"), std::string::npos);
        EXPECT_NE(source.find("uniform mat4 uBones[72];"), std::string::npos);
    }
}

TEST(EasyGLStockShader, ClipDepthConversionAndPointSizePreserveTheAuthorsBody)
{
    using CNA::Internal::Renderers::GlStockShaders::AdaptStockVertexShaderForOpenGL;
    // D3D clip z in [0,w] becomes GL [-w,w]: z'=2z-w. GLES point rasterization
    // needs an explicit size. Existing gl_PointSize must retain the author's value.
    EXPECT_EQ(AdaptStockVertexShaderForOpenGL("void main(){gl_Position=p;tail();}"),
              "void main(){gl_Position=p;\n    gl_Position.z=gl_Position.z*2.0-gl_Position.w;\n    gl_PointSize=1.0;tail();}");
    EXPECT_EQ(AdaptStockVertexShaderForOpenGL("void main(){gl_Position=p;gl_PointSize=7.0;}"),
              "void main(){gl_Position=p;\n    gl_Position.z=gl_Position.z*2.0-gl_Position.w;gl_PointSize=7.0;}");
    EXPECT_EQ(AdaptStockVertexShaderForOpenGL("void main(){}"), "void main(){}");
    EXPECT_EQ(AdaptStockVertexShaderForOpenGL("gl_Position"), "gl_Position");
}
