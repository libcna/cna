// SPDX-License-Identifier: MS-PL
//
// CNA-ISLAND-002: every EasyGL stock program must link on the GLES 3.0 / WebGL 2 floor. Both
// guarantee only GL_MAX_VERTEX_UNIFORM_VECTORS = 256 and GL_MAX_FRAGMENT_UNIFORM_VECTORS = 224; the
// stock SkinnedEffect programs declared `mat4 uBones[72]` (288 vectors) and failed to link on
// Android's SwiftShader. These tests need no GL context: they read each stock source and count its
// uniform vectors by the GLSL rules, the count a linker compares with the limit -- a scalar or
// vector is one vector, a matN is N, a matCxR is C (one per column) and an array is its element
// count times that. Every declared uniform counts, used or not, so the count is an upper bound
// on what any driver can call active.
//
// The expected vertex-stage totals are derived by hand from the sources (the derivation sits beside
// each value) rather than from the counter, so a change to either one fails here.

#include <gtest/gtest.h>

#if defined(CNA_RENDERER_EASYGL) || defined(CNA_RENDERER_PRESENT_OPENGLES3) \
    || defined(CNA_RENDERER_PRESENT_OPENGL33) || defined(CNA_RENDERER_PRESENT_WEBGL2)

#include <cmath>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Internal/Renderers/EasyGL/GlStockShaderSources.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"

namespace
{
    namespace Stock = CNA::Internal::Renderers::GlStockShaders;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;

    /// The GLES 3.0 / WebGL 2 minimums, the limits the affected Android configuration reports.
    constexpr int kVertexFloor = 256;
    constexpr int kFragmentFloor = 224;

    struct UniformDeclaration
    {
        std::string type;
        std::string name;
        int elements = 1;
    };

    struct UniformCount
    {
        int vectors = 0;
        int samplers = 0;
        std::vector<UniformDeclaration> declarations;
        /// Lines mentioning `uniform` that are not one plain declaration; the count is then unsafe.
        std::vector<std::string> unparsed;

        [[nodiscard]] const UniformDeclaration* Find(const std::string& name) const
        {
            for (const auto& declaration : declarations)
            {
                if (declaration.name == name)
                    return &declaration;
            }
            return nullptr;
        }
    };

    /// Vectors one element of @p type occupies; -1 for a sampler, -2 for an unknown type.
    int VectorsPerElement(const std::string& type)
    {
        static const std::regex scalarOrVector("(float|int|uint|bool)|([biu]?vec[234])");
        static const std::regex squareMatrix("mat([234])");
        static const std::regex matrix("mat([234])x([234])");
        static const std::regex sampler("[iu]?sampler[A-Za-z0-9]+");
        std::smatch match;
        if (std::regex_match(type, scalarOrVector))
            return 1;
        if (std::regex_match(type, match, squareMatrix))
            return std::stoi(match[1].str());
        if (std::regex_match(type, match, matrix))
            return std::stoi(match[1].str());  // matCxR: C columns, each one vector
        if (std::regex_match(type, sampler))
            return -1;
        return -2;
    }

    UniformCount CountUniforms(const std::string& source)
    {
        static const std::regex word("\\buniform\\b");
        static const std::regex declaration(
            "\\s*uniform\\s+(?:(?:lowp|mediump|highp)\\s+)?(\\w+)\\s+(\\w+)\\s*"
            "(?:\\[\\s*(\\d+)\\s*\\])?\\s*;\\s*");
        UniformCount count;
        std::istringstream lines(source);
        std::string line;
        while (std::getline(lines, line))
        {
            const std::size_t comment = line.find("//");
            if (comment != std::string::npos)
                line.erase(comment);
            const auto words = std::distance(
                std::sregex_iterator(line.begin(), line.end(), word), std::sregex_iterator());
            if (words == 0)
                continue;
            std::smatch match;
            if (words != 1 || !std::regex_match(line, match, declaration))
            {
                count.unparsed.push_back(line);
                continue;
            }
            UniformDeclaration parsed{match[1].str(), match[2].str(),
                                      match[3].matched ? std::stoi(match[3].str()) : 1};
            const int perElement = VectorsPerElement(parsed.type);
            if (perElement == -2)
                count.unparsed.push_back(line);
            else if (perElement == -1)
                count.samplers += parsed.elements;
            else
                count.vectors += perElement * parsed.elements;
            count.declarations.push_back(std::move(parsed));
        }
        return count;
    }

    struct StockProgram
    {
        const char* label;
        std::function<Stock::GlStockProgramSource()> source;
        int expectedVertexVectors;
        bool skinned;
    };

    // CNA_GL_INSTANCE_TRANSFORM_DECL contributes uCnaInstanced (1) to every 3D vertex stage.
    // CNA_GL_BONE_PALETTE_DECL contributes uBones[216] (216) and uWeightsPerVertex (1).
    std::vector<StockProgram> StockPrograms()
    {
        return {
            // uCnaInstanced 1, uWVP 4, uFogVector 1, uDiffuseColor 1, uVertexColorEnabled 1
            {"colored", [] { return Stock::Colored3DSource(); }, 8, false},
            // uCnaInstanced 1, uWVP 4, uFogVector 1, uDiffuseColor 1
            {"textured", [] { return Stock::Textured3DSource(); }, 7, false},
            // uCnaInstanced 1, uWVP 4, uFogVector 1, uDiffuseColor 1, uVertexColorEnabled 1
            {"col+textured", [] { return Stock::ColoredTextured3DSource(); }, 8, false},
            // uCnaInstanced 1, uWVP 4, uWorld 4, uNormalMatrix 3, uFogVector 1
            {"lit+textured", [] { return Stock::Lit3DSource(); }, 13, false},
            // 13 as above + uDiffuseColor, uAmbientColor (2) + three light directions and diffuse
            // colours (6) + three specular colours (3) + uSpecularColor, uSpecularPower,
            // uEyePosition, uEmissiveColor, uVertexColorEnabled (5)
            {"lit+textured (vertex-lit)", [] { return Stock::Lit3DVertexLitSource(); }, 29, false},
            // uCnaInstanced 1, uWVP 4, uFogVector 1, uDiffuseColor 1
            {"dual+textured", [] { return Stock::DualTextured3DSource(); }, 7, false},
            // as dual+textured + uVertexColorEnabled 1
            {"dual+textured+colored", [] { return Stock::DualTexturedColored3DSource(); }, 8, false},
            // uCnaInstanced 1, uWVP 4, uNormalMatrix 3, uWorld 4, uEyePosition 1, uFogVector 1,
            // uEnvMapAmount 1, uFresnelEnabled 1, uFresnelFactor 1
            {"env+mapped", [] { return Stock::EnvMapped3DSource(); }, 17, false},
            // uCnaInstanced 1, uWVP 4, uWorld 4, uNormalMatrix 3, uBones 216, uWeightsPerVertex 1,
            // uFogVector 1. Was 302 with mat4 uBones[72].
            {"skinned", [] { return Stock::SkinnedSource(); }, 230, true},
            // 230 as above + uDiffuseColor, uEmissiveColor, uVertexColorEnabled (3) + six light
            // direction/diffuse (6) + three light specular (3) + uSpecularColor, uSpecularPower,
            // uEyePosition (3). Was 317 -- the program Android's SwiftShader refused.
            {"skinned (vertex-lit)", [] { return Stock::SkinnedVertexLitSource(); }, 245, true},
            // uCnaInstanced 1, uWVP 4, uWorld 4, uNormalMatrix 3, uFogVector 1
            {"pbr", [] { return Stock::PbrSource(false); }, 13, false},
            {"pbr_dual_uv", [] { return Stock::PbrSource(true); }, 13, false},
            // uCnaInstanced 1, uWVP 4, uWorld 4, uNormalMatrix 3, uBones 216, uWeightsPerVertex 1,
            // uFogVector 1. Was 302.
            {"pbr_skinned", [] { return Stock::PbrSkinnedSource(false); }, 230, true},
            {"pbr_skinned_dual_uv", [] { return Stock::PbrSkinnedSource(true); }, 230, true},
            // projection 4
            {"sprite", [] { return Stock::SpriteSource(); }, 4, false},
        };
    }
}

TEST(GlStockUniformBudget, CounterFollowsTheGlslVectorRules)
{
    const UniformCount count = CountUniforms(
        "uniform float a;\n"
        "uniform highp vec3 b; // uniform in a comment is ignored\n"
        "uniform mat3 c;\n"
        "uniform mat4 d[2];\n"
        "uniform mat2x4 e;\n"
        "uniform vec4 f[216];\n"
        "uniform sampler2D g;\n");
    EXPECT_TRUE(count.unparsed.empty());
    // 1 + 1 + 3 + 2*4 + 2 + 216
    EXPECT_EQ(count.vectors, 231);
    EXPECT_EQ(count.samplers, 1);

    // A declaration the counter cannot read is reported rather than silently skipped.
    EXPECT_EQ(CountUniforms("uniform vec4 a, b;\n").unparsed.size(), 1u);
    EXPECT_EQ(CountUniforms("uniform Block { vec4 a; };\n").unparsed.size(), 1u);
}

TEST(GlStockUniformBudget, TheFormerMat4PaletteExceedsTheGlesVertexFloor)
{
    // The 2026-10-11 Android link failure, reproduced on its own terms: 72 mat4 bones are 288
    // vectors before any other uniform, already over 256. The float4x3 palette is 216.
    EXPECT_EQ(CountUniforms("uniform mat4 uBones[72];\n").vectors, 288);
    EXPECT_GT(CountUniforms("uniform mat4 uBones[72];\n").vectors, kVertexFloor);
    EXPECT_EQ(CountUniforms("uniform vec4 uBones[216];\n").vectors, 216);
}

TEST(GlStockUniformBudget, EveryStockVertexStageFitsTheGlesFloorWithItsHandDerivedCount)
{
    for (const StockProgram& program : StockPrograms())
    {
        SCOPED_TRACE(program.label);
        const UniformCount vertex = CountUniforms(program.source().vertex);
        EXPECT_TRUE(vertex.unparsed.empty()) << vertex.unparsed.front();
        EXPECT_EQ(vertex.samplers, 0) << "a vertex-stage sampler is not counted here";
        EXPECT_EQ(vertex.vectors, program.expectedVertexVectors);
        EXPECT_LE(vertex.vectors, kVertexFloor);
    }
}

TEST(GlStockUniformBudget, EveryStockFragmentStageFitsTheGlesFloor)
{
    for (const StockProgram& program : StockPrograms())
    {
        SCOPED_TRACE(program.label);
        const UniformCount fragment = CountUniforms(program.source().fragment);
        EXPECT_TRUE(fragment.unparsed.empty()) << fragment.unparsed.front();
        // Samplers are counted as a vector each here, the more conservative reading.
        EXPECT_LE(fragment.vectors + fragment.samplers, kFragmentFloor);
    }
}

TEST(GlStockUniformBudget, EverySkinnedProgramKeepsXnasSeventyTwoBonePalette)
{
    EXPECT_EQ(Stock::kSkinnedPaletteBones, Microsoft::Xna::Framework::Graphics::SkinnedEffect::MaxBones);
    EXPECT_EQ(Stock::kSkinnedPaletteBones, Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect::MaxBones);
    EXPECT_EQ(Stock::kSkinnedPaletteBones, 72);
    EXPECT_EQ(Stock::kSkinnedPaletteVectorsPerBone, 3);
    for (const StockProgram& program : StockPrograms())
    {
        SCOPED_TRACE(program.label);
        const UniformCount vertex = CountUniforms(program.source().vertex);
        const UniformDeclaration* bones = vertex.Find("uBones");
        if (!program.skinned)
        {
            EXPECT_EQ(bones, nullptr);
            continue;
        }
        ASSERT_NE(bones, nullptr);
        EXPECT_EQ(bones->type, "vec4");
        EXPECT_EQ(bones->elements, 72 * 3);
        const UniformDeclaration* weights = vertex.Find("uWeightsPerVertex");
        ASSERT_NE(weights, nullptr);
        EXPECT_EQ(weights->type, "int");
    }
}

// The palette layout: the rows a bone uploads, dotted with (p, 1), must give XNA's own
// Vector3.Transform(p, M) -- v*M with the row-vector convention -- and dotted with a normal, XNA's
// mul(n, (float3x3)M). Hand-derived: M = Scale(2,3,4) * RotationZ(90 deg) * Translation(5,6,7)
// takes (1,0,0) -> (2,0,0) -> (0,2,0) -> (5,8,7) and (0,1,0) -> (0,3,0) -> (-3,0,0) -> (2,6,7);
// the normal (0,1,0) -> (-3,0,0) with no translation.
TEST(GlStockUniformBudget, PackedPaletteRowsReproduceXnaRowVectorTransforms)
{
    const Matrix bone = Matrix::CreateScale(2.0f, 3.0f, 4.0f) *
                        Matrix::CreateRotationZ(1.5707963267948966f) *
                        Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
    std::vector<Matrix> palette(Stock::kSkinnedPaletteBones, Matrix::getIdentityProperty());
    palette[71] = bone;
    std::vector<float> fields(palette.size() * 16);
    for (std::size_t i = 0; i < palette.size(); ++i)
        palette[i].ToColumnMajor(fields.data() + i * 16);  // XNA field order M11, M12, ...
    std::vector<float> rows(palette.size() * 12, -1.0f);
    Stock::PackSkinnedPaletteRows(fields.data(), static_cast<int>(palette.size()), rows.data());

    const auto transform = [&rows](int boneIndex, float x, float y, float z, float w) {
        const float* r = rows.data() + boneIndex * 12;
        return Vector3(r[0] * x + r[1] * y + r[2] * z + r[3] * w,
                       r[4] * x + r[5] * y + r[6] * z + r[7] * w,
                       r[8] * x + r[9] * y + r[10] * z + r[11] * w);
    };
    const auto near = [](const Vector3& actual, float x, float y, float z) {
        return std::fabs(actual.X - x) < 1e-5f && std::fabs(actual.Y - y) < 1e-5f &&
               std::fabs(actual.Z - z) < 1e-5f;
    };

    EXPECT_TRUE(near(transform(71, 1, 0, 0, 1), 5, 8, 7));
    EXPECT_TRUE(near(transform(71, 0, 1, 0, 1), 2, 6, 7));
    EXPECT_TRUE(near(transform(71, 0, 1, 0, 0), -3, 0, 0));
    // The same answers from XNA's own math, for an arbitrary point.
    const Vector3 point(0.25f, -1.5f, 2.0f);
    const Vector3 expected = Vector3::Transform(point, bone);
    EXPECT_TRUE(near(transform(71, point.X, point.Y, point.Z, 1), expected.X, expected.Y, expected.Z));
    const Vector3 expectedNormal = Vector3::TransformNormal(point, bone);
    EXPECT_TRUE(near(transform(71, point.X, point.Y, point.Z, 0),
                     expectedNormal.X, expectedNormal.Y, expectedNormal.Z));
    // Untouched bones stay identity rows: (1,0,0,0), (0,1,0,0), (0,0,1,0).
    EXPECT_TRUE(near(transform(0, 3, 4, 5, 1), 3, 4, 5));
}

TEST(GlStockUniformBudget, PackedPaletteDropsTheColumnFloat4x3DoesNotHave)
{
    // XNA's float4x3 has no fourth column, so M14/M24/M34/M44 never reach the shader.
    Matrix bone = Matrix::getIdentityProperty();
    bone.M14 = 9.0f;
    bone.M24 = 9.0f;
    bone.M34 = 9.0f;
    bone.M44 = 9.0f;
    float fields[16];
    bone.ToColumnMajor(fields);
    float rows[12];
    Stock::PackSkinnedPaletteRows(fields, 1, rows);
    const float identityRows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (int i = 0; i < 12; ++i)
        EXPECT_EQ(rows[i], identityRows[i]) << i;
}

#endif
