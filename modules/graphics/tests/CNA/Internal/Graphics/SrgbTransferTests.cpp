// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-210/GLTF-212: the sRGB transfer function itself.
//
// The formula exists in two languages that cannot share code -- C++ and GLSL -- so
// CNA/Internal/Graphics/SrgbTransfer.hpp states it once as text and derives both. These tests hold
// the C++ half to the specification's own numbers. They are what makes the shader trustworthy in
// an environment where no shader can run: if the arithmetic here is right and the GLSL is generated
// from the same text, the only remaining risk is a typo the string comparison below also catches.

#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/SrgbTransfer.hpp"

using CNA::Internal::Graphics::kSrgbTransferGlsl;
using CNA::Internal::Graphics::LinearToSrgbEXT;
using CNA::Internal::Graphics::SrgbToLinearEXT;

namespace
{
    constexpr float kTolerance = 1e-6f;
}

// The three anchor values every sRGB implementation agrees on.
TEST(SrgbTransferTest, EndpointsAreExact)
{
    EXPECT_NEAR(0.0f, SrgbToLinearEXT(0.0f), kTolerance);
    EXPECT_NEAR(1.0f, SrgbToLinearEXT(1.0f), kTolerance);
    EXPECT_NEAR(0.0f, LinearToSrgbEXT(0.0f), kTolerance);
    EXPECT_NEAR(1.0f, LinearToSrgbEXT(1.0f), kTolerance);
}

// Mid-grey is the value people actually notice: sRGB 0.5 is linear ~0.2140, not 0.5. Treating an
// sRGB texture as linear -- which is what CNA did before GLTF-210 -- makes a mid-grey albedo more
// than twice as bright as the file says.
TEST(SrgbTransferTest, MidGreyMatchesTheSpecification)
{
    EXPECT_NEAR(0.21404114f, SrgbToLinearEXT(0.5f), 1e-6f);
    EXPECT_NEAR(0.5f, LinearToSrgbEXT(0.21404114f), 1e-5f);
    // The size of the error the fix removes, stated as a number rather than as an adjective.
    EXPECT_GT(0.5f / SrgbToLinearEXT(0.5f), 2.3f);
}

// The piecewise knee is where a hand-copied formula goes wrong. Continuity is asserted by
// evaluating the two BRANCHES at the same input rather than by stepping across the knee: the power
// branch is steep there, so a finite difference measures its slope, not a discontinuity. What must
// hold is that the linear branch and the power branch agree at the switch-over point.
TEST(SrgbTransferTest, BothKneesJoinTheirTwoBranchesWithoutAStep)
{
    constexpr float kDecodeKnee = 0.04045f;
    const float decodeLinearBranch = kDecodeKnee / 12.92f;
    const float decodePowerBranch  = std::pow((kDecodeKnee + 0.055f) / 1.055f, 2.4f);
    EXPECT_NEAR(decodeLinearBranch, decodePowerBranch, 1e-7f)
        << "the decode branches disagree at the knee -- a visible band";
    EXPECT_NEAR(decodeLinearBranch, SrgbToLinearEXT(kDecodeKnee), 1e-7f)
        << "the decode knee is on the wrong side of the comparison";

    constexpr float kEncodeKnee = 0.0031308f;
    const float encodeLinearBranch = kEncodeKnee * 12.92f;
    const float encodePowerBranch  = 1.055f * std::pow(kEncodeKnee, 1.0f / 2.4f) - 0.055f;
    EXPECT_NEAR(encodeLinearBranch, encodePowerBranch, 1e-7f)
        << "the encode branches disagree at the knee";
    EXPECT_NEAR(encodeLinearBranch, LinearToSrgbEXT(kEncodeKnee), 1e-7f);

    // The two knees are each other's image, which is what makes them one curve read two ways.
    EXPECT_NEAR(kDecodeKnee, LinearToSrgbEXT(kEncodeKnee), 1e-6f);
    EXPECT_NEAR(kEncodeKnee, SrgbToLinearEXT(kDecodeKnee), 1e-7f);

    // Monotonic across the knee in both directions, so nothing folds back on itself.
    EXPECT_LT(SrgbToLinearEXT(kDecodeKnee - 1e-3f), SrgbToLinearEXT(kDecodeKnee));
    EXPECT_LT(SrgbToLinearEXT(kDecodeKnee), SrgbToLinearEXT(kDecodeKnee + 1e-3f));
    EXPECT_LT(LinearToSrgbEXT(kEncodeKnee - 1e-4f), LinearToSrgbEXT(kEncodeKnee));
    EXPECT_LT(LinearToSrgbEXT(kEncodeKnee), LinearToSrgbEXT(kEncodeKnee + 1e-4f));
}

// Round-tripping is the property the renderer actually depends on: decode a texture, light it,
// encode the result. If the pair were not inverse, every unlit surface would drift in brightness.
TEST(SrgbTransferTest, DecodeThenEncodeIsIdentityAcrossTheRange)
{
    for (int i = 0; i <= 255; ++i)
    {
        const float encoded = static_cast<float>(i) / 255.0f;
        SCOPED_TRACE("8-bit level " + std::to_string(i));
        EXPECT_NEAR(encoded, LinearToSrgbEXT(SrgbToLinearEXT(encoded)), 1e-5f);
    }
}

// Neither direction may clamp. KHR_materials_emissive_strength (GLTF-222) legitimately produces
// emissive values above 1, and a transfer that quietly clamped would undo the extension the
// importer went to the trouble of honouring.
TEST(SrgbTransferTest, ValuesAboveOneAreNotClamped)
{
    EXPECT_GT(SrgbToLinearEXT(2.0f), 1.0f);
    EXPECT_GT(LinearToSrgbEXT(2.0f), 1.0f);
    EXPECT_GT(LinearToSrgbEXT(5.0f), LinearToSrgbEXT(2.0f));
}

// The GLSL is the other half of the contract and cannot be executed here, so what is checkable is
// checked: that it declares both functions, and that every constant in it is one of the constants
// the C++ above was just proved against. A typo in a coefficient is the realistic failure, and it
// would show up as a missing substring.
TEST(SrgbTransferTest, TheGlslDeclaresBothFunctionsWithTheSameConstants)
{
    const std::string glsl = kSrgbTransferGlsl;
    EXPECT_NE(std::string::npos, glsl.find("vec3 cnaSrgbToLinear(vec3 c)"));
    EXPECT_NE(std::string::npos, glsl.find("vec3 cnaLinearToSrgb(vec3 c)"));

    for (const char* constant : {"12.92", "0.055", "1.055", "2.4", "0.04045", "0.0031308"})
    {
        EXPECT_NE(std::string::npos, glsl.find(constant))
            << "the GLSL no longer mentions " << constant
            << " -- it has drifted from the C++ the tests above pin";
    }

    // vec3, never vec4: glTF §3.9.4 makes alpha coverage rather than colour, so transferring it
    // would be wrong.
    EXPECT_EQ(std::string::npos, glsl.find("vec4"))
        << "the transfer took a vec4 -- alpha must never be encoded";
}

// The transfer is a MACRO of string literals rather than a `const char*` for one reason: a renderer
// builds its shader source by literal concatenation, and a variable cannot join that. The first
// version of SrgbTransfer.hpp got this wrong and would not have compiled inside a shader source.
// This reproduces the exact splice EasyGLRenderer performs, so the mistake cannot come back --
// especially since the renderers that consume it need sibling checkouts (easy-gl, meta-gl) that a
// STUB-configured tree does not have, and therefore are not compiled by this suite at all.
TEST(SrgbTransferTest, TheMacroSplicesIntoShaderSourceConcatenation)
{
    static const char* fragmentSource =
        "#version 300 es\n"
        "uniform vec3 uSrgb;\n"
        "out vec4 FragColor;\n"
        CNA_GLSL_SRGB_TRANSFER
        "void main(){ FragColor=vec4(cnaLinearToSrgb(cnaSrgbToLinear(vec3(0.5))),1.0); }\n";

    const std::string source = fragmentSource;
    EXPECT_NE(std::string::npos, source.find("out vec4 FragColor;\nvec3 cnaSrgbToLinear"))
        << "the macro did not splice in place -- shader source would be malformed";
    EXPECT_NE(std::string::npos, source.find("void main()"))
        << "the text after the macro was lost";
    EXPECT_EQ(std::string(kSrgbTransferGlsl), std::string(CNA_GLSL_SRGB_TRANSFER))
        << "the value form and the macro form are no longer the same text";
}
