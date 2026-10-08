// SPDX-License-Identifier: MS-PL
// plans/plan_apple_m4.md AM4-116: the GLSL ES-to-desktop header rewrite parses the #version directive
// instead of matching the exact text "#version 300 es\n", which missed CRLF sources (MojoShader's
// output in a Windows build) and blanks inside the directive.

#include <gtest/gtest.h>

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
