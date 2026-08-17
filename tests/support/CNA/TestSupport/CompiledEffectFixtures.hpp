// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/TestSupport/CompiledEffectFormat.hpp"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

/**
 * @file
 * @brief Deterministic compiled-effect fixtures for the shared backend conformance suite.
 *
 * plan_fx.md FX-060: every backend that claims `GraphicsCapability::CompiledEffects` runs the same
 * contract, and that contract needs fixtures with known reflection, known pass states and known
 * sampler assignments. These builders emit an Effect Framework 9.1 container -- and, on request, a
 * hand-assembled Shader Model 2.0 program with its Direct3D 9 constant table -- straight from the
 * documented byte layout. Nothing here depends on a parser, a renderer or a proprietary compiler,
 * so a new backend needs only its own device setup to run the suite.
 */
namespace CNA::TestSupport
{
    struct SyntheticRenderState
    {
        std::uint32_t type;
        std::uint32_t valueBits;
        bool isFloat = false;
    };

    inline void AppendUInt32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
        bytes.push_back(static_cast<std::uint8_t>(value >> 16));
        bytes.push_back(static_cast<std::uint8_t>(value >> 24));
    }

    inline void PatchUInt32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                     std::uint32_t value)
    {
        if (offset + 4 > bytes.size()) return;
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
        bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
        bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
    }

    inline std::uint32_t AppendEffectString(std::vector<std::uint8_t>& bytes,
                                     const std::string& value)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(value.size() + 1));
        bytes.insert(bytes.end(), value.begin(), value.end());
        bytes.push_back(0);
        while ((bytes.size() & 3u) != 0) bytes.push_back(0);
        return offset;
    }

    inline std::uint32_t AppendNumericType(std::vector<std::uint8_t>& bytes,
                                    std::uint32_t type,
                                    std::uint32_t parameterClass,
                                    std::uint32_t nameOffset,
                                    std::uint32_t semanticOffset,
                                    std::uint32_t elementCount,
                                    std::uint32_t columns,
                                    std::uint32_t rows)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(type));
        AppendUInt32(bytes, static_cast<std::uint32_t>(parameterClass));
        AppendUInt32(bytes, nameOffset);
        AppendUInt32(bytes, semanticOffset);
        AppendUInt32(bytes, elementCount);
        AppendUInt32(bytes, columns);
        AppendUInt32(bytes, rows);
        return offset;
    }

    inline std::uint32_t AppendScalarType(std::vector<std::uint8_t>& bytes,
                                   std::uint32_t type,
                                   std::uint32_t nameOffset,
                                   std::uint32_t semanticOffset,
                                   std::uint32_t elementCount = 0)
    {
        return AppendNumericType(bytes, type, EffectFormat::ClassScalar,
                                 nameOffset, semanticOffset, elementCount, 1, 1);
    }

    inline std::uint32_t FloatBits(float value);

    inline std::uint32_t AppendLightingStructType(std::vector<std::uint8_t>& bytes,
                                           std::uint32_t nameOffset,
                                           std::uint32_t intensityName,
                                           std::uint32_t directionName,
                                           std::uint32_t thresholdsName,
                                           std::uint32_t empty)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(EffectFormat::TypeVoid));
        AppendUInt32(bytes, static_cast<std::uint32_t>(EffectFormat::ClassStruct));
        AppendUInt32(bytes, nameOffset);
        AppendUInt32(bytes, empty); // semantic
        AppendUInt32(bytes, 0); // elements
        AppendUInt32(bytes, 3); // members

        auto appendMember = [&](std::uint32_t parameterClass,
                                std::uint32_t memberName, std::uint32_t elements,
                                std::uint32_t columns, std::uint32_t rows)
        {
            AppendUInt32(bytes, static_cast<std::uint32_t>(EffectFormat::TypeFloat));
            AppendUInt32(bytes, static_cast<std::uint32_t>(parameterClass));
            AppendUInt32(bytes, memberName);
            AppendUInt32(bytes, empty); // semantic
            AppendUInt32(bytes, elements);
            AppendUInt32(bytes, columns);
            AppendUInt32(bytes, rows);
        };
        // The Effect Framework struct encoding stores the concrete element multiplicity for each
        // member, including one for non-array members (unlike top-level numeric declarations,
        // where zero means non-array).
        appendMember(EffectFormat::ClassScalar, intensityName, 1, 1, 1);
        appendMember(EffectFormat::ClassVector, directionName, 1, 3, 1);
        appendMember(EffectFormat::ClassScalar, thresholdsName, 2, 1, 1);

        // Struct defaults live immediately after the member metadata. MojoShader expands every
        // member row to a float4 register while parsing this tight compiler representation.
        for (const float value : {0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f})
            AppendUInt32(bytes, FloatBits(value));
        return offset;
    }

    inline std::uint32_t AppendValueBits(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, value);
        return offset;
    }

    inline std::uint32_t FloatBits(float value)
    {
        std::uint32_t result = 0;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    inline std::uint32_t AppendFloatValues(std::vector<std::uint8_t>& bytes,
                                    std::initializer_list<float> values)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        for (const float value : values) AppendUInt32(bytes, FloatBits(value));
        return offset;
    }

    /** @brief One sampler-state assignment written into the fixture's sampler parameter. */
    struct SyntheticSamplerState
    {
        std::uint32_t type;
        std::uint32_t valueBits;
        bool isFloat = false;
    };

    /** @brief What the synthetic conformance fixture should contain. */
    struct SyntheticEffectOptions
    {
        std::vector<SyntheticRenderState> renderStates;
        /// Adds an FxTexture parameter, an FxSampler parameter and a real Shader Model 2.0
        /// pixel-shader object, which is what makes MojoShader report sampler state registers.
        bool includeSampler = false;
        std::vector<SyntheticSamplerState> samplerStates;
        std::uint32_t samplerRegister = 0;
        /// Names a constant in the shader's constant table that no effect parameter declares.
        /// MojoShader then fails while it is already several objects into building the effect,
        /// which is the deterministic mid-construction failure the lifecycle suite needs.
        bool breakShaderSymbolBinding = false;
        /// plan_fx.md FX-084: adds a hand-assembled Shader Model 2.0 **vertex** shader alongside
        /// the pixel shader, so `StatePass` binds a complete program pair and the fixture can
        /// actually be DRAWN. Without it the suite could only observe reflection and state, never
        /// whether the compiled shader is the one that ran.
        ///
        /// The vertex shader is `oPos = mul(POSITION0, Transform)`, using the `Transform`
        /// parameter the fixture already declares; the pixel shader writes `Tint` unchanged. So a
        /// full-target quad in the space `Transform` maps to NDC comes out exactly `Tint`, which
        /// no stock shader in any CNA renderer would produce for the same inputs.
        bool includeDrawableProgram = false;
        /// plan_fx.md FX-084: the vertex shader additionally consumes TEXCOORD0, scaled by a new
        /// `StreamMix` float4 parameter that defaults to zero -- `oPos = mul(TEXCOORD0 * StreamMix
        /// + POSITION0, Transform)`. That is what makes a genuine multi-stream compiled-effect
        /// draw observable: with `StreamMix` at zero the second stream contributes nothing, and
        /// with it set the geometry moves by exactly the second stream's own values, so binding
        /// that stream from the wrong buffer, stride or offset changes the pixels.
        bool vertexShaderReadsSecondStream = false;
    };

    inline std::uint32_t AppendObjectType(std::vector<std::uint8_t>& bytes,
                                   std::uint32_t type,
                                   std::uint32_t nameOffset,
                                   std::uint32_t semanticOffset)
    {
        const auto offset = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, static_cast<std::uint32_t>(type));
        AppendUInt32(bytes, static_cast<std::uint32_t>(EffectFormat::ClassObject));
        AppendUInt32(bytes, nameOffset);
        AppendUInt32(bytes, semanticOffset);
        AppendUInt32(bytes, 0); // elements
        return offset;
    }

    inline void AppendUInt16(std::vector<std::uint8_t>& bytes, std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    }

    /**
     * Assembles a Shader Model 2.0 pixel-shader program, including the Direct3D 9 constant table
     * its Effect Framework container is reflected from. The Effect Framework derives every
     * sampler state register from those CTAB symbols, so a sampler conformance fixture needs a
     * real program even though the shader body itself is trivial. `mov oC0, c0` keeps the
     * generated shader valid on every profile a backend may select at runtime.
     */
    inline std::vector<std::uint8_t> BuildSyntheticPixelShader(std::uint32_t samplerRegister,
                                                       bool breakSymbolBinding = false,
                                                       bool includeSampler = true)
    {
        constexpr std::uint32_t versionToken = 0xFFFF0200u;
        const int constantCount = includeSampler ? 2 : 1;
        std::vector<std::uint8_t> ctab;
        AppendUInt32(ctab, 28);           // 0  sizeof(D3DXSHADER_CONSTANTTABLE)
        AppendUInt32(ctab, 0);            // 4  Creator, patched below
        AppendUInt32(ctab, versionToken); // 8  Version -- must equal the shader version token
        AppendUInt32(ctab, static_cast<std::uint32_t>(constantCount)); // 12 Constants
        AppendUInt32(ctab, 28);           // 16 ConstantInfo offset
        AppendUInt32(ctab, 0);            // 20 Flags
        AppendUInt32(ctab, 0);            // 24 Target, patched below

        const auto constantInfo = static_cast<std::uint32_t>(ctab.size());
        for (int i = 0; i < constantCount; ++i)
        {
            AppendUInt32(ctab, 0); // Name, patched below
            AppendUInt16(ctab, 0); // RegisterSet, patched below
            AppendUInt16(ctab, 0); // RegisterIndex, patched below
            AppendUInt16(ctab, 1); // RegisterCount
            AppendUInt16(ctab, 0); // Reserved
            AppendUInt32(ctab, 0); // TypeInfo, patched below
            AppendUInt32(ctab, 0); // DefaultValue
        }

        const auto tintType = static_cast<std::uint32_t>(ctab.size());
        AppendUInt16(ctab, EffectFormat::ClassVector);
        AppendUInt16(ctab, EffectFormat::TypeFloat);
        AppendUInt16(ctab, 1); // rows
        AppendUInt16(ctab, 4); // columns
        AppendUInt16(ctab, 1); // elements
        AppendUInt16(ctab, 0); // struct members
        AppendUInt32(ctab, 0); // struct member info

        const auto samplerType = static_cast<std::uint32_t>(ctab.size());
        AppendUInt16(ctab, EffectFormat::ClassObject);
        AppendUInt16(ctab, EffectFormat::TypeSampler2D);
        AppendUInt16(ctab, 1);
        AppendUInt16(ctab, 1);
        AppendUInt16(ctab, 1);
        AppendUInt16(ctab, 0);
        AppendUInt32(ctab, 0);

        const auto appendCtabString = [&ctab](const std::string& value) {
            const auto offset = static_cast<std::uint32_t>(ctab.size());
            ctab.insert(ctab.end(), value.begin(), value.end());
            ctab.push_back(0);
            return offset;
        };
        const std::uint32_t tintName =
            appendCtabString(breakSymbolBinding ? "NoSuchParameter" : "Tint");
        const std::uint32_t samplerName = appendCtabString("FxSampler");
        const std::uint32_t target = appendCtabString("ps_2_0");
        const std::uint32_t creator = appendCtabString("CNA synthetic conformance fixture");
        while ((ctab.size() & 3u) != 0) ctab.push_back(0);

        PatchUInt32(ctab, 4, creator);
        PatchUInt32(ctab, 24, target);
        PatchUInt32(ctab, constantInfo, tintName);
        ctab[constantInfo + 4] = 2; // RegisterSet: float
        ctab[constantInfo + 6] = 0; // RegisterIndex c0
        PatchUInt32(ctab, constantInfo + 12, tintType);
        if (includeSampler)
        {
            PatchUInt32(ctab, constantInfo + 20, samplerName);
            ctab[constantInfo + 24] = 3; // RegisterSet: sampler
            ctab[constantInfo + 26] = static_cast<std::uint8_t>(samplerRegister);
            PatchUInt32(ctab, constantInfo + 32, samplerType);
        }

        std::vector<std::uint8_t> shader;
        AppendUInt32(shader, versionToken);
        AppendUInt32(shader, 0x0000FFFEu |
                                 ((1u + static_cast<std::uint32_t>(ctab.size() / 4)) << 16));
        AppendUInt32(shader, 0x42415443u); // 'CTAB'
        shader.insert(shader.end(), ctab.begin(), ctab.end());

        // mov oC0, c0 -- one destination and one source token.
        constexpr std::uint32_t colorOut = 8;
        constexpr std::uint32_t constantRegister = 2;
        const auto registerBits = [](std::uint32_t type) {
            return ((type & 0x7u) << 28) | ((type >> 3) << 11);
        };
        AppendUInt32(shader, 0x00000001u | (2u << 24));
        AppendUInt32(shader, 0x80000000u | registerBits(colorOut) | (0xFu << 16));
        AppendUInt32(shader, 0x80000000u | registerBits(constantRegister) | (0xE4u << 16));
        AppendUInt32(shader, 0x0000FFFFu);
        return shader;
    }

    /**
     * Assembles a Shader Model 2.0 vertex-shader program and its Direct3D 9 constant table.
     *
     * plan_fx.md FX-084. The program is
     * `oPos = mul(POSITION0 + TEXCOORD0 * StreamMix, Transform)` (the TEXCOORD0 term only when
     * @p readsSecondStream), written straight in Direct3D 9 shader tokens so the shared
     * conformance suite stays free of any compiler dependency, exactly like its pixel-shader
     * sibling above.
     *
     * @param readsSecondStream Whether the shader declares and consumes a TEXCOORD0 input.
     * @return The complete vertex-shader token buffer.
     */
    inline std::vector<std::uint8_t> BuildSyntheticVertexShader(bool readsSecondStream)
    {
        constexpr std::uint32_t versionToken = 0xFFFE0200u;
        const std::uint32_t constantCount = readsSecondStream ? 2u : 1u;

        std::vector<std::uint8_t> ctab;
        AppendUInt32(ctab, 28);                 // 0  sizeof(D3DXSHADER_CONSTANTTABLE)
        AppendUInt32(ctab, 0);                  // 4  Creator, patched below
        AppendUInt32(ctab, versionToken);       // 8  Version -- must equal the shader version
        AppendUInt32(ctab, constantCount);      // 12 Constants
        AppendUInt32(ctab, 28);                 // 16 ConstantInfo offset
        AppendUInt32(ctab, 0);                  // 20 Flags
        AppendUInt32(ctab, 0);                  // 24 Target, patched below

        const auto constantInfo = static_cast<std::uint32_t>(ctab.size());
        for (std::uint32_t i = 0; i < constantCount; ++i)
        {
            AppendUInt32(ctab, 0); // Name, patched below
            AppendUInt16(ctab, 0); // RegisterSet, patched below
            AppendUInt16(ctab, 0); // RegisterIndex, patched below
            AppendUInt16(ctab, 0); // RegisterCount, patched below
            AppendUInt16(ctab, 0); // Reserved
            AppendUInt32(ctab, 0); // TypeInfo, patched below
            AppendUInt32(ctab, 0); // DefaultValue
        }

        const auto transformType = static_cast<std::uint32_t>(ctab.size());
        AppendUInt16(ctab, EffectFormat::ClassMatrixColumns);
        AppendUInt16(ctab, EffectFormat::TypeFloat);
        AppendUInt16(ctab, 4); // rows
        AppendUInt16(ctab, 4); // columns
        AppendUInt16(ctab, 1); // elements
        AppendUInt16(ctab, 0); // struct members
        AppendUInt32(ctab, 0); // struct member info

        const auto streamMixType = static_cast<std::uint32_t>(ctab.size());
        AppendUInt16(ctab, EffectFormat::ClassVector);
        AppendUInt16(ctab, EffectFormat::TypeFloat);
        AppendUInt16(ctab, 1); // rows
        AppendUInt16(ctab, 4); // columns
        AppendUInt16(ctab, 1); // elements
        AppendUInt16(ctab, 0); // struct members
        AppendUInt32(ctab, 0); // struct member info

        const auto appendCtabString = [&ctab](const std::string& value) {
            const auto offset = static_cast<std::uint32_t>(ctab.size());
            ctab.insert(ctab.end(), value.begin(), value.end());
            ctab.push_back(0);
            return offset;
        };
        const std::uint32_t transformName = appendCtabString("Transform");
        const std::uint32_t streamMixName = appendCtabString("StreamMix");
        const std::uint32_t target = appendCtabString("vs_2_0");
        const std::uint32_t creator = appendCtabString("CNA synthetic conformance fixture");
        while ((ctab.size() & 3u) != 0) ctab.push_back(0);

        PatchUInt32(ctab, 4, creator);
        PatchUInt32(ctab, 24, target);
        PatchUInt32(ctab, constantInfo, transformName);
        ctab[constantInfo + 4] = 2;  // RegisterSet: float
        ctab[constantInfo + 6] = 0;  // RegisterIndex c0
        ctab[constantInfo + 8] = 4;  // RegisterCount: four rows of the matrix
        PatchUInt32(ctab, constantInfo + 12, transformType);
        if (readsSecondStream)
        {
            PatchUInt32(ctab, constantInfo + 20, streamMixName);
            ctab[constantInfo + 24] = 2;  // RegisterSet: float
            ctab[constantInfo + 26] = 4;  // RegisterIndex c4
            ctab[constantInfo + 28] = 1;  // RegisterCount
            PatchUInt32(ctab, constantInfo + 32, streamMixType);
        }

        // Direct3D 9 shader-token register types, and the two token shapes every instruction
        // below is built from. Identical encoding to BuildSyntheticPixelShader's own.
        constexpr std::uint32_t regTemp = 0;
        constexpr std::uint32_t regInput = 1;
        constexpr std::uint32_t regConst = 2;
        constexpr std::uint32_t regRastOut = 4;
        const auto registerBits = [](std::uint32_t type) {
            return ((type & 0x7u) << 28) | ((type >> 3) << 11);
        };
        const auto destination = [&registerBits](std::uint32_t type, std::uint32_t number) {
            return 0x80000000u | registerBits(type) | number | (0xFu << 16);
        };
        const auto source = [&registerBits](std::uint32_t type, std::uint32_t number) {
            return 0x80000000u | registerBits(type) | number | (0xE4u << 16);
        };

        std::vector<std::uint8_t> shader;
        AppendUInt32(shader, versionToken);
        AppendUInt32(shader, 0x0000FFFEu |
                                 ((1u + static_cast<std::uint32_t>(ctab.size() / 4)) << 16));
        AppendUInt32(shader, 0x42415443u); // 'CTAB'
        shader.insert(shader.end(), ctab.begin(), ctab.end());

        // dcl_position v0
        AppendUInt32(shader, 0x0000001Fu | (2u << 24));
        AppendUInt32(shader, 0x80000000u | 0u);            // D3DDECLUSAGE_POSITION, index 0
        AppendUInt32(shader, destination(regInput, 0));
        if (readsSecondStream)
        {
            // dcl_texcoord v1
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | 5u);        // D3DDECLUSAGE_TEXCOORD, index 0
            AppendUInt32(shader, destination(regInput, 1));
            // mad r0, v1, c4, v0
            AppendUInt32(shader, 0x00000004u | (4u << 24));
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regInput, 1));
            AppendUInt32(shader, source(regConst, 4));
            AppendUInt32(shader, source(regInput, 0));
        }
        // m4x4 oPos, <r0|v0>, c0
        AppendUInt32(shader, 0x00000014u | (3u << 24));
        AppendUInt32(shader, destination(regRastOut, 0));
        AppendUInt32(shader, readsSecondStream ? source(regTemp, 0) : source(regInput, 0));
        AppendUInt32(shader, source(regConst, 0));
        AppendUInt32(shader, 0x0000FFFFu);
        return shader;
    }

    /**
     * Builds a small Effect Framework 9.1 container directly from the documented layout consumed
     * by CNA's pinned MojoShader. State-only passes make it a deterministic conformance fixture
     * for public reflection, exact pass identity, and every render-state translation without
     * redistributing a proprietary compiler or compiler-produced program; requesting a sampler
     * additionally emits a hand-assembled Shader Model 2.0 program so the sampler-state
     * translation can be observed too.
     */
    inline std::vector<std::uint8_t> BuildSyntheticEffect(const SyntheticEffectOptions& options)
    {
        const std::vector<SyntheticRenderState>& renderStates = options.renderStates;
        std::vector<std::uint8_t> bytes;
        AppendUInt32(bytes, 0xFEFF0901u);
        AppendUInt32(bytes, 0); // structure offset, patched below

        const std::uint32_t empty = static_cast<std::uint32_t>(bytes.size() - 8);
        AppendUInt32(bytes, 0);
        const std::uint32_t gainName = AppendEffectString(bytes, "Gain");
        const std::uint32_t gainSemantic = AppendEffectString(bytes, "SCALAR");
        const std::uint32_t tintName = AppendEffectString(bytes, "Tint");
        const std::uint32_t transformName = AppendEffectString(bytes, "Transform");
        const std::uint32_t weightsName = AppendEffectString(bytes, "Weights");
        const std::uint32_t lightingName = AppendEffectString(bytes, "Lighting");
        const std::uint32_t intensityName = AppendEffectString(bytes, "Intensity");
        const std::uint32_t directionName = AppendEffectString(bytes, "Direction");
        const std::uint32_t thresholdsName = AppendEffectString(bytes, "Thresholds");
        const std::uint32_t visibleName = AppendEffectString(bytes, "Visible");
        const std::uint32_t qualityName = AppendEffectString(bytes, "Quality");
        const std::uint32_t passTagName = AppendEffectString(bytes, "PassTag");
        const std::uint32_t firstTechnique = AppendEffectString(bytes, "FirstTechnique");
        const std::uint32_t secondTechnique = AppendEffectString(bytes, "SecondTechnique");
        const std::uint32_t firstPass = AppendEffectString(bytes, "P0");
        const std::uint32_t statePass = AppendEffectString(bytes, "StatePass");
        const std::uint32_t secondPass = AppendEffectString(bytes, "P1");
        const std::uint32_t textureName = AppendEffectString(bytes, "FxTexture");
        const std::uint32_t samplerName = AppendEffectString(bytes, "FxSampler");
        const std::uint32_t streamMixName = AppendEffectString(bytes, "StreamMix");

        // plan_fx.md FX-084: a drawable fixture needs a shader pair on StatePass; the multi-stream
        // variant additionally declares StreamMix, the parameter its vertex shader scales
        // TEXCOORD0 by. Both are opt-in so the reflection contract's parameter/object counts, and
        // every existing assertion about them, are untouched by this addition.
        const bool includeProgram = options.includeSampler || options.includeDrawableProgram;
        const bool includeVertexShader = options.includeDrawableProgram;
        const bool includeStreamMix =
            options.includeDrawableProgram && options.vertexShaderReadsSecondStream;

        const std::uint32_t unnamedIntType =
            AppendScalarType(bytes, EffectFormat::TypeInt, empty, empty);
        const std::uint32_t unnamedFloatType =
            AppendScalarType(bytes, EffectFormat::TypeFloat, empty, empty);
        const std::uint32_t gainType =
            AppendScalarType(bytes, EffectFormat::TypeFloat, gainName, gainSemantic);
        const std::uint32_t tintType = AppendNumericType(
            bytes, EffectFormat::TypeFloat, EffectFormat::ClassVector,
            tintName, empty, 0, 4, 1);
        const std::uint32_t transformType = AppendNumericType(
            bytes, EffectFormat::TypeFloat, EffectFormat::ClassMatrixColumns,
            transformName, empty, 0, 4, 4);
        const std::uint32_t weightsType =
            AppendScalarType(bytes, EffectFormat::TypeFloat, weightsName, empty, 2);
        const std::uint32_t lightingType = AppendLightingStructType(
            bytes, lightingName, intensityName, directionName, thresholdsName, empty);
        const std::uint32_t visibleType =
            AppendScalarType(bytes, EffectFormat::TypeBool, visibleName, empty);
        const std::uint32_t qualityType =
            AppendScalarType(bytes, EffectFormat::TypeInt, qualityName, empty);
        const std::uint32_t passTagType =
            AppendScalarType(bytes, EffectFormat::TypeInt, passTagName, empty);
        const std::uint32_t textureType =
            AppendObjectType(bytes, EffectFormat::TypeTexture2D, textureName, empty);
        const std::uint32_t samplerType =
            AppendObjectType(bytes, EffectFormat::TypeSampler2D, samplerName, empty);
        const std::uint32_t stateTextureType =
            AppendObjectType(bytes, EffectFormat::TypeTexture2D, empty, empty);
        const std::uint32_t pixelShaderType =
            AppendObjectType(bytes, EffectFormat::TypePixelShader, empty, empty);
        const std::uint32_t vertexShaderType =
            AppendObjectType(bytes, EffectFormat::TypeVertexShader, empty, empty);
        const std::uint32_t streamMixType = AppendNumericType(
            bytes, EffectFormat::TypeFloat, EffectFormat::ClassVector,
            streamMixName, empty, 0, 4, 1);

        const std::uint32_t gainValue = AppendValueBits(bytes, FloatBits(0.25f));
        const std::uint32_t tintValue =
            AppendFloatValues(bytes, {0.1f, 0.2f, 0.3f, 0.4f});
        const std::uint32_t transformValue = AppendFloatValues(bytes, {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        });
        const std::uint32_t weightsValue = AppendFloatValues(bytes, {0.2f, 0.8f});
        const std::uint32_t visibleValue = AppendValueBits(bytes, 1);
        const std::uint32_t qualityValue = AppendValueBits(bytes, 7);
        const std::uint32_t passTagValue = AppendValueBits(bytes, 3);
        // A zero default keeps the second stream's contribution off unless a test asks for it.
        const std::uint32_t streamMixValue =
            AppendFloatValues(bytes, {0.0f, 0.0f, 0.0f, 0.0f});
        std::vector<std::uint32_t> stateValueOffsets;
        stateValueOffsets.reserve(renderStates.size());
        for (const auto& state : renderStates)
            stateValueOffsets.push_back(AppendValueBits(bytes, state.valueBits));

        // Object indices: 0 stays unused (the Effect Framework reserves it); the rest are packed
        // in emission order so no index is ever declared without a record behind it.
        constexpr std::uint32_t textureObjectIndex = 1;
        const std::uint32_t pixelShaderObjectIndex = options.includeSampler ? 2u : 1u;
        const std::uint32_t vertexShaderObjectIndex = pixelShaderObjectIndex + 1u;
        const std::uint32_t objectCount = includeProgram
            ? (includeVertexShader ? vertexShaderObjectIndex + 1u : pixelShaderObjectIndex + 1u)
            : 0u;
        std::uint32_t textureValue = 0;
        std::uint32_t samplerValue = 0;
        // One value dword per pass that references a shader object, so no two pass states share
        // storage: three passes exist and each names the program independently.
        std::uint32_t pixelShaderValues[3] = {0, 0, 0};
        std::uint32_t vertexShaderValues[3] = {0, 0, 0};
        if (includeProgram)
        {
            for (std::uint32_t& value : pixelShaderValues)
                value = AppendValueBits(bytes, pixelShaderObjectIndex);
        }
        if (includeVertexShader)
        {
            for (std::uint32_t& value : vertexShaderValues)
                value = AppendValueBits(bytes, vertexShaderObjectIndex);
        }
        if (options.includeSampler)
        {
            textureValue = AppendValueBits(bytes, textureObjectIndex);
            const std::uint32_t samplerTextureValue =
                AppendValueBits(bytes, textureObjectIndex);
            std::vector<std::uint32_t> samplerValueOffsets;
            samplerValueOffsets.reserve(options.samplerStates.size());
            for (const auto& state : options.samplerStates)
                samplerValueOffsets.push_back(AppendValueBits(bytes, state.valueBits));

            samplerValue = static_cast<std::uint32_t>(bytes.size() - 8);
            AppendUInt32(bytes, static_cast<std::uint32_t>(options.samplerStates.size() + 1));
            // The texture assignment always comes first: it is what binds the reflected
            // FxTexture parameter to this sampler register.
            AppendUInt32(bytes, EffectFormat::SampTexture);
            AppendUInt32(bytes, 0); // ignored legacy field
            AppendUInt32(bytes, stateTextureType);
            AppendUInt32(bytes, samplerTextureValue);
            for (std::size_t i = 0; i < options.samplerStates.size(); ++i)
            {
                AppendUInt32(bytes, options.samplerStates[i].type);
                AppendUInt32(bytes, 0); // ignored legacy field
                AppendUInt32(bytes, options.samplerStates[i].isFloat
                                          ? unnamedFloatType : unnamedIntType);
                AppendUInt32(bytes, samplerValueOffsets[i]);
            }
        }

        const auto structureOffset = static_cast<std::uint32_t>(bytes.size() - 8);
        PatchUInt32(bytes, 4, structureOffset);
        AppendUInt32(bytes, 5u + (options.includeSampler ? 2u : 0u) +
                                (includeStreamMix ? 1u : 0u)); // parameters
        AppendUInt32(bytes, 2); // techniques
        AppendUInt32(bytes, 0); // ignored legacy count
        AppendUInt32(bytes, objectCount); // objects

        AppendUInt32(bytes, gainType);
        AppendUInt32(bytes, gainValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, visibleType);
        AppendUInt32(bytes, visibleValue);

        AppendUInt32(bytes, tintType);
        AppendUInt32(bytes, tintValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, lightingType);
        AppendUInt32(bytes, empty); // ignored for struct values; defaults follow type metadata
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, transformType);
        AppendUInt32(bytes, transformValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        AppendUInt32(bytes, weightsType);
        AppendUInt32(bytes, weightsValue);
        AppendUInt32(bytes, 0); // flags
        AppendUInt32(bytes, 0); // annotations

        if (options.includeSampler)
        {
            AppendUInt32(bytes, textureType);
            AppendUInt32(bytes, textureValue);
            AppendUInt32(bytes, 0); // flags
            AppendUInt32(bytes, 0); // annotations

            AppendUInt32(bytes, samplerType);
            AppendUInt32(bytes, samplerValue);
            AppendUInt32(bytes, 0); // flags
            AppendUInt32(bytes, 0); // annotations
        }

        if (includeStreamMix)
        {
            AppendUInt32(bytes, streamMixType);
            AppendUInt32(bytes, streamMixValue);
            AppendUInt32(bytes, 0); // flags
            AppendUInt32(bytes, 0); // annotations
        }

        AppendUInt32(bytes, firstTechnique);
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, 2); // passes
        AppendUInt32(bytes, qualityType);
        AppendUInt32(bytes, qualityValue);

        // plan_fx.md FX-084: a drawable fixture binds the same program in every pass, so any pass a
        // contract applies -- including the one SpriteBatch picks for itself -- has a shader pair.
        const auto appendProgramStates = [&](int passOrdinal) {
            if (includeVertexShader)
            {
                AppendUInt32(bytes, EffectFormat::RsVertexShader);
                AppendUInt32(bytes, 0); // ignored legacy field
                AppendUInt32(bytes, vertexShaderType);
                AppendUInt32(bytes, vertexShaderValues[passOrdinal]);
            }
            if (includeProgram)
            {
                AppendUInt32(bytes, EffectFormat::RsPixelShader);
                AppendUInt32(bytes, 0); // ignored legacy field
                AppendUInt32(bytes, pixelShaderType);
                AppendUInt32(bytes, pixelShaderValues[passOrdinal]);
            }
        };
        const std::uint32_t programStateCount =
            (includeProgram ? 1u : 0u) + (includeVertexShader ? 1u : 0u);
        // Only the drawable variant repeats the program on the other two passes: the sampler
        // fixture's own reflection and pass-state expectations are unchanged by this addition.
        const std::uint32_t otherPassStateCount =
            options.includeDrawableProgram ? programStateCount : 0u;

        AppendUInt32(bytes, firstPass);
        AppendUInt32(bytes, 0); // annotations
        AppendUInt32(bytes, otherPassStateCount);
        if (otherPassStateCount > 0) appendProgramStates(0);

        AppendUInt32(bytes, statePass);
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, static_cast<std::uint32_t>(renderStates.size()) + programStateCount);
        AppendUInt32(bytes, passTagType);
        AppendUInt32(bytes, passTagValue);
        for (std::size_t i = 0; i < renderStates.size(); ++i)
        {
            AppendUInt32(bytes, renderStates[i].type);
            AppendUInt32(bytes, 0); // ignored legacy field
            AppendUInt32(bytes, renderStates[i].isFloat
                                      ? unnamedFloatType : unnamedIntType);
            AppendUInt32(bytes, stateValueOffsets[i]);
        }
        appendProgramStates(1);

        AppendUInt32(bytes, secondTechnique);
        AppendUInt32(bytes, 0); // annotations
        AppendUInt32(bytes, 1); // passes
        AppendUInt32(bytes, secondPass);
        AppendUInt32(bytes, 0); // annotations
        AppendUInt32(bytes, otherPassStateCount);
        if (otherPassStateCount > 0) appendProgramStates(2);

        if (!includeProgram)
        {
            AppendUInt32(bytes, 0); // small objects
            AppendUInt32(bytes, 0); // large objects
            return bytes;
        }

        AppendUInt32(bytes, 1u + (options.includeSampler ? 1u : 0u) +
                                (includeVertexShader ? 1u : 0u)); // small objects
        AppendUInt32(bytes, 0); // large objects

        if (options.includeSampler)
        {
            // Object 1 carries only the name of the texture the sampler maps to.
            const std::string mappedTexture = "FxTexture";
            AppendUInt32(bytes, textureObjectIndex);
            AppendUInt32(bytes, static_cast<std::uint32_t>(mappedTexture.size() + 1));
            bytes.insert(bytes.end(), mappedTexture.begin(), mappedTexture.end());
            bytes.push_back(0);
            while ((bytes.size() & 3u) != 0) bytes.push_back(0);
        }

        const std::vector<std::uint8_t> shader = BuildSyntheticPixelShader(
            options.samplerRegister, options.breakShaderSymbolBinding, options.includeSampler);
        AppendUInt32(bytes, pixelShaderObjectIndex);
        AppendUInt32(bytes, static_cast<std::uint32_t>(shader.size()));
        bytes.insert(bytes.end(), shader.begin(), shader.end());

        if (includeVertexShader)
        {
            const std::vector<std::uint8_t> vertexShader =
                BuildSyntheticVertexShader(options.vertexShaderReadsSecondStream);
            AppendUInt32(bytes, vertexShaderObjectIndex);
            AppendUInt32(bytes, static_cast<std::uint32_t>(vertexShader.size()));
            bytes.insert(bytes.end(), vertexShader.begin(), vertexShader.end());
        }
        return bytes;
    }

    inline std::vector<std::uint8_t> BuildSyntheticConformanceEffect(
        const std::vector<SyntheticRenderState>& renderStates)
    {
        SyntheticEffectOptions options;
        options.renderStates = renderStates;
        return BuildSyntheticEffect(options);
    }

    /**
     * @brief plan_fx.md FX-084: the conformance fixture with a real, drawable program pair.
     *
     * `StatePass` (technique 0, pass 1) binds `oPos = mul(POSITION0, Transform)` and a pixel
     * shader that writes `Tint` unchanged, so a full-target quad comes out exactly `Tint` and a
     * draw that silently used a stock shader instead cannot produce that colour.
     *
     * @param readsSecondStream Whether the vertex shader also consumes TEXCOORD0, scaled by the
     *        `StreamMix` parameter this option adds -- the multi-stream draw contract's fixture.
     * @return The complete effect bytecode.
     */
    inline std::vector<std::uint8_t> BuildSyntheticDrawableEffect(bool readsSecondStream = false)
    {
        SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.vertexShaderReadsSecondStream = readsSecondStream;
        return BuildSyntheticEffect(options);
    }

}
