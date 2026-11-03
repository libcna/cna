// SPDX-License-Identifier: MS-PL
//
// TQ-012: the stock effects' uniform packing (WebGPUStateMapping.hpp Fill*Uniforms) against the WGSL
// that reads it (webgpu_shaders.hpp). CPU-only.
//
// Two independent oracles meet here:
//  - the WGSL memory-layout rules (vec4f: size 16, align 16; mat4x4f: size 64, align 16;
//    array<vec4f, N>: stride 16; a struct is rounded up to its largest alignment). Every member
//    offset below is derived by hand from the struct text, and the reflected offsets of the real
//    shader source must match it -- so a shader edit that moves a member is caught;
//  - XNA's maths: a row vector times a matrix (Vector4.Transform = v * M), and normals transformed
//    by the inverse transpose of World (FNA Lighting.fxh: mul(normal, WorldInverseTranspose)).
//    The shader-side products are simulated from the packed floats with WGSL's column-major rule,
//    so a transposition or a misplaced field changes the result.

#include "CNA/Internal/Renderers/WebGPU/WebGPUStateMapping.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPUWgslReflection.hpp"
#include "CNA/Internal/Renderers/WebGPU/webgpu_shaders.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace CNA::Internal::Renderers::WebGPU;
using CNA::Internal::Renderers::GpuDrawParams;
using Microsoft::Xna::Framework::Matrix;

namespace
{
    using Members = std::vector<std::pair<const char*, std::uint32_t>>;

    /// Reflects @p source and checks that @p structName has exactly @p expected members at the
    /// hand-derived byte offsets, and the hand-derived size.
    void ExpectStructLayout(const char* shaderName, const char* source, const char* structName,
                            const Members& expected, std::uint32_t expectedSize)
    {
        const WgslModuleReflection reflection = ReflectWgsl(source);
        ASSERT_TRUE(reflection.ok) << shaderName << ": " << reflection.error;
        const auto found = reflection.structs.find(structName);
        ASSERT_NE(found, reflection.structs.end()) << shaderName << " declares no struct " << structName;
        const WgslStruct& layout = found->second;
        ASSERT_EQ(layout.members.size(), expected.size()) << shaderName << "::" << structName;
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(layout.members[i].name, expected[i].first) << shaderName << "::" << structName << " member " << i;
            EXPECT_EQ(layout.members[i].offset, expected[i].second)
                << shaderName << "::" << structName << "." << expected[i].first;
        }
        EXPECT_EQ(layout.layout.size, expectedSize) << shaderName << "::" << structName << " size";
    }

    // Hand-derived: mat4x4f at 0 (64 bytes), then seven vec4f at 64, 80, ..., 160; size 176.
    const Members kPrimaryUniforms{
        {"mvp", 0}, {"diffuseColor", 64}, {"ambientLighting", 80}, {"light0DirTexture", 96},
        {"light0DiffuseVertexColor", 112}, {"fogColor", 128}, {"fogVector", 144}, {"samplerBias", 160}};
    // AlphaTestEffect reuses the same 176-byte block with its own names for the middle members.
    const Members kAlphaTestUniforms{
        {"mvp", 0}, {"diffuseColor", 64}, {"alphaTest", 80}, {"extra", 96}, {"fogPad", 112},
        {"fogColor", 128}, {"fogVector", 144}, {"samplerBias", 160}};
    // Five vec4f (0..64), mat4x4f world at 80 (16-aligned, 64 bytes), then nine vec4f from 144;
    // the last ends at 272 = 68 floats.
    const Members kLitLightParams{
        {"light1Dir", 0}, {"light1Diffuse", 16}, {"light2Dir", 32}, {"light2Diffuse", 48},
        {"emissiveColor", 64}, {"world", 80}, {"eyePos", 144}, {"light0Specular", 160},
        {"light1Specular", 176}, {"light2Specular", 192}, {"specularColorPower", 208},
        {"normalMatrixCol0", 224}, {"normalMatrixCol1", 240}, {"normalMatrixCol2", 256}};
    // Sixteen vec4f, 16 bytes apart; size 256 = 64 floats.
    const Members kEnvMapParams{
        {"eyePos", 0}, {"diffuseColor", 16}, {"emissiveAmount", 32}, {"light0Dir", 48},
        {"light0DiffuseFresnelEn", 64}, {"envMapSpecFresnelF", 80}, {"fogColor", 96}, {"fogVector", 112},
        {"light1Dir", 128}, {"light1Diffuse", 144}, {"light2Dir", 160}, {"light2Diffuse", 176},
        {"normalMatrixCol0", 192}, {"normalMatrixCol1", 208}, {"normalMatrixCol2", 224}, {"samplerBias", 240}};
    // Four vec4f (0..48), array<vec4f, 10> at 64 (160 bytes -> 224), vec4f at 224,
    // array<vec4f, 4> at 240 (64 bytes -> 304), vec4f at 304; size 320 = 80 floats.
    const Members kPbrFactors{
        {"metallicRoughness", 0}, {"alphaTest", 16}, {"srgbFlags", 32}, {"dielectricFresnel", 48},
        {"textureTransformRows", 64}, {"specularFresnelInputs", 224}, {"specularTextureTransformRows", 240},
        {"materialFlags", 304}};
    // vec4f at 0, array<mat4x4f, 72> at 16 (72 * 64 = 4608 bytes); size 4624 = (4 + 72 * 16) floats.
    const Members kSkinningParams{{"weightsPerVertex", 0}, {"bones", 16}};

    /// XNA's row-vector transform: out_j = sum_i v_i * M(i+1)(j+1), with @p m in field order
    /// M11, M12, ..., M44.
    std::array<float, 4> RowTimesMatrix(const std::array<float, 4>& v, const std::array<float, 16>& m)
    {
        std::array<float, 4> out{};
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i)
                out[static_cast<std::size_t>(j)] += v[static_cast<std::size_t>(i)] * m[static_cast<std::size_t>(i * 4 + j)];
        return out;
    }

    /// WGSL `mat4x4f * vec4f` over 16 packed floats: column c occupies floats 4c..4c+3.
    std::array<float, 4> WgslMat4TimesVec(const float* columns, const std::array<float, 4>& v)
    {
        std::array<float, 4> out{};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                out[static_cast<std::size_t>(r)] += columns[c * 4 + r] * v[static_cast<std::size_t>(c)];
        return out;
    }

    /// WGSL `mat3x3f(col0.xyz, col1.xyz, col2.xyz) * n` over three packed vec4f columns.
    std::array<float, 3> WgslMat3TimesVec(const float* col0, const float* col1, const float* col2,
                                          const std::array<float, 3>& n)
    {
        std::array<float, 3> out{};
        for (int r = 0; r < 3; ++r)
            out[static_cast<std::size_t>(r)] = col0[r] * n[0] + col1[r] * n[1] + col2[r] * n[2];
        return out;
    }

    Matrix FromFields(const std::array<float, 16>& m)
    {
        return Matrix(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7],
                      m[8], m[9], m[10], m[11], m[12], m[13], m[14], m[15]);
    }

    /// A World of a rotation by 90 degrees about Z with scale (2, 4, 0.5) and translation (5, 6, 7):
    /// rows (0, 2, 0), (-4, 0, 0), (0, 0, 0.5). Its upper 3x3 A has the hand-derived inverse
    /// rows (0, -1/4, 0), (1/2, 0, 0), (0, 0, 2) -- A x = (2 x2, -4 x1, x3 / 2) undoes to
    /// x1 = -y2 / 4, x2 = y1 / 2, x3 = 2 y3.
    constexpr std::array<float, 16> kWorld{
        0.0f, 2.0f, 0.0f, 0.0f,
        -4.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        5.0f, 6.0f, 7.0f, 1.0f};

    void FillWorld(GpuDrawParams& p, const std::array<float, 16>& world)
    {
        // GpuDrawParams::worldColMajor is World.ToColumnMajor(): the fields M11, M12, ... in order.
        for (std::size_t i = 0; i < 16; ++i) p.worldColMajor[i] = world[i];
    }

    GpuDrawParams DistinctParams()
    {
        GpuDrawParams p;
        const float diffuse[4] = {0.11f, 0.12f, 0.13f, 0.14f};
        const float ambient[3] = {0.21f, 0.22f, 0.23f};
        const float d0[3] = {0.31f, 0.32f, 0.33f};
        const float c0[3] = {0.41f, 0.42f, 0.43f};
        const float d1[3] = {0.51f, 0.52f, 0.53f};
        const float c1[3] = {0.61f, 0.62f, 0.63f};
        const float d2[3] = {0.71f, 0.72f, 0.73f};
        const float c2[3] = {0.81f, 0.82f, 0.83f};
        const float emissive[3] = {0.91f, 0.92f, 0.93f};
        const float fogColor[3] = {1.1f, 1.2f, 1.3f};
        const float fogVector[4] = {1.4f, 1.5f, 1.6f, 1.7f};
        for (int i = 0; i < 4; ++i) { p.diffuseColor[i] = diffuse[i]; p.fogVector[i] = fogVector[i]; }
        for (int i = 0; i < 3; ++i)
        {
            p.ambientColor[i] = ambient[i];
            p.light0Dir[i] = d0[i]; p.light0Diffuse[i] = c0[i];
            p.light1Dir[i] = d1[i]; p.light1Diffuse[i] = c1[i];
            p.light2Dir[i] = d2[i]; p.light2Diffuse[i] = c2[i];
            p.emissiveColor[i] = emissive[i];
            p.fogColor[i] = fogColor[i];
            p.eyePositionWorld[i] = 2.1f + 0.1f * static_cast<float>(i);
            p.light0Specular[i] = 3.1f + 0.1f * static_cast<float>(i);
            p.light1Specular[i] = 3.4f + 0.1f * static_cast<float>(i);
            p.light2Specular[i] = 3.7f + 0.1f * static_cast<float>(i);
            p.specularColor[i] = 4.1f + 0.1f * static_cast<float>(i);
            p.envMapSpecular[i] = 4.4f + 0.1f * static_cast<float>(i);
        }
        p.specularPower = 24.0f;
        p.envMapAmount = 0.375f;
        p.fresnelFactor = 2.5f;
        p.fresnelEnabled = true;
        p.lightingEnabled = true;
        p.textureEnabled = false;
        p.vertexColorEnabled = true;
        p.alphaTest[0] = 0.25f; p.alphaTest[1] = 0.125f; p.alphaTest[2] = 1.0f; p.alphaTest[3] = -1.0f;
        FillWorld(p, kWorld);
        return p;
    }

    /// Index of a member's first float: its hand-derived byte offset / 4.
    constexpr std::size_t F(std::uint32_t byteOffset) { return byteOffset / 4u; }

    void ExpectVec(const float* actual, std::initializer_list<float> expected, const char* what)
    {
        std::size_t i = 0;
        for (const float value : expected)
        {
            EXPECT_FLOAT_EQ(actual[i], value) << what << "[" << i << "]";
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// 1. The shader-side layouts.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUUniformPacking, StockShaderUniformBlocksHaveTheHandDerivedWgslLayout)
{
    using namespace CNA::Internal::Renderers::WebGPU::webgpu_shaders;
    for (const auto& [name, source] : std::vector<std::pair<const char*, const char*>>{
             {"kColored", kColored}, {"kTextured", kTextured}, {"kColoredTextured", kColoredTextured},
             {"kDualTexture", kDualTexture}, {"kDualTextureColored", kDualTextureColored},
             {"kLitTextured", kLitTextured}, {"kLitTexturedVertexLit", kLitTexturedVertexLit}})
        ExpectStructLayout(name, source, "Uniforms", kPrimaryUniforms, 176);
    for (const auto& [name, source] : std::vector<std::pair<const char*, const char*>>{
             {"kAlphaTest", kAlphaTest}, {"kAlphaTestColored", kAlphaTestColored}})
        ExpectStructLayout(name, source, "Uniforms", kAlphaTestUniforms, 176);
    for (const auto& [name, source] : std::vector<std::pair<const char*, const char*>>{
             {"kLitTextured", kLitTextured}, {"kLitTexturedVertexLit", kLitTexturedVertexLit},
             {"kPbr", kPbr}, {"kSkinned", kSkinned}, {"kSkinnedPbr", kSkinnedPbr}})
        ExpectStructLayout(name, source, "LitLightParams", kLitLightParams, 272);
    ExpectStructLayout("kEnvMap", kEnvMap, "Transform", {{"mvp", 0}, {"world", 64}}, 128);
    ExpectStructLayout("kEnvMap", kEnvMap, "EnvMapParams", kEnvMapParams, 256);
    ExpectStructLayout("kPbr", kPbr, "PbrFactors", kPbrFactors, 320);
    ExpectStructLayout("kSkinnedPbr", kSkinnedPbr, "PbrFactors", kPbrFactors, 320);
    ExpectStructLayout("kSkinned", kSkinned, "SkinningParams", kSkinningParams, 4624);
    ExpectStructLayout("kSkinnedPbr", kSkinnedPbr, "SkinningParams", kSkinningParams, 4624);
}

TEST(WebGPUUniformPacking, CpuBlockSizesEqualTheWgslStructSizes)
{
    // The binding-0 minBindingSize and every packer's float count must equal the struct the shader
    // declares (the hand-derived sizes above): a smaller CPU block fails validation, a larger one
    // means a field the shader never sees.
    EXPECT_EQ(kPrimaryUniformByteSize, 176u);
    EXPECT_EQ(sizeof(std::array<float, 44>), 176u);   // FillExtUniforms / FillAlphaTestUniforms
    EXPECT_EQ(sizeof(std::array<float, 68>), 272u);   // FillLitLightUniforms
    EXPECT_EQ(sizeof(std::array<float, 32>), 128u);   // FillEnvMapTransform
    EXPECT_EQ(sizeof(std::array<float, 64>), 256u);   // FillEnvMapParams
    EXPECT_EQ(sizeof(std::array<float, 80>), 320u);   // FillPbrFactors
    EXPECT_EQ(sizeof(std::array<float, 4 + 72 * 16>), 4624u);  // FillSkinningParams
}

// ---------------------------------------------------------------------------------------------
// 2. What lands in each member.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUUniformPacking, PrimaryBlockMvpTransformsLikeXnaAndEveryMemberCarriesItsField)
{
    // A WVP with sixteen distinct fields, so any transposition or shuffle changes the product.
    const std::array<float, 16> wvp{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
                                    9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f};
    const GpuDrawParams p = DistinctParams();
    std::array<float, 44> out{};
    out.fill(-99.0f);
    FillExtUniforms(out, FromFields(wvp), p);

    // XNA: clip = (2, -1, 0.5, 1) * WVP. By hand: x = 2*1 - 1*5 + 0.5*9 + 13 = 14.5,
    // y = 2*2 - 6 + 5 + 14 = 17, z = 6 - 7 + 5.5 + 15 = 19.5, w = 8 - 8 + 6 + 16 = 22.
    const std::array<float, 4> v{2.0f, -1.0f, 0.5f, 1.0f};
    const std::array<float, 4> expected{14.5f, 17.0f, 19.5f, 22.0f};
    ASSERT_EQ(RowTimesMatrix(v, wvp), expected) << "the hand derivation itself";
    const std::array<float, 4> shader = WgslMat4TimesVec(&out[F(0)], v);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(shader[i], expected[i]) << "u.mvp * position, component " << i;

    ExpectVec(&out[F(64)], {0.11f, 0.12f, 0.13f, 0.14f}, "diffuseColor");
    ExpectVec(&out[F(80)], {0.21f, 0.22f, 0.23f, 1.0f}, "ambientLighting (rgb, lighting enabled)");
    ExpectVec(&out[F(96)], {0.31f, 0.32f, 0.33f, 0.0f}, "light0DirTexture (dir, texture enabled)");
    ExpectVec(&out[F(112)], {0.41f, 0.42f, 0.43f, 1.0f}, "light0DiffuseVertexColor (rgb, vertex colour)");
    ExpectVec(&out[F(128)], {1.1f, 1.2f, 1.3f, 0.0f}, "fogColor");
    ExpectVec(&out[F(144)], {1.4f, 1.5f, 1.6f, 1.7f}, "fogVector");
    // samplerBias belongs to the caller (WEBGPU-205): the packer must not touch it.
    ExpectVec(&out[F(160)], {-99.0f, -99.0f, -99.0f, -99.0f}, "samplerBias");

    // The three flags really are taken from their own fields.
    GpuDrawParams flipped = p;
    flipped.lightingEnabled = false;
    flipped.textureEnabled = true;
    flipped.vertexColorEnabled = false;
    FillExtUniforms(out, FromFields(wvp), flipped);
    EXPECT_FLOAT_EQ(out[F(80) + 3], 0.0f);
    EXPECT_FLOAT_EQ(out[F(96) + 3], 1.0f);
    EXPECT_FLOAT_EQ(out[F(112) + 3], 0.0f);
}

TEST(WebGPUUniformPacking, ColoredPrimitivesUseWorldViewProjectionInXnaOrderWithFogOff)
{
    // DrawColoredPrimitives has no effect parameters: the vertex colour passes through unmodified
    // (diffuse white, vertex colour on, lighting/texture off) and fog is off (fogVector = 0).
    // The MVP is World * View * Projection applied to a row vector -- XNA's order -- so
    // v * W * V * P must come out of the shader.
    const std::array<float, 16> world{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 0, 0, 1};      // translate x+3
    const std::array<float, 16> view{2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1};       // scale 2
    const std::array<float, 16> projection{0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // rotate z +90
    std::array<float, 44> out{};
    FillColoredUniforms(out, FromFields(world), FromFields(view), FromFields(projection));

    // (1, 0, 0, 1): translate -> (4, 0, 0), scale -> (8, 0, 0), rotate +90 about z -> (0, 8, 0).
    // The other order (P * V * W) would give rotate (0, 1, 0), scale (0, 2, 0), translate (3, 2, 0).
    const std::array<float, 4> v{1.0f, 0.0f, 0.0f, 1.0f};
    const std::array<float, 4> shader = WgslMat4TimesVec(&out[F(0)], v);
    EXPECT_FLOAT_EQ(shader[0], 0.0f);
    EXPECT_FLOAT_EQ(shader[1], 8.0f);
    EXPECT_FLOAT_EQ(shader[2], 0.0f);
    EXPECT_FLOAT_EQ(shader[3], 1.0f);
    ExpectVec(&out[F(64)], {1.0f, 1.0f, 1.0f, 1.0f}, "diffuseColor");
    ExpectVec(&out[F(80)], {0.0f, 0.0f, 0.0f, 0.0f}, "ambientLighting");
    ExpectVec(&out[F(96)], {0.0f, 0.0f, 0.0f, 0.0f}, "light0DirTexture");
    ExpectVec(&out[F(112)], {0.0f, 0.0f, 0.0f, 1.0f}, "light0DiffuseVertexColor");
    ExpectVec(&out[F(128)], {0.0f, 0.0f, 0.0f, 0.0f}, "fogColor");
    ExpectVec(&out[F(144)], {0.0f, 0.0f, 0.0f, 0.0f}, "fogVector (dot -> 0 -> no fog)");
}

TEST(WebGPUUniformPacking, LitBlockPlacesLightsWorldEyeSpecularAndTheInverseTransposeNormalMatrix)
{
    const GpuDrawParams p = DistinctParams();
    std::array<float, 68> out{};
    FillLitLightUniforms(out, p);
    ExpectVec(&out[F(0)], {0.51f, 0.52f, 0.53f}, "light1Dir");
    ExpectVec(&out[F(16)], {0.61f, 0.62f, 0.63f}, "light1Diffuse");
    ExpectVec(&out[F(32)], {0.71f, 0.72f, 0.73f}, "light2Dir");
    ExpectVec(&out[F(48)], {0.81f, 0.82f, 0.83f}, "light2Diffuse");
    ExpectVec(&out[F(64)], {0.91f, 0.92f, 0.93f}, "emissiveColor");
    ExpectVec(&out[F(144)], {2.1f, 2.2f, 2.3f}, "eyePos");
    ExpectVec(&out[F(160)], {3.1f, 3.2f, 3.3f}, "light0Specular");
    ExpectVec(&out[F(176)], {3.4f, 3.5f, 3.6f}, "light1Specular");
    ExpectVec(&out[F(192)], {3.7f, 3.8f, 3.9f}, "light2Specular");
    ExpectVec(&out[F(208)], {4.1f, 4.2f, 4.3f, 24.0f}, "specularColorPower (rgb, SpecularPower)");

    // lp.world * position is XNA's position * World: (1, 1, 1) -> (0 - 4 + 0 + 5, 2 + 6, 0.5 + 7)
    // = (1, 8, 7.5).
    const std::array<float, 4> world = WgslMat4TimesVec(&out[F(80)], {1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_FLOAT_EQ(world[0], 1.0f);
    EXPECT_FLOAT_EQ(world[1], 8.0f);
    EXPECT_FLOAT_EQ(world[2], 7.5f);
    EXPECT_FLOAT_EQ(world[3], 1.0f);

    // normalMatrix * n must be A^-1 n (A = World's upper 3x3 in XNA's row layout): the
    // inverse-transpose rule written for a column vector. By the hand inverse in kWorld's comment:
    // x -> (0, 1/2, 0), y -> (-1/4, 0, 0), z -> (0, 0, 2). Geometrically: the plane x = 0 is mapped
    // onto the plane y = 0, so its normal must come out along y.
    const auto normal = [&](const std::array<float, 3>& n) {
        return WgslMat3TimesVec(&out[F(224)], &out[F(240)], &out[F(256)], n);
    };
    const std::array<std::pair<std::array<float, 3>, std::array<float, 3>>, 3> rows{{
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {-0.25f, 0.0f, 0.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 2.0f}},
    }};
    for (const auto& [n, expected] : rows)
    {
        const std::array<float, 3> actual = normal(n);
        for (std::size_t i = 0; i < 3; ++i)
            EXPECT_NEAR(actual[i], expected[i], 1e-6f) << "normal (" << n[0] << "," << n[1] << "," << n[2]
                                                     << ") component " << i;
    }
}

TEST(WebGPUUniformPacking, AlphaTestBlockCarriesTheAlphaFunctionAndVertexColourFlag)
{
    const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    const GpuDrawParams p = DistinctParams();
    std::array<float, 44> out{};
    FillAlphaTestUniforms(out, FromFields(identity), p);
    ExpectVec(&out[F(64)], {0.11f, 0.12f, 0.13f, 0.14f}, "diffuseColor");
    ExpectVec(&out[F(80)], {0.25f, 0.125f, 1.0f, -1.0f}, "alphaTest (ref, tolerance, pass, fail)");
    EXPECT_FLOAT_EQ(out[F(96)], 1.0f) << "extra.x = VertexColorEnabled";
    ExpectVec(&out[F(128)], {1.1f, 1.2f, 1.3f, 0.0f}, "fogColor");
    ExpectVec(&out[F(144)], {1.4f, 1.5f, 1.6f, 1.7f}, "fogVector");
    GpuDrawParams noVertexColour = p;
    noVertexColour.vertexColorEnabled = false;
    FillAlphaTestUniforms(out, FromFields(identity), noVertexColour);
    EXPECT_FLOAT_EQ(out[F(96)], 0.0f);
}

TEST(WebGPUUniformPacking, EnvironmentMapBlockPacksEveryMemberWhereTheShaderReadsIt)
{
    const GpuDrawParams p = DistinctParams();
    std::array<float, 64> out{};
    out.fill(-99.0f);
    FillEnvMapParams(out, p);
    ExpectVec(&out[F(0)], {2.1f, 2.2f, 2.3f}, "eyePos");
    ExpectVec(&out[F(16)], {0.11f, 0.12f, 0.13f, 0.14f}, "diffuseColor");
    ExpectVec(&out[F(32)], {0.91f, 0.92f, 0.93f, 0.375f}, "emissiveAmount (emissive, EnvironmentMapAmount)");
    ExpectVec(&out[F(48)], {0.31f, 0.32f, 0.33f}, "light0Dir");
    ExpectVec(&out[F(64)], {0.41f, 0.42f, 0.43f, 1.0f}, "light0DiffuseFresnelEn (rgb, Fresnel on)");
    ExpectVec(&out[F(80)], {4.4f, 4.5f, 4.6f, 2.5f}, "envMapSpecFresnelF (specular, FresnelFactor)");
    ExpectVec(&out[F(96)], {1.1f, 1.2f, 1.3f, 0.0f}, "fogColor");
    ExpectVec(&out[F(112)], {1.4f, 1.5f, 1.6f, 1.7f}, "fogVector");
    ExpectVec(&out[F(128)], {0.51f, 0.52f, 0.53f}, "light1Dir");
    ExpectVec(&out[F(144)], {0.61f, 0.62f, 0.63f}, "light1Diffuse");
    ExpectVec(&out[F(160)], {0.71f, 0.72f, 0.73f}, "light2Dir");
    ExpectVec(&out[F(176)], {0.81f, 0.82f, 0.83f}, "light2Diffuse");
    const std::array<float, 3> nx =
        WgslMat3TimesVec(&out[F(192)], &out[F(208)], &out[F(224)], {1.0f, 0.0f, 0.0f});
    EXPECT_NEAR(nx[0], 0.0f, 1e-6f);
    EXPECT_NEAR(nx[1], 0.5f, 1e-6f);
    EXPECT_NEAR(nx[2], 0.0f, 1e-6f);
    ExpectVec(&out[F(240)], {-99.0f, -99.0f, -99.0f, -99.0f}, "samplerBias (caller-owned)");

    std::array<float, 32> transform{};
    const std::array<float, 16> mvp{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    FillEnvMapTransform(transform, FromFields(mvp), FromFields(kWorld));
    const std::array<float, 4> clip = WgslMat4TimesVec(&transform[0], {2.0f, -1.0f, 0.5f, 1.0f});
    EXPECT_FLOAT_EQ(clip[0], 14.5f);
    EXPECT_FLOAT_EQ(clip[3], 22.0f);
    const std::array<float, 4> world = WgslMat4TimesVec(&transform[16], {1.0f, 1.0f, 1.0f, 1.0f});
    EXPECT_FLOAT_EQ(world[0], 1.0f);
    EXPECT_FLOAT_EQ(world[1], 8.0f);
    EXPECT_FLOAT_EQ(world[2], 7.5f);
}

TEST(WebGPUUniformPacking, PbrDoubleSidedSignFollowsTheWorldDeterminant)
{
    // glTF 2.0: a doubleSided material shades both faces, and a World with a negative determinant
    // mirrors the geometry, which reverses which face is front -- the shader is told by the sign.
    // Hand determinants: identity +1; scale(-1, 1, 1) -1; kWorld's upper 3x3 = 0*(0) - 2*(-4*0.5)
    // + 0 = +4.
    GpuDrawParams p = DistinctParams();
    std::array<float, 80> out{};
    p.pbrDoubleSided = false;
    FillPbrFactors(out, p);
    EXPECT_FLOAT_EQ(out[F(304)], 0.0f) << "single-sided";
    p.pbrDoubleSided = true;
    FillWorld(p, {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    FillPbrFactors(out, p);
    EXPECT_FLOAT_EQ(out[F(304)], 1.0f) << "identity World";
    FillWorld(p, {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
    FillPbrFactors(out, p);
    EXPECT_FLOAT_EQ(out[F(304)], -1.0f) << "mirroring World";
    FillWorld(p, kWorld);
    FillPbrFactors(out, p);
    EXPECT_FLOAT_EQ(out[F(304)], 1.0f) << "rotated and scaled World, determinant +4";

    // The other members land where the struct says.
    p.pbrMetallicFactor = 0.3f;
    p.pbrRoughnessFactor = 0.6f;
    p.pbrSpecularFactor = 0.9f;
    FillPbrFactors(out, p);
    EXPECT_FLOAT_EQ(out[F(0)], 0.3f) << "metallicRoughness.x";
    EXPECT_FLOAT_EQ(out[F(0) + 1], 0.6f) << "metallicRoughness.y";
    ExpectVec(&out[F(16)], {0.25f, 0.125f, 1.0f, -1.0f}, "alphaTest");
    EXPECT_FLOAT_EQ(out[F(224) + 3], 0.9f) << "specularFresnelInputs.w = specularFactor";
}

TEST(WebGPUUniformPacking, SkinningBlockCarriesWeightsPerVertexAndEveryBoneInOrder)
{
    GpuDrawParams p;
    p.weightsPerVertex = 2;
    for (int i = 0; i < 72 * 16; ++i) p.boneTransforms[i] = static_cast<float>(i) + 0.5f;
    std::array<float, 4 + 72 * 16> out{};
    FillSkinningParams(out, p);
    EXPECT_FLOAT_EQ(out[F(0)], 2.0f) << "weightsPerVertex.x";
    // bones[k] starts at byte 16 + 64 k: bone 0, bone 1 and the last bone (71).
    for (const int bone : {0, 1, 71})
        for (int e = 0; e < 16; ++e)
            EXPECT_FLOAT_EQ(out[F(16 + 64 * static_cast<std::uint32_t>(bone)) + static_cast<std::size_t>(e)],
                            static_cast<float>(bone * 16 + e) + 0.5f) << "bone " << bone << " element " << e;
}
