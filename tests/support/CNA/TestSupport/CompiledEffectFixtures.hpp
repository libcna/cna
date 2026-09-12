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
 * plans/plan_fx.md FX-060: every backend that claims `GraphicsCapability::CompiledEffects` runs the same
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
        std::uint32_t target = 0;
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
        // Effect Framework uses zero for a non-array structure member, just as it does for a
        // top-level value. This is observable through EffectParameter.Elements and matches the
        // committed fxc-produced conformance effect's recorded FNA reflection.
        appendMember(EffectFormat::ClassScalar, intensityName, 0, 1, 1);
        appendMember(EffectFormat::ClassVector, directionName, 0, 3, 1);
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

    /**
     * @brief The dimension of the sampler a fixture declares. CNAEXT.
     *
     * plans/plan_fx.md FX-110. XNA's `Texture2D`, `Texture3D` and `TextureCube` all reach a compiled
     * Effect through the same texture parameter, and MojoShader reflects the shader's own
     * expectation as `MOJOSHADER_SAMPLER_2D` / `_VOLUME` / `_CUBE`. A backend has to match the two.
     */
    enum class SyntheticSamplerKind
    {
        /** @brief `sampler2D`, a two-component coordinate. */
        Sampler2D,
        /** @brief `samplerCUBE`, a three-component direction. */
        SamplerCube,
        /** @brief `sampler3D`, a three-component volume coordinate. */
        Sampler3D,
    };

    /** @brief Legacy dependent-coordinate texture remap emitted by the synthetic pixel shader. */
    enum class SyntheticLegacyTextureRemap
    {
        /** @brief Do not emit a legacy component-remap texture instruction. */
        None,
        /** @brief Emit `TEXREG2AR`, selecting source alpha/red as u/v. */
        AlphaRed,
        /** @brief Emit `TEXREG2GB`, selecting source green/blue as u/v. */
        GreenBlue,
    };

    /** @brief Legacy dependent texture/dot instruction emitted by the synthetic pixel shader. */
    enum class SyntheticLegacyDependentTexture
    {
        /** @brief Do not emit a legacy dependent texture/dot instruction. */
        None,
        /** @brief Emit `TEXREG2RGB`, using source RGB as a dependent texture coordinate. */
        RegisterRgb,
        /** @brief Emit `TEXDP3TEX`, using a texture-coordinate dot product for a 1D lookup. */
        DotSample,
        /** @brief Emit `TEXDP3`, replicating a texture-coordinate dot product to RGBA. */
        Dot,
    };

    /** @brief Legacy bump-environment instruction emitted by the synthetic pixel shader. */
    enum class SyntheticLegacyBumpEnvironment
    {
        /** @brief Do not emit a legacy bump-environment instruction. */
        None,
        /** @brief Emit ps_1_2 `TEXBEM`. */
        Texture,
        /** @brief Emit ps_1_2 `TEXBEML`. */
        TextureLuminance,
        /** @brief Emit ps_1_4 arithmetic `BEM`. */
        Arithmetic,
        /** @brief Invalidly read a `TEXBEM` source through ordinary arithmetic afterward. */
        TextureReadSourceLater,
        /** @brief Invalidly read a `TEXBEML` source through ordinary arithmetic afterward. */
        TextureLuminanceReadSourceLater,
        /** @brief Legally read a consumed source through another bump-environment instruction. */
        TextureReadSourceByBumpLater,
    };

    /** @brief Shader Model 1.4 `BEM` operand/component contract probe. */
    enum class SyntheticBemOperandProbe
    {
        /** @brief Emit no dedicated `BEM` operand probe. */
        None,
        /** @brief Invalidly use a texture register as source zero. */
        Source0Texture,
        /** @brief Invalidly use a constant register as source one. */
        Source1Constant,
        /** @brief Invalidly use a texture register as source one. */
        Source1Texture,
        /** @brief Read an uninitialized source-zero Y component. */
        Source0UninitializedY,
        /** @brief Read an uninitialized source-one Y component. */
        Source1UninitializedY,
        /** @brief Use the valid constant/temporary form without an explicit phase marker. */
        ConstantTemporary,
        /** @brief Read only initialized X through replicated source swizzles. */
        ReplicatedX,
        /** @brief Use the valid saturating destination modifier. */
        DestinationSaturate,
        /** @brief Use valid temporary negate and signed-scale source modifiers. */
        TemporarySourceModifiers,
    };

    /** @brief Shader Model 1.4 texture-coordinate selector contract probe. */
    enum class SyntheticTextureCoordinateSelectorProbe
    {
        /** @brief Emit no dedicated texture-coordinate selector probe. */
        None,
        /** @brief Invalidly use a selector other than XYZ or XYW for `TEXCRD`. */
        TexcrdInvalidSelector,
        /** @brief Invalidly use a selector other than XYZ or XYW for `TEXLD`. */
        TexldInvalidSelector,
        /** @brief Invalidly mix XYZ and XYW reads of the same texture coordinate. */
        MixedSameRegister,
        /** @brief Invalidly mix omitted/XYZ and XYW reads of the same texture coordinate. */
        IdentityThenXyw,
        /** @brief Invalidly mix XYZ with a later XYW projective read. */
        XyzThenDw,
        /** @brief Validly mix omitted and explicit XYZ reads. */
        IdentityThenXyz,
        /** @brief Validly repeat XYW reads. */
        RepeatXyw,
        /** @brief Validly use different selectors on different texture coordinates. */
        DifferentRegisters,
        /** @brief Validly mix ordinary and projective reads that both select XYW. */
        XywThenDw,
    };

    /** @brief Shader Model 1.4 dependent temporary `TEXLD` selector probe. */
    enum class SyntheticTemporaryTextureSelectorProbe
    {
        /** @brief Emit no dedicated temporary-coordinate selector probe. */
        None,
        /** @brief Invalidly select XYW from the temporary coordinate. */
        Xyw,
        /** @brief Invalidly reorder the temporary coordinate components. */
        Reordered,
        /** @brief Validly omit the selector, which means XYZ. */
        Identity,
        /** @brief Validly select XYZ explicitly. */
        Xyz,
    };

    /** @brief Legacy pixel-depth instruction emitted by the synthetic pixel shader. */
    enum class SyntheticLegacyDepthOutput
    {
        /** @brief Do not emit a legacy pixel-depth instruction. */
        None,
        /** @brief Emit the ps_1_3 `TEXM3X2PAD`/`TEXM3X2DEPTH` pair. */
        TextureMatrix2,
        /** @brief Emit ps_1_4 `TEXDEPTH r5` after a phase boundary. */
        Register,
    };

    /** @brief Scratch-operand form emitted for the synthetic vertex `SGN` instruction. */
    enum class SyntheticSgnScratchOperands
    {
        /** @brief Do not emit `SGN`. */
        None,
        /** @brief Use two distinct, intentionally uninitialized temporary scratch registers. */
        ValidUninitialized,
        /** @brief Invalidly name the same temporary scratch register twice. */
        Aliased,
        /** @brief Invalidly use constant registers instead of temporary scratch registers. */
        NonTemporary,
    };

    /** @brief Composite arithmetic result exercised through a partial destination write mask. */
    enum class SyntheticCompositeWriteMaskProbe
    {
        /** @brief Emit no composite-result write-mask probe. */
        None,
        /** @brief Emit vertex `DST r0.xz, c240, c241`. */
        VertexDst,
        /** @brief Emit vertex `CRS r0.xz, c240, c241`. */
        VertexCrs,
    };

    /** @brief Shader Model 2+ arithmetic opcode deliberately emitted in a ps_1_4 program. */
    enum class SyntheticInvalidPixelShaderModel1Opcode
    {
        /** @brief Emit no deliberately invalid opcode. */
        None,
        /** @brief Emit `RCP`. */
        Rcp,
        /** @brief Emit `RSQ`. */
        Rsq,
        /** @brief Emit `MIN`. */
        Min,
        /** @brief Emit `MAX`. */
        Max,
        /** @brief Emit `EXP`. */
        Exp,
        /** @brief Emit `LOG`. */
        Log,
        /** @brief Emit `FRC`. */
        Frc,
        /** @brief Emit `POW`. */
        Pow,
        /** @brief Emit `CRS`. */
        Crs,
        /** @brief Emit `ABS`. */
        Abs,
        /** @brief Emit `NRM`. */
        Nrm,
        /** @brief Emit `DSX`. */
        Dsx,
        /** @brief Emit `DSY`. */
        Dsy,
    };

    /** @brief Later-profile vertex opcode deliberately emitted in a vs_1_1 program. */
    enum class SyntheticInvalidVertexShaderModel1Opcode
    {
        /** @brief Emit no deliberately invalid opcode. */
        None,
        /** @brief Emit `ABS`. */
        Abs,
        /** @brief Emit `CRS`. */
        Crs,
        /** @brief Emit `NRM`. */
        Nrm,
        /** @brief Emit `POW`. */
        Pow,
        /** @brief Emit `SINCOS`. */
        SinCos,
        /** @brief Emit `SGN`. */
        Sgn,
        /** @brief Emit `MOVA`. */
        Mova,
        /** @brief Emit `DEFB`. */
        Defb,
        /** @brief Emit `DEFI`. */
        Defi,
    };

    /** @brief Later-profile pixel opcode deliberately emitted in a ps_2_0 program. */
    enum class SyntheticInvalidPixelShaderModel20Opcode
    {
        /** @brief Emit no deliberately invalid opcode. */
        None,
        /** @brief Emit `DEFB`. */
        Defb,
        /** @brief Emit `DEFI`. */
        Defi,
        /** @brief Emit `REP`/`ENDREP`. */
        Rep,
        /** @brief Emit `IF`/`ENDIF`. */
        If,
        /** @brief Emit `DSX`. */
        Dsx,
        /** @brief Emit `DSY`. */
        Dsy,
        /** @brief Emit `SETP`. */
        Setp,
    };

    /** @brief Dynamic-flow or predication feature deliberately emitted in an exact 2.0 shader. */
    enum class SyntheticInvalidShaderModel20DynamicFeature
    {
        /** @brief Emit no deliberately invalid feature. */
        None,
        /** @brief Emit pixel-shader instruction predication. */
        PixelPredicatedMov,
        /** @brief Emit vertex `IFC`. */
        VertexIfc,
        /** @brief Emit vertex `BREAK` inside a legal static `REP` block. */
        VertexBreak,
        /** @brief Emit vertex `BREAKC` inside a legal static `REP` block. */
        VertexBreakc,
        /** @brief Emit vertex `SETP`. */
        VertexSetp,
        /** @brief Emit vertex `IF` with a predicate rather than Boolean source. */
        VertexIfPredicate,
        /** @brief Emit vertex `CALLNZ` with a predicate rather than Boolean source. */
        VertexCallnzPredicate,
        /** @brief Emit vertex-shader instruction predication. */
        VertexPredicatedMov,
    };

    /** @brief Structured-flow program used to probe D3D9 block matching and nesting limits. */
    enum class SyntheticFlowControlProbe
    {
        /** @brief Emit no dedicated structured-flow probe. */
        None,
        /** @brief Emit a correctly nested loop containing an if block. */
        ProperlyNestedLoopIf,
        /** @brief Emit `ELSE` without a preceding `IF`. */
        ElseWithoutIf,
        /** @brief Emit `ENDIF` without a preceding `IF`. */
        EndIfWithoutIf,
        /** @brief Emit `IF` without a closing `ENDIF`. */
        IfWithoutEndIf,
        /** @brief Emit two `ELSE` instructions for one `IF`. */
        DuplicateElse,
        /** @brief End a loop before an if block opened inside it. */
        LoopEndsBeforeIf,
        /** @brief End an if block before a loop opened inside it. */
        IfEndsBeforeLoop,
        /** @brief End a repeat block before an if block opened inside it. */
        RepEndsBeforeIf,
        /** @brief End an if block before a repeat block opened inside it. */
        IfEndsBeforeRep,
        /** @brief Emit the maximum 24 nested static if blocks. */
        StaticIfDepth24,
        /** @brief Emit 25 nested static if blocks. */
        StaticIfDepth25,
        /** @brief Emit the maximum 24 nested dynamic if blocks. */
        DynamicIfDepth24,
        /** @brief Emit 25 nested dynamic if blocks. */
        DynamicIfDepth25,
        /** @brief Emit the maximum four nested loop/repeat blocks. */
        LoopRepDepth4,
        /** @brief Emit five nested loop/repeat blocks. */
        LoopRepDepth5,
        /** @brief Emit one legal loop/repeat level in an exact vertex Shader Model 2.0 program. */
        Vertex20LoopRepDepth1,
        /** @brief Emit two loop/repeat levels in an exact vertex Shader Model 2.0 program. */
        Vertex20LoopRepDepth2,
        /** @brief Emit exactly 16 mixed static-flow operations in a vertex Shader Model 2.0 program. */
        Vertex20StaticFlowCount16,
        /** @brief Emit a seventeenth static-flow operation through `IF`. */
        Vertex20StaticFlowCount17If,
        /** @brief Emit a seventeenth static-flow operation through `ELSE`. */
        Vertex20StaticFlowCount17Else,
        /** @brief Emit a seventeenth static-flow operation through `LOOP`. */
        Vertex20StaticFlowCount17Loop,
        /** @brief Emit a seventeenth static-flow operation through `REP`. */
        Vertex20StaticFlowCount17Rep,
        /** @brief Emit a seventeenth static-flow operation through `CALL`. */
        Vertex20StaticFlowCount17Call,
        /** @brief Emit a seventeenth static-flow operation through Boolean `CALLNZ`. */
        Vertex20StaticFlowCount17CallNz,
        /** @brief Emit exactly 16 mixed static-flow operations in a vertex Shader Model 2.x program. */
        Vertex2xStaticFlowCount16,
        /** @brief Emit 17 mixed static-flow operations in a vertex Shader Model 2.x program. */
        Vertex2xStaticFlowCount17,
    };

    /** @brief Shader-stage/profile program used to probe D3D9 subroutine call-graph rules. */
    enum class SyntheticCallGraphProbe
    {
        /** @brief Emit no dedicated subroutine call-graph probe. */
        None,
        /** @brief Emit four legal nested calls in a pixel Shader Model 2.x program. */
        Pixel2xDepth4,
        /** @brief Emit five nested calls in a pixel Shader Model 2.x program. */
        Pixel2xDepth5,
        /** @brief Emit four legal nested calls in a pixel Shader Model 3.0 program. */
        Pixel30Depth4,
        /** @brief Emit five nested calls in a pixel Shader Model 3.0 program. */
        Pixel30Depth5,
        /** @brief Emit a backward unconditional call in a pixel Shader Model 3.0 program. */
        Pixel30BackwardCall,
        /** @brief Emit a backward Boolean conditional call in a pixel Shader Model 3.0 program. */
        Pixel30BackwardCallNz,
        /** @brief Call a pixel Shader Model 3.0 label that is never defined. */
        Pixel30UndefinedLabel,
        /** @brief Define the same pixel Shader Model 3.0 label twice. */
        Pixel30DuplicateLabel,
        /** @brief End a pixel Shader Model 3.0 subroutine without `RET`. */
        Pixel30MissingReturn,
        /** @brief Call the first Shader Model 3 label above the Shader Model 2.x limit. */
        Pixel30Label16,
        /** @brief Call the maximum Shader Model 3 label. */
        Pixel30Label2047,
        /** @brief Emit one legal call in an exact vertex Shader Model 2.0 program. */
        Vertex20Depth1,
        /** @brief Emit two nested calls in an exact vertex Shader Model 2.0 program. */
        Vertex20Depth2,
        /** @brief Emit four legal nested calls in a vertex Shader Model 2.x program. */
        Vertex2xDepth4,
        /** @brief Emit five nested calls in a vertex Shader Model 2.x program. */
        Vertex2xDepth5,
        /** @brief Emit four legal nested calls in a vertex Shader Model 3.0 program. */
        Vertex30Depth4,
        /** @brief Emit five nested calls in a vertex Shader Model 3.0 program. */
        Vertex30Depth5,
        /** @brief Emit a backward unconditional call in a vertex Shader Model 3.0 program. */
        Vertex30BackwardCall,
        /** @brief Emit a backward Boolean conditional call in a vertex Shader Model 3.0 program. */
        Vertex30BackwardCallNz,
        /** @brief Call a vertex Shader Model 3.0 label that is never defined. */
        Vertex30UndefinedLabel,
        /** @brief Define the same vertex Shader Model 3.0 label twice. */
        Vertex30DuplicateLabel,
        /** @brief End a vertex Shader Model 3.0 subroutine without `RET`. */
        Vertex30MissingReturn,
        /** @brief Call the first vertex Shader Model 3 label above the Shader Model 2.x limit. */
        Vertex30Label16,
        /** @brief Call the maximum vertex Shader Model 3 label. */
        Vertex30Label2047,
    };

    /** @brief Invalid source/destination relationship deliberately emitted for a matrix opcode. */
    enum class SyntheticInvalidMatrixOperands
    {
        /** @brief Emit no deliberately invalid matrix operands. */
        None,
        /** @brief Negate the matrix base source. */
        NegatedMatrixSource,
        /** @brief Swizzle the matrix base source. */
        SwizzledMatrixSource,
        /** @brief Use the destination register as the input vector source. */
        DestinationAliasesVectorSource,
    };

    /** @brief Shader-stage/profile combination using an absolute source modifier before SM3. */
    enum class SyntheticInvalidPreShaderModel3AbsoluteSource
    {
        /** @brief Emit no deliberately invalid absolute source modifier. */
        None,
        /** @brief Emit `abs(c0)` in a pixel Shader Model 2.0 instruction. */
        PixelAbsolute,
        /** @brief Emit `-abs(c0)` in a pixel Shader Model 2.0 instruction. */
        PixelAbsoluteNegate,
        /** @brief Emit `abs(c0)` in a vertex Shader Model 2.0 instruction. */
        VertexAbsolute,
        /** @brief Emit `-abs(c0)` in a vertex Shader Model 2.0 instruction. */
        VertexAbsoluteNegate,
    };

    /** @brief Shader-stage pattern for SM3 absolute float-constant consistency tests. */
    enum class SyntheticInvalidShaderModel3MixedConstantAbsolute
    {
        /** @brief Emit no deliberately mixed Shader Model 3 constant reads. */
        None,
        /** @brief Emit an ordinary then absolute pixel constant read. */
        PixelPlainThenAbsolute,
        /** @brief Emit an absolute then ordinary pixel constant read. */
        PixelAbsoluteThenPlain,
        /** @brief Emit only absolute pixel constant reads as a valid control. */
        PixelAllAbsolute,
        /** @brief Emit an ordinary then absolute vertex constant read. */
        VertexPlainThenAbsolute,
        /** @brief Emit an absolute then ordinary vertex constant read. */
        VertexAbsoluteThenPlain,
        /** @brief Emit only absolute vertex constant reads as a valid control. */
        VertexAllAbsolute,
    };

    /** @brief Shader profile and temporary-register boundary exercised by a synthetic program. */
    enum class SyntheticTemporaryRegisterProbe
    {
        /** @brief Emit no dedicated temporary-register boundary instruction. */
        None,
        /** @brief Write the last valid ps_1_1 temporary, r1. */
        Pixel11Maximum,
        /** @brief Write the first invalid ps_1_1 temporary, r2. */
        Pixel11OutOfRange,
        /** @brief Write the last valid ps_1_4 temporary, r5. */
        Pixel14Maximum,
        /** @brief Write the first invalid ps_1_4 temporary, r6. */
        Pixel14OutOfRange,
        /** @brief Write the last valid ps_2_0 temporary, r11. */
        Pixel20Maximum,
        /** @brief Write the first invalid ps_2_0 temporary, r12. */
        Pixel20OutOfRange,
        /** @brief Write the maximum potentially valid ps_2_x temporary, r31. */
        Pixel2xMaximum,
        /** @brief Write the universally invalid ps_2_x temporary, r32. */
        Pixel2xOutOfRange,
        /** @brief Write the last valid ps_3_0 temporary, r31. */
        Pixel30Maximum,
        /** @brief Write the first invalid ps_3_0 temporary, r32. */
        Pixel30OutOfRange,
        /** @brief Write the last valid vs_1_1 temporary, r11. */
        Vertex11Maximum,
        /** @brief Write the first invalid vs_1_1 temporary, r12. */
        Vertex11OutOfRange,
        /** @brief Write the last valid vs_2_0 temporary, r11. */
        Vertex20Maximum,
        /** @brief Write the first invalid vs_2_0 temporary, r12. */
        Vertex20OutOfRange,
        /** @brief Write the maximum potentially valid vs_2_x temporary, r31. */
        Vertex2xMaximum,
        /** @brief Write the universally invalid vs_2_x temporary, r32. */
        Vertex2xOutOfRange,
        /** @brief Write the last valid vs_3_0 temporary, r31. */
        Vertex30Maximum,
        /** @brief Write the first invalid vs_3_0 temporary, r32. */
        Vertex30OutOfRange,
    };

    /** @brief Pixel-shader profile and operand form exercised for `TEXKILL`. */
    enum class SyntheticTexkillOperandProbe
    {
        /** @brief Emit no dedicated `TEXKILL` operand probe. */
        None,
        /** @brief Kill from a ps_1_4 temporary initialized in XYZ in the prior phase. */
        Pixel14TemporaryXyz,
        /** @brief Kill from a ps_2_0 texture input declaring only XY. */
        Pixel20TextureXy,
        /** @brief Kill from a ps_2_0 texture input declaring XYZ. */
        Pixel20TextureXyz,
        /** @brief Kill from a ps_3_0 temporary initialized only in XY. */
        Pixel30TemporaryXy,
        /** @brief Kill from a ps_3_0 interpolator declaring only XY. */
        Pixel30InputXy,
        /** @brief Kill from a ps_3_0 interpolator declaring XYZ. */
        Pixel30InputXyz,
        /** @brief Kill from a ps_3_0 interpolator declaring all four components. */
        Pixel30InputFull,
    };

    /** @brief Invalid Shader Model 1.4 `TEXCRD`/`TEXLD` projective operand form. */
    enum class SyntheticShaderModel14TextureOperandProbe
    {
        /** @brief Emit no dedicated Shader Model 1.4 projective operand probe. */
        None,
        /** @brief Invalidly apply `_dz.xyz` to a `TEXCRD` texture-register source. */
        TexcrdDz,
        /** @brief Pair a `TEXCRD` texture-register `_dw` modifier with identity swizzle. */
        TexcrdDwIdentity,
        /** @brief Pair canonical `TEXCRD` `_dw.xyw` with the forbidden XYZ destination mask. */
        TexcrdDwWrongDestinationMask,
        /** @brief Invalidly apply `_dz.xyz` to a `TEXLD` texture-register source. */
        TexldTextureDz,
        /** @brief Pair a `TEXLD` texture-register `_dw` modifier with identity swizzle. */
        TexldTextureDwIdentity,
        /** @brief Invalidly apply `_dw.xyw` to a `TEXLD` temporary-register source. */
        TexldTemporaryDw,
        /** @brief Pair a `TEXLD` temporary-register `_dz` modifier with identity swizzle. */
        TexldTemporaryDzIdentity,
    };

    /** @brief Shader Model 1.4 phase/state rule exercised by a synthetic program. */
    enum class SyntheticShaderModel14PhaseProbe
    {
        /** @brief Emit no dedicated Shader Model 1.4 phase probe. */
        None,
        /** @brief Put a texture instruction after arithmetic in the single phase-2 block. */
        TextureAfterArithmetic,
        /** @brief Put a texture instruction after arithmetic in the explicit second phase. */
        SecondPhaseTextureAfterArithmetic,
        /** @brief Read a color input before an explicit phase marker. */
        ColorReadBeforePhase,
        /** @brief Execute `TEXKILL` before an explicit phase marker. */
        TexkillBeforePhase,
        /** @brief Feed a texture result into another texture instruction in the same block. */
        DependentTextureReadInSameBlock,
        /** @brief Reuse one texture destination stage in the same texture block. */
        TextureDestinationReuse,
        /** @brief Execute `TEXDEPTH` before its required phase marker. */
        TexdepthBeforePhase,
        /** @brief Read r5 after `TEXDEPTH` consumed it. */
        TexdepthReadAfter,
        /** @brief Read a temporary alpha component invalidated by `PHASE`. */
        AlphaReadAfterPhase,
        /** @brief Leave r0 alpha unwritten after `PHASE` invalidated its old value. */
        OutputAlphaLostAtPhase,
        /** @brief Read a color input in implicit phase 2 when no marker is present. */
        NoMarkerColorRead,
        /** @brief Execute `TEXKILL` in implicit phase 2 when no marker is present. */
        NoMarkerTexkill,
        /** @brief Carry temporary RGB, but not alpha, across `PHASE`. */
        PreserveRgbAcrossPhase,
        /** @brief Reinitialize temporary alpha after `PHASE` before reading it. */
        ReinitializeAlphaAfterPhase,
        /** @brief Use texture-then-arithmetic blocks in both explicit phases. */
        TextureThenArithmeticBothPhases,
        /** @brief Use phase-1 texture output as a phase-2 dependent texture coordinate. */
        DependentTextureReadFromPreviousPhase,
        /** @brief Kill without overwriting a coordinate before a later texture read. */
        TexkillPreservesCoordinate,
        /** @brief Execute `TEXDEPTH` after its required phase marker. */
        TexdepthAfterPhase,
    };

    /** @brief Pixel Shader Model 1.x r0 output-component liveness probe. */
    enum class SyntheticPixel1OutputLivenessProbe
    {
        /** @brief Emit no dedicated Pixel Shader Model 1.x output-liveness probe. */
        None,
        /** @brief Leave ps_1_1 output alpha unwritten. */
        Pixel11RgbOnly,
        /** @brief Leave ps_1_1 output RGB unwritten. */
        Pixel11AlphaOnly,
        /** @brief Leave ps_1_4 output green and alpha unwritten. */
        Pixel14XzOnly,
        /** @brief Initialize ps_1_4 output through complementary XZ and YW writes. */
        Pixel14SplitFull,
    };

    /** @brief Shader profile exercised for a read from an uninitialized temporary register. */
    enum class SyntheticTemporaryInitializationProbe
    {
        /** @brief Emit no dedicated temporary-initialization probe. */
        None,
        /** @brief Read an uninitialized temporary in a ps_3_0 program. */
        Pixel30,
        /** @brief In ps_2_0, write r0.x and then read that initialized X component. */
        Pixel20MoveWrittenX,
        /** @brief In ps_2_0, write r0.x and then read the uninitialized Y component. */
        Pixel20MoveUnwrittenY,
        /** @brief Read an uninitialized temporary in a vs_1_1 program. */
        Vertex11,
        /** @brief In vs_1_1, write r0.x and then read that initialized X component. */
        Vertex11MoveWrittenX,
        /** @brief In vs_1_1, write r0.x and then read the uninitialized Y component. */
        Vertex11MoveUnwrittenY,
        /** @brief Read an uninitialized temporary in a vs_2_0 program. */
        Vertex20,
        /** @brief Read an uninitialized temporary in a vs_3_0 program. */
        Vertex30,
    };

    /** @brief Component-wise arithmetic reads from partially initialized temporaries. */
    enum class SyntheticComponentwiseInitializationProbe
    {
        /** @brief Emit no dedicated component-wise initialization probe. */
        None,
        /** @brief In ps_2_0, read undefined Y from ADD source zero. */
        Pixel20AddSource0UnwrittenY,
        /** @brief In ps_2_0, read undefined Y from ADD source one. */
        Pixel20AddSource1UnwrittenY,
        /** @brief In ps_2_0, read undefined Y from MAD source two. */
        Pixel20MadSource2UnwrittenY,
        /** @brief In ps_2_0, read undefined Y through unary FRC. */
        Pixel20FrcUnwrittenY,
        /** @brief In ps_2_0, read initialized X through ADD. */
        Pixel20AddSource0WrittenX,
        /** @brief In ps_2_0, replicate initialized X across a full ADD destination. */
        Pixel20AddFullFromWrittenX,
        /** @brief In ps_1_4, read undefined Y through ADD. */
        Pixel14AddUnwrittenY,
        /** @brief In ps_1_4, read initialized X through ADD. */
        Pixel14AddWrittenX,
        /** @brief In vs_1_1, read undefined Y through ADD. */
        Vertex11AddUnwrittenY,
        /** @brief In vs_1_1, read initialized X through ADD. */
        Vertex11AddWrittenX,
    };

    /** @brief Scalar arithmetic reads from partially initialized temporaries. */
    enum class SyntheticScalarInitializationProbe
    {
        /** @brief Emit no dedicated scalar initialization probe. */
        None,
        /** @brief In ps_2_0, select undefined Y for RCP. */
        Pixel20RcpUnwrittenY,
        /** @brief In ps_2_0, select undefined Y for POW source zero. */
        Pixel20PowSource0UnwrittenY,
        /** @brief In ps_2_0, select undefined Y for POW source one. */
        Pixel20PowSource1UnwrittenY,
        /** @brief In ps_2_0, select initialized X for RCP. */
        Pixel20RcpWrittenX,
        /** @brief In ps_2_0, select initialized X for POW source zero. */
        Pixel20PowSource0WrittenX,
        /** @brief In ps_2_0, select initialized X for POW source one. */
        Pixel20PowSource1WrittenX,
        /** @brief In vs_1_1, select undefined Y for RCP. */
        Vertex11RcpUnwrittenY,
        /** @brief In vs_1_1, select undefined Y for EXPP. */
        Vertex11ExppUnwrittenY,
        /** @brief In vs_1_1, select initialized X for RCP. */
        Vertex11RcpWrittenX,
    };

    /** @brief Fixed-vector arithmetic reads from partially initialized temporaries. */
    enum class SyntheticFixedVectorInitializationProbe
    {
        /** @brief Emit no dedicated fixed-vector initialization probe. */
        None,
        /** @brief In ps_2_0, DP3 reads undefined YZ through an identity swizzle. */
        Pixel20Dp3UnwrittenYz,
        /** @brief In ps_2_0, DP4 reads undefined YZW through an identity swizzle. */
        Pixel20Dp4UnwrittenYzw,
        /** @brief In ps_2_0, DP2ADD source zero reads undefined Y. */
        Pixel20Dp2AddSource0UnwrittenY,
        /** @brief In ps_2_0, DP2ADD source one reads undefined Y. */
        Pixel20Dp2AddSource1UnwrittenY,
        /** @brief In ps_2_0, DP2ADD scalar source two selects undefined Y. */
        Pixel20Dp2AddSource2UnwrittenY,
        /** @brief In ps_2_0, NRM reads undefined YZ through an identity swizzle. */
        Pixel20NrmUnwrittenYz,
        /** @brief In ps_2_0, DP3 replicates initialized X. */
        Pixel20Dp3ReplicatedX,
        /** @brief In ps_2_0, DP4 replicates initialized X. */
        Pixel20Dp4ReplicatedX,
        /** @brief In ps_2_0, DP2ADD source zero replicates initialized X. */
        Pixel20Dp2AddSource0ReplicatedX,
        /** @brief In ps_2_0, DP2ADD source one replicates initialized X. */
        Pixel20Dp2AddSource1ReplicatedX,
        /** @brief In ps_2_0, DP2ADD scalar source two selects initialized X. */
        Pixel20Dp2AddSource2WrittenX,
        /** @brief In ps_2_0, NRM replicates initialized X. */
        Pixel20NrmReplicatedX,
        /** @brief In vs_1_1, DP3 reads undefined YZ through an identity swizzle. */
        Vertex11Dp3UnwrittenYz,
        /** @brief In vs_1_1, DP4 reads undefined YZW through an identity swizzle. */
        Vertex11Dp4UnwrittenYzw,
        /** @brief In vs_1_1, DP3 replicates initialized X. */
        Vertex11Dp3ReplicatedX,
        /** @brief In vs_1_1, DP4 replicates initialized X. */
        Vertex11Dp4ReplicatedX,
    };

    /** @brief Matrix-vector and implicit matrix-row reads from partial temporaries. */
    enum class SyntheticMatrixInitializationProbe
    {
        /** @brief Emit no dedicated matrix initialization probe. */
        None,
        /** @brief In ps_2_0, M4X4 reads undefined W from its vector. */
        Pixel20M4x4VectorUnwrittenW,
        /** @brief In ps_2_0, M4X3 reads undefined W from its vector. */
        Pixel20M4x3VectorUnwrittenW,
        /** @brief In ps_2_0, M3X4 reads undefined Z from its vector. */
        Pixel20M3x4VectorUnwrittenZ,
        /** @brief In ps_2_0, M3X3 reads undefined Z from its vector. */
        Pixel20M3x3VectorUnwrittenZ,
        /** @brief In ps_2_0, M3X2 reads undefined Z from its vector. */
        Pixel20M3x2VectorUnwrittenZ,
        /** @brief In ps_2_0, M3X2 reads undefined Z from its second temporary matrix row. */
        Pixel20M3x2MatrixRowUnwrittenZ,
        /** @brief In ps_2_0, M4X4 reads a fully initialized vector. */
        Pixel20M4x4VectorWritten,
        /** @brief In ps_2_0, M4X3 reads a fully initialized vector. */
        Pixel20M4x3VectorWritten,
        /** @brief In ps_2_0, M3X4 reads initialized XYZ from its vector. */
        Pixel20M3x4VectorWrittenXyz,
        /** @brief In ps_2_0, M3X3 reads initialized XYZ from its vector. */
        Pixel20M3x3VectorWrittenXyz,
        /** @brief In ps_2_0, M3X2 reads initialized XYZ from its vector. */
        Pixel20M3x2VectorWrittenXyz,
        /** @brief In ps_2_0, M3X2 reads initialized XYZ from both temporary matrix rows. */
        Pixel20M3x2MatrixRowsWrittenXyz,
        /** @brief In vs_1_1, M4X4 reads undefined W from its vector. */
        Vertex11M4x4VectorUnwrittenW,
        /** @brief In vs_1_1, M4X3 reads undefined W from its vector. */
        Vertex11M4x3VectorUnwrittenW,
        /** @brief In vs_1_1, M3X4 reads undefined Z from its vector. */
        Vertex11M3x4VectorUnwrittenZ,
        /** @brief In vs_1_1, M3X3 reads undefined Z from its vector. */
        Vertex11M3x3VectorUnwrittenZ,
        /** @brief In vs_1_1, M3X2 reads undefined Z from its vector. */
        Vertex11M3x2VectorUnwrittenZ,
        /** @brief In vs_1_1, M3X2 reads undefined Z from its second temporary matrix row. */
        Vertex11M3x2MatrixRowUnwrittenZ,
        /** @brief In vs_1_1, M4X4 reads a fully initialized vector. */
        Vertex11M4x4VectorWritten,
        /** @brief In vs_1_1, M4X3 reads a fully initialized vector. */
        Vertex11M4x3VectorWritten,
        /** @brief In vs_1_1, M3X4 reads initialized XYZ from its vector. */
        Vertex11M3x4VectorWrittenXyz,
        /** @brief In vs_1_1, M3X3 reads initialized XYZ from its vector. */
        Vertex11M3x3VectorWrittenXyz,
        /** @brief In vs_1_1, M3X2 reads initialized XYZ from its vector. */
        Vertex11M3x2VectorWrittenXyz,
        /** @brief In vs_1_1, M3X2 reads initialized XYZ from both temporary matrix rows. */
        Vertex11M3x2MatrixRowsWrittenXyz,
    };

    /** @brief LIT, DST and CRS reads from partial temporary registers. */
    enum class SyntheticSpecialVectorInitializationProbe
    {
        /** @brief Emit no dedicated special-vector initialization probe. */
        None,
        /** @brief In vs_1_1, LIT reads undefined W. */
        Vertex11LitUnwrittenW,
        /** @brief In vs_1_1, DST source zero reads undefined Z. */
        Vertex11DstSource0UnwrittenZ,
        /** @brief In vs_1_1, DST source one reads undefined W. */
        Vertex11DstSource1UnwrittenW,
        /** @brief In ps_2_0, CRS source zero reads undefined Z. */
        Pixel20CrsSource0UnwrittenZ,
        /** @brief In ps_2_0, CRS source one reads undefined Z. */
        Pixel20CrsSource1UnwrittenZ,
        /** @brief In vs_1_1, LIT reads initialized XYW. */
        Vertex11LitWrittenXyw,
        /** @brief In vs_1_1, DST source zero reads initialized YZ. */
        Vertex11DstSource0WrittenYz,
        /** @brief In vs_1_1, DST source one reads initialized YW. */
        Vertex11DstSource1WrittenYw,
        /** @brief In ps_2_0, CRS source zero reads initialized XYZ. */
        Pixel20CrsSource0WrittenXyz,
        /** @brief In ps_2_0, CRS source one reads initialized XYZ. */
        Pixel20CrsSource1WrittenXyz,
    };

    /** @brief Shader profile and input-register boundary exercised by a synthetic program. */
    enum class SyntheticInputRegisterProbe
    {
        /** @brief Emit no dedicated input-register boundary instruction. */
        None,
        /** @brief Read the last valid ps_1_1 color input, v1. */
        Pixel11ColorMaximum,
        /** @brief Read the first invalid ps_1_1 color input, v2. */
        Pixel11ColorOutOfRange,
        /** @brief Read the last valid ps_1_1 texture input, t3. */
        Pixel11TextureMaximum,
        /** @brief Read the first invalid ps_1_1 texture input, t4. */
        Pixel11TextureOutOfRange,
        /** @brief Read the last valid ps_1_4 texture input, t5. */
        Pixel14TextureMaximum,
        /** @brief Read the first invalid ps_1_4 texture input, t6. */
        Pixel14TextureOutOfRange,
        /** @brief Read the last valid ps_2_0 color input, v1. */
        Pixel20ColorMaximum,
        /** @brief Read the first invalid ps_2_0 color input, v2. */
        Pixel20ColorOutOfRange,
        /** @brief Read the last valid ps_2_0 texture-coordinate input, t7. */
        Pixel20TexCoordMaximum,
        /** @brief Read the first invalid ps_2_0 texture-coordinate input, t8. */
        Pixel20TexCoordOutOfRange,
        /** @brief Read the last valid ps_3_0 interpolated input, v9. */
        Pixel30Maximum,
        /** @brief Read the first invalid ps_3_0 interpolated input, v10. */
        Pixel30OutOfRange,
        /** @brief Read the last valid vs_2_0 vertex input, v15. */
        Vertex20Maximum,
        /** @brief Read the first invalid vs_2_0 vertex input, v16. */
        Vertex20OutOfRange,
        /** @brief Read the last valid vs_3_0 vertex input, v15. */
        Vertex30Maximum,
        /** @brief Read the first invalid vs_3_0 vertex input, v16. */
        Vertex30OutOfRange,
        /** @brief Write the read-only ps_1_1 color input v0. */
        Pixel11ColorDestination,
        /** @brief Write the read-only ps_2_0 color input v0. */
        Pixel20ColorDestination,
        /** @brief Write the read-only ps_2_0 texture-coordinate input t0. */
        Pixel20TexCoordDestination,
        /** @brief Write the read-only ps_3_0 interpolated input v0. */
        Pixel30Destination,
        /** @brief Write the read-only vs_2_0 vertex input v0. */
        Vertex20Destination,
        /** @brief Write the read-only vs_3_0 vertex input v0. */
        Vertex30Destination,
    };

    /** @brief Shader profile and output-register boundary exercised by a synthetic program. */
    enum class SyntheticOutputRegisterProbe
    {
        /** @brief Emit no dedicated output-register boundary instruction. */
        None,
        /** @brief Write the last pixel color output, oC3. */
        Pixel30ColorMaximum,
        /** @brief Write the first invalid pixel color output, oC4. */
        Pixel30ColorOutOfRange,
        /** @brief Write the sole pixel depth output, oDepth0. */
        Pixel30DepthMaximum,
        /** @brief Write the first invalid pixel depth output, oDepth1. */
        Pixel30DepthOutOfRange,
        /** @brief Write the last vs_2_0 color output, oD1. */
        Vertex20ColorMaximum,
        /** @brief Write the first invalid vs_2_0 color output, oD2. */
        Vertex20ColorOutOfRange,
        /** @brief Write the last vs_2_0 texture-coordinate output, oT7. */
        Vertex20TexCoordMaximum,
        /** @brief Write the first invalid vs_2_0 texture-coordinate output, oT8. */
        Vertex20TexCoordOutOfRange,
        /** @brief Write the last vs_2_0 raster output kind, oPts. */
        Vertex20RasterMaximum,
        /** @brief Write the first invalid vs_2_0 raster output kind. */
        Vertex20RasterOutOfRange,
        /** @brief Write the last vs_3_0 generic output, o11. */
        Vertex30Maximum,
        /** @brief Write the first invalid vs_3_0 generic output, o12. */
        Vertex30OutOfRange,
    };

    /** @brief Constant or singleton-control register boundary exercised by a synthetic program. */
    enum class SyntheticConstantControlRegisterProbe
    {
        /** @brief Emit no dedicated constant/control-register boundary instruction. */
        None,
        /** @brief Read the last ps_1_1 float constant, c7. */
        Pixel11FloatMaximum,
        /** @brief Read the first invalid ps_1_1 float constant, c8. */
        Pixel11FloatOutOfRange,
        /** @brief Read the last ps_2_0 float constant, c31. */
        Pixel20FloatMaximum,
        /** @brief Read the first invalid ps_2_0 float constant, c32. */
        Pixel20FloatOutOfRange,
        /** @brief Read the last ps_3_0 float constant, c223. */
        Pixel30FloatMaximum,
        /** @brief Read the first invalid ps_3_0 float constant, c224. */
        Pixel30FloatOutOfRange,
        /** @brief Read the last pixel integer constant, i15. */
        Pixel30IntegerMaximum,
        /** @brief Read the first invalid pixel integer constant, i16. */
        Pixel30IntegerOutOfRange,
        /** @brief Read the last pixel Boolean constant, b15. */
        Pixel30BooleanMaximum,
        /** @brief Read the first invalid pixel Boolean constant, b16. */
        Pixel30BooleanOutOfRange,
        /** @brief Write the sole pixel predicate, p0. */
        Pixel30PredicateMaximum,
        /** @brief Write the first invalid pixel predicate, p1. */
        Pixel30PredicateOutOfRange,
        /** @brief Read the last vertex integer constant, i15. */
        Vertex30IntegerMaximum,
        /** @brief Read the first invalid vertex integer constant, i16. */
        Vertex30IntegerOutOfRange,
        /** @brief Read the last vertex Boolean constant, b15. */
        Vertex30BooleanMaximum,
        /** @brief Read the first invalid vertex Boolean constant, b16. */
        Vertex30BooleanOutOfRange,
        /** @brief Write the sole vertex predicate, p0. */
        Vertex30PredicateMaximum,
        /** @brief Write the first invalid vertex predicate, p1. */
        Vertex30PredicateOutOfRange,
        /** @brief Write the sole vertex address register, a0. */
        Vertex20AddressMaximum,
        /** @brief Write the first invalid vertex address register, a1. */
        Vertex20AddressOutOfRange,
        /** @brief Read the sole vertex loop register, aL0. */
        Vertex20LoopMaximum,
        /** @brief Read the first invalid vertex loop register, aL1. */
        Vertex20LoopOutOfRange,
    };

    /** @brief Fixed vertex-profile instruction-slot boundary exercised by a synthetic program. */
    enum class SyntheticVertexInstructionSlotProbe
    {
        /** @brief Emit no dedicated instruction-slot boundary program. */
        None,
        /** @brief Emit exactly the 128 instruction slots allowed by vs_1_1. */
        Vertex11Maximum,
        /** @brief Emit 129 instruction slots in vs_1_1. */
        Vertex11OutOfRange,
        /** @brief Emit exactly the 256 instruction slots allowed by vs_2_0. */
        Vertex20Maximum,
        /** @brief Emit 257 instruction slots in vs_2_0. */
        Vertex20OutOfRange,
        /** @brief Emit exactly the 256 instruction slots allowed by vs_2_x. */
        Vertex2xMaximum,
        /** @brief Emit 257 instruction slots in vs_2_x. */
        Vertex2xOutOfRange,
    };

    /** @brief Fixed pixel Shader Model 2.0 instruction-slot boundary exercised by a program. */
    enum class SyntheticPixel20InstructionSlotProbe
    {
        /** @brief Emit no dedicated instruction-slot boundary program. */
        None,
        /** @brief Emit exactly the 64 arithmetic slots allowed by ps_2_0. */
        ArithmeticMaximum,
        /** @brief Emit 65 arithmetic slots in ps_2_0. */
        ArithmeticOutOfRange,
        /** @brief Emit exactly the 32 texture slots allowed by ps_2_0. */
        TextureMaximum,
        /** @brief Emit 33 texture slots in ps_2_0. */
        TextureOutOfRange,
    };

    /** @brief Fixed pixel Shader Model 1.x instruction-slot boundary exercised by a program. */
    enum class SyntheticPixel1InstructionSlotProbe
    {
        /** @brief Emit no dedicated instruction-slot boundary program. */
        None,
        /** @brief Emit exactly eight arithmetic slots in ps_1_1. */
        Pixel11ArithmeticMaximum,
        /** @brief Emit nine arithmetic slots in ps_1_1. */
        Pixel11ArithmeticOutOfRange,
        /** @brief Emit exactly four texture slots in ps_1_1. */
        Pixel11TextureMaximum,
        /** @brief Emit five texture slots in ps_1_1. */
        Pixel11TextureOutOfRange,
        /** @brief Emit four two-slot DP4 instructions in ps_1_2. */
        Pixel12DoubleArithmeticMaximum,
        /** @brief Emit five two-slot DP4 instructions in ps_1_2. */
        Pixel12DoubleArithmeticOutOfRange,
        /** @brief Emit exactly eight arithmetic slots in ps_1_4. */
        Pixel14ArithmeticMaximum,
        /** @brief Emit nine arithmetic slots in ps_1_4. */
        Pixel14ArithmeticOutOfRange,
        /** @brief Emit exactly six texture slots in ps_1_4. */
        Pixel14TextureMaximum,
        /** @brief Emit seven texture slots in ps_1_4. */
        Pixel14TextureOutOfRange,
        /** @brief Emit eight arithmetic slots in each ps_1_4 phase. */
        Pixel14TwoPhaseArithmeticMaximum,
        /** @brief Emit eight then nine arithmetic slots across the two ps_1_4 phases. */
        Pixel14SecondPhaseArithmeticOutOfRange,
        /** @brief Emit eight co-issued RGB/alpha pairs occupying eight ps_1_1 slots. */
        Pixel11CoissuedArithmeticMaximum,
        /** @brief Emit nine co-issued RGB/alpha pairs occupying nine ps_1_1 slots. */
        Pixel11CoissuedArithmeticOutOfRange,
        /** @brief Combine TEXBEML's extra arithmetic slot with seven ps_1_1 MOV slots. */
        Pixel11TexbemlArithmeticMaximum,
        /** @brief Combine TEXBEML's extra arithmetic slot with eight ps_1_1 MOV slots. */
        Pixel11TexbemlArithmeticOutOfRange,
        /** @brief Prove zero-slot ps_1_1 NOP instructions do not consume the arithmetic budget. */
        Pixel11ZeroSlotNopControl,
    };

    /** @brief Pixel Shader Model 1.x destination-mask rule exercised by a program. */
    enum class SyntheticPixel1DestinationMaskProbe
    {
        /** @brief Emit no dedicated destination-mask program. */
        None,
        /** @brief Emit a ps_1_1 MOV with the permitted full destination mask. */
        Pixel11MovFull,
        /** @brief Emit a ps_1_1 MOV with the permitted RGB destination mask. */
        Pixel11MovRgb,
        /** @brief Emit a ps_1_1 MOV with the permitted alpha destination mask. */
        Pixel11MovAlpha,
        /** @brief Emit a ps_1_1 MOV with a forbidden single-color destination mask. */
        Pixel11MovArbitrary,
        /** @brief Emit a ps_1_1 DP3 with its forbidden alpha-only destination mask. */
        Pixel11Dp3Alpha,
        /** @brief Emit a ps_1_1 texture instruction with a forbidden partial mask. */
        Pixel11TexturePartial,
        /** @brief Emit a ps_1_4 MOV with an arbitrary destination mask. */
        Pixel14MovArbitrary,
        /** @brief Emit a ps_1_4 TEXCRD with an arbitrary destination mask. */
        Pixel14TexcrdArbitrary,
    };

    /** @brief Pixel Shader Model 1.x co-issue opcode rule exercised by a program. */
    enum class SyntheticPixel1CoissueProbe
    {
        /** @brief Emit no dedicated co-issue program. */
        None,
        /** @brief Emit a legal RGB instruction followed by a co-issued alpha instruction. */
        Pixel11RgbThenAlpha,
        /** @brief Emit a legal alpha instruction followed by a co-issued RGB instruction. */
        Pixel11AlphaThenRgb,
        /** @brief Co-issue a forbidden texture-address instruction. */
        Pixel11Texture,
        /** @brief Begin a program with an orphaned co-issued alpha instruction. */
        Pixel11OrphanAlpha,
        /** @brief Pair two instructions that both use the RGB pipeline. */
        Pixel11RgbThenRgb,
        /** @brief Pair an alpha instruction with a preceding full-pipeline write. */
        Pixel11FullThenAlpha,
        /** @brief Pair an alpha instruction with a preceding texture instruction. */
        Pixel11TextureThenAlpha,
        /** @brief Attempt to attach a third instruction to one co-issued pair. */
        Pixel11Triple,
        /** @brief Co-issue DP4 even though it occupies both arithmetic pipelines. */
        Pixel14Dp4,
        /** @brief Pair an alpha instruction with a preceding full-pipeline DP4. */
        Pixel14Dp4ThenAlpha,
        /** @brief Pair instructions across a phase boundary. */
        Pixel14PhaseThenAlpha,
    };

    /** @brief Non-writable or instruction-restricted destination register used by a probe. */
    enum class SyntheticDestinationRegisterAccessProbe
    {
        /** @brief Emit no dedicated destination-access probe. */
        None,
        /** @brief Target a ps_2_0 float constant with MOV rather than DEF. */
        Pixel20FloatConstant,
        /** @brief Target a ps_3_0 integer constant with MOV rather than DEFI. */
        Pixel30IntegerConstant,
        /** @brief Target a ps_3_0 Boolean constant with MOV rather than DEFB. */
        Pixel30BooleanConstant,
        /** @brief Target a ps_3_0 sampler pseudo-register with MOV. */
        Pixel30Sampler,
        /** @brief Target the ps_3_0 vPos miscellaneous input with MOV. */
        Pixel30Miscellaneous,
        /** @brief Target the ps_3_0 loop counter with MOV. */
        Pixel30Loop,
        /** @brief Target the ps_3_0 predicate with MOV rather than SETP. */
        Pixel30Predicate,
        /** @brief Target a vs_2_0 float constant with MOV rather than DEF. */
        Vertex20FloatConstant,
        /** @brief Target a vs_3_0 integer constant with MOV rather than DEFI. */
        Vertex30IntegerConstant,
        /** @brief Target a vs_3_0 Boolean constant with MOV rather than DEFB. */
        Vertex30BooleanConstant,
        /** @brief Target a vs_3_0 sampler pseudo-register with MOV. */
        Vertex30Sampler,
        /** @brief Target the vs_3_0 loop counter with MOV. */
        Vertex30Loop,
        /** @brief Target the vs_3_0 predicate with MOV rather than SETP. */
        Vertex30Predicate,
    };

    /** @brief Write-only shader output register used as an instruction source by a probe. */
    enum class SyntheticOutputRegisterSourceProbe
    {
        /** @brief Emit no dedicated output-source probe. */
        None,
        /** @brief Read a ps_2_0 color output register. */
        Pixel20Color,
        /** @brief Read the ps_3_0 depth output register. */
        Pixel30Depth,
        /** @brief Read the vs_2_0 position output register. */
        Vertex20Raster,
        /** @brief Read a vs_2_0 color output register. */
        Vertex20Color,
        /** @brief Read a vs_2_0 texture-coordinate output register. */
        Vertex20TexCoord,
        /** @brief Read a generic vs_3_0 output register. */
        Vertex30Generic,
    };

    /** @brief Profile-specific vertex address-register access used by a probe. */
    enum class SyntheticAddressRegisterAccessProbe
    {
        /** @brief Emit no dedicated address-register probe. */
        None,
        /** @brief Write vs_1_1 a0.x with its legal MOV instruction. */
        Vertex11MovDestination,
        /** @brief Write vs_2_0 a0.x with its legal MOVA instruction. */
        Vertex20MovaDestination,
        /** @brief Write vs_3_0 a0 with its legal MOVA instruction. */
        Vertex30MovaDestination,
        /** @brief Read vs_1_1 a0.x as an ordinary MOV source. */
        Vertex11Source,
        /** @brief Read vs_2_0 a0.x as an ordinary MOV source. */
        Vertex20Source,
        /** @brief Read vs_3_0 a0.x as an ordinary MOV source. */
        Vertex30Source,
        /** @brief Write vs_1_1 a0.x with ADD rather than MOV. */
        Vertex11AddDestination,
        /** @brief Write vs_2_0 a0 with MOV rather than MOVA. */
        Vertex20MovDestination,
        /** @brief Write vs_3_0 a0 with MOV rather than MOVA. */
        Vertex30MovDestination,
    };

    /** @brief A sampler pseudo-register used as an ordinary arithmetic source by a probe. */
    enum class SyntheticSamplerRegisterSourceProbe
    {
        /** @brief Emit no dedicated sampler-source probe. */
        None,
        /** @brief Read ps_2_0 s0 directly through MOV. */
        Pixel20,
        /** @brief Read ps_3_0 s0 directly through MOV. */
        Pixel30,
        /** @brief Read vs_3_0 s0 directly through MOV. */
        Vertex30,
    };

    /** @brief Shader stage/profile that samples an undeclared sampler register. */
    enum class SyntheticMissingSamplerDeclarationProbe
    {
        /** @brief Emit no missing sampler declaration. */
        None,
        /** @brief Sample undeclared s0 from a ps_2_0 program. */
        Pixel20,
        /** @brief Sample undeclared s0 from a ps_3_0 program. */
        Pixel30,
        /** @brief Sample undeclared s0 from a vs_3_0 program. */
        Vertex30,
    };

    /** @brief Immediate-constant definition rule exercised by a forged Shader Model 3 program. */
    enum class SyntheticImmediateConstantDefinitionProbe
    {
        /** @brief Emit no immediate-constant definition probe. */
        None,
        /** @brief Define one pixel float-constant register twice. */
        PixelFloatDuplicate,
        /** @brief Define one vertex float-constant register twice. */
        VertexFloatDuplicate,
        /** @brief Define one pixel integer-constant register twice. */
        PixelIntegerDuplicate,
        /** @brief Define one vertex integer-constant register twice. */
        VertexIntegerDuplicate,
        /** @brief Define one pixel Boolean-constant register twice. */
        PixelBooleanDuplicate,
        /** @brief Define one vertex Boolean-constant register twice. */
        VertexBooleanDuplicate,
        /** @brief Define a pixel loop tuple at every lower inclusive bound. */
        PixelIntegerMinimum,
        /** @brief Define a pixel loop tuple at every upper inclusive bound. */
        PixelIntegerMaximum,
        /** @brief Define a negative pixel loop count. */
        PixelIntegerCountBelow,
        /** @brief Define a pixel loop count above 255. */
        PixelIntegerCountAbove,
        /** @brief Define a negative pixel loop initial value. */
        PixelIntegerInitialBelow,
        /** @brief Define a pixel loop initial value above 255. */
        PixelIntegerInitialAbove,
        /** @brief Define a pixel loop step below -128. */
        PixelIntegerStepBelow,
        /** @brief Define a pixel loop step above 127. */
        PixelIntegerStepAbove,
        /** @brief Define a pixel integer constant with a nonzero reserved W component. */
        PixelIntegerReservedW,
        /** @brief Define a vertex loop tuple at every lower inclusive bound. */
        VertexIntegerMinimum,
        /** @brief Define a vertex loop tuple at every upper inclusive bound. */
        VertexIntegerMaximum,
        /** @brief Define a vertex integer constant with a nonzero reserved W component. */
        VertexIntegerReservedW,
    };

    /** @brief Duplicate shader declaration exercised by a forged program. */
    enum class SyntheticDuplicateDeclarationProbe
    {
        /** @brief Emit no duplicate declaration probe. */
        None,
        /** @brief Declare one pixel sampler twice with the same dimension. */
        PixelSamplerSame,
        /** @brief Declare one pixel sampler twice with conflicting dimensions. */
        PixelSamplerConflict,
        /** @brief Declare one vertex sampler twice with the same dimension. */
        VertexSamplerSame,
        /** @brief Declare one vertex sampler twice with conflicting dimensions. */
        VertexSamplerConflict,
        /** @brief Declare one Shader Model 2 pixel texture input twice. */
        Pixel20TextureInputSame,
        /** @brief Redeclare one Shader Model 2 pixel texture input with a different mask. */
        Pixel20TextureInputDifferentMask,
        /** @brief Declare one Shader Model 2 pixel color input twice. */
        Pixel20ColorInputSame,
        /** @brief Declare one Shader Model 2 vertex input register twice. */
        Vertex20InputSameRegister,
        /** @brief Assign one Shader Model 2 vertex semantic to two registers. */
        Vertex20SemanticSameDifferentRegister,
    };

    /** @brief Texture-instruction source modifier exercised by a Shader Model 3 program. */
    enum class SyntheticTextureSourceModifierProbe
    {
        /** @brief Emit no dedicated texture source-modifier probe. */
        None,
        /** @brief Negate the pixel TEXLDD coordinate operand. */
        PixelTexlddCoordinate,
        /** @brief Negate the pixel TEXLDD sampler operand. */
        PixelTexlddSampler,
        /** @brief Negate the first pixel TEXLDD gradient operand. */
        PixelTexlddGradient,
        /** @brief Negate the pixel TEXLDL coordinate/LOD operand. */
        PixelTexldlCoordinate,
        /** @brief Negate the pixel TEXLDL sampler operand. */
        PixelTexldlSampler,
        /** @brief Negate the vertex TEXLDL coordinate/LOD operand. */
        VertexTexldlCoordinate,
        /** @brief Negate the vertex TEXLDL sampler operand. */
        VertexTexldlSampler,
    };

    /** @brief Pixel Shader Model 2 TEXLD operand rule exercised by a forged program. */
    enum class SyntheticTexld20OperandProbe
    {
        /** @brief Emit no dedicated TEXLD operand probe. */
        None,
        /** @brief Apply the permitted partial-precision destination modifier. */
        PartialPrecisionDestination,
        /** @brief Write the TEXLD result to colour input v0. */
        InputDestination,
        /** @brief Write the TEXLD result to texture input t0. */
        TextureDestination,
        /** @brief Write the TEXLD result directly to oC0. */
        OutputDestination,
        /** @brief Write only the TEXLD destination's XY components. */
        PartialDestination,
        /** @brief Apply the saturate destination modifier to TEXLD. */
        SaturateDestination,
        /** @brief Read the texture coordinate from colour input v0. */
        ColorCoordinate,
        /** @brief Read the texture coordinate from float constant c0. */
        ConstantCoordinate,
        /** @brief Apply a BGRA swizzle to the sampler operand. */
        SamplerSwizzle,
    };

    /** @brief Shader Model 3 TEXLD-family destination modifier and control probe. */
    enum class SyntheticTexldDestinationModifierProbe
    {
        /** @brief Emit no dedicated TEXLD-family destination-modifier probe. */
        None,
        /** @brief Apply forbidden saturation to an ordinary pixel TEXLD destination. */
        Pixel30TexldSaturate,
        /** @brief Apply forbidden saturation to a projective pixel TEXLDP destination. */
        Pixel30TexldpSaturate,
        /** @brief Apply forbidden saturation to a biased pixel TEXLDB destination. */
        Pixel30TexldbSaturate,
        /** @brief Apply permitted partial precision to a Shader Model 2 projective TEXLDP. */
        Pixel20TexldpPartialPrecision,
        /** @brief Apply permitted partial precision to a projective pixel TEXLDP destination. */
        Pixel30TexldpPartialPrecision,
        /** @brief Apply permitted partial precision to a biased pixel TEXLDB destination. */
        Pixel30TexldbPartialPrecision,
    };

    /** @brief Texture instruction encoded at a Shader Model profile boundary. */
    enum class SyntheticTextureInstructionProfileProbe
    {
        /** @brief Emit no dedicated texture instruction profile probe. */
        None,
        /** @brief Emit TEXLDD in a Pixel Shader Model 2.0 program. */
        Pixel20Texldd,
        /** @brief Emit the valid TEXLDD form in a Pixel Shader Model 2.x program. */
        Pixel2xTexldd,
        /** @brief Emit TEXLDL in a Pixel Shader Model 2.0 program. */
        Pixel20Texldl,
        /** @brief Emit TEXLDL in a Pixel Shader Model 2.x program. */
        Pixel2xTexldl,
        /** @brief Emit TEXLDL in a Vertex Shader Model 2.0 program. */
        Vertex20Texldl,
        /** @brief Emit TEXLDL in a Vertex Shader Model 2.x program. */
        Vertex2xTexldl,
    };

    /** @brief TEXLDD operand rule exercised by a forged profile-specific program. */
    enum class SyntheticTexlddOperandProbe
    {
        /** @brief Emit no dedicated TEXLDD operand probe. */
        None,
        /** @brief Apply the permitted partial-precision modifier in Pixel Shader Model 2.x. */
        Pixel2xPartialPrecisionDestination,
        /** @brief Read Pixel Shader Model 2.x coordinates from an initialized temporary. */
        Pixel2xTemporaryCoordinate,
        /** @brief Read Pixel Shader Model 2.x gradients from float constants. */
        Pixel2xConstantGradients,
        /** @brief Write a Pixel Shader Model 3 TEXLDD result directly to oC0. */
        Pixel30OutputDestination,
        /** @brief Write only XY in a Pixel Shader Model 3 TEXLDD. */
        Pixel30PartialDestination,
        /** @brief Read Pixel Shader Model 3 coordinates from a float constant. */
        Pixel30ConstantCoordinate,
        /** @brief Swizzle all four Pixel Shader Model 3 TEXLDD sources. */
        Pixel30SourceSwizzles,
        /** @brief Write a Pixel Shader Model 2.x TEXLDD result directly to oC0. */
        Pixel2xOutputDestination,
        /** @brief Write only XY in a Pixel Shader Model 2.x TEXLDD. */
        Pixel2xPartialDestination,
        /** @brief Apply saturation to a Pixel Shader Model 2.x TEXLDD destination. */
        Pixel2xSaturateDestination,
        /** @brief Read Pixel Shader Model 2.x coordinates from colour input v0. */
        Pixel2xColorCoordinate,
        /** @brief Read Pixel Shader Model 2.x coordinates from float constant c0. */
        Pixel2xConstantCoordinate,
        /** @brief Swizzle the Pixel Shader Model 2.x coordinate operand. */
        Pixel2xCoordinateSwizzle,
        /** @brief Swizzle the Pixel Shader Model 2.x sampler operand. */
        Pixel2xSamplerSwizzle,
        /** @brief Swizzle both Pixel Shader Model 2.x gradient operands. */
        Pixel2xGradientSwizzle,
        /** @brief Apply saturation to a Pixel Shader Model 3 TEXLDD destination. */
        Pixel30SaturateDestination,
    };

    /** @brief TEXLDL destination modifier exercised by a forged Shader Model 3 program. */
    enum class SyntheticTexldlDestinationModifierProbe
    {
        /** @brief Emit no dedicated TEXLDL destination-modifier probe. */
        None,
        /** @brief Apply forbidden saturation to a pixel TEXLDL destination. */
        PixelSaturate,
        /** @brief Apply permitted partial precision to a pixel TEXLDL destination. */
        PixelPartialPrecision,
        /** @brief Apply forbidden saturation to a vertex TEXLDL destination. */
        VertexSaturate,
    };

    /** @brief A typed flow-control constant used as an ordinary arithmetic source by a probe. */
    enum class SyntheticTypedControlSourceProbe
    {
        /** @brief Emit no dedicated typed-control source probe. */
        None,
        /** @brief Read ps_3_0 i0 directly through MOV. */
        PixelInteger,
        /** @brief Read ps_3_0 b0 directly through MOV. */
        PixelBoolean,
        /** @brief Read vs_3_0 i0 directly through MOV. */
        VertexInteger,
        /** @brief Read vs_3_0 b0 directly through MOV. */
        VertexBoolean,
    };

    /** @brief A special flow-control register used as an ordinary arithmetic source by a probe. */
    enum class SyntheticSpecialControlSourceProbe
    {
        /** @brief Emit no dedicated special-control source probe. */
        None,
        /** @brief Read ps_3_0 p0 directly through MOV. */
        PixelPredicate,
        /** @brief Read ps_3_0 l0 directly through MOV. */
        PixelLabel,
        /** @brief Read ps_3_0 aL directly through MOV. */
        PixelLoop,
        /** @brief Read vs_3_0 p0 directly through MOV. */
        VertexPredicate,
        /** @brief Read vs_3_0 l0 directly through MOV. */
        VertexLabel,
        /** @brief Read vs_3_0 aL directly through MOV. */
        VertexLoop,
    };

    /** @brief D3D9 relative-source addressing rule exercised by a program. */
    enum class SyntheticRelativeAddressingProbe
    {
        /** @brief Emit no dedicated relative-addressing validation program. */
        None,
        /** @brief Read `v0[aL]` in a pixel shader outside any loop. */
        PixelInputOutsideLoop,
        /** @brief Read `v0[a0.x]` in a pixel shader even though only `aL` is legal. */
        PixelInputAddressInsideLoop,
        /** @brief Read `c0[aL]` in a pixel shader even though pixel constants are not indexable. */
        PixelConstantInsideLoop,
        /** @brief Read `v0[aL]` in a subroutine called from a pixel-shader loop. */
        PixelInputSubroutineInsideLoop,
        /** @brief Read `c0[aL]` in a vertex shader outside any loop. */
        VertexConstantOutsideLoop,
        /** @brief Read `c0[aL]` directly inside a vertex-shader loop. */
        VertexConstantInsideLoop,
        /** @brief Read `c0[aL]` in a subroutine called from a vertex-shader loop. */
        VertexConstantSubroutineInsideLoop,
        /** @brief Read `v0[aL]` directly inside a vertex-shader loop. */
        VertexInputInsideLoop,
    };

    /** @brief D3D9 pixel miscellaneous-input rule exercised by a program. */
    enum class SyntheticMiscellaneousInputProbe
    {
        /** @brief Emit no dedicated miscellaneous-input validation program. */
        None,
        /** @brief Declare `vPos` with the forbidden `.z` mask. */
        PositionMaskZ,
        /** @brief Declare `vPos` with the forbidden `.xyz` mask. */
        PositionMaskXYZ,
        /** @brief Declare `vPos` with the forbidden full mask. */
        PositionMaskFull,
        /** @brief Read the condition-only `vFace` through an ordinary `MOV`. */
        FaceOrdinaryMove,
        /** @brief Declare the legal `.x` subset of `vPos`. */
        PositionMaskX,
        /** @brief Declare the legal `.y` subset of `vPos`. */
        PositionMaskY,
        /** @brief Declare the legal `.xy` subset of `vPos`. */
        PositionMaskXY,
        /** @brief Read `vFace` as the condition of `CMP`. */
        FaceConditionalCompare,
    };

    /** @brief Shader Model 3 semantic-declaration rule exercised by a program pair. */
    enum class SyntheticSemanticDeclarationProbe
    {
        /** @brief Emit no dedicated semantic-declaration program. */
        None,
        /** @brief Pack TEXCOORD0.xy and COLOR0.zw into the same register in both stages. */
        PackedDisjoint,
        /** @brief Pack two separately stored vertex outputs into one pixel input register. */
        PixelPackedFromSeparateOutputs,
        /** @brief Interpolate TEXCOORD0 at the ordinary pixel centre in a multisampled target. */
        PixelCentroidControl,
        /** @brief Apply the explicit centroid modifier to a TEXCOORD0 pixel input. */
        PixelCentroidExplicit,
        /** @brief Exercise the implicit centroid rule for a COLOR0 pixel input. */
        PixelCentroidImplicitColor,
        /** @brief Interpolate Shader Model 2 TEXCOORD0 at the ordinary pixel centre. */
        Pixel20CentroidControl,
        /** @brief Apply the explicit centroid modifier to a Shader Model 2 TEXCOORD0 input. */
        Pixel20CentroidExplicit,
        /** @brief Exercise Shader Model 2's implicit centroid rule for a COLOR0 input. */
        Pixel20CentroidImplicitColor,
        /** @brief Overlap two pixel-input semantic masks on one register component. */
        PixelOverlappingMasks,
        /** @brief Declare the same pixel-input semantic on two different registers. */
        PixelDuplicateSemantic,
        /** @brief Overlap two vertex-output semantic masks on one register component. */
        VertexOutputOverlappingMasks,
        /** @brief Declare the same vertex-output semantic on two different registers. */
        VertexOutputDuplicateSemantic,
        /** @brief Apply a partial write mask to a vertex input declaration. */
        VertexInputPartialMask,
        /** @brief Apply a partial write mask to the required POSITION0 vertex output. */
        VertexPositionPartialMask,
        /** @brief Apply a partial write mask to a POINTSIZE0 vertex output declaration. */
        VertexPointSizePartialMask,
    };

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
        /// plans/plan_fx.md FX-084: adds a hand-assembled Shader Model 2.0 **vertex** shader alongside
        /// the pixel shader, so `StatePass` binds a complete program pair and the fixture can
        /// actually be DRAWN. Without it the suite could only observe reflection and state, never
        /// whether the compiled shader is the one that ran.
        ///
        /// The vertex shader is `oPos = mul(POSITION0, Transform)`, using the `Transform`
        /// parameter the fixture already declares; the pixel shader writes `Tint` unchanged. So a
        /// full-target quad in the space `Transform` maps to NDC comes out exactly `Tint`, which
        /// no stock shader in any CNA renderer would produce for the same inputs.
        bool includeDrawableProgram = false;
        /// plans/plan_fx.md FX-084: the vertex shader additionally consumes TEXCOORD0, scaled by a new
        /// `StreamMix` float4 parameter that defaults to zero -- `oPos = mul(TEXCOORD0 * StreamMix
        /// + POSITION0, Transform)`. That is what makes a genuine multi-stream compiled-effect
        /// draw observable: with `StreamMix` at zero the second stream contributes nothing, and
        /// with it set the geometry moves by exactly the second stream's own values, so binding
        /// that stream from the wrong buffer, stride or offset changes the pixels.
        bool vertexShaderReadsSecondStream = false;
        /// Emits a Shader Model 3 vertex program that loads its clip-space POSITION0 from the
        /// declared sampler with TEXLDL. The sampler belongs to VertexTextures/VertexSamplerStates,
        /// independently from the pixel-stage collections. Requires @ref includeSampler.
        bool vertexShaderSamplesTexture = false;
        /// Applies the sampler operand's BGRA source swizzle after the vertex TEXLDL lookup.
        /// This keeps sampler-result swizzle order observable independently from the coordinate.
        bool vertexShaderSwizzlesSampleResult = false;
        /// Emits a Shader Model 1.1 vertex program that consumes its implicit POSITION0 input.
        bool vertexShaderUsesShaderModel11Input = false;
        /// Emits a Shader Model 1.1 vertex program that consumes the fixed COLOR0 and TEXCOORD7
        /// inputs in addition to POSITION0.
        bool vertexShaderUsesShaderModel11ExtendedInputs = false;
        /// Emits a Shader Model 1.1 vertex program whose coverage depends on the legacy four-part
        /// `EXPP` result rather than the Shader Model 2+ replicated result.
        bool vertexShaderUsesLegacyExpp = false;
        /// Emits vertex `SGN` with the selected scratch-operand form.
        SyntheticSgnScratchOperands vertexShaderSgnScratchOperands =
            SyntheticSgnScratchOperands::None;
        /// Emits a vertex instruction whose vector result is written through a non-contiguous
        /// destination mask, then uses all four preserved/result components for clip position.
        SyntheticCompositeWriteMaskProbe vertexShaderCompositeWriteMaskProbe =
            SyntheticCompositeWriteMaskProbe::None;
        /// plans/plan_fx.md FX-104: adds a `Caption` parameter of reflected type String, with an initial
        /// value, so the XNA `SetValue(string)`/`GetValueString()` pair can be exercised on a
        /// parameter that really is one instead of only through its rejection path.
        bool includeStringParameter = false;
        /// plans/plan_fx.md FX-110: which sampler dimension the fixture declares. A compiled Effect can
        /// bind a cube or volume texture to a sampler just as easily as a 2D one, and a renderer
        /// that resolves only 2D has to say so rather than bind the wrong kind -- so the suite
        /// needs a fixture of each shape. Affects the texture and sampler parameters' reflected
        /// object types, the pixel shader's `dcl_<kind>` token and its constant-table entry, and
        /// the width of the texture coordinate the vertex shader forwards.
        SyntheticSamplerKind samplerKind = SyntheticSamplerKind::Sampler2D;
        /// Emits four distinct pixel outputs instead of COLOR0 only. The values are swizzles of
        /// Tint, so MRT routing and per-target write masks remain observable without extra
        /// reflected parameters. Mutually exclusive with @ref pixelShaderSamplesTexture.
        bool pixelShaderWritesMrt = false;
        /// Emits a Shader Model 3 pixel program whose colour records how many times a D3D9
        /// `LOOP` body executes. `pixelLoopCount`, `pixelLoopInitial` and `pixelLoopStep` become
        /// the local `DEFI` tuple, allowing the shared renderer test to distinguish the required
        /// count from an incorrect address-based termination condition.
        bool pixelShaderUsesLoop = false;
        std::int32_t pixelLoopCount = 3;
        std::int32_t pixelLoopInitial = 10;
        std::int32_t pixelLoopStep = 2;
        /// Emits a Shader Model 3 pixel program with unconditional, Boolean-conditional and
        /// nested forward subroutine calls. Mutually exclusive with the loop/sampling/MRT forms.
        bool pixelShaderUsesSubroutine = false;
        /// Emits a Shader Model 3 pixel program that squares TEXCOORD0 before applying `DSX` and
        /// `DSY`. The output exposes horizontal and vertical 2x2-quad changes independently.
        bool pixelShaderUsesDerivatives = false;
        /// Emits a Shader Model 3 pixel program that samples with explicit `TEXLDD` gradients.
        bool pixelShaderUsesTextureGradients = false;
        /// Emits a Shader Model 3 pixel program that encodes integer-centred `vPos.xy` in red/green
        /// and the clockwise-positive `vFace` sign in blue.
        bool pixelShaderUsesRasterInputs = false;
        /// Emits Shader Model 3 vertex and pixel programs whose component writes are controlled by
        /// `p0` (including a replicated, negated predicate). The resulting colour makes both
        /// stages observable independently.
        bool shadersUsePredication = false;
        /// Emits a Shader Model 3 pixel program that predicates `TEXKILL` through replicated `p0.x`.
        bool pixelShaderUsesPredicatedTexkill = false;
        /// Emits a Shader Model 1.4 pixel program that samples `r#_dz.xyz` through `TEXLD`.
        bool pixelShaderUsesShaderModel14TexldDz = false;
        /// Emits a ps_1_4 TEXLD using the texture-register `_dw.xyw` operand form.
        bool pixelShaderUsesShaderModel14TexldDw = false;
        /// Emits a ps_1_4 TEXCRD using its canonical `_dw.xyw` source and XY destination.
        bool pixelShaderUsesShaderModel14TexcrdDw = false;
        /// Emits the selected invalid ps_1_4 projective texture-operand form.
        SyntheticShaderModel14TextureOperandProbe shaderModel14TextureOperandProbe =
            SyntheticShaderModel14TextureOperandProbe::None;
        /// Emits a Shader Model 1.4 pixel program whose destination selects the sampler stage.
        bool pixelShaderUsesShaderModel14TextureLoad = false;
        /// Emits a two-phase Shader Model 1.4 pixel program and carries temporary RGB across it.
        bool pixelShaderUsesShaderModel14Phase = false;
        /// Emits the selected Shader Model 1.4 phase/state validation program.
        SyntheticShaderModel14PhaseProbe shaderModel14PhaseProbe =
            SyntheticShaderModel14PhaseProbe::None;
        /// Emits the selected Pixel Shader Model 1.x r0 output-liveness program.
        SyntheticPixel1OutputLivenessProbe pixel1OutputLivenessProbe =
            SyntheticPixel1OutputLivenessProbe::None;
        /// Emits the Shader Model 1.2 stateful `TEXM3X3PAD`/`TEXM3X3` sequence.
        bool pixelShaderUsesLegacyTextureMatrix = false;
        /// Emits the sampled Shader Model 1.2 `TEXM3X2PAD`/`TEXM3X2TEX` sequence.
        bool pixelShaderUsesLegacyTextureMatrix2 = false;
        /// Emits the sampled Shader Model 1.2 `TEXM3X3PAD`/`TEXM3X3TEX` sequence.
        bool pixelShaderUsesLegacyTextureMatrix3Sample = false;
        /// Emits sampled Shader Model 1.2 `TEXM3X3SPEC` with a constant eye ray.
        bool pixelShaderUsesLegacyTextureMatrix3Specular = false;
        /// Emits sampled Shader Model 1.2 `TEXM3X3VSPEC` with a varying eye ray.
        bool pixelShaderUsesLegacyTextureMatrix3VertexSpecular = false;
        /// Emits a legacy instruction that replaces raster depth for the pixel.
        SyntheticLegacyDepthOutput pixelShaderLegacyDepthOutput =
            SyntheticLegacyDepthOutput::None;
        /// Supplies a zero divisor so the legacy depth instruction must write one.
        bool pixelShaderLegacyDepthZeroDivisor = false;
        /// Emits Shader Model 1.2 `TEXREG2AR` or `TEXREG2GB` against the destination stage.
        SyntheticLegacyTextureRemap pixelShaderLegacyTextureRemap =
            SyntheticLegacyTextureRemap::None;
        /// Emits Shader Model 1.2 dependent RGB/dot texture operations.
        SyntheticLegacyDependentTexture pixelShaderLegacyDependentTexture =
            SyntheticLegacyDependentTexture::None;
        /// Emits a legacy texture-stage bump-environment instruction.
        SyntheticLegacyBumpEnvironment pixelShaderLegacyBumpEnvironment =
            SyntheticLegacyBumpEnvironment::None;
        /// Emits the selected Shader Model 1.4 `BEM` operand/component probe.
        SyntheticBemOperandProbe pixelShaderBemOperandProbe = SyntheticBemOperandProbe::None;
        /// Emits the selected Shader Model 1.4 texture-coordinate selector probe.
        SyntheticTextureCoordinateSelectorProbe pixelShaderTextureCoordinateSelectorProbe =
            SyntheticTextureCoordinateSelectorProbe::None;
        /// Emits the selected Shader Model 1.4 temporary `TEXLD` selector probe.
        SyntheticTemporaryTextureSelectorProbe pixelShaderTemporaryTextureSelectorProbe =
            SyntheticTemporaryTextureSelectorProbe::None;
        /// Emits a second ps_1_4 PHASE marker so parser validation can reject it.
        bool pixelShaderDuplicatesShaderModel14Phase = false;
        /// Emits an SM3 texture load whose coordinates are `v0[aL]` inside a one-iteration loop.
        /// This is the pixel-input relative-addressing form accepted by Microsoft's D3D9
        /// assembler and requires the translator/interpreter to execute the indexed varying.
        bool pixelShaderUsesRelativeTextureCoordinate = false;
        /// Scales interpolated coordinates into a temporary before an implicit SM3 texture load.
        /// This requires LOD derivatives from the executed expression rather than from the
        /// temporary register number's unrelated TEXCOORD semantic.
        bool pixelShaderUsesDependentTemporaryTextureCoordinate = false;
        /// plans/plan_fx.md FX-093: the drawable pixel shader SAMPLES the effect's own sampler instead
        /// of writing `Tint` flat -- `oC0 = tex2D(FxSampler, TEXCOORD0) * Tint` -- and the vertex
        /// shader forwards TEXCOORD0 to it. Without this every drawable fixture had no sampler at
        /// all, so the whole texture/sampler half of a compiled Effect could break with the draw
        /// suite still green: the read-back pixel was `Tint` whatever the backend did with its
        /// sampler state. Requires `includeSampler`.
        bool pixelShaderSamplesTexture = false;
        /// Emits the sampling program as Shader Model 3 and reads its sampler operand as `.bgra`.
        /// D3D9 applies that source swizzle to the sampled texel before the destination write.
        bool pixelShaderSwizzlesSampleResult = false;
        /// Emits a Shader Model 2 program that makes LOG's sign-ignore and zero-result rules
        /// visible as exact red/green output channels.
        bool pixelShaderUsesSignedLog = false;
        /// Emits a Shader Model 2 program that distinguishes NRM's fixed XYZ length from
        /// destination-write-mask dimensionality.
        bool pixelShaderUsesNrmWriteMask = false;
        /// Prepends `mov r0, r0` before r0 has been initialized. D3D9 shader validation must
        /// reject the self-read even though the same instruction also names r0 as its destination.
        bool pixelShaderReadsUninitializedDestination = false;
        /// Emits the selected profile-specific uninitialized-temporary read.
        SyntheticTemporaryInitializationProbe temporaryInitializationProbe =
            SyntheticTemporaryInitializationProbe::None;
        /// Emits an arithmetic read from a selected component of a partially initialized r#.
        SyntheticComponentwiseInitializationProbe componentwiseInitializationProbe =
            SyntheticComponentwiseInitializationProbe::None;
        /// Emits a scalar arithmetic read from one component of a partially initialized r#.
        SyntheticScalarInitializationProbe scalarInitializationProbe =
            SyntheticScalarInitializationProbe::None;
        /// Emits a fixed-vector arithmetic read from selected components of a partial r#.
        SyntheticFixedVectorInitializationProbe fixedVectorInitializationProbe =
            SyntheticFixedVectorInitializationProbe::None;
        /// Emits a matrix read from selected components of partial temporary registers.
        SyntheticMatrixInitializationProbe matrixInitializationProbe =
            SyntheticMatrixInitializationProbe::None;
        /// Emits a LIT, DST or CRS read from a partial temporary register.
        SyntheticSpecialVectorInitializationProbe specialVectorInitializationProbe =
            SyntheticSpecialVectorInitializationProbe::None;
        /// Writes only r0.xy before TEXKILL reads XYZ. D3D9 shader validation must reject the
        /// undefined Z component in Shader Model 2.0.
        bool pixelShaderTexkillReadsPartialTemporary = false;
        /// Defines r0.xy and r0.z in separate instructions before TEXKILL. Validation must
        /// accumulate the two write masks and must not require the unread W component.
        bool pixelShaderTexkillReadsSplitTemporary = false;
        /// Emits the selected profile-specific `TEXKILL` operand/declaration form.
        SyntheticTexkillOperandProbe pixelShaderTexkillOperandProbe =
            SyntheticTexkillOperandProbe::None;
        /// Emits an otherwise well-formed pixel `SGN`; D3D9 exposes this opcode only to vertex
        /// shaders, so shared parser validation must reject it.
        bool pixelShaderUsesInvalidSgn = false;
        /// Emits an otherwise well-formed pixel `EXPP`; D3D9 exposes this opcode only to vertex
        /// shaders, so shared parser validation must reject it.
        bool pixelShaderUsesInvalidExpp = false;
        /// Emits an otherwise well-formed pixel `LOGP`; D3D9 exposes this opcode only to vertex
        /// shaders, so shared parser validation must reject it.
        bool pixelShaderUsesInvalidLogp = false;
        /// Emits a vertex `EXPP` with an identity rather than replicate source swizzle.
        bool vertexShaderUsesInvalidExppSwizzle = false;
        /// Emits an otherwise well-formed pixel `LIT`; D3D9 exposes it only to vertex shaders.
        bool pixelShaderUsesInvalidLit = false;
        /// Emits an otherwise well-formed pixel `SLT`; D3D9 exposes it only to vertex shaders.
        bool pixelShaderUsesInvalidSlt = false;
        /// Emits an otherwise well-formed pixel `SGE`; D3D9 exposes it only to vertex shaders.
        bool pixelShaderUsesInvalidSge = false;
        /// Emits a pixel `EXP` with an identity rather than replicate source swizzle.
        bool pixelShaderUsesInvalidExpSwizzle = false;
        /// Emits a vertex `EXP` with an identity rather than replicate source swizzle.
        bool vertexShaderUsesInvalidExpSwizzle = false;
        /// Emits the selected Shader Model 2+ arithmetic opcode in a ps_1_4 program.
        SyntheticInvalidPixelShaderModel1Opcode pixelShaderModel1InvalidOpcode =
            SyntheticInvalidPixelShaderModel1Opcode::None;
        /// Emits the selected later-profile vertex opcode in a vs_1_1 program.
        SyntheticInvalidVertexShaderModel1Opcode vertexShaderModel1InvalidOpcode =
            SyntheticInvalidVertexShaderModel1Opcode::None;
        /// Emits the selected later-profile pixel opcode in a ps_2_0 program.
        SyntheticInvalidPixelShaderModel20Opcode pixelShaderModel20InvalidOpcode =
            SyntheticInvalidPixelShaderModel20Opcode::None;
        /** @brief Emits the selected dynamic-flow or predication feature in an exact 2.0 shader. */
        SyntheticInvalidShaderModel20DynamicFeature shaderModel20InvalidDynamicFeature =
            SyntheticInvalidShaderModel20DynamicFeature::None;
        /** @brief Emits a `LOOP` block in a pixel Shader Model 2.x program. */
        bool pixelShaderModel2xUsesInvalidLoop = false;
        /** @brief Emits the selected invalid matrix operand combination in the pixel shader. */
        SyntheticInvalidMatrixOperands pixelShaderInvalidMatrixOperands =
            SyntheticInvalidMatrixOperands::None;
        /** @brief Emits an absolute source modifier in the selected pre-SM3 shader stage. */
        SyntheticInvalidPreShaderModel3AbsoluteSource invalidPreShaderModel3AbsoluteSource =
            SyntheticInvalidPreShaderModel3AbsoluteSource::None;
        /** @brief Selects the SM3 float-constant absolute-read validation pattern. */
        SyntheticInvalidShaderModel3MixedConstantAbsolute
            invalidShaderModel3MixedConstantAbsolute =
                SyntheticInvalidShaderModel3MixedConstantAbsolute::None;
        /** @brief Selects a Shader Model 3 structured-flow validation probe. */
        SyntheticFlowControlProbe flowControlProbe = SyntheticFlowControlProbe::None;
        /** @brief Selects a D3D9 subroutine call-graph validation probe. */
        SyntheticCallGraphProbe callGraphProbe = SyntheticCallGraphProbe::None;
        /** @brief Selects a fixed-profile temporary-register boundary probe. */
        SyntheticTemporaryRegisterProbe temporaryRegisterProbe =
            SyntheticTemporaryRegisterProbe::None;
        /** @brief Selects a fixed-profile input-register boundary probe. */
        SyntheticInputRegisterProbe inputRegisterProbe =
            SyntheticInputRegisterProbe::None;
        /** @brief Selects a fixed-profile output-register boundary probe. */
        SyntheticOutputRegisterProbe outputRegisterProbe =
            SyntheticOutputRegisterProbe::None;
        /** @brief Selects a constant or singleton-control register boundary probe. */
        SyntheticConstantControlRegisterProbe constantControlRegisterProbe =
            SyntheticConstantControlRegisterProbe::None;
        /** @brief Selects a fixed vertex-profile instruction-slot boundary program. */
        SyntheticVertexInstructionSlotProbe vertexInstructionSlotProbe =
            SyntheticVertexInstructionSlotProbe::None;
        /** @brief Selects a fixed ps_2_0 instruction-slot boundary program. */
        SyntheticPixel20InstructionSlotProbe pixel20InstructionSlotProbe =
            SyntheticPixel20InstructionSlotProbe::None;
        /** @brief Selects a fixed ps_1_x instruction-slot boundary program. */
        SyntheticPixel1InstructionSlotProbe pixel1InstructionSlotProbe =
            SyntheticPixel1InstructionSlotProbe::None;
        /** @brief Selects a pixel Shader Model 1.x destination-mask program. */
        SyntheticPixel1DestinationMaskProbe pixel1DestinationMaskProbe =
            SyntheticPixel1DestinationMaskProbe::None;
        /** @brief Selects a pixel Shader Model 1.x co-issue program. */
        SyntheticPixel1CoissueProbe pixel1CoissueProbe = SyntheticPixel1CoissueProbe::None;
        /** @brief Selects a non-writable or instruction-restricted destination-register probe. */
        SyntheticDestinationRegisterAccessProbe destinationRegisterAccessProbe =
            SyntheticDestinationRegisterAccessProbe::None;
        /** @brief Selects a write-only output register used as a source. */
        SyntheticOutputRegisterSourceProbe outputRegisterSourceProbe =
            SyntheticOutputRegisterSourceProbe::None;
        /** @brief Selects a profile-specific vertex address-register access probe. */
        SyntheticAddressRegisterAccessProbe addressRegisterAccessProbe =
            SyntheticAddressRegisterAccessProbe::None;
        /** @brief Selects a sampler pseudo-register used as an ordinary source. */
        SyntheticSamplerRegisterSourceProbe samplerRegisterSourceProbe =
            SyntheticSamplerRegisterSourceProbe::None;
        /** @brief Selects a texture instruction whose sampler lacks a DCL. */
        SyntheticMissingSamplerDeclarationProbe missingSamplerDeclarationProbe =
            SyntheticMissingSamplerDeclarationProbe::None;
        /** @brief Selects an immediate-constant definition validation probe. */
        SyntheticImmediateConstantDefinitionProbe immediateConstantDefinitionProbe =
            SyntheticImmediateConstantDefinitionProbe::None;
        /** @brief Selects a duplicate shader declaration validation probe. */
        SyntheticDuplicateDeclarationProbe duplicateDeclarationProbe =
            SyntheticDuplicateDeclarationProbe::None;
        /** @brief Selects an invalid TEXLDD/TEXLDL source modifier. */
        SyntheticTextureSourceModifierProbe textureSourceModifierProbe =
            SyntheticTextureSourceModifierProbe::None;
        /** @brief Selects an invalid Pixel Shader Model 2 TEXLD operand form. */
        SyntheticTexld20OperandProbe texld20OperandProbe =
            SyntheticTexld20OperandProbe::None;
        /** @brief Selects a Shader Model 3 TEXLD-family destination modifier and control. */
        SyntheticTexldDestinationModifierProbe texldDestinationModifierProbe =
            SyntheticTexldDestinationModifierProbe::None;
        /** @brief Selects a texture instruction encoded at a profile boundary. */
        SyntheticTextureInstructionProfileProbe textureInstructionProfileProbe =
            SyntheticTextureInstructionProfileProbe::None;
        /** @brief Selects a profile-specific TEXLDD operand form. */
        SyntheticTexlddOperandProbe texlddOperandProbe =
            SyntheticTexlddOperandProbe::None;
        /** @brief Selects a Shader Model 3 TEXLDL destination modifier. */
        SyntheticTexldlDestinationModifierProbe texldlDestinationModifierProbe =
            SyntheticTexldlDestinationModifierProbe::None;
        /** @brief Selects a typed flow-control constant used as an ordinary source. */
        SyntheticTypedControlSourceProbe typedControlSourceProbe =
            SyntheticTypedControlSourceProbe::None;
        /** @brief Selects a special flow-control register used as an ordinary source. */
        SyntheticSpecialControlSourceProbe specialControlSourceProbe =
            SyntheticSpecialControlSourceProbe::None;
        /** @brief Selects a D3D9 relative-source addressing scope/register probe. */
        SyntheticRelativeAddressingProbe relativeAddressingProbe =
            SyntheticRelativeAddressingProbe::None;
        /** @brief Selects a D3D9 pixel miscellaneous-input validation probe. */
        SyntheticMiscellaneousInputProbe miscellaneousInputProbe =
            SyntheticMiscellaneousInputProbe::None;
        /** @brief Selects a Shader Model 3 semantic declaration/packing probe. */
        SyntheticSemanticDeclarationProbe semanticDeclarationProbe =
            SyntheticSemanticDeclarationProbe::None;
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
    inline std::vector<std::uint8_t> BuildSyntheticPixelShader(
        std::uint32_t samplerRegister,
        bool breakSymbolBinding = false,
        bool includeSampler = true,
        bool samplesTexture = false,
        bool swizzleTint = false,
        SyntheticSamplerKind samplerKind = SyntheticSamplerKind::Sampler2D,
        bool writesMrt = false,
        bool usesLoop = false,
        std::int32_t loopCount = 3,
        std::int32_t loopInitial = 10,
        std::int32_t loopStep = 2,
        bool usesSubroutine = false,
        bool usesDerivatives = false,
        bool usesTextureGradients = false,
        bool usesRasterInputs = false,
        bool usesPredication = false,
        bool usesPredicatedTexkill = false,
        bool usesShaderModel14TexldDz = false,
        bool usesShaderModel14TexldDw = false,
        bool usesShaderModel14TexcrdDw = false,
        SyntheticShaderModel14TextureOperandProbe shaderModel14TextureOperandProbe =
            SyntheticShaderModel14TextureOperandProbe::None,
        bool usesShaderModel14TextureLoad = false,
        bool usesShaderModel14Phase = false,
        SyntheticShaderModel14PhaseProbe shaderModel14PhaseProbe =
            SyntheticShaderModel14PhaseProbe::None,
        SyntheticPixel1OutputLivenessProbe pixel1OutputLivenessProbe =
            SyntheticPixel1OutputLivenessProbe::None,
        bool usesLegacyTextureMatrix = false,
        SyntheticLegacyTextureRemap legacyTextureRemap = SyntheticLegacyTextureRemap::None,
        bool usesLegacyTextureMatrix2 = false,
        bool usesLegacyTextureMatrix3Sample = false,
        bool usesLegacyTextureMatrix3Specular = false,
        bool usesLegacyTextureMatrix3VertexSpecular = false,
        SyntheticLegacyDepthOutput legacyDepthOutput = SyntheticLegacyDepthOutput::None,
        bool legacyDepthZeroDivisor = false,
        SyntheticLegacyDependentTexture legacyDependentTexture =
            SyntheticLegacyDependentTexture::None,
        SyntheticLegacyBumpEnvironment legacyBumpEnvironment =
            SyntheticLegacyBumpEnvironment::None,
        SyntheticBemOperandProbe bemOperandProbe = SyntheticBemOperandProbe::None,
        SyntheticTextureCoordinateSelectorProbe textureCoordinateSelectorProbe =
            SyntheticTextureCoordinateSelectorProbe::None,
        SyntheticTemporaryTextureSelectorProbe temporaryTextureSelectorProbe =
            SyntheticTemporaryTextureSelectorProbe::None,
        bool duplicatesShaderModel14Phase = false,
        bool usesRelativeTextureCoordinate = false,
        bool usesDependentTemporaryTextureCoordinate = false,
        bool swizzlesSampleResult = false,
        bool usesSignedLog = false,
        bool usesNrmWriteMask = false,
        bool readsUninitializedDestination = false,
        SyntheticTemporaryInitializationProbe temporaryInitializationProbe =
            SyntheticTemporaryInitializationProbe::None,
        bool texkillReadsPartialTemporary = false,
        bool texkillReadsSplitTemporary = false,
        SyntheticTexkillOperandProbe texkillOperandProbe =
            SyntheticTexkillOperandProbe::None,
        bool usesInvalidSgn = false,
        bool usesInvalidExpp = false,
        bool usesInvalidLogp = false,
        bool usesInvalidLit = false,
        bool usesInvalidSlt = false,
        bool usesInvalidSge = false,
        bool usesInvalidExpSwizzle = false,
        SyntheticInvalidPixelShaderModel1Opcode shaderModel1InvalidOpcode =
            SyntheticInvalidPixelShaderModel1Opcode::None,
        SyntheticInvalidPixelShaderModel20Opcode shaderModel20InvalidOpcode =
            SyntheticInvalidPixelShaderModel20Opcode::None,
        SyntheticInvalidShaderModel20DynamicFeature shaderModel20InvalidDynamicFeature =
            SyntheticInvalidShaderModel20DynamicFeature::None,
        bool shaderModel2xUsesInvalidLoop = false,
        SyntheticInvalidMatrixOperands invalidMatrixOperands =
            SyntheticInvalidMatrixOperands::None,
        SyntheticInvalidPreShaderModel3AbsoluteSource invalidAbsoluteSource =
            SyntheticInvalidPreShaderModel3AbsoluteSource::None,
        SyntheticInvalidShaderModel3MixedConstantAbsolute invalidMixedConstantAbsolute =
            SyntheticInvalidShaderModel3MixedConstantAbsolute::None,
        SyntheticTemporaryRegisterProbe temporaryRegisterProbe =
            SyntheticTemporaryRegisterProbe::None,
        SyntheticInputRegisterProbe inputRegisterProbe =
            SyntheticInputRegisterProbe::None,
        SyntheticOutputRegisterProbe outputRegisterProbe =
            SyntheticOutputRegisterProbe::None,
        SyntheticConstantControlRegisterProbe constantControlRegisterProbe =
            SyntheticConstantControlRegisterProbe::None,
        SyntheticPixel20InstructionSlotProbe instructionSlotProbe =
            SyntheticPixel20InstructionSlotProbe::None,
        SyntheticPixel1InstructionSlotProbe pixel1InstructionSlotProbe =
            SyntheticPixel1InstructionSlotProbe::None,
        SyntheticPixel1DestinationMaskProbe pixel1DestinationMaskProbe =
            SyntheticPixel1DestinationMaskProbe::None,
        SyntheticPixel1CoissueProbe pixel1CoissueProbe = SyntheticPixel1CoissueProbe::None,
        SyntheticDestinationRegisterAccessProbe destinationRegisterAccessProbe =
            SyntheticDestinationRegisterAccessProbe::None,
        SyntheticOutputRegisterSourceProbe outputRegisterSourceProbe =
            SyntheticOutputRegisterSourceProbe::None,
        SyntheticSamplerRegisterSourceProbe samplerRegisterSourceProbe =
            SyntheticSamplerRegisterSourceProbe::None,
        SyntheticMissingSamplerDeclarationProbe missingSamplerDeclarationProbe =
            SyntheticMissingSamplerDeclarationProbe::None,
        SyntheticImmediateConstantDefinitionProbe immediateConstantDefinitionProbe =
            SyntheticImmediateConstantDefinitionProbe::None,
        SyntheticDuplicateDeclarationProbe duplicateDeclarationProbe =
            SyntheticDuplicateDeclarationProbe::None,
        SyntheticTypedControlSourceProbe typedControlSourceProbe =
            SyntheticTypedControlSourceProbe::None,
        SyntheticSpecialControlSourceProbe specialControlSourceProbe =
            SyntheticSpecialControlSourceProbe::None,
        SyntheticRelativeAddressingProbe relativeAddressingProbe =
            SyntheticRelativeAddressingProbe::None,
        SyntheticMiscellaneousInputProbe miscellaneousInputProbe =
            SyntheticMiscellaneousInputProbe::None,
        SyntheticSemanticDeclarationProbe semanticDeclarationProbe =
            SyntheticSemanticDeclarationProbe::None,
        SyntheticFlowControlProbe flowControlProbe = SyntheticFlowControlProbe::None,
        SyntheticCallGraphProbe callGraphProbe = SyntheticCallGraphProbe::None,
        SyntheticTextureSourceModifierProbe textureSourceModifierProbe =
            SyntheticTextureSourceModifierProbe::None,
        SyntheticTexld20OperandProbe texld20OperandProbe =
            SyntheticTexld20OperandProbe::None,
        SyntheticTexldDestinationModifierProbe texldDestinationModifierProbe =
            SyntheticTexldDestinationModifierProbe::None,
        SyntheticTextureInstructionProfileProbe textureInstructionProfileProbe =
            SyntheticTextureInstructionProfileProbe::None,
        SyntheticTexlddOperandProbe texlddOperandProbe =
            SyntheticTexlddOperandProbe::None,
        SyntheticTexldlDestinationModifierProbe texldlDestinationModifierProbe =
            SyntheticTexldlDestinationModifierProbe::None,
        SyntheticComponentwiseInitializationProbe componentwiseInitializationProbe =
            SyntheticComponentwiseInitializationProbe::None,
        SyntheticScalarInitializationProbe scalarInitializationProbe =
            SyntheticScalarInitializationProbe::None,
        SyntheticFixedVectorInitializationProbe fixedVectorInitializationProbe =
            SyntheticFixedVectorInitializationProbe::None,
        SyntheticMatrixInitializationProbe matrixInitializationProbe =
            SyntheticMatrixInitializationProbe::None,
        SyntheticSpecialVectorInitializationProbe specialVectorInitializationProbe =
            SyntheticSpecialVectorInitializationProbe::None)
    {
        const bool probesPixel11Temporary =
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel11Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel11OutOfRange;
        const bool probesPixel14Temporary =
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel14Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel14OutOfRange;
        const bool probesPixel20Temporary =
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel20Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel20OutOfRange;
        const bool probesPixel2xTemporary =
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel2xMaximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel2xOutOfRange;
        const bool probesPixel30Temporary =
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel30Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel30OutOfRange;
        const bool probesPixel30TemporaryInitialization =
            temporaryInitializationProbe == SyntheticTemporaryInitializationProbe::Pixel30;
        const bool probesPixel20MoveComponentInitialization =
            temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Pixel20MoveWrittenX ||
            temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Pixel20MoveUnwrittenY;
        const bool probesPixel14ComponentwiseInitialization =
            componentwiseInitializationProbe ==
                SyntheticComponentwiseInitializationProbe::Pixel14AddUnwrittenY ||
            componentwiseInitializationProbe ==
                SyntheticComponentwiseInitializationProbe::Pixel14AddWrittenX;
        const bool probesPixel20ComponentwiseInitialization =
            componentwiseInitializationProbe >=
                SyntheticComponentwiseInitializationProbe::Pixel20AddSource0UnwrittenY &&
            componentwiseInitializationProbe <=
                SyntheticComponentwiseInitializationProbe::Pixel20AddFullFromWrittenX;
        const bool probesPixel30Texkill =
            texkillOperandProbe >= SyntheticTexkillOperandProbe::Pixel30TemporaryXy;
        const bool probesPixel20ColorInput =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20ColorMaximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20ColorOutOfRange;
        const bool probesPixel20TexCoordInput =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20TexCoordMaximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20TexCoordOutOfRange;
        const bool probesPixel30Input =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel30Maximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel30OutOfRange;
        const bool probesPixel11ColorInput =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11ColorMaximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11ColorOutOfRange;
        const bool probesPixel11TextureInput =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11TextureMaximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11TextureOutOfRange;
        const bool probesPixel14TextureInput =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel14TextureMaximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel14TextureOutOfRange;
        const bool probesPixel11InputDestination =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11ColorDestination;
        const bool probesPixel20InputDestination =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20ColorDestination ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20TexCoordDestination;
        const bool probesPixel30InputDestination =
            inputRegisterProbe == SyntheticInputRegisterProbe::Pixel30Destination;
        const bool probesPixelOutput =
            outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30ColorMaximum ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30ColorOutOfRange ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30DepthMaximum ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30DepthOutOfRange;
        const bool probesPixel11FloatControl =
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel11FloatMaximum ||
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel11FloatOutOfRange;
        const bool probesPixel20FloatControl =
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel20FloatMaximum ||
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel20FloatOutOfRange;
        const bool probesPixel30ConstantControl =
            constantControlRegisterProbe >=
                SyntheticConstantControlRegisterProbe::Pixel30FloatMaximum &&
            constantControlRegisterProbe <=
                SyntheticConstantControlRegisterProbe::Pixel30PredicateOutOfRange;
        const bool probesPixel11InstructionSlots =
            (pixel1InstructionSlotProbe >=
                 SyntheticPixel1InstructionSlotProbe::Pixel11ArithmeticMaximum &&
             pixel1InstructionSlotProbe <=
                 SyntheticPixel1InstructionSlotProbe::Pixel11TextureOutOfRange) ||
            (pixel1InstructionSlotProbe >=
                 SyntheticPixel1InstructionSlotProbe::Pixel11CoissuedArithmeticMaximum &&
             pixel1InstructionSlotProbe <=
                 SyntheticPixel1InstructionSlotProbe::Pixel11ZeroSlotNopControl);
        const bool probesPixel12InstructionSlots =
            pixel1InstructionSlotProbe >=
                SyntheticPixel1InstructionSlotProbe::Pixel12DoubleArithmeticMaximum &&
            pixel1InstructionSlotProbe <=
                SyntheticPixel1InstructionSlotProbe::Pixel12DoubleArithmeticOutOfRange;
        const bool probesPixel14InstructionSlots =
            pixel1InstructionSlotProbe >=
                SyntheticPixel1InstructionSlotProbe::Pixel14ArithmeticMaximum &&
            pixel1InstructionSlotProbe <=
                SyntheticPixel1InstructionSlotProbe::Pixel14SecondPhaseArithmeticOutOfRange;
        const bool probesPixel11DestinationMask =
            pixel1DestinationMaskProbe >= SyntheticPixel1DestinationMaskProbe::Pixel11MovFull &&
            pixel1DestinationMaskProbe <=
                SyntheticPixel1DestinationMaskProbe::Pixel11TexturePartial;
        const bool probesPixel14DestinationMask =
            pixel1DestinationMaskProbe >=
                SyntheticPixel1DestinationMaskProbe::Pixel14MovArbitrary;
        const bool probesPixel11OutputLiveness =
            pixel1OutputLivenessProbe == SyntheticPixel1OutputLivenessProbe::Pixel11RgbOnly ||
            pixel1OutputLivenessProbe == SyntheticPixel1OutputLivenessProbe::Pixel11AlphaOnly;
        const bool probesPixel14OutputLiveness =
            pixel1OutputLivenessProbe == SyntheticPixel1OutputLivenessProbe::Pixel14XzOnly ||
            pixel1OutputLivenessProbe == SyntheticPixel1OutputLivenessProbe::Pixel14SplitFull;
        const bool probesPixel11Coissue =
            pixel1CoissueProbe >= SyntheticPixel1CoissueProbe::Pixel11RgbThenAlpha &&
            pixel1CoissueProbe <= SyntheticPixel1CoissueProbe::Pixel11Triple;
        const bool probesPixel14Coissue =
            pixel1CoissueProbe >= SyntheticPixel1CoissueProbe::Pixel14Dp4;
        const bool probesPixel20DestinationAccess =
            destinationRegisterAccessProbe ==
            SyntheticDestinationRegisterAccessProbe::Pixel20FloatConstant;
        const bool probesPixel30DestinationAccess =
            destinationRegisterAccessProbe >=
                SyntheticDestinationRegisterAccessProbe::Pixel30IntegerConstant &&
            destinationRegisterAccessProbe <=
                SyntheticDestinationRegisterAccessProbe::Pixel30Predicate;
        const bool probesPixel20OutputSource =
            outputRegisterSourceProbe == SyntheticOutputRegisterSourceProbe::Pixel20Color;
        const bool probesPixel30OutputSource =
            outputRegisterSourceProbe == SyntheticOutputRegisterSourceProbe::Pixel30Depth;
        const bool probesPixel20SamplerSource =
            samplerRegisterSourceProbe == SyntheticSamplerRegisterSourceProbe::Pixel20;
        const bool probesPixel30SamplerSource =
            samplerRegisterSourceProbe == SyntheticSamplerRegisterSourceProbe::Pixel30;
        const bool probesMissingPixelSamplerDeclaration =
            missingSamplerDeclarationProbe ==
                SyntheticMissingSamplerDeclarationProbe::Pixel20 ||
            missingSamplerDeclarationProbe ==
                SyntheticMissingSamplerDeclarationProbe::Pixel30;
        const bool probesMissingPixel30SamplerDeclaration =
            missingSamplerDeclarationProbe ==
            SyntheticMissingSamplerDeclarationProbe::Pixel30;
        const bool probesPixelImmediateConstantDefinition =
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::PixelFloatDuplicate ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::PixelIntegerDuplicate ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::PixelBooleanDuplicate ||
            (immediateConstantDefinitionProbe >=
                 SyntheticImmediateConstantDefinitionProbe::PixelIntegerMinimum &&
             immediateConstantDefinitionProbe <=
                 SyntheticImmediateConstantDefinitionProbe::PixelIntegerReservedW);
        const bool probesVertexFloatRedefinition =
            immediateConstantDefinitionProbe ==
            SyntheticImmediateConstantDefinitionProbe::VertexFloatDuplicate;
        const bool probesPixelSamplerDuplicate =
            duplicateDeclarationProbe == SyntheticDuplicateDeclarationProbe::PixelSamplerSame ||
            duplicateDeclarationProbe ==
                SyntheticDuplicateDeclarationProbe::PixelSamplerConflict;
        const bool probesPixel20DeclarationDuplicate =
            duplicateDeclarationProbe >=
                SyntheticDuplicateDeclarationProbe::Pixel20TextureInputSame &&
            duplicateDeclarationProbe <=
                SyntheticDuplicateDeclarationProbe::Pixel20ColorInputSame;
        const bool probesPixel30TypedControlSource =
            typedControlSourceProbe == SyntheticTypedControlSourceProbe::PixelInteger ||
            typedControlSourceProbe == SyntheticTypedControlSourceProbe::PixelBoolean;
        const bool probesPixel30SpecialControlSource =
            specialControlSourceProbe == SyntheticSpecialControlSourceProbe::PixelPredicate ||
            specialControlSourceProbe == SyntheticSpecialControlSourceProbe::PixelLabel ||
            specialControlSourceProbe == SyntheticSpecialControlSourceProbe::PixelLoop;
        const bool probesPixelRelativeAddressing =
            relativeAddressingProbe >=
                SyntheticRelativeAddressingProbe::PixelInputOutsideLoop &&
            relativeAddressingProbe <=
                SyntheticRelativeAddressingProbe::PixelInputSubroutineInsideLoop;
        const bool probesPixelMiscellaneousInput =
            miscellaneousInputProbe != SyntheticMiscellaneousInputProbe::None;
        const bool probesPixel20Centroid =
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidControl ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidExplicit ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidImplicitColor;
        const bool probesPixelSemanticDeclarations =
            semanticDeclarationProbe == SyntheticSemanticDeclarationProbe::PackedDisjoint ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidControl ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidExplicit ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidImplicitColor ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelOverlappingMasks ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelDuplicateSemantic;
        const bool probesPixelFlowControl =
            flowControlProbe >= SyntheticFlowControlProbe::ProperlyNestedLoopIf &&
            flowControlProbe <= SyntheticFlowControlProbe::LoopRepDepth5;
        const bool probesPixelCallGraph =
            callGraphProbe >= SyntheticCallGraphProbe::Pixel2xDepth4 &&
            callGraphProbe <= SyntheticCallGraphProbe::Pixel30Label2047;
        const bool probesPixel2xCallGraph =
            callGraphProbe == SyntheticCallGraphProbe::Pixel2xDepth4 ||
            callGraphProbe == SyntheticCallGraphProbe::Pixel2xDepth5;
        const bool probesPixelTextureSourceModifier =
            textureSourceModifierProbe >=
                SyntheticTextureSourceModifierProbe::PixelTexlddCoordinate &&
            textureSourceModifierProbe <=
                SyntheticTextureSourceModifierProbe::PixelTexldlSampler;
        const bool probesPixelTexlddSourceModifier =
            textureSourceModifierProbe >=
                SyntheticTextureSourceModifierProbe::PixelTexlddCoordinate &&
            textureSourceModifierProbe <=
                SyntheticTextureSourceModifierProbe::PixelTexlddGradient;
        const bool probesTexld20Operand =
            texld20OperandProbe != SyntheticTexld20OperandProbe::None;
        const bool probesPixelTexldDestinationModifier =
            texldDestinationModifierProbe != SyntheticTexldDestinationModifierProbe::None;
        const bool probesPixel30TexldDestinationModifier =
            probesPixelTexldDestinationModifier &&
            texldDestinationModifierProbe !=
                SyntheticTexldDestinationModifierProbe::Pixel20TexldpPartialPrecision;
        const bool probesPixelTextureInstructionProfile =
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Pixel20Texldd ||
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Pixel2xTexldd ||
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Pixel20Texldl ||
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Pixel2xTexldl;
        const bool probesPixel2xTextureInstructionProfile =
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Pixel2xTexldd ||
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Pixel2xTexldl;
        const bool probesTexlddOperand =
            texlddOperandProbe != SyntheticTexlddOperandProbe::None;
        const bool probesPixel30TexlddOperand =
            (texlddOperandProbe >= SyntheticTexlddOperandProbe::Pixel30OutputDestination &&
             texlddOperandProbe <= SyntheticTexlddOperandProbe::Pixel30SourceSwizzles) ||
            texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel30SaturateDestination;
        const bool probesPixel2xTexlddOperand =
            probesTexlddOperand && !probesPixel30TexlddOperand;
        const bool probesPixelTexldlDestinationModifier =
            texldlDestinationModifierProbe ==
                SyntheticTexldlDestinationModifierProbe::PixelSaturate ||
            texldlDestinationModifierProbe ==
                SyntheticTexldlDestinationModifierProbe::PixelPartialPrecision;
        const bool usesShaderModel3 =
            usesLoop || usesSubroutine || usesDerivatives || usesTextureGradients ||
            usesRasterInputs || usesPredication ||
            usesPredicatedTexkill || usesRelativeTextureCoordinate ||
            usesDependentTemporaryTextureCoordinate || swizzlesSampleResult ||
            probesPixel30Texkill || probesPixel30TemporaryInitialization ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelPlainThenAbsolute ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAbsoluteThenPlain ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAllAbsolute ||
            probesPixel30Temporary || probesPixel30Input || probesPixel30InputDestination ||
            probesPixelOutput || probesPixel30DestinationAccess || probesPixel30OutputSource ||
            probesPixel30SamplerSource || probesPixel30TypedControlSource ||
            probesPixel30SpecialControlSource || probesPixelRelativeAddressing ||
            probesPixelMiscellaneousInput ||
            probesPixelSemanticDeclarations ||
            probesPixel30ConstantControl || probesPixelFlowControl ||
            (probesPixelCallGraph && !probesPixel2xCallGraph) ||
            probesMissingPixel30SamplerDeclaration || probesPixelImmediateConstantDefinition ||
            probesVertexFloatRedefinition || probesPixelSamplerDuplicate ||
            probesPixelTextureSourceModifier || probesPixel30TexlddOperand ||
            probesPixelTexldlDestinationModifier || probesPixel30TexldDestinationModifier;
        const bool usesShaderModel14 = usesShaderModel14TexldDz ||
                                       usesShaderModel14TexldDw ||
                                       usesShaderModel14TexcrdDw ||
                                       shaderModel14TextureOperandProbe !=
                                           SyntheticShaderModel14TextureOperandProbe::None ||
                                       usesShaderModel14TextureLoad ||
                                       usesShaderModel14Phase || duplicatesShaderModel14Phase ||
                                       shaderModel14PhaseProbe !=
                                           SyntheticShaderModel14PhaseProbe::None ||
                                       probesPixel14OutputLiveness ||
                                       shaderModel1InvalidOpcode !=
                                           SyntheticInvalidPixelShaderModel1Opcode::None ||
                                       texkillOperandProbe ==
                                           SyntheticTexkillOperandProbe::Pixel14TemporaryXyz ||
                                       legacyDepthOutput == SyntheticLegacyDepthOutput::Register ||
                                       legacyBumpEnvironment ==
                                           SyntheticLegacyBumpEnvironment::Arithmetic ||
                                       bemOperandProbe != SyntheticBemOperandProbe::None ||
                                       textureCoordinateSelectorProbe !=
                                           SyntheticTextureCoordinateSelectorProbe::None ||
                                       temporaryTextureSelectorProbe !=
                                           SyntheticTemporaryTextureSelectorProbe::None ||
                                       probesPixel14ComponentwiseInitialization;
        const bool usesShaderModel13 =
            legacyDepthOutput == SyntheticLegacyDepthOutput::TextureMatrix2;
        const std::uint32_t versionToken = probesPixel11Temporary || probesPixel11ColorInput ||
                                                   probesPixel11TextureInput ||
                                                   probesPixel11InputDestination ||
                                                   probesPixel11FloatControl ||
                                                   probesPixel11InstructionSlots ||
                                                   probesPixel11DestinationMask ||
                                                   probesPixel11Coissue ||
                                                   probesPixel11OutputLiveness
                                               ? 0xFFFF0101u
                                           : probesPixel14Temporary || probesPixel14TextureInput ||
                                                     probesPixel14InstructionSlots ||
                                                     probesPixel14DestinationMask ||
                                                     probesPixel14Coissue ||
                                                     probesPixel14OutputLiveness
                                               ? 0xFFFF0104u
                                           : probesPixel12InstructionSlots
                                               ? 0xFFFF0102u
                                           : probesPixel20Temporary || probesPixel20FloatControl ||
                                                     probesPixel20InputDestination ||
                                                     probesPixel20DestinationAccess ||
                                                     probesPixel20OutputSource ||
                                                     probesPixel20SamplerSource
                                               ? 0xFFFF0200u
                                           : probesPixel2xTemporary || probesPixel2xCallGraph ||
                                                     probesPixel2xTextureInstructionProfile ||
                                                     probesPixel2xTexlddOperand
                                               ? 0xFFFF02FFu
                                           : usesShaderModel14
                                               ? 0xFFFF0104u
                                               : usesShaderModel13
                                                     ? 0xFFFF0103u
                                               : usesLegacyTextureMatrix || usesLegacyTextureMatrix2 ||
                                                         usesLegacyTextureMatrix3Sample ||
                                                         usesLegacyTextureMatrix3Specular ||
                                                         usesLegacyTextureMatrix3VertexSpecular ||
                                                         legacyTextureRemap !=
                                                             SyntheticLegacyTextureRemap::None ||
                                                         legacyDependentTexture !=
                                                             SyntheticLegacyDependentTexture::None ||
                                                         legacyBumpEnvironment !=
                                                             SyntheticLegacyBumpEnvironment::None
                                                     ? 0xFFFF0102u
                                                     : shaderModel2xUsesInvalidLoop
                                                           ? 0xFFFF02FFu
                                                     : usesShaderModel3 ? 0xFFFF0300u : 0xFFFF0200u;
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
        AppendUInt16(ctab, samplerKind == SyntheticSamplerKind::SamplerCube
                               ? EffectFormat::TypeSamplerCube
                               : samplerKind == SyntheticSamplerKind::Sampler3D
                                     ? EffectFormat::TypeSampler3D
                                     : EffectFormat::TypeSampler2D);
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
        const std::uint32_t target = appendCtabString(
            probesPixel11Temporary || probesPixel11ColorInput || probesPixel11TextureInput ||
                    probesPixel11FloatControl || probesPixel11InstructionSlots ||
                    probesPixel11DestinationMask || probesPixel11Coissue ||
                    probesPixel11OutputLiveness
                ? "ps_1_1"
                : probesPixel14Temporary || probesPixel14TextureInput ||
                          probesPixel14InstructionSlots || probesPixel14DestinationMask ||
                          probesPixel14Coissue || probesPixel14OutputLiveness
                      ? "ps_1_4"
                      : probesPixel12InstructionSlots
                            ? "ps_1_2"
                      : probesPixel20Temporary || probesPixel20FloatControl
                            ? "ps_2_0"
                      : probesPixel2xTemporary || probesPixel2xTextureInstructionProfile ||
                                probesPixel2xTexlddOperand
                            ? "ps_2_x"
            : usesShaderModel14
                ? "ps_1_4"
                : usesShaderModel13
                      ? "ps_1_3"
                : usesLegacyTextureMatrix || usesLegacyTextureMatrix2 ||
                          usesLegacyTextureMatrix3Sample ||
                          usesLegacyTextureMatrix3Specular ||
                          usesLegacyTextureMatrix3VertexSpecular ||
                          legacyTextureRemap != SyntheticLegacyTextureRemap::None ||
                          legacyDependentTexture != SyntheticLegacyDependentTexture::None ||
                          legacyBumpEnvironment != SyntheticLegacyBumpEnvironment::None
                      ? "ps_1_2"
                      : shaderModel2xUsesInvalidLoop
                            ? "ps_2_x"
                      : usesShaderModel3 ? "ps_3_0" : "ps_2_0");
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

        // Direct3D 9 shader-token register types and the two token shapes every instruction below
        // is built from. Every encoding here was checked against fxc's own output for
        // modules/renderers/fna3d/effects/CnaConformanceEffect.fx, whose MainPixelShader is the
        // same `tex2D(sampler, texcoord) * constant` shape.
        constexpr std::uint32_t regTemp = 0;
        constexpr std::uint32_t regInput = 1;      // ps_3_0 interpolated input v#
        constexpr std::uint32_t regTexture = 3;    // ps_2_0 texture-coordinate input t#
        constexpr std::uint32_t regConst = 2;
        constexpr std::uint32_t regConstInt = 7;
        constexpr std::uint32_t regColorOut = 8;   // oC#
        constexpr std::uint32_t regDepthOut = 9;   // oDepth
        constexpr std::uint32_t regSampler = 10;   // s#
        constexpr std::uint32_t regConstBool = 14;
        constexpr std::uint32_t regLoop = 15;
        constexpr std::uint32_t regMiscellaneous = 17;
        constexpr std::uint32_t regLabel = 18;
        constexpr std::uint32_t regPredicate = 19;
        constexpr std::uint32_t swizzleIdentity = 0xE4u;  // .xyzw
        // .yzxw: x<-y, y<-z, z<-x, w<-w, two bits per component, lowest component first.
        constexpr std::uint32_t swizzleYzxw = 1u | (2u << 2) | (0u << 4) | (3u << 6);
        const auto registerBits = [](std::uint32_t type) {
            return ((type & 0x7u) << 28) | ((type >> 3) << 11);
        };
        const auto destination = [&registerBits](std::uint32_t type, std::uint32_t number,
                                                 std::uint32_t writeMask) {
            return 0x80000000u | registerBits(type) | number | (writeMask << 16);
        };
        const auto source = [&registerBits](std::uint32_t type, std::uint32_t number,
                                            std::uint32_t swizzle = 0xE4u,
                                            std::uint32_t modifier = 0u) {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16) |
                   (modifier << 24);
        };

        if (probesPixelImmediateConstantDefinition)
        {
            const auto appendFloatDefinition = [&](float red, float green) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, 200, 0xFu));
                AppendUInt32(shader, FloatBits(red));
                AppendUInt32(shader, FloatBits(green));
                AppendUInt32(shader, FloatBits(0.0f));
                AppendUInt32(shader, FloatBits(1.0f));
            };
            const auto appendIntegerDefinition =
                [&](std::int32_t count, std::int32_t initial, std::int32_t step,
                    std::int32_t reserved) {
                    AppendUInt32(shader, 0x00000030u | (5u << 24));
                    AppendUInt32(shader, destination(regConstInt, 15, 0xFu));
                    AppendUInt32(shader, static_cast<std::uint32_t>(count));
                    AppendUInt32(shader, static_cast<std::uint32_t>(initial));
                    AppendUInt32(shader, static_cast<std::uint32_t>(step));
                    AppendUInt32(shader, static_cast<std::uint32_t>(reserved));
                };
            const auto appendBooleanDefinition = [&](bool value) {
                AppendUInt32(shader, 0x0000002Fu | (2u << 24));
                AppendUInt32(shader, destination(regConstBool, 15, 0xFu));
                AppendUInt32(shader, value ? 1u : 0u);
            };

            switch (immediateConstantDefinitionProbe)
            {
                case SyntheticImmediateConstantDefinitionProbe::PixelFloatDuplicate:
                    appendFloatDefinition(1.0f, 0.0f);
                    appendFloatDefinition(0.0f, 1.0f);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerDuplicate:
                    appendIntegerDefinition(1, 0, 1, 0);
                    appendIntegerDefinition(2, 0, 2, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelBooleanDuplicate:
                    appendBooleanDefinition(true);
                    appendBooleanDefinition(false);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerMinimum:
                    appendIntegerDefinition(0, 0, -128, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerMaximum:
                    appendIntegerDefinition(255, 255, 127, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerCountBelow:
                    appendIntegerDefinition(-1, 0, 1, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerCountAbove:
                    appendIntegerDefinition(256, 0, 1, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerInitialBelow:
                    appendIntegerDefinition(1, -1, 1, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerInitialAbove:
                    appendIntegerDefinition(1, 256, 1, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerStepBelow:
                    appendIntegerDefinition(1, 0, -129, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerStepAbove:
                    appendIntegerDefinition(1, 0, 128, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::PixelIntegerReservedW:
                    appendIntegerDefinition(1, 0, 1, 1);
                    break;
                default: break;
            }
        }

        if (probesPixelSamplerDuplicate)
        {
            const auto appendSamplerDeclaration = [&](std::uint32_t textureType) {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u | (textureType << 27));
                AppendUInt32(shader, destination(regSampler, 0, 0xFu));
            };
            appendSamplerDeclaration(EffectFormat::SamplerType2D);
            appendSamplerDeclaration(
                duplicateDeclarationProbe ==
                        SyntheticDuplicateDeclarationProbe::PixelSamplerConflict
                    ? EffectFormat::SamplerTypeCube
                    : EffectFormat::SamplerType2D);
        }
        if (texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel20TextureXy ||
            texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel20TextureXyz ||
            texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputXy ||
            texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputXyz ||
            texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputFull)
        {
            const bool shaderModel3 = probesPixel30Texkill;
            const bool xyz =
                texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel20TextureXyz ||
                texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputXyz;
            const bool full =
                texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputFull;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | (shaderModel3 ? 5u : 0u));
            AppendUInt32(shader,
                         destination(shaderModel3 ? regInput : regTexture, 0,
                                     full ? 0xFu : xyz ? 0x7u : 0x3u));
        }
        if (probesPixel20DeclarationDuplicate)
        {
            const bool color = duplicateDeclarationProbe ==
                               SyntheticDuplicateDeclarationProbe::Pixel20ColorInputSame;
            const auto appendInputDeclaration = [&](std::uint32_t mask) {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u);
                AppendUInt32(shader, destination(color ? regInput : regTexture, 0, mask));
            };
            appendInputDeclaration(color ? 0xFu : 0x3u);
            appendInputDeclaration(
                duplicateDeclarationProbe ==
                        SyntheticDuplicateDeclarationProbe::Pixel20TextureInputDifferentMask
                    ? 0x7u
                    : color ? 0xFu : 0x3u);
        }

        if (probesPixel11ColorInput || probesPixel11TextureInput ||
            probesPixel14TextureInput)
        {
            const bool maximum =
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11ColorMaximum ||
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel11TextureMaximum ||
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel14TextureMaximum;
            const std::uint32_t registerType = probesPixel11ColorInput ? regInput : regTexture;
            const std::uint32_t registerNumber = probesPixel11ColorInput
                                                     ? (maximum ? 1u : 2u)
                                                 : probesPixel11TextureInput
                                                     ? (maximum ? 3u : 4u)
                                                     : (maximum ? 5u : 6u);
            AppendUInt32(shader, 0x00000001u); // mov r0, input
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(registerType, registerNumber));
        }

        if (probesPixel20ColorInput || probesPixel20TexCoordInput || probesPixel30Input)
        {
            const bool maximum =
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20ColorMaximum ||
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20TexCoordMaximum ||
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel30Maximum;
            const std::uint32_t registerNumber = probesPixel20ColorInput
                                                     ? (maximum ? 1u : 2u)
                                                 : probesPixel20TexCoordInput
                                                     ? (maximum ? 7u : 8u)
                                                     : (maximum ? 9u : 10u);
            const std::uint32_t registerType =
                probesPixel20TexCoordInput ? regTexture : regInput;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl input
            AppendUInt32(shader, probesPixel30Input ? 0x80000005u : 0x80000000u);
            AppendUInt32(shader, destination(registerType, registerNumber, 0xFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, input
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(registerType, registerNumber));
        }

        if (probesPixel11InputDestination)
        {
            AppendUInt32(shader, 0x00000001u); // mov v0, c0
            AppendUInt32(shader, destination(regInput, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (probesPixel20InputDestination || probesPixel30InputDestination)
        {
            const bool texture =
                inputRegisterProbe == SyntheticInputRegisterProbe::Pixel20TexCoordDestination;
            const std::uint32_t registerType = texture ? regTexture : regInput;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl input
            AppendUInt32(shader, 0x80000000u | (texture ? 5u : 10u));
            AppendUInt32(shader, destination(registerType, 0, 0xFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov input, c0
            AppendUInt32(shader, destination(registerType, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }

        if (probesPixelOutput)
        {
            const bool color =
                outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30ColorMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30ColorOutOfRange;
            const bool maximum =
                outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30ColorMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Pixel30DepthMaximum;
            const std::uint32_t registerType = color ? regColorOut : regDepthOut;
            const std::uint32_t registerNumber = color ? (maximum ? 3u : 4u)
                                                       : (maximum ? 0u : 1u);
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov output, c0
            AppendUInt32(shader, destination(registerType, registerNumber, color ? 0xFu : 0x1u));
            AppendUInt32(shader, source(regConst, 0, color ? 0xE4u : 0x00u));
        }

        const bool probesPixel30FloatControl =
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30FloatMaximum ||
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30FloatOutOfRange;
        if (probesPixel11FloatControl || probesPixel20FloatControl ||
            probesPixel30FloatControl)
        {
            const bool maximum =
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Pixel11FloatMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Pixel20FloatMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Pixel30FloatMaximum;
            const std::uint32_t registerNumber = probesPixel11FloatControl
                                                     ? (maximum ? 7u : 8u)
                                                 : probesPixel20FloatControl
                                                     ? (maximum ? 31u : 32u)
                                                     : (maximum ? 223u : 224u);
            AppendUInt32(shader, 0x00000001u |
                                     (probesPixel11FloatControl ? 0u : (2u << 24)));
            AppendUInt32(shader,
                         destination(probesPixel11FloatControl ? regTemp : regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regConst, registerNumber));
        }
        if (constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30IntegerMaximum ||
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30IntegerOutOfRange)
        {
            const bool maximum = constantControlRegisterProbe ==
                                 SyntheticConstantControlRegisterProbe::Pixel30IntegerMaximum;
            AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i#
            AppendUInt32(shader, source(regConstInt, maximum ? 15u : 16u, 0x00u));
            AppendUInt32(shader, 0x00000027u); // endrep
        }
        if (constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30BooleanMaximum ||
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30BooleanOutOfRange)
        {
            const bool maximum = constantControlRegisterProbe ==
                                 SyntheticConstantControlRegisterProbe::Pixel30BooleanMaximum;
            AppendUInt32(shader, 0x00000028u | (1u << 24)); // if b#
            AppendUInt32(shader, source(regConstBool, maximum ? 15u : 16u, 0x00u));
            AppendUInt32(shader, 0x0000002Bu); // endif
        }
        if (constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30PredicateMaximum ||
            constantControlRegisterProbe ==
                SyntheticConstantControlRegisterProbe::Pixel30PredicateOutOfRange)
        {
            const bool maximum = constantControlRegisterProbe ==
                                 SyntheticConstantControlRegisterProbe::Pixel30PredicateMaximum;
            AppendUInt32(shader, 0x0000005Eu | (1u << 16) | (3u << 24)); // setp_gt p#, c0.x, c0.x
            AppendUInt32(shader, destination(regPredicate, maximum ? 0u : 1u, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
        }

        if (probesPixel20DestinationAccess || probesPixel30DestinationAccess)
        {
            std::uint32_t registerType = regConst;
            std::uint32_t writeMask = 0xFu;
            switch (destinationRegisterAccessProbe)
            {
                case SyntheticDestinationRegisterAccessProbe::Pixel20FloatConstant:
                    break;
                case SyntheticDestinationRegisterAccessProbe::Pixel30IntegerConstant:
                    registerType = regConstInt;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Pixel30BooleanConstant:
                    registerType = regConstBool;
                    writeMask = 0x1u;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Pixel30Sampler:
                    registerType = regSampler;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Pixel30Miscellaneous:
                    registerType = regMiscellaneous;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Pixel30Loop:
                    registerType = regLoop;
                    writeMask = 0x1u;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Pixel30Predicate:
                    registerType = regPredicate;
                    break;
                default: break;
            }
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov restricted destination, c0
            AppendUInt32(shader, destination(registerType, 0, writeMask));
            AppendUInt32(shader, source(regConst, 0));
        }

        if (probesPixel20OutputSource || probesPixel30OutputSource)
        {
            const bool depth = probesPixel30OutputSource;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, write-only output
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(depth ? regDepthOut : regColorOut, 0,
                                        depth ? 0x00u : 0xE4u));
        }
        if (probesPixel20SamplerSource || probesPixel30SamplerSource)
        {
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_2d s0
            AppendUInt32(shader, 0x80000000u | (2u << 27));
            AppendUInt32(shader, destination(regSampler, 0, 0xFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, s0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regSampler, 0));
        }
        if (probesPixel30TypedControlSource)
        {
            const bool boolean =
                typedControlSourceProbe == SyntheticTypedControlSourceProbe::PixelBoolean;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, i0/b0.x
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(boolean ? regConstBool : regConstInt, 0,
                                        boolean ? 0x00u : 0xE4u));
        }
        if (probesPixel30SpecialControlSource)
        {
            const std::uint32_t registerType =
                specialControlSourceProbe == SyntheticSpecialControlSourceProbe::PixelPredicate
                    ? regPredicate
                : specialControlSourceProbe == SyntheticSpecialControlSourceProbe::PixelLabel
                    ? regLabel
                    : regLoop;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, p0/l0/aL
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(registerType, 0, 0x00u));
        }

        if (readsUninitializedDestination)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, r0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        if (probesPixel30TemporaryInitialization)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, uninitialized r0
            AppendUInt32(shader, destination(regTemp, 1, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        if (probesPixel20MoveComponentInitialization)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, r0.x/y
            AppendUInt32(shader, destination(regTemp, 1, 0xFu));
            const bool readsY = temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Pixel20MoveUnwrittenY;
            AppendUInt32(shader, source(regTemp, 0, readsY ? 0x55u : 0x00u));
        }
        if (probesPixel20ComponentwiseInitialization)
        {
            using Probe = SyntheticComponentwiseInitializationProbe;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            const bool readsX = componentwiseInitializationProbe ==
                                    Probe::Pixel20AddSource0WrittenX ||
                                componentwiseInitializationProbe ==
                                    Probe::Pixel20AddFullFromWrittenX;
            const std::uint32_t partialSource = source(
                regTemp, 0, readsX ? 0x00u : 0x55u);
            const bool frc = componentwiseInitializationProbe == Probe::Pixel20FrcUnwrittenY;
            const bool mad = componentwiseInitializationProbe ==
                Probe::Pixel20MadSource2UnwrittenY;
            const std::uint32_t opcode = frc ? 0x00000013u : mad ? 0x00000004u : 0x00000002u;
            const std::uint32_t operandCount = frc ? 2u : mad ? 4u : 3u;
            AppendUInt32(shader, opcode | (operandCount << 24));
            AppendUInt32(shader,
                         destination(regTemp, 1,
                                     componentwiseInitializationProbe ==
                                             Probe::Pixel20AddFullFromWrittenX
                                         ? 0xFu
                                         : 0x1u));
            if (componentwiseInitializationProbe == Probe::Pixel20AddSource1UnwrittenY)
            {
                AppendUInt32(shader, source(regConst, 0, 0x00u));
                AppendUInt32(shader, partialSource);
            }
            else if (mad)
            {
                AppendUInt32(shader, source(regConst, 0, 0x00u));
                AppendUInt32(shader, source(regConst, 0, 0x00u));
                AppendUInt32(shader, partialSource);
            }
            else
            {
                AppendUInt32(shader, partialSource);
                if (!frc) AppendUInt32(shader, source(regConst, 0, 0x00u));
            }
        }
        if (probesPixel14ComponentwiseInitialization)
        {
            using Probe = SyntheticComponentwiseInitializationProbe;
            AppendUInt32(shader, 0x00000001u); // mov r1.x, c0.x
            AppendUInt32(shader, destination(regTemp, 1, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, 0x00000002u); // add r0.x, r1.x/y, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader,
                         source(regTemp, 1,
                                componentwiseInitializationProbe == Probe::Pixel14AddWrittenX
                                    ? 0x00u
                                    : 0x55u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
        }
        if (scalarInitializationProbe >=
                SyntheticScalarInitializationProbe::Pixel20RcpUnwrittenY &&
            scalarInitializationProbe <=
                SyntheticScalarInitializationProbe::Pixel20PowSource1WrittenX)
        {
            using Probe = SyntheticScalarInitializationProbe;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            const bool pow = scalarInitializationProbe == Probe::Pixel20PowSource0UnwrittenY ||
                             scalarInitializationProbe == Probe::Pixel20PowSource1UnwrittenY ||
                             scalarInitializationProbe == Probe::Pixel20PowSource0WrittenX ||
                             scalarInitializationProbe == Probe::Pixel20PowSource1WrittenX;
            const bool sourceOne =
                scalarInitializationProbe == Probe::Pixel20PowSource1UnwrittenY ||
                scalarInitializationProbe == Probe::Pixel20PowSource1WrittenX;
            const bool readsX = scalarInitializationProbe == Probe::Pixel20RcpWrittenX ||
                                scalarInitializationProbe == Probe::Pixel20PowSource0WrittenX ||
                                scalarInitializationProbe == Probe::Pixel20PowSource1WrittenX;
            AppendUInt32(shader,
                         (pow ? 0x00000020u : 0x00000006u) |
                             ((pow ? 3u : 2u) << 24)); // pow/rcp r1.x, ...
            AppendUInt32(shader, destination(regTemp, 1, 0x1u));
            const std::uint32_t partialSource =
                source(regTemp, 0, readsX ? 0x00u : 0x55u);
            if (sourceOne) AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, partialSource);
            if (pow && !sourceOne) AppendUInt32(shader, source(regConst, 0, 0x00u));
        }
        if (fixedVectorInitializationProbe >=
                SyntheticFixedVectorInitializationProbe::Pixel20Dp3UnwrittenYz &&
            fixedVectorInitializationProbe <=
                SyntheticFixedVectorInitializationProbe::Pixel20NrmReplicatedX)
        {
            using Probe = SyntheticFixedVectorInitializationProbe;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            const bool dp3 = fixedVectorInitializationProbe == Probe::Pixel20Dp3UnwrittenYz ||
                             fixedVectorInitializationProbe == Probe::Pixel20Dp3ReplicatedX;
            const bool dp4 = fixedVectorInitializationProbe == Probe::Pixel20Dp4UnwrittenYzw ||
                             fixedVectorInitializationProbe == Probe::Pixel20Dp4ReplicatedX;
            const bool nrm = fixedVectorInitializationProbe == Probe::Pixel20NrmUnwrittenYz ||
                             fixedVectorInitializationProbe == Probe::Pixel20NrmReplicatedX;
            const bool sourceOne =
                fixedVectorInitializationProbe == Probe::Pixel20Dp2AddSource1UnwrittenY ||
                fixedVectorInitializationProbe == Probe::Pixel20Dp2AddSource1ReplicatedX;
            const bool sourceTwo =
                fixedVectorInitializationProbe == Probe::Pixel20Dp2AddSource2UnwrittenY ||
                fixedVectorInitializationProbe == Probe::Pixel20Dp2AddSource2WrittenX;
            const bool initialized =
                fixedVectorInitializationProbe >= Probe::Pixel20Dp3ReplicatedX;
            const std::uint32_t opcode = dp3 ? 0x00000008u
                : dp4                           ? 0x00000009u
                : nrm                           ? 0x00000024u
                                                : 0x0000005Au;
            const bool dp2add = !dp3 && !dp4 && !nrm;
            const std::uint32_t operandCount = nrm ? 2u : dp2add ? 4u : 3u;
            AppendUInt32(shader, opcode | (operandCount << 24));
            AppendUInt32(shader, destination(regTemp, 1, nrm ? 0xFu : 0x1u));
            const std::uint32_t partialSource = source(
                regTemp, 0, sourceTwo && !initialized ? 0x55u
                                                      : initialized ? 0x00u : swizzleIdentity);
            if (sourceOne)
            {
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, partialSource);
                AppendUInt32(shader, source(regConst, 0, 0x00u));
            }
            else if (sourceTwo)
            {
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, partialSource);
            }
            else
            {
                AppendUInt32(shader, partialSource);
                if (!nrm) AppendUInt32(shader, source(regConst, 0));
                if (dp2add) AppendUInt32(shader, source(regConst, 0, 0x00u));
            }
        }
        if (matrixInitializationProbe >=
                SyntheticMatrixInitializationProbe::Pixel20M4x4VectorUnwrittenW &&
            matrixInitializationProbe <=
                SyntheticMatrixInitializationProbe::Pixel20M3x2MatrixRowsWrittenXyz)
        {
            using Probe = SyntheticMatrixInitializationProbe;
            const bool matrixRows =
                matrixInitializationProbe == Probe::Pixel20M3x2MatrixRowUnwrittenZ ||
                matrixInitializationProbe == Probe::Pixel20M3x2MatrixRowsWrittenXyz;
            const bool initialized =
                matrixInitializationProbe >= Probe::Pixel20M4x4VectorWritten;
            if (matrixRows)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.xyz, c0
                AppendUInt32(shader, destination(regTemp, 0, 0x7u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1.xy[z], c0
                AppendUInt32(shader, destination(regTemp, 1, initialized ? 0x7u : 0x3u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, 0x00000018u | (3u << 24)); // m3x2 r4.xy, c0, r0
                AppendUInt32(shader, destination(regTemp, 4, 0x3u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regTemp, 0));
            }
            else
            {
                std::uint32_t opcode = 0x00000014u;
                std::uint32_t destinationMask = 0xFu;
                bool vectorHasFourComponents = true;
                switch (matrixInitializationProbe)
                {
                    case Probe::Pixel20M4x3VectorUnwrittenW:
                    case Probe::Pixel20M4x3VectorWritten:
                        opcode = 0x00000015u;
                        destinationMask = 0x7u;
                        break;
                    case Probe::Pixel20M3x4VectorUnwrittenZ:
                    case Probe::Pixel20M3x4VectorWrittenXyz:
                        opcode = 0x00000016u;
                        vectorHasFourComponents = false;
                        break;
                    case Probe::Pixel20M3x3VectorUnwrittenZ:
                    case Probe::Pixel20M3x3VectorWrittenXyz:
                        opcode = 0x00000017u;
                        destinationMask = 0x7u;
                        vectorHasFourComponents = false;
                        break;
                    case Probe::Pixel20M3x2VectorUnwrittenZ:
                    case Probe::Pixel20M3x2VectorWrittenXyz:
                        opcode = 0x00000018u;
                        destinationMask = 0x3u;
                        vectorHasFourComponents = false;
                        break;
                    default:
                        break;
                }
                const std::uint32_t vectorMask = initialized
                    ? (vectorHasFourComponents ? 0xFu : 0x7u)
                    : (vectorHasFourComponents ? 0x7u : 0x3u);
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.mask, c0
                AppendUInt32(shader, destination(regTemp, 0, vectorMask));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, opcode | (3u << 24));
                AppendUInt32(shader, destination(regTemp, 4, destinationMask));
                AppendUInt32(shader, source(regTemp, 0));
                AppendUInt32(shader, source(regConst, 4));
            }
        }
        const bool probesPixel20CrsInitialization =
            specialVectorInitializationProbe ==
                SyntheticSpecialVectorInitializationProbe::Pixel20CrsSource0UnwrittenZ ||
            specialVectorInitializationProbe ==
                SyntheticSpecialVectorInitializationProbe::Pixel20CrsSource1UnwrittenZ ||
            specialVectorInitializationProbe ==
                SyntheticSpecialVectorInitializationProbe::Pixel20CrsSource0WrittenXyz ||
            specialVectorInitializationProbe ==
                SyntheticSpecialVectorInitializationProbe::Pixel20CrsSource1WrittenXyz;
        if (probesPixel20CrsInitialization)
        {
            using Probe = SyntheticSpecialVectorInitializationProbe;
            const bool initialized =
                specialVectorInitializationProbe == Probe::Pixel20CrsSource0WrittenXyz ||
                specialVectorInitializationProbe == Probe::Pixel20CrsSource1WrittenXyz;
            const bool sourceOne =
                specialVectorInitializationProbe == Probe::Pixel20CrsSource1UnwrittenZ ||
                specialVectorInitializationProbe == Probe::Pixel20CrsSource1WrittenXyz;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.xy[z], c0
            AppendUInt32(shader, destination(regTemp, 0, initialized ? 0x7u : 0x3u));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000021u | (3u << 24)); // crs r1.xyz, ..., ...
            AppendUInt32(shader, destination(regTemp, 1, 0x7u));
            AppendUInt32(shader, source(sourceOne ? regConst : regTemp, 0));
            AppendUInt32(shader, source(sourceOne ? regTemp : regConst, 0));
        }

        if (pixel1OutputLivenessProbe != SyntheticPixel1OutputLivenessProbe::None)
        {
            const bool alphaOnly =
                pixel1OutputLivenessProbe == SyntheticPixel1OutputLivenessProbe::Pixel11AlphaOnly;
            const bool splitFull =
                pixel1OutputLivenessProbe == SyntheticPixel1OutputLivenessProbe::Pixel14SplitFull;
            const std::uint32_t firstMask = alphaOnly ? 0x8u
                : pixel1OutputLivenessProbe ==
                      SyntheticPixel1OutputLivenessProbe::Pixel11RgbOnly
                    ? 0x7u
                    : 0x5u;
            AppendUInt32(shader, 0x00000001u); // mov r0.mask, c0
            AppendUInt32(shader, destination(regTemp, 0, firstMask));
            AppendUInt32(shader, source(regConst, 0));
            if (splitFull)
            {
                AppendUInt32(shader, 0x00000001u); // mov r0.yw, c0
                AppendUInt32(shader, destination(regTemp, 0, 0xAu));
                AppendUInt32(shader, source(regConst, 0));
            }
        }

        if (shaderModel14PhaseProbe != SyntheticShaderModel14PhaseProbe::None)
        {
            using Probe = SyntheticShaderModel14PhaseProbe;
            const auto appendMov = [&](std::uint32_t destinationRegister,
                                       std::uint32_t writeMask,
                                       std::uint32_t sourceRegister,
                                       std::uint32_t sourceSwizzle = 0xE4u,
                                       std::uint32_t sourceType = 2u) {
                AppendUInt32(shader, 0x00000001u); // mov r#, source
                AppendUInt32(shader, destination(regTemp, destinationRegister, writeMask));
                AppendUInt32(shader, source(sourceType, sourceRegister, sourceSwizzle));
            };
            const auto appendTexcrd = [&](std::uint32_t destinationRegister,
                                          std::uint32_t sourceRegister) {
                AppendUInt32(shader, 0x00000040u); // texcrd r#.xyz, t#
                AppendUInt32(shader, destination(regTemp, destinationRegister, 0x7u));
                AppendUInt32(shader, source(regTexture, sourceRegister));
            };
            const auto appendTexkill = [&](std::uint32_t registerType,
                                           std::uint32_t registerNumber) {
                AppendUInt32(shader, 0x00000041u); // texkill register
                AppendUInt32(shader, destination(registerType, registerNumber, 0xFu));
            };
            const auto appendPhase = [&]() { AppendUInt32(shader, 0x0000FFFDu); };

            switch (shaderModel14PhaseProbe)
            {
                case Probe::TextureAfterArithmetic:
                    appendMov(0, 0xFu, 0);
                    appendTexkill(regTexture, 0);
                    break;
                case Probe::SecondPhaseTextureAfterArithmetic:
                    appendMov(1, 0xFu, 0);
                    appendPhase();
                    appendMov(0, 0xFu, 0);
                    appendTexkill(regTexture, 0);
                    break;
                case Probe::ColorReadBeforePhase:
                    appendMov(1, 0xFu, 0, swizzleIdentity, regInput);
                    appendPhase();
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::TexkillBeforePhase:
                    appendTexkill(regTexture, 0);
                    appendPhase();
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::DependentTextureReadInSameBlock:
                    appendTexcrd(1, 0);
                    AppendUInt32(shader, 0x00000042u); // texld r0, r1
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regTemp, 1));
                    break;
                case Probe::TextureDestinationReuse:
                    appendTexcrd(1, 0);
                    appendTexcrd(1, 1);
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::TexdepthBeforePhase:
                    appendMov(5, 0x3u, 0);
                    AppendUInt32(shader, 0x00000057u); // texdepth r5
                    AppendUInt32(shader, destination(regTemp, 5, 0xFu));
                    appendPhase();
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::TexdepthReadAfter:
                    appendMov(5, 0x3u, 0);
                    appendPhase();
                    AppendUInt32(shader, 0x00000057u); // texdepth r5
                    AppendUInt32(shader, destination(regTemp, 5, 0xFu));
                    appendMov(0, 0xFu, 5, swizzleIdentity, regTemp);
                    break;
                case Probe::AlphaReadAfterPhase:
                    appendMov(1, 0x8u, 0, 0xFFu);
                    appendPhase();
                    appendMov(0, 0xFu, 1, 0xFFu, regTemp);
                    break;
                case Probe::OutputAlphaLostAtPhase:
                    appendMov(0, 0xFu, 0);
                    appendPhase();
                    break;
                case Probe::NoMarkerColorRead:
                    appendMov(0, 0xFu, 0, swizzleIdentity, regInput);
                    break;
                case Probe::NoMarkerTexkill:
                    appendTexkill(regTexture, 0);
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::PreserveRgbAcrossPhase:
                    appendMov(1, 0x7u, 0);
                    appendPhase();
                    appendMov(0, 0x7u, 1, swizzleIdentity, regTemp);
                    appendMov(0, 0x8u, 0, 0xFFu);
                    break;
                case Probe::ReinitializeAlphaAfterPhase:
                    appendMov(1, 0x8u, 0, 0xFFu);
                    appendPhase();
                    appendMov(1, 0x8u, 0, 0xFFu);
                    appendMov(0, 0xFu, 1, 0xFFu, regTemp);
                    break;
                case Probe::TextureThenArithmeticBothPhases:
                    appendTexcrd(1, 0);
                    appendMov(1, 0x8u, 0, 0xFFu);
                    appendPhase();
                    appendTexcrd(2, 1);
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::DependentTextureReadFromPreviousPhase:
                    appendTexcrd(1, 0);
                    appendPhase();
                    AppendUInt32(shader, 0x00000042u); // texld r0, r1
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regTemp, 1));
                    break;
                case Probe::TexkillPreservesCoordinate:
                    appendTexcrd(1, 0);
                    appendPhase();
                    appendTexkill(regTemp, 1);
                    AppendUInt32(shader, 0x00000042u); // texld r0, r1
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regTemp, 1));
                    break;
                case Probe::TexdepthAfterPhase:
                    appendMov(5, 0x3u, 0);
                    appendPhase();
                    AppendUInt32(shader, 0x00000057u); // texdepth r5
                    AppendUInt32(shader, destination(regTemp, 5, 0xFu));
                    appendMov(0, 0xFu, 0);
                    break;
                case Probe::None: break;
            }
        }

        if (temporaryRegisterProbe != SyntheticTemporaryRegisterProbe::None &&
            (probesPixel11Temporary || probesPixel14Temporary || probesPixel20Temporary ||
             probesPixel2xTemporary || probesPixel30Temporary))
        {
            const std::uint32_t registerNumber =
                temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel11Maximum
                    ? 1u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel11OutOfRange
                    ? 2u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel14Maximum
                    ? 5u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel14OutOfRange
                    ? 6u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel20Maximum
                    ? 11u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel20OutOfRange
                    ? 12u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel2xMaximum
                    ? 31u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel2xOutOfRange
                    ? 32u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Pixel30Maximum
                    ? 31u
                    : 32u;
            AppendUInt32(shader, 0x00000001u |
                                     ((probesPixel11Temporary || probesPixel14Temporary)
                                          ? 0u
                                          : (2u << 24))); // mov r#, c0
            AppendUInt32(shader, destination(regTemp, registerNumber, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }

        if (texkillReadsPartialTemporary)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.xy, c0.xy
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000041u | (1u << 24)); // texkill r0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
        }

        if (texkillReadsSplitTemporary)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.xy, c0.xy
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.z, c0.z
            AppendUInt32(shader, destination(regTemp, 0, 0x4u));
            AppendUInt32(shader, source(regConst, 0, 0xAAu));
            AppendUInt32(shader, 0x00000041u | (1u << 24)); // texkill r0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
        }

        if (texkillOperandProbe != SyntheticTexkillOperandProbe::None)
        {
            if (texkillOperandProbe ==
                SyntheticTexkillOperandProbe::Pixel14TemporaryXyz)
            {
                AppendUInt32(shader, 0x00000040u); // texcrd r0.xyz, t0
                AppendUInt32(shader, destination(regTemp, 0, 0x7u));
                AppendUInt32(shader, source(regTexture, 0));
                AppendUInt32(shader, 0x0000FFFDu); // phase
                AppendUInt32(shader, 0x00000041u); // texkill r0
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader, 0x00000001u); // mov r0.w, c0.w
                AppendUInt32(shader, destination(regTemp, 0, 0x8u));
                AppendUInt32(shader, source(regConst, 0, 0xFFu));
            }
            else
            {
                if (texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30TemporaryXy)
                {
                    AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.xy, c0.xy
                    AppendUInt32(shader, destination(regTemp, 0, 0x3u));
                    AppendUInt32(shader, source(regConst, 0));
                }
                const bool shaderModel3Input =
                    texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputXy ||
                    texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputXyz ||
                    texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel30InputFull;
                const bool shaderModel20Texture =
                    texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel20TextureXy ||
                    texkillOperandProbe == SyntheticTexkillOperandProbe::Pixel20TextureXyz;
                AppendUInt32(shader, 0x00000041u | (1u << 24)); // texkill r0/v0/t0
                AppendUInt32(shader,
                             destination(shaderModel3Input ? regInput
                                         : shaderModel20Texture ? regTexture
                                                                : regTemp,
                                         0, 0xFu));
            }
        }

        if (usesInvalidSgn)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, c0
            AppendUInt32(shader, destination(regTemp, 1, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r2, c0
            AppendUInt32(shader, destination(regTemp, 2, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000022u | (4u << 24)); // sgn r0, c0, r1, r2
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, source(regTemp, 1));
            AppendUInt32(shader, source(regTemp, 2));
        }

        if (usesInvalidExpp || usesInvalidLogp)
        {
            const std::uint32_t opcode = usesInvalidExpp ? 0x0000004Eu : 0x0000004Fu;
            AppendUInt32(shader, opcode | (2u << 24));
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
        }

        if (usesInvalidLit || usesInvalidExpSwizzle)
        {
            const std::uint32_t opcode = usesInvalidLit ? 0x00000010u : 0x0000000Eu;
            AppendUInt32(shader, opcode | (2u << 24));
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }

        if (usesInvalidSlt || usesInvalidSge)
        {
            const std::uint32_t opcode = usesInvalidSlt ? 0x0000000Cu : 0x0000000Du;
            AppendUInt32(shader, opcode | (3u << 24));
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, source(regConst, 0));
        }

        if (shaderModel1InvalidOpcode != SyntheticInvalidPixelShaderModel1Opcode::None)
        {
            const auto appendScalar = [&](std::uint32_t opcode) {
                AppendUInt32(shader, opcode);
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader, source(regConst, 0, 0x00u));
            };
            const auto appendVector = [&](std::uint32_t opcode, std::uint32_t writeMask = 0xFu) {
                AppendUInt32(shader, opcode);
                AppendUInt32(shader, destination(regTemp, 0, writeMask));
                AppendUInt32(shader, source(regConst, 0));
            };
            const auto appendBinary = [&](std::uint32_t opcode, std::uint32_t writeMask = 0xFu) {
                AppendUInt32(shader, opcode);
                AppendUInt32(shader, destination(regTemp, 0, writeMask));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
            };
            switch (shaderModel1InvalidOpcode)
            {
                case SyntheticInvalidPixelShaderModel1Opcode::Rcp: appendScalar(0x00000006u); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Rsq: appendScalar(0x00000007u); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Min: appendBinary(0x0000000Au); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Max: appendBinary(0x0000000Bu); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Exp: appendScalar(0x0000000Eu); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Log: appendScalar(0x0000000Fu); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Frc:
                    appendVector(0x00000013u, 0x2u);
                    break;
                case SyntheticInvalidPixelShaderModel1Opcode::Pow:
                    AppendUInt32(shader, 0x00000020u);
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    break;
                case SyntheticInvalidPixelShaderModel1Opcode::Crs:
                    appendBinary(0x00000021u, 0x7u);
                    break;
                case SyntheticInvalidPixelShaderModel1Opcode::Abs: appendVector(0x00000023u); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Nrm: appendVector(0x00000024u); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Dsx: appendVector(0x0000005Bu); break;
                case SyntheticInvalidPixelShaderModel1Opcode::Dsy: appendVector(0x0000005Cu); break;
                case SyntheticInvalidPixelShaderModel1Opcode::None: break;
            }
        }

        if (shaderModel20InvalidOpcode != SyntheticInvalidPixelShaderModel20Opcode::None)
        {
            switch (shaderModel20InvalidOpcode)
            {
                case SyntheticInvalidPixelShaderModel20Opcode::Defb:
                    AppendUInt32(shader, 0x0000002Fu | (2u << 24)); // defb b0, true
                    AppendUInt32(shader, destination(regConstBool, 0, 0xFu));
                    AppendUInt32(shader, 1u);
                    break;
                case SyntheticInvalidPixelShaderModel20Opcode::Defi:
                    AppendUInt32(shader, 0x00000030u | (5u << 24)); // defi i0, 1, 0, 1, 0
                    AppendUInt32(shader, destination(regConstInt, 0, 0xFu));
                    AppendUInt32(shader, 1u);
                    AppendUInt32(shader, 0u);
                    AppendUInt32(shader, 1u);
                    AppendUInt32(shader, 0u);
                    break;
                case SyntheticInvalidPixelShaderModel20Opcode::Rep:
                    AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i0
                    AppendUInt32(shader, source(regConstInt, 0, 0x00u));
                    AppendUInt32(shader, 0x00000027u); // endrep
                    break;
                case SyntheticInvalidPixelShaderModel20Opcode::If:
                    AppendUInt32(shader, 0x00000028u | (1u << 24)); // if b0
                    AppendUInt32(shader, source(regConstBool, 0, 0x00u));
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticInvalidPixelShaderModel20Opcode::Dsx:
                case SyntheticInvalidPixelShaderModel20Opcode::Dsy:
                    AppendUInt32(
                        shader,
                        (shaderModel20InvalidOpcode == SyntheticInvalidPixelShaderModel20Opcode::Dsx
                             ? 0x0000005Bu
                             : 0x0000005Cu) |
                            (2u << 24));
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0));
                    break;
                case SyntheticInvalidPixelShaderModel20Opcode::Setp:
                    AppendUInt32(shader,
                                 0x0000005Eu | (1u << 16) | (3u << 24)); // setp_gt p0, c0.x, c0.y
                    AppendUInt32(shader, destination(regPredicate, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    AppendUInt32(shader, source(regConst, 0, 0x55u));
                    break;
                case SyntheticInvalidPixelShaderModel20Opcode::None: break;
            }
        }

        if (shaderModel20InvalidDynamicFeature ==
            SyntheticInvalidShaderModel20DynamicFeature::PixelPredicatedMov)
        {
            AppendUInt32(shader, 0x10000001u | (3u << 24)); // (p0.x) mov r0, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, source(regPredicate, 0, 0x00u));
        }

        if (invalidMatrixOperands != SyntheticInvalidMatrixOperands::None)
        {
            for (std::uint32_t row = 1; row <= 3; ++row)
            {
                AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c#, identity row
                AppendUInt32(shader, destination(regConst, row, 0xFu));
                for (std::uint32_t component = 0; component < 4; ++component)
                    AppendUInt32(shader, FloatBits(component == row - 1 ? 1.0f : 0.0f));
            }
            if (invalidMatrixOperands ==
                SyntheticInvalidMatrixOperands::DestinationAliasesVectorSource)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader, source(regConst, 0));
            }
            AppendUInt32(shader, 0x00000015u | (3u << 24)); // m4x3 r0.xyz, src0, c1
            AppendUInt32(shader, destination(regTemp, 0, 0x7u));
            AppendUInt32(
                shader,
                source(invalidMatrixOperands ==
                               SyntheticInvalidMatrixOperands::DestinationAliasesVectorSource
                           ? regTemp
                           : regConst,
                       0));
            constexpr std::uint32_t swizzleYzxw =
                1u | (2u << 2u) | (0u << 4u) | (3u << 6u);
            AppendUInt32(
                shader,
                source(regConst, 1,
                       invalidMatrixOperands ==
                               SyntheticInvalidMatrixOperands::SwizzledMatrixSource
                           ? swizzleYzxw
                           : 0xE4u,
                       invalidMatrixOperands ==
                               SyntheticInvalidMatrixOperands::NegatedMatrixSource
                           ? 1u
                           : 0u));
        }

        if (invalidAbsoluteSource ==
                SyntheticInvalidPreShaderModel3AbsoluteSource::PixelAbsolute ||
            invalidAbsoluteSource ==
                SyntheticInvalidPreShaderModel3AbsoluteSource::PixelAbsoluteNegate)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, abs(c0)
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(
                shader,
                source(regConst, 0, 0xE4u,
                       invalidAbsoluteSource ==
                               SyntheticInvalidPreShaderModel3AbsoluteSource::PixelAbsolute
                           ? 11u
                           : 12u));
        }

        if (invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelPlainThenAbsolute ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAbsoluteThenPlain ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAllAbsolute)
        {
            const bool absoluteFirst =
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAbsoluteThenPlain ||
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAllAbsolute;
            const bool absoluteSecond =
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelPlainThenAbsolute ||
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::PixelAllAbsolute;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0/abs(c0)
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0, 0xE4u, absoluteFirst ? 11u : 0u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, abs(c1)/c1
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 1, 0xE4u, absoluteSecond ? 11u : 0u));
        }

        if (pixel1CoissueProbe != SyntheticPixel1CoissueProbe::None)
        {
            const auto appendMov = [&](std::uint32_t writeMask, bool coissued) {
                AppendUInt32(shader, 0x00000001u | (coissued ? 0x40000000u : 0u));
                AppendUInt32(shader, destination(regTemp, 0, writeMask));
                AppendUInt32(shader, source(regConst, 0));
            };
            if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11RgbThenAlpha)
            {
                appendMov(0x7u, false);
                appendMov(0x8u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11AlphaThenRgb)
            {
                appendMov(0x8u, false);
                appendMov(0x7u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11Texture)
            {
                appendMov(0x7u, false);
                AppendUInt32(shader, 0x40000040u); // +texcoord t0
                AppendUInt32(shader, destination(regTexture, 0, 0xFu));
                appendMov(0xFu, false);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11OrphanAlpha)
            {
                appendMov(0x8u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11RgbThenRgb)
            {
                appendMov(0x7u, false);
                appendMov(0x7u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11FullThenAlpha)
            {
                appendMov(0xFu, false);
                appendMov(0x8u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11TextureThenAlpha)
            {
                AppendUInt32(shader, 0x00000040u); // texcoord t0
                AppendUInt32(shader, destination(regTexture, 0, 0xFu));
                appendMov(0x8u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel11Triple)
            {
                appendMov(0x7u, false);
                appendMov(0x8u, true);
                appendMov(0x7u, true);
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel14Dp4)
            {
                appendMov(0x7u, false);
                AppendUInt32(shader, 0x40000009u); // +dp4 r0.a, c0, c0
                AppendUInt32(shader, destination(regTemp, 0, 0x8u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
            }
            else if (pixel1CoissueProbe == SyntheticPixel1CoissueProbe::Pixel14Dp4ThenAlpha)
            {
                AppendUInt32(shader, 0x00000009u); // dp4 r0.rgb, c0, c0
                AppendUInt32(shader, destination(regTemp, 0, 0x7u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
                appendMov(0x8u, true);
            }
            else
            {
                appendMov(0x7u, false);
                AppendUInt32(shader, 0x0000FFFDu); // phase
                appendMov(0x8u, true);
            }
        }
        else if (pixel1DestinationMaskProbe != SyntheticPixel1DestinationMaskProbe::None)
        {
            if (pixel1DestinationMaskProbe ==
                SyntheticPixel1DestinationMaskProbe::Pixel11Dp3Alpha)
            {
                AppendUInt32(shader, 0x00000008u); // dp3 r0.a, c0, c0
                AppendUInt32(shader, destination(regTemp, 0, 0x8u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
            }
            else if (pixel1DestinationMaskProbe ==
                     SyntheticPixel1DestinationMaskProbe::Pixel11TexturePartial)
            {
                AppendUInt32(shader, 0x00000040u); // texcoord t0.rgb
                AppendUInt32(shader, destination(regTexture, 0, 0x7u));
                AppendUInt32(shader, 0x00000001u); // mov r0, t0
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader, source(regTexture, 0));
            }
            else if (pixel1DestinationMaskProbe ==
                     SyntheticPixel1DestinationMaskProbe::Pixel14TexcrdArbitrary)
            {
                AppendUInt32(shader, 0x00000040u); // texcrd r0.rb, t0
                AppendUInt32(shader, destination(regTemp, 0, 0x5u));
                AppendUInt32(shader, source(regTexture, 0));
            }
            else
            {
                std::uint32_t writeMask = 0xFu;
                if (pixel1DestinationMaskProbe ==
                    SyntheticPixel1DestinationMaskProbe::Pixel11MovRgb)
                {
                    writeMask = 0x7u;
                }
                else if (pixel1DestinationMaskProbe ==
                         SyntheticPixel1DestinationMaskProbe::Pixel11MovAlpha)
                {
                    writeMask = 0x8u;
                }
                else if (pixel1DestinationMaskProbe ==
                             SyntheticPixel1DestinationMaskProbe::Pixel11MovArbitrary ||
                         pixel1DestinationMaskProbe ==
                             SyntheticPixel1DestinationMaskProbe::Pixel14MovArbitrary)
                {
                    writeMask = 0x5u;
                }
                AppendUInt32(shader, 0x00000001u); // mov r0.mask, c0
                AppendUInt32(shader, destination(regTemp, 0, writeMask));
                AppendUInt32(shader, source(regConst, 0));
                if (pixel1DestinationMaskProbe ==
                    SyntheticPixel1DestinationMaskProbe::Pixel11MovRgb)
                {
                    AppendUInt32(shader, 0x00000001u); // mov r0.a, c0.a
                    AppendUInt32(shader, destination(regTemp, 0, 0x8u));
                    AppendUInt32(shader, source(regConst, 0, 0xFFu));
                }
                else if (pixel1DestinationMaskProbe ==
                         SyntheticPixel1DestinationMaskProbe::Pixel11MovAlpha)
                {
                    AppendUInt32(shader, 0x00000001u); // mov r0.rgb, c0
                    AppendUInt32(shader, destination(regTemp, 0, 0x7u));
                    AppendUInt32(shader, source(regConst, 0));
                }
                else if (pixel1DestinationMaskProbe ==
                         SyntheticPixel1DestinationMaskProbe::Pixel14MovArbitrary)
                {
                    AppendUInt32(shader, 0x00000001u); // mov r0.ga, c0
                    AppendUInt32(shader, destination(regTemp, 0, 0xAu));
                    AppendUInt32(shader, source(regConst, 0));
                }
            }
        }
        else if (pixel1InstructionSlotProbe != SyntheticPixel1InstructionSlotProbe::None)
        {
            const bool outOfRange =
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel11ArithmeticOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel11TextureOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel12DoubleArithmeticOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel14ArithmeticOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel14TextureOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel14SecondPhaseArithmeticOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel11CoissuedArithmeticOutOfRange ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel11TexbemlArithmeticOutOfRange;
            const auto appendMov = [&](std::uint32_t writeMask = 0xFu,
                                       bool coissued = false) {
                AppendUInt32(shader, 0x00000001u | (coissued ? 0x40000000u : 0u));
                AppendUInt32(shader, destination(regTemp, 0, writeMask));
                AppendUInt32(shader, source(regConst, 0));
            };
            if (pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel11TextureMaximum ||
                pixel1InstructionSlotProbe ==
                    SyntheticPixel1InstructionSlotProbe::Pixel11TextureOutOfRange)
            {
                const std::uint32_t textureSlots = outOfRange ? 5u : 4u;
                for (std::uint32_t slot = 0; slot < textureSlots; ++slot)
                {
                    AppendUInt32(shader, 0x00000041u); // texkill t0
                    AppendUInt32(shader, destination(regTexture, 0, 0xFu));
                }
                appendMov();
            }
            else if (probesPixel12InstructionSlots)
            {
                const std::uint32_t instructions = outOfRange ? 5u : 4u;
                for (std::uint32_t slot = 0; slot < instructions; ++slot)
                {
                    AppendUInt32(shader, 0x00000009u); // dp4 r0, c0, c0
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0));
                    AppendUInt32(shader, source(regConst, 0));
                }
            }
            else if (pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel14TextureMaximum ||
                     pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel14TextureOutOfRange)
            {
                const std::uint32_t textureSlots = outOfRange ? 7u : 6u;
                for (std::uint32_t slot = 0; slot < textureSlots; ++slot)
                {
                    AppendUInt32(shader, 0x00000041u); // texkill t0
                    AppendUInt32(shader, destination(regTexture, 0, 0xFu));
                }
                appendMov();
            }
            else if (pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel11TexbemlArithmeticMaximum ||
                     pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel11TexbemlArithmeticOutOfRange)
            {
                AppendUInt32(shader, 0x00000044u); // texbeml t1, t0
                AppendUInt32(shader, destination(regTexture, 1, 0xFu));
                AppendUInt32(shader, source(regTexture, 0));
                const std::uint32_t movSlots = outOfRange ? 8u : 7u;
                for (std::uint32_t slot = 0; slot < movSlots; ++slot) appendMov();
            }
            else if (pixel1InstructionSlotProbe ==
                     SyntheticPixel1InstructionSlotProbe::Pixel11ZeroSlotNopControl)
            {
                for (std::uint32_t slot = 0; slot < 32u; ++slot)
                    AppendUInt32(shader, 0x00000000u); // nop
                for (std::uint32_t slot = 0; slot < 8u; ++slot) appendMov();
            }
            else if (pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel11CoissuedArithmeticMaximum ||
                     pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel11CoissuedArithmeticOutOfRange)
            {
                const std::uint32_t pairs = outOfRange ? 9u : 8u;
                for (std::uint32_t slot = 0; slot < pairs; ++slot)
                {
                    appendMov(0x7u);
                    appendMov(0x8u, true);
                }
            }
            else if (pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel14TwoPhaseArithmeticMaximum ||
                     pixel1InstructionSlotProbe ==
                         SyntheticPixel1InstructionSlotProbe::Pixel14SecondPhaseArithmeticOutOfRange)
            {
                for (std::uint32_t slot = 0; slot < 8u; ++slot) appendMov();
                AppendUInt32(shader, 0x0000FFFDu); // phase
                const std::uint32_t secondPhaseSlots = outOfRange ? 9u : 8u;
                for (std::uint32_t slot = 0; slot < secondPhaseSlots; ++slot) appendMov();
            }
            else
            {
                const std::uint32_t arithmeticSlots = outOfRange ? 9u : 8u;
                for (std::uint32_t slot = 0; slot < arithmeticSlots; ++slot) appendMov();
            }
        }
        else if (instructionSlotProbe != SyntheticPixel20InstructionSlotProbe::None)
        {
            const bool texture =
                instructionSlotProbe == SyntheticPixel20InstructionSlotProbe::TextureMaximum ||
                instructionSlotProbe == SyntheticPixel20InstructionSlotProbe::TextureOutOfRange;
            const bool maximum =
                instructionSlotProbe == SyntheticPixel20InstructionSlotProbe::ArithmeticMaximum ||
                instructionSlotProbe == SyntheticPixel20InstructionSlotProbe::TextureMaximum;
            if (texture)
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl t0
                AppendUInt32(shader, 0x80000000u);
                AppendUInt32(shader, destination(regTexture, 0, 0xFu));
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_2d s0
                AppendUInt32(shader, 0x80000000u | (2u << 27));
                AppendUInt32(shader, destination(regSampler, 0, 0xFu));
                const std::uint32_t textureSlots = maximum ? 32u : 33u;
                for (std::uint32_t slot = 0; slot < textureSlots; ++slot)
                {
                    AppendUInt32(shader, 0x00000042u | (3u << 24)); // texld r0, t0, s0
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regTexture, 0));
                    AppendUInt32(shader, source(regSampler, 0));
                }
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regTemp, 0));
            }
            else
            {
                // The final output MOV is itself the 64th/65th arithmetic slot.
                const std::uint32_t temporaryMoves = maximum ? 63u : 64u;
                for (std::uint32_t slot = 0; slot < temporaryMoves; ++slot)
                {
                    AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0));
                }
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regTemp, 0));
            }
        }
        else if (temporaryTextureSelectorProbe !=
                 SyntheticTemporaryTextureSelectorProbe::None)
        {
            using Probe = SyntheticTemporaryTextureSelectorProbe;
            AppendUInt32(shader, 0x00000040u); // texcrd r1.xyz, t0
            AppendUInt32(shader, destination(regTemp, 1, 0x7u));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x0000FFFDu); // phase
            AppendUInt32(shader, 0x00000042u); // texld r0, r1.selector
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            const std::uint32_t selector = temporaryTextureSelectorProbe == Probe::Xyw
                                               ? 0xF4u
                                           : temporaryTextureSelectorProbe == Probe::Reordered
                                               ? 0x52u
                                           : temporaryTextureSelectorProbe == Probe::Xyz
                                               ? 0xA4u
                                               : swizzleIdentity;
            AppendUInt32(shader, source(regTemp, 1, selector));
        }
        else if (textureCoordinateSelectorProbe !=
                 SyntheticTextureCoordinateSelectorProbe::None)
        {
            using Probe = SyntheticTextureCoordinateSelectorProbe;
            const bool invalidTexcrd =
                textureCoordinateSelectorProbe == Probe::TexcrdInvalidSelector;
            const bool invalidTexld =
                textureCoordinateSelectorProbe == Probe::TexldInvalidSelector;
            const bool identityFirst =
                textureCoordinateSelectorProbe == Probe::IdentityThenXyw ||
                textureCoordinateSelectorProbe == Probe::IdentityThenXyz;
            const bool xywFirst =
                textureCoordinateSelectorProbe == Probe::RepeatXyw ||
                textureCoordinateSelectorProbe == Probe::XywThenDw;

            if (invalidTexld)
            {
                AppendUInt32(shader, 0x00000042u); // texld r0, t0.zxy
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader, source(regTexture, 0, 0x52u));
            }
            else
            {
                AppendUInt32(shader, 0x00000040u); // texcrd r1.xyz, t0.selector
                AppendUInt32(shader, destination(regTemp, 1, 0x7u));
                AppendUInt32(shader, source(
                    regTexture, 0,
                    invalidTexcrd ? 0x52u
                                  : identityFirst ? swizzleIdentity : xywFirst ? 0xF4u : 0xA4u));

                if (!invalidTexcrd)
                {
                    const bool differentRegisters =
                        textureCoordinateSelectorProbe == Probe::DifferentRegisters;
                    const bool projective =
                        textureCoordinateSelectorProbe == Probe::XyzThenDw ||
                        textureCoordinateSelectorProbe == Probe::XywThenDw;
                    const bool secondXyw =
                        textureCoordinateSelectorProbe != Probe::IdentityThenXyz;
                    AppendUInt32(shader, 0x00000040u); // texcrd r2.xyz/xy, t#.selector
                    AppendUInt32(shader, destination(regTemp, 2, projective ? 0x3u : 0x7u));
                    AppendUInt32(shader, source(
                        regTexture, differentRegisters ? 1u : 0u,
                        secondXyw ? 0xF4u : 0xA4u, projective ? 10u : 0u));
                }

                AppendUInt32(shader, 0x00000001u); // mov r0, c0
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader, source(regConst, 0));
            }
        }
        else if (legacyBumpEnvironment == SyntheticLegacyBumpEnvironment::Arithmetic ||
                 bemOperandProbe != SyntheticBemOperandProbe::None)
        {
            using Probe = SyntheticBemOperandProbe;
            AppendUInt32(shader, 0x00000051u); // def c1, .1, .2, .3, .4
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.1f));
            AppendUInt32(shader, FloatBits(0.2f));
            AppendUInt32(shader, FloatBits(0.3f));
            AppendUInt32(shader, FloatBits(0.4f));
            AppendUInt32(shader, 0x00000051u); // def c2, .5, .25, 0, 0
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            AppendUInt32(shader, FloatBits(0.5f));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            const bool source0UninitializedY =
                bemOperandProbe == Probe::Source0UninitializedY;
            const bool source1UninitializedY =
                bemOperandProbe == Probe::Source1UninitializedY;
            const bool replicatedX = bemOperandProbe == Probe::ReplicatedX;
            const bool temporaryModifiers =
                bemOperandProbe == Probe::TemporarySourceModifiers;
            const bool source0Temporary =
                source0UninitializedY || replicatedX || temporaryModifiers;
            AppendUInt32(shader, 0x00000001u); // mov r1.xy/x, c1
            AppendUInt32(shader, destination(
                regTemp, 1, source0UninitializedY || replicatedX ? 0x1u : 0x3u));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000001u); // mov r2.xy/x, c2
            AppendUInt32(shader, destination(
                regTemp, 2, source1UninitializedY || replicatedX ? 0x1u : 0x3u));
            AppendUInt32(shader, source(regConst, 2));
            AppendUInt32(shader, 0x00000059u); // bem r0.xy, source0, source1
            AppendUInt32(shader, destination(regTemp, 0, 0x3u) |
                                     (bemOperandProbe == Probe::DestinationSaturate
                                          ? (1u << 20)
                                          : 0u));
            AppendUInt32(shader, source(
                bemOperandProbe == Probe::Source0Texture
                    ? regTexture
                    : source0Temporary ? regTemp : regConst,
                source0Temporary ? 1u : bemOperandProbe == Probe::Source0Texture ? 0u : 1u,
                replicatedX ? 0x00u : swizzleIdentity,
                temporaryModifiers ? 1u : 0u));
            AppendUInt32(shader, source(
                bemOperandProbe == Probe::Source1Constant
                    ? regConst
                    : bemOperandProbe == Probe::Source1Texture ? regTexture : regTemp,
                bemOperandProbe == Probe::Source1Constant
                    ? 2u
                    : bemOperandProbe == Probe::Source1Texture ? 0u : 2u,
                replicatedX ? 0x00u : swizzleIdentity,
                temporaryModifiers ? 4u : 0u));
            AppendUInt32(shader, 0x00000001u); // mov r0.zw, c1
            AppendUInt32(shader, destination(regTemp, 0, 0xCu));
            AppendUInt32(shader, source(regConst, 1));
        }
        else if (legacyBumpEnvironment != SyntheticLegacyBumpEnvironment::None)
        {
            const bool usesTexbem =
                legacyBumpEnvironment == SyntheticLegacyBumpEnvironment::Texture ||
                legacyBumpEnvironment ==
                    SyntheticLegacyBumpEnvironment::TextureReadSourceLater ||
                legacyBumpEnvironment ==
                    SyntheticLegacyBumpEnvironment::TextureReadSourceByBumpLater;
            AppendUInt32(shader,
                         usesTexbem ? 0x00000043u : 0x00000044u); // texbem/l t1, t0_bx2
            AppendUInt32(shader, destination(regTexture, samplerRegister, 0xFu));
            AppendUInt32(shader, source(regTexture, 0, swizzleIdentity, 4u));
            const bool readsSourceByBump =
                legacyBumpEnvironment ==
                    SyntheticLegacyBumpEnvironment::TextureReadSourceByBumpLater;
            if (readsSourceByBump)
            {
                AppendUInt32(shader, 0x00000044u); // texbeml t2, t0_bx2
                AppendUInt32(shader, destination(regTexture, 2, 0xFu));
                AppendUInt32(shader, source(regTexture, 0, swizzleIdentity, 4u));
            }
            const bool readsConsumedSource =
                legacyBumpEnvironment ==
                    SyntheticLegacyBumpEnvironment::TextureReadSourceLater ||
                legacyBumpEnvironment ==
                    SyntheticLegacyBumpEnvironment::TextureLuminanceReadSourceLater;
            AppendUInt32(shader, 0x00000001u); // mov r0, t1 or invalidly t0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(
                regTexture, readsConsumedSource ? 0u : readsSourceByBump ? 2u : samplerRegister));
        }
        else if (legacyDependentTexture != SyntheticLegacyDependentTexture::None)
        {
            const std::uint32_t destinationRegister = samplerRegister == 0u ? 1u : samplerRegister;
            const std::uint32_t opcode =
                legacyDependentTexture == SyntheticLegacyDependentTexture::RegisterRgb
                    ? 0x00000052u
                    : legacyDependentTexture == SyntheticLegacyDependentTexture::DotSample
                          ? 0x00000053u
                          : 0x00000055u;
            AppendUInt32(shader, opcode); // texreg2rgb/texdp3tex/texdp3 t#, t0
            AppendUInt32(shader, destination(regTexture, destinationRegister, 0xFu));
            // ps_1_2/1_3 permit signed scaling on texture-address sources. Exercising it on the
            // RGB path proves both the sampled coordinate and its implicit derivatives use _bx2.
            AppendUInt32(shader, source(
                regTexture, 0, swizzleIdentity,
                legacyDependentTexture == SyntheticLegacyDependentTexture::RegisterRgb ? 4u : 0u));
            AppendUInt32(shader, 0x00000001u); // mov r0, t#
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, destinationRegister));
        }
        else if (legacyDepthOutput == SyntheticLegacyDepthOutput::TextureMatrix2)
        {
            AppendUInt32(shader, 0x00000040u); // texcrd t0
            AppendUInt32(shader, destination(regTexture, 0, 0xFu));
            AppendUInt32(shader, 0x00000047u); // texm3x2pad t1, t0
            AppendUInt32(shader, destination(regTexture, 1, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000054u); // texm3x2depth t2, t0
            AppendUInt32(shader, destination(regTexture, 2, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000001u); // mov r0, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (legacyDepthOutput == SyntheticLegacyDepthOutput::Register)
        {
            AppendUInt32(shader, 0x00000051u); // def c1, .25, divisor, 0, 0
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(legacyDepthZeroDivisor ? 0.0f : 1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x00000001u); // mov r5.xy, c1
            AppendUInt32(shader, destination(regTemp, 5, 0x3u));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x0000FFFDu); // phase
            AppendUInt32(shader, 0x00000057u); // texdepth r5
            AppendUInt32(shader, destination(regTemp, 5, 0xFu));
            AppendUInt32(shader, 0x00000001u); // mov r0, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (usesLegacyTextureMatrix3Specular)
        {
            AppendUInt32(shader, 0x00000051u); // def c1, 1, .2, .1, 0
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.2f));
            AppendUInt32(shader, FloatBits(0.1f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x00000040u); // texcrd t0
            AppendUInt32(shader, destination(regTexture, 0, 0xFu));
            for (std::uint32_t stage = 1; stage <= 2; ++stage)
            {
                AppendUInt32(shader, 0x00000049u); // texm3x3pad t#, t0
                AppendUInt32(shader, destination(regTexture, stage, 0xFu));
                AppendUInt32(shader, source(regTexture, 0));
            }
            AppendUInt32(shader, 0x0000004Cu); // texm3x3spec t3, t0, c1
            AppendUInt32(shader, destination(regTexture, 3, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000001u); // mov r0, t3
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, 3));
        }
        else if (usesLegacyTextureMatrix3VertexSpecular)
        {
            AppendUInt32(shader, 0x00000040u); // texcrd t0
            AppendUInt32(shader, destination(regTexture, 0, 0xFu));
            for (std::uint32_t stage = 1; stage <= 2; ++stage)
            {
                AppendUInt32(shader, 0x00000049u); // texm3x3pad t#, t0
                AppendUInt32(shader, destination(regTexture, stage, 0xFu));
                AppendUInt32(shader, source(regTexture, 0));
            }
            AppendUInt32(shader, 0x0000004Du); // texm3x3vspec t3, t0
            AppendUInt32(shader, destination(regTexture, 3, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000001u); // mov r0, t3
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, 3));
        }
        else if (usesLegacyTextureMatrix3Sample)
        {
            AppendUInt32(shader, 0x00000040u); // texcrd t0
            AppendUInt32(shader, destination(regTexture, 0, 0xFu));
            for (std::uint32_t stage = 1; stage <= 2; ++stage)
            {
                AppendUInt32(shader, 0x00000049u); // texm3x3pad t#, t0
                AppendUInt32(shader, destination(regTexture, stage, 0xFu));
                AppendUInt32(shader, source(regTexture, 0));
            }
            AppendUInt32(shader, 0x0000004Au); // texm3x3tex t3, t0
            AppendUInt32(shader, destination(regTexture, 3, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000001u); // mov r0, t3
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, 3));
        }
        else if (usesLegacyTextureMatrix2)
        {
            AppendUInt32(shader, 0x00000040u); // texcrd t0
            AppendUInt32(shader, destination(regTexture, 0, 0xFu));
            AppendUInt32(shader, 0x00000047u); // texm3x2pad t1, t0
            AppendUInt32(shader, destination(regTexture, 1, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000048u); // texm3x2tex t2, t0
            AppendUInt32(shader, destination(regTexture, 2, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000001u); // mov r0, t2
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, 2));
        }
        else if (legacyTextureRemap != SyntheticLegacyTextureRemap::None)
        {
            // The destination number selects sampler stage 1. Source t0 contributes either AR
            // or GB as the two-dimensional lookup coordinate.
            AppendUInt32(shader,
                         legacyTextureRemap == SyntheticLegacyTextureRemap::AlphaRed
                             ? 0x00000045u
                             : 0x00000046u);
            AppendUInt32(shader, destination(regTexture, samplerRegister, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000001u); // mov r0, t<samplerRegister>
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, samplerRegister));
        }
        else if (usesLegacyTextureMatrix)
        {
            // t0 is the vector and t1/t2/t3 are the three matrix rows. The paired pad
            // instructions retain the first two dot products until TEXM3X3 writes all three.
            AppendUInt32(shader, 0x00000040u); // texcrd t0
            AppendUInt32(shader, destination(regTexture, 0, 0xFu));
            AppendUInt32(shader, 0x00000049u); // texm3x3pad t1, t0
            AppendUInt32(shader, destination(regTexture, 1, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000049u); // texm3x3pad t2, t0
            AppendUInt32(shader, destination(regTexture, 2, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000056u); // texm3x3 t3, t0
            AppendUInt32(shader, destination(regTexture, 3, 0xFu));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x00000001u); // mov r0, t3
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, 3));
        }
        else if (usesShaderModel14Phase || duplicatesShaderModel14Phase)
        {
            // RGB temporary components persist across the ps_1_4 phase transition. Alpha does not,
            // so the valid phase-2 program initializes the output's z/w channels independently.
            AppendUInt32(shader, 0x00000040u); // texcrd r1.xyz, t0
            AppendUInt32(shader, destination(regTemp, 1, 0x7u));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x0000FFFDu); // phase
            if (duplicatesShaderModel14Phase)
                AppendUInt32(shader, 0x0000FFFDu); // invalid duplicate phase
            AppendUInt32(shader, 0x00000001u); // mov r0.xy, r1
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regTemp, 1));
            AppendUInt32(shader, 0x00000001u); // mov r0.zw, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xCu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (usesShaderModel14TexcrdDw)
        {
            AppendUInt32(shader, 0x00000040u); // texcrd r0.xy, t0_dw.xyw
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regTexture, 0, 0xF4u, 10u));
            AppendUInt32(shader, 0x00000001u); // mov r0.zw, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xCu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (shaderModel14TextureOperandProbe !=
                 SyntheticShaderModel14TextureOperandProbe::None)
        {
            using Probe = SyntheticShaderModel14TextureOperandProbe;
            const bool temporarySource =
                shaderModel14TextureOperandProbe == Probe::TexldTemporaryDw ||
                shaderModel14TextureOperandProbe == Probe::TexldTemporaryDzIdentity;
            if (temporarySource)
            {
                AppendUInt32(shader, 0x00000040u); // texcrd r1.xyz, t0
                AppendUInt32(shader, destination(regTemp, 1, 0x7u));
                AppendUInt32(shader, source(regTexture, 0));
                AppendUInt32(shader, 0x0000FFFDu); // phase
            }

            if (shaderModel14TextureOperandProbe == Probe::TexcrdDz ||
                shaderModel14TextureOperandProbe == Probe::TexcrdDwIdentity ||
                shaderModel14TextureOperandProbe == Probe::TexcrdDwWrongDestinationMask)
            {
                const bool dz = shaderModel14TextureOperandProbe == Probe::TexcrdDz;
                const bool identitySelector =
                    shaderModel14TextureOperandProbe == Probe::TexcrdDwIdentity;
                AppendUInt32(shader, 0x00000040u); // texcrd r0.mask, invalid projective t0
                AppendUInt32(shader,
                             destination(regTemp, 0, identitySelector ? 0x3u : 0x7u));
                AppendUInt32(
                    shader,
                    source(regTexture, 0, identitySelector ? swizzleIdentity : dz ? 0xA4u : 0xF4u,
                           dz ? 9u : 10u));
                AppendUInt32(shader, 0x00000001u); // mov r0.w, c0.w
                AppendUInt32(shader, destination(regTemp, 0, 0x8u));
                AppendUInt32(shader, source(regConst, 0, 0xFFu));
            }
            else
            {
                const bool dz =
                    shaderModel14TextureOperandProbe == Probe::TexldTextureDz ||
                    shaderModel14TextureOperandProbe == Probe::TexldTemporaryDzIdentity;
                const bool identitySelector =
                    shaderModel14TextureOperandProbe == Probe::TexldTextureDwIdentity ||
                    shaderModel14TextureOperandProbe == Probe::TexldTemporaryDzIdentity;
                AppendUInt32(shader, 0x00000042u); // texld r0, invalid projective source
                AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                AppendUInt32(shader,
                             source(temporarySource ? regTemp : regTexture,
                                    temporarySource ? 1u : 0u,
                                    identitySelector ? swizzleIdentity : dz ? 0xA4u : 0xF4u,
                                    dz ? 9u : 10u));
            }
        }
        else if (usesShaderModel14TextureLoad)
        {
            // ps_1_4 selects the texture stage from the destination register number while the
            // source independently selects the coordinate set. Microsoft requires the canonical
            // _dw selector (.xyw), so sample stage 1 at TEXCOORD0 with that selector.
            AppendUInt32(shader, 0x00000042u); // texld r<samplerRegister>, t0_dw.xyw
            AppendUInt32(shader, destination(regTemp, samplerRegister, 0xFu));
            AppendUInt32(shader, source(regTexture, 0, 0xF4u, 10u));
            AppendUInt32(shader, 0x00000001u); // mov r0, sampled value (ps_1_x output is r0)
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, samplerRegister));
        }
        else if (usesShaderModel14TexldDw)
        {
            AppendUInt32(shader, 0x00000042u); // texld r0, t0_dw.xyw
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTexture, 0, 0xF4u, 10u));
        }
        else if (usesShaderModel14TexldDz)
        {
            AppendUInt32(shader, 0x00000040u); // texcrd r1.xyz, t0
            AppendUInt32(shader, destination(regTemp, 1, 0x7u));
            AppendUInt32(shader, source(regTexture, 0));
            AppendUInt32(shader, 0x0000FFFDu); // phase
            AppendUInt32(shader, 0x00000042u); // texld r0, r1_dz.xyz
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 1, 0xA4u, 9u));
        }
        else if (usesPredicatedTexkill)
        {
            // Tint.x controls whether p0.x enables the kill. The killed operand is negative only
            // in x, so masking that one predicate component is the complete observable contract.
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, .5, .5, .5, .5
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            for (int i = 0; i < 4; ++i) AppendUInt32(shader, FloatBits(0.5f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, -1, 1, 1, 1
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            AppendUInt32(shader, FloatBits(-1.0f));
            for (int i = 0; i < 3; ++i) AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c3, 0, 1, 0, 1
            AppendUInt32(shader, destination(regConst, 3, 0xFu));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x0000005Eu | (1u << 16) | (3u << 24)); // setp_gt p0, c0, c1
            AppendUInt32(shader, destination(regPredicate, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c2
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 2));
            AppendUInt32(shader, 0x10000041u | (2u << 24)); // (p0.x) texkill r0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regPredicate, 0, 0x00u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, c3
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 3));
        }
        else if (usesPredication)
        {
            // Vertex COLOR0 arrives as (.75, .625, .5, 1). Add .125 only to the x/z components
            // selected by p0, then use (!p0.y) to replace alpha with .125.
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, .125, .125, .125, .125
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            for (int i = 0; i < 4; ++i) AppendUInt32(shader, FloatBits(0.125f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, 1, 0, 1, 0
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c3, .5, .5, .5, .5
            AppendUInt32(shader, destination(regConst, 3, 0xFu));
            for (int i = 0; i < 4; ++i) AppendUInt32(shader, FloatBits(0.5f));
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_color0 v0
            AppendUInt32(shader, 0x80000000u | 10u);
            AppendUInt32(shader, destination(regInput, 0, 0xFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, v0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, 0x0000005Eu | (1u << 16) | (3u << 24)); // setp_gt p0, c2, c3
            AppendUInt32(shader, destination(regPredicate, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 2));
            AppendUInt32(shader, source(regConst, 3));
            AppendUInt32(shader, 0x10000002u | (4u << 24)); // (p0) add r0, r0, c1
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, source(regPredicate, 0));
            AppendUInt32(shader, 0x10000001u | (3u << 24)); // (!p0.y) mov r0.w, c1.x
            AppendUInt32(shader, destination(regTemp, 0, 0x8u));
            AppendUInt32(shader, source(regConst, 1, 0x00u));
            AppendUInt32(shader, source(regPredicate, 0, 0x55u, 13u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (probesPixelMiscellaneousInput)
        {
            const bool face =
                miscellaneousInputProbe ==
                    SyntheticMiscellaneousInputProbe::FaceOrdinaryMove ||
                miscellaneousInputProbe ==
                    SyntheticMiscellaneousInputProbe::FaceConditionalCompare;
            const bool conditionalFace = miscellaneousInputProbe ==
                SyntheticMiscellaneousInputProbe::FaceConditionalCompare;
            std::uint32_t mask = 0xFu;
            if (miscellaneousInputProbe == SyntheticMiscellaneousInputProbe::PositionMaskZ)
                mask = 0x4u;
            else if (miscellaneousInputProbe ==
                     SyntheticMiscellaneousInputProbe::PositionMaskXYZ)
                mask = 0x7u;
            else if (miscellaneousInputProbe ==
                     SyntheticMiscellaneousInputProbe::PositionMaskX)
                mask = 0x1u;
            else if (miscellaneousInputProbe ==
                     SyntheticMiscellaneousInputProbe::PositionMaskY)
                mask = 0x2u;
            else if (miscellaneousInputProbe ==
                     SyntheticMiscellaneousInputProbe::PositionMaskXY)
                mask = 0x3u;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl vPos/vFace
            AppendUInt32(shader, 0x80000000u);
            AppendUInt32(shader, destination(regMiscellaneous, face ? 1u : 0u, mask));
            if (conditionalFace)
            {
                AppendUInt32(shader, 0x00000058u | (4u << 24)); // cmp oC0, vFace, c0, c0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regMiscellaneous, 1u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
            }
            else
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, vPos/vFace
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regMiscellaneous, face ? 1u : 0u));
            }
        }
        else if (probesPixel20Centroid)
        {
            const bool centroidExplicit = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidExplicit;
            const bool centroidColor = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidImplicitColor;
            const std::uint32_t inputRegister = centroidColor ? regInput : regTexture;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u);
            AppendUInt32(shader, destination(inputRegister, 0, 0xFu) |
                                     (centroidExplicit ? 0x00400000u : 0u));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, green
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, red
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x00000058u | (4u << 24));
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(inputRegister, 0, 0x00u));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, source(regConst, 2));
        }
        else if (probesPixelSemanticDeclarations)
        {
            const bool centroidControl = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidControl;
            const bool centroidExplicit = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidExplicit;
            const bool centroidColor = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidImplicitColor;
            const bool duplicate = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelDuplicateSemantic;
            const bool overlap = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelOverlappingMasks;
            if (centroidControl || centroidExplicit || centroidColor)
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u | (centroidColor ? 10u : 5u));
                AppendUInt32(shader, destination(regInput, 0, 0xFu) |
                                         (centroidExplicit ? 0x00400000u : 0u));
                AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, green
                AppendUInt32(shader, destination(regConst, 1, 0xFu));
                AppendUInt32(shader, FloatBits(0.0f));
                AppendUInt32(shader, FloatBits(1.0f));
                AppendUInt32(shader, FloatBits(0.0f));
                AppendUInt32(shader, FloatBits(1.0f));
                AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, red
                AppendUInt32(shader, destination(regConst, 2, 0xFu));
                AppendUInt32(shader, FloatBits(1.0f));
                AppendUInt32(shader, FloatBits(0.0f));
                AppendUInt32(shader, FloatBits(0.0f));
                AppendUInt32(shader, FloatBits(1.0f));
                AppendUInt32(shader, 0x00000058u | (4u << 24)); // cmp oC0, v0.x, c1, c2
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regInput, 0, 0x00u));
                AppendUInt32(shader, source(regConst, 1));
                AppendUInt32(shader, source(regConst, 2));
            }
            else if (semanticDeclarationProbe ==
                         SyntheticSemanticDeclarationProbe::PackedDisjoint ||
                semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs)
            {
                // Reverse the vertex declaration order deliberately. D3D linkage is semantic- and
                // mask-driven, so declaration order cannot decide which half survives.
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_color0 v0.zw
                AppendUInt32(shader, 0x80000000u | 10u);
                AppendUInt32(shader, destination(regInput, 0, 0xCu));
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord0 v0.xy
                AppendUInt32(shader, 0x80000000u | 5u);
                AppendUInt32(shader, destination(regInput, 0, 0x3u));
            }
            else
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord0 v0.xy
                AppendUInt32(shader, 0x80000000u | 5u);
                AppendUInt32(shader, destination(regInput, 0, 0x3u));
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u | (duplicate ? 5u : 10u));
                AppendUInt32(shader,
                             destination(regInput, duplicate ? 1u : 0u,
                                         overlap ? 0x6u : 0xCu));
            }
            if (!centroidControl && !centroidExplicit && !centroidColor)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, v0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regInput, 0));
            }
        }
        else if (usesRasterInputs)
        {
            // D3D9 vPos has integer-centred top-down pixel coordinates. Scale a 4x4 target into
            // normalized red/green while vFace selects blue only for clockwise/front triangles.
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, .25, .25, 0, 1
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, 0, 0, 1, 0
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c3, 0, 0, 0, 0
            AppendUInt32(shader, destination(regConst, 3, 0xFu));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl vPos.xy
            AppendUInt32(shader, 0x80000000u);
            AppendUInt32(shader, destination(regMiscellaneous, 0, 0x3u));
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl vFace
            AppendUInt32(shader, 0x80000000u);
            AppendUInt32(shader, destination(regMiscellaneous, 1, 0xFu));
            AppendUInt32(shader, 0x00000005u | (3u << 24)); // mul r0.xy, vPos, c1
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regMiscellaneous, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000029u | (1u << 16) | (2u << 24)); // if_gt vFace, c1.z
            AppendUInt32(shader, source(regMiscellaneous, 1, 0x00u));
            AppendUInt32(shader, source(regConst, 1, 0xAAu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.z, c2.z
            AppendUInt32(shader, destination(regTemp, 0, 0x4u));
            AppendUInt32(shader, source(regConst, 2, 0xAAu));
            AppendUInt32(shader, 0x0000002Au); // else
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.z, c3.z
            AppendUInt32(shader, destination(regTemp, 0, 0x4u));
            AppendUInt32(shader, source(regConst, 3, 0xAAu));
            AppendUInt32(shader, 0x0000002Bu); // endif
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.w, c1.w
            AppendUInt32(shader, destination(regTemp, 0, 0x8u));
            AppendUInt32(shader, source(regConst, 1, 0xFFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (usesDerivatives)
        {
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, 0, 0, 0, 1
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            // dcl_texcoord0 v0.xy
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | 5u);
            AppendUInt32(shader, destination(regInput, 0, 0x3u));
            // r0 = v0², so each aligned 2x2 quad has a different finite change and a scalar
            // interpolation-gradient shortcut cannot accidentally satisfy the contract.
            AppendUInt32(shader, 0x00000005u | (3u << 24));
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, 0x0000005Bu | (2u << 24)); // dsx r1, r0
            AppendUInt32(shader, destination(regTemp, 1, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, 0x0000005Cu | (2u << 24)); // dsy r2, r0
            AppendUInt32(shader, destination(regTemp, 2, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
            // The y sign depends on render-target orientation, but D3D/GL both define the rate
            // per native target direction. Magnitude is the portable observable here.
            AppendUInt32(shader, 0x00000023u | (2u << 24)); // abs r1, r1
            AppendUInt32(shader, destination(regTemp, 1, 0xFu));
            AppendUInt32(shader, source(regTemp, 1));
            AppendUInt32(shader, 0x00000023u | (2u << 24)); // abs r2, r2
            AppendUInt32(shader, destination(regTemp, 2, 0xFu));
            AppendUInt32(shader, source(regTemp, 2));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, c1
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0.x, r1.x
            AppendUInt32(shader, destination(regColorOut, 0, 0x1u));
            AppendUInt32(shader, source(regTemp, 1, 0x00u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0.y, r2.y
            AppendUInt32(shader, destination(regColorOut, 0, 0x2u));
            AppendUInt32(shader, source(regTemp, 2, 0x55u));
        }
        else if (probesPixelRelativeAddressing)
        {
            const bool outsideLoop = relativeAddressingProbe ==
                                     SyntheticRelativeAddressingProbe::PixelInputOutsideLoop;
            const bool addressRegister = relativeAddressingProbe ==
                SyntheticRelativeAddressingProbe::PixelInputAddressInsideLoop;
            const bool constantRegister = relativeAddressingProbe ==
                SyntheticRelativeAddressingProbe::PixelConstantInsideLoop;
            const bool subroutine = relativeAddressingProbe ==
                SyntheticRelativeAddressingProbe::PixelInputSubroutineInsideLoop;

            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord0 v0
            AppendUInt32(shader, 0x80000000u | 5u);
            AppendUInt32(shader, destination(regInput, 0, 0xFu));
            if (!outsideLoop)
            {
                AppendUInt32(shader, 0x00000030u | (5u << 24)); // defi i0, 1, 0, 1, 0
                AppendUInt32(shader, destination(regConstInt, 0, 0xFu));
                AppendUInt32(shader, 1u);
                AppendUInt32(shader, 0u);
                AppendUInt32(shader, 1u);
                AppendUInt32(shader, 0u);
                if (subroutine)
                {
                    AppendUInt32(shader, 0x00000001u | (2u << 24)); // initialize r0
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0));
                }
                AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL, i0
                AppendUInt32(shader, source(regLoop, 0));
                AppendUInt32(shader, source(regConstInt, 0));
            }
            if (subroutine)
            {
                AppendUInt32(shader, 0x00000019u | (1u << 24)); // call l0
                AppendUInt32(shader, source(regLabel, 0));
                AppendUInt32(shader, 0x0000001Du); // endloop
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regTemp, 0));
                AppendUInt32(shader, 0x0000001Cu); // ret from main
                AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l0
                AppendUInt32(shader, source(regLabel, 0));
            }
            AppendUInt32(shader, 0x00000001u | (3u << 24)); // mov r0, v0/c0[aL/a0.x]
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader,
                         source(constantRegister ? regConst : regInput, 0) | (1u << 13u));
            AppendUInt32(shader, source(addressRegister ? regTexture : regLoop, 0,
                                        addressRegister ? 0x00u : 0xE4u));
            if (subroutine)
            {
                AppendUInt32(shader, 0x0000001Cu); // ret from subroutine
            }
            else
            {
                if (!outsideLoop) AppendUInt32(shader, 0x0000001Du); // endloop
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regTemp, 0));
            }
        }
        else if (probesPixelCallGraph)
        {
            const bool backward =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30BackwardCall ||
                callGraphProbe == SyntheticCallGraphProbe::Pixel30BackwardCallNz;
            const bool conditionalBackward =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30BackwardCallNz;
            const bool undefinedLabel =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30UndefinedLabel;
            const bool duplicateLabel =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30DuplicateLabel;
            const bool missingReturn =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30MissingReturn;
            const bool probesLabelRange =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30Label16 ||
                callGraphProbe == SyntheticCallGraphProbe::Pixel30Label2047;
            const std::uint32_t boundaryLabel =
                callGraphProbe == SyntheticCallGraphProbe::Pixel30Label2047 ? 2047u : 16u;
            const int depth =
                callGraphProbe == SyntheticCallGraphProbe::Pixel2xDepth5 ||
                        callGraphProbe == SyntheticCallGraphProbe::Pixel30Depth5
                    ? 5
                    : 4;
            if (conditionalBackward)
            {
                AppendUInt32(shader, 0x0000002Fu | (2u << 24)); // defb b0, true
                AppendUInt32(shader, destination(regConstBool, 0, 0x1u));
                AppendUInt32(shader, 1u);
            }
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, c0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000019u | (1u << 24)); // call label
            AppendUInt32(shader,
                         source(regLabel,
                                probesLabelRange ? boundaryLabel : backward ? 1u : 0u));
            AppendUInt32(shader, 0x0000001Cu); // ret from main

            int labelCount = depth;
            if (probesLabelRange || missingReturn)
                labelCount = 1;
            else if (undefinedLabel)
                labelCount = 0;
            else if (backward || duplicateLabel)
                labelCount = 2;
            for (int label = 0; label < labelCount; ++label)
            {
                AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l#
                AppendUInt32(shader,
                             source(regLabel,
                                    probesLabelRange
                                        ? boundaryLabel
                                    : duplicateLabel
                                        ? 0u
                                        : static_cast<std::uint32_t>(label)));
                if (backward && label == 1)
                {
                    AppendUInt32(shader,
                                 (conditionalBackward ? 0x0000001Au | (2u << 24)
                                                      : 0x00000019u | (1u << 24)));
                    AppendUInt32(shader, source(regLabel, 0));
                    if (conditionalBackward)
                        AppendUInt32(shader, source(regConstBool, 0, 0x00u));
                }
                else if (!backward && label + 1 < labelCount)
                {
                    if (!duplicateLabel)
                    {
                        AppendUInt32(shader, 0x00000019u | (1u << 24)); // call next label
                        AppendUInt32(shader,
                                     source(regLabel, static_cast<std::uint32_t>(label + 1)));
                    }
                }
                if (missingReturn)
                {
                    AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0));
                }
                else
                    AppendUInt32(shader, 0x0000001Cu); // ret from subroutine
            }
        }
        else if (usesSubroutine)
        {
            // Local constants make the called writes independent from reflected parameter data.
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, .25, .25, .25, 0
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x0000002Fu | (2u << 24)); // defb b0, true
            AppendUInt32(shader, destination(regConstBool, 0, 0xFu));
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000019u | (1u << 24)); // call l0
            AppendUInt32(shader, source(regLabel, 0));
            AppendUInt32(shader, 0x0000001Au | (2u << 24)); // callnz l2, b0
            AppendUInt32(shader, source(regLabel, 2));
            AppendUInt32(shader, source(regConstBool, 0, 0x00u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, 0x0000001Cu); // ret from main

            AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l0
            AppendUInt32(shader, source(regLabel, 0));
            AppendUInt32(shader, 0x00000002u | (3u << 24)); // add r0.x, r0.x, c1.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000019u | (1u << 24)); // nested call l1
            AppendUInt32(shader, source(regLabel, 1));
            AppendUInt32(shader, 0x0000001Cu);

            AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l1
            AppendUInt32(shader, source(regLabel, 1));
            AppendUInt32(shader, 0x00000002u | (3u << 24)); // add r0.y, r0.y, c1.y
            AppendUInt32(shader, destination(regTemp, 0, 0x2u));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x0000001Cu);

            AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l2
            AppendUInt32(shader, source(regLabel, 2));
            AppendUInt32(shader, 0x00000002u | (3u << 24)); // add r0.z, r0.z, c1.z
            AppendUInt32(shader, destination(regTemp, 0, 0x4u));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x0000001Cu);
        }
        else if (usesDependentTemporaryTextureCoordinate)
        {
            const std::uint32_t samplerTextureType =
                samplerKind == SyntheticSamplerKind::SamplerCube
                    ? EffectFormat::SamplerTypeCube
                    : samplerKind == SyntheticSamplerKind::Sampler3D
                          ? EffectFormat::SamplerTypeVolume
                          : EffectFormat::SamplerType2D;
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, 8, 8, 1, 1
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(8.0f));
            AppendUInt32(shader, FloatBits(8.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            const bool threeComponent = samplerKind != SyntheticSamplerKind::Sampler2D;
            const std::uint32_t coordinateMask = threeComponent ? 0x7u : 0x3u;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord0 v0.xy(z)
            AppendUInt32(shader, 0x80000000u | 5u);
            AppendUInt32(shader, destination(regInput, 0, coordinateMask));
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_2d s#
            AppendUInt32(shader, 0x80000000u | (samplerTextureType << 27));
            AppendUInt32(shader, destination(regSampler, samplerRegister, 0xFu));
            AppendUInt32(shader, 0x00000005u | (3u << 24)); // mul r0, v0, c1
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000042u | (3u << 24)); // texld r1, r0, s#
            AppendUInt32(shader, destination(regTemp, 1, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regSampler, samplerRegister));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r1
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 1));
        }
        else if (usesRelativeTextureCoordinate)
        {
            const std::uint32_t samplerTextureType =
                samplerKind == SyntheticSamplerKind::SamplerCube
                    ? EffectFormat::SamplerTypeCube
                    : samplerKind == SyntheticSamplerKind::Sampler3D
                          ? EffectFormat::SamplerTypeVolume
                          : EffectFormat::SamplerType2D;
            // A one-iteration loop supplies aL=0. Microsoft's D3D9 assembler accepts relative
            // pixel INPUTS only in loop scope; pixel float constants are not indexable.
            AppendUInt32(shader, 0x00000030u | (5u << 24)); // defi i0, 1, 0, 1, 0
            AppendUInt32(shader, destination(regConstInt, 0, 0xFu));
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0u);
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0u);
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord0 v0
            AppendUInt32(shader, 0x80000000u | 5u);
            AppendUInt32(shader, destination(regInput, 0, 0x3u));
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_2d s#
            AppendUInt32(shader, 0x80000000u | (samplerTextureType << 27));
            AppendUInt32(shader, destination(regSampler, samplerRegister, 0xFu));
            AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL, i0
            AppendUInt32(shader, source(regLoop, 0));
            AppendUInt32(shader, source(regConstInt, 0));
            AppendUInt32(shader, 0x00000042u | (4u << 24)); // texld r0, v0[aL], s#
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regInput, 0) | (1u << 13u));
            AppendUInt32(shader, source(regLoop, 0));
            AppendUInt32(shader, source(regSampler, samplerRegister));
            AppendUInt32(shader, 0x0000001Du); // endloop
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (probesPixelFlowControl)
        {
            const auto appendIf = [&]()
            {
                AppendUInt32(shader, 0x00000028u | (1u << 24)); // if b0
                AppendUInt32(shader, source(regConstBool, 0, 0x00u));
            };
            const auto appendIfc = [&]()
            {
                AppendUInt32(shader,
                             0x00000029u | (1u << 16) | (2u << 24)); // if_gt c0.x, c0.y
                AppendUInt32(shader, source(regConst, 0, 0x00u));
                AppendUInt32(shader, source(regConst, 0, 0x55u));
            };
            const auto appendLoop = [&]()
            {
                AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL, i0
                AppendUInt32(shader, source(regLoop, 0, 0x00u));
                AppendUInt32(shader, source(regConstInt, 0));
            };
            const auto appendRep = [&]()
            {
                AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i0
                AppendUInt32(shader, source(regConstInt, 0, 0x00u));
            };

            AppendUInt32(shader, 0x0000002Fu | (2u << 24)); // defb b0, true
            AppendUInt32(shader, destination(regConstBool, 0, 0x1u));
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0x00000030u | (5u << 24)); // defi i0, 1, 0, 1, 0
            AppendUInt32(shader, destination(regConstInt, 0, 0xFu));
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0u);
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0u);

            switch (flowControlProbe)
            {
                case SyntheticFlowControlProbe::ProperlyNestedLoopIf:
                    appendLoop();
                    appendIf();
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    AppendUInt32(shader, 0x0000001Du); // endloop
                    break;
                case SyntheticFlowControlProbe::ElseWithoutIf:
                    AppendUInt32(shader, 0x0000002Au); // else
                    break;
                case SyntheticFlowControlProbe::EndIfWithoutIf:
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticFlowControlProbe::IfWithoutEndIf:
                    appendIf();
                    break;
                case SyntheticFlowControlProbe::DuplicateElse:
                    appendIf();
                    AppendUInt32(shader, 0x0000002Au); // else
                    AppendUInt32(shader, 0x0000002Au); // else
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticFlowControlProbe::LoopEndsBeforeIf:
                    appendLoop();
                    appendIf();
                    AppendUInt32(shader, 0x0000001Du); // endloop
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticFlowControlProbe::IfEndsBeforeLoop:
                    appendIf();
                    appendLoop();
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    AppendUInt32(shader, 0x0000001Du); // endloop
                    break;
                case SyntheticFlowControlProbe::RepEndsBeforeIf:
                    appendRep();
                    appendIf();
                    AppendUInt32(shader, 0x00000027u); // endrep
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticFlowControlProbe::IfEndsBeforeRep:
                    appendIf();
                    appendRep();
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    AppendUInt32(shader, 0x00000027u); // endrep
                    break;
                case SyntheticFlowControlProbe::StaticIfDepth24:
                case SyntheticFlowControlProbe::StaticIfDepth25:
                {
                    const int depth = flowControlProbe == SyntheticFlowControlProbe::StaticIfDepth24
                                          ? 24
                                          : 25;
                    for (int i = 0; i < depth; ++i) appendIf();
                    for (int i = 0; i < depth; ++i) AppendUInt32(shader, 0x0000002Bu);
                    break;
                }
                case SyntheticFlowControlProbe::DynamicIfDepth24:
                case SyntheticFlowControlProbe::DynamicIfDepth25:
                {
                    const int depth =
                        flowControlProbe == SyntheticFlowControlProbe::DynamicIfDepth24 ? 24 : 25;
                    for (int i = 0; i < depth; ++i) appendIfc();
                    for (int i = 0; i < depth; ++i) AppendUInt32(shader, 0x0000002Bu);
                    break;
                }
                case SyntheticFlowControlProbe::LoopRepDepth4:
                case SyntheticFlowControlProbe::LoopRepDepth5:
                {
                    const int depth = flowControlProbe == SyntheticFlowControlProbe::LoopRepDepth4
                                          ? 4
                                          : 5;
                    for (int i = 0; i < depth; ++i)
                    {
                        if ((i & 1) == 0) appendLoop();
                        else appendRep();
                    }
                    for (int i = depth - 1; i >= 0; --i)
                        AppendUInt32(shader, (i & 1) == 0 ? 0x0000001Du : 0x00000027u);
                    break;
                }
                case SyntheticFlowControlProbe::None:
                case SyntheticFlowControlProbe::Vertex20LoopRepDepth1:
                case SyntheticFlowControlProbe::Vertex20LoopRepDepth2: break;
            }
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, c0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (usesLoop || shaderModel2xUsesInvalidLoop)
        {
            // def c1, 0.25, 0, 0, 0
            AppendUInt32(shader, 0x00000051u | (5u << 24));
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(0.0f));
            // defi i0, count, initial, step, 0
            AppendUInt32(shader, 0x00000030u | (5u << 24));
            AppendUInt32(shader, destination(regConstInt, 0, 0xFu));
            AppendUInt32(shader, static_cast<std::uint32_t>(loopCount));
            AppendUInt32(shader, static_cast<std::uint32_t>(loopInitial));
            AppendUInt32(shader, static_cast<std::uint32_t>(loopStep));
            AppendUInt32(shader, 0u);
            // r0 starts at Tint. A test supplies opaque black, so each iteration adds exactly
            // one quarter to red while retaining alpha one.
            AppendUInt32(shader, 0x00000001u | (2u << 24));
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
            // loop aL, i0
            AppendUInt32(shader, 0x0000001Bu | (2u << 24));
            AppendUInt32(shader, source(regLoop, 0));
            AppendUInt32(shader, source(regConstInt, 0));
            // add r0.x, r0.x, c1.x
            AppendUInt32(shader, 0x00000002u | (3u << 24));
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regTemp, 0, 0x00u));
            AppendUInt32(shader, source(regConst, 1, 0x00u));
            AppendUInt32(shader, 0x0000001Du); // endloop
            AppendUInt32(shader, 0x00000001u | (2u << 24));
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (usesNrmWriteMask)
        {
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, .5, .5, 0, 1
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            AppendUInt32(shader, FloatBits(0.5f));
            AppendUInt32(shader, FloatBits(0.5f));
            AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, 0, 0, 0, 1
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            for (int i = 0; i < 3; ++i) AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c3, 2, 0, 0, 0
            AppendUInt32(shader, destination(regConst, 3, 0xFu));
            AppendUInt32(shader, FloatBits(2.0f));
            for (int i = 0; i < 3; ++i) AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x00000024u | (2u << 24)); // nrm r0.xy, c0
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000002u | (3u << 24)); // add r0.xy, -r0, c1
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regTemp, 0, swizzleIdentity, 1u));
            AppendUInt32(shader, source(regConst, 1));
            AppendUInt32(shader, 0x00000058u | (4u << 24)); // cmp r0.xy, r0, c1.w, c1.z
            AppendUInt32(shader, destination(regTemp, 0, 0x3u));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regConst, 1, 0xFFu));
            AppendUInt32(shader, source(regConst, 1, 0xAAu));
            AppendUInt32(shader, 0x00000024u | (2u << 24)); // nrm r0.w, c2
            AppendUInt32(shader, destination(regTemp, 0, 0x8u));
            AppendUInt32(shader, source(regConst, 2));
            AppendUInt32(shader, 0x00000002u | (3u << 24)); // add r0.z, r0.w, -c3.x
            AppendUInt32(shader, destination(regTemp, 0, 0x4u));
            AppendUInt32(shader, source(regTemp, 0, 0xFFu));
            AppendUInt32(shader, source(regConst, 3, 0x00u, 1u));
            AppendUInt32(shader, 0x00000058u | (4u << 24)); // cmp r0.z, r0.z, c1.w, c1.z
            AppendUInt32(shader, destination(regTemp, 0, 0x4u));
            AppendUInt32(shader, source(regTemp, 0, 0xAAu));
            AppendUInt32(shader, source(regConst, 1, 0xFFu));
            AppendUInt32(shader, source(regConst, 1, 0xAAu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.w, c1.w
            AppendUInt32(shader, destination(regTemp, 0, 0x8u));
            AppendUInt32(shader, source(regConst, 1, 0xFFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (usesSignedLog)
        {
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c1, 0, 0, 0, 0
            AppendUInt32(shader, destination(regConst, 1, 0xFu));
            for (int i = 0; i < 4; ++i) AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c2, 0, 0, 0, 1
            AppendUInt32(shader, destination(regConst, 2, 0xFu));
            for (int i = 0; i < 3; ++i) AppendUInt32(shader, FloatBits(0.0f));
            AppendUInt32(shader, FloatBits(1.0f));
            AppendUInt32(shader, 0x0000000Fu | (2u << 24)); // log r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, 0x0000000Fu | (2u << 24)); // log r0.y, c0.y
            AppendUInt32(shader, destination(regTemp, 0, 0x2u));
            AppendUInt32(shader, source(regConst, 0, 0x55u));
            AppendUInt32(shader, 0x00000005u | (3u << 24)); // mul r0.y, r0.y, c1.y
            AppendUInt32(shader, destination(regTemp, 0, 0x2u));
            AppendUInt32(shader, source(regTemp, 0, 0x55u));
            AppendUInt32(shader, source(regConst, 1, 0x55u));
            AppendUInt32(shader, 0x00000058u | (4u << 24)); // cmp r0.y, r0.y, c2.w, c1.y
            AppendUInt32(shader, destination(regTemp, 0, 0x2u));
            AppendUInt32(shader, source(regTemp, 0, 0x55u));
            AppendUInt32(shader, source(regConst, 2, 0xFFu));
            AppendUInt32(shader, source(regConst, 1, 0x55u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0.zw, c2.zw
            AppendUInt32(shader, destination(regTemp, 0, 0xCu));
            AppendUInt32(shader, source(regConst, 2));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (probesMissingPixelSamplerDeclaration)
        {
            const bool shaderModel3 = missingSamplerDeclarationProbe ==
                                      SyntheticMissingSamplerDeclarationProbe::Pixel30;
            const std::uint32_t coordinateRegister = shaderModel3 ? regInput : regTexture;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl texture coordinate
            AppendUInt32(shader, 0x80000000u | (shaderModel3 ? 5u : 0u));
            AppendUInt32(shader, destination(coordinateRegister, 0, 0x3u));
            AppendUInt32(shader, 0x00000042u | (3u << 24)); // texld r0, input, s0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(coordinateRegister, 0));
            AppendUInt32(shader, source(regSampler, 0));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, r0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (probesVertexFloatRedefinition)
        {
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_color0 v0
            AppendUInt32(shader, 0x80000000u | 10u);
            AppendUInt32(shader, destination(regInput, 0, 0xFu));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, v0
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regInput, 0));
        }
        else if (samplesTexture || usesTextureGradients || probesPixelTextureSourceModifier ||
                 probesTexld20Operand || probesPixelTexldDestinationModifier ||
                 probesPixelTextureInstructionProfile ||
                 probesTexlddOperand || probesPixelTexldlDestinationModifier)
        {
            // plans/plan_fx.md FX-110: a cube or volume sampler reads three components, a 2D one reads
            // two, and the declaration has to say which -- both in the coordinate register's write
            // mask and in the sampler's own texture-type field.
            const bool threeComponent = samplerKind != SyntheticSamplerKind::Sampler2D;
            const bool textureUsesGradients =
                usesTextureGradients || probesPixelTexlddSourceModifier || probesTexlddOperand ||
                textureInstructionProfileProbe ==
                    SyntheticTextureInstructionProfileProbe::Pixel20Texldd ||
                textureInstructionProfileProbe ==
                    SyntheticTextureInstructionProfileProbe::Pixel2xTexldd;
            const bool textureUsesExplicitLod =
                textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexldlCoordinate ||
                textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexldlSampler ||
                textureInstructionProfileProbe ==
                    SyntheticTextureInstructionProfileProbe::Pixel20Texldl ||
                textureInstructionProfileProbe ==
                    SyntheticTextureInstructionProfileProbe::Pixel2xTexldl ||
                probesPixelTexldlDestinationModifier;
            const bool probesProjectedTexld =
                texldDestinationModifierProbe ==
                    SyntheticTexldDestinationModifierProbe::Pixel30TexldpSaturate ||
                texldDestinationModifierProbe ==
                    SyntheticTexldDestinationModifierProbe::Pixel30TexldpPartialPrecision ||
                texldDestinationModifierProbe ==
                    SyntheticTexldDestinationModifierProbe::Pixel20TexldpPartialPrecision;
            const bool probesBiasedTexld =
                texldDestinationModifierProbe ==
                    SyntheticTexldDestinationModifierProbe::Pixel30TexldbSaturate ||
                texldDestinationModifierProbe ==
                    SyntheticTexldDestinationModifierProbe::Pixel30TexldbPartialPrecision;
            const bool textureUsesFourComponents =
                textureUsesGradients || textureUsesExplicitLod ||
                probesPixelTexldDestinationModifier;
            const std::uint32_t coordinateMask =
                textureUsesFourComponents ? 0xFu : threeComponent ? 0x7u : 0x3u;
            const std::uint32_t samplerTextureType =
                samplerKind == SyntheticSamplerKind::SamplerCube
                    ? EffectFormat::SamplerTypeCube
                    : samplerKind == SyntheticSamplerKind::Sampler3D
                          ? EffectFormat::SamplerTypeVolume
                          : EffectFormat::SamplerType2D;
            const std::uint32_t coordinateRegister =
                usesShaderModel3 ? regInput : regTexture;
            // dcl t0.xy(z) for ps_2_0 or dcl_texcoord0 v0.xy(z) for ps_3_0 -- the
            // interpolated texture coordinate the vertex shader forwards.
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | (usesShaderModel3 ? 5u : 0u));
            AppendUInt32(shader, destination(coordinateRegister, 0, coordinateMask));
            if (texld20OperandProbe == SyntheticTexld20OperandProbe::InputDestination ||
                texld20OperandProbe == SyntheticTexld20OperandProbe::ColorCoordinate ||
                texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xColorCoordinate)
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_color0 v0
                AppendUInt32(shader, 0x80000000u | 10u);
                AppendUInt32(shader, destination(regInput, 0, 0xFu));
            }
            // dcl_<2d|cube|volume> s<samplerRegister> -- the texture type lives in bits 27..30 of
            // the usage token (D3DSAMPLER_TEXTURE_TYPE).
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | (samplerTextureType << 27));
            AppendUInt32(shader, destination(regSampler, samplerRegister, 0xFu));
            if (texlddOperandProbe ==
                SyntheticTexlddOperandProbe::Pixel2xTemporaryCoordinate)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, t0
                AppendUInt32(shader, destination(regTemp, 1, 0xFu));
                AppendUInt32(shader, source(coordinateRegister, 0));
            }
            // texld r0, t0, s# or texldd r0, v0, s#, v0, v0.
            const std::uint32_t textureOpcode = textureUsesGradients
                                                    ? 0x0000005Du
                                                : textureUsesExplicitLod
                                                    ? 0x0000005Fu
                                                    : 0x00000042u;
            const std::uint32_t textureLength = textureUsesGradients ? 5u : 3u;
            const std::uint32_t textureControl =
                probesProjectedTexld ? 1u : probesBiasedTexld ? 2u : 0u;
            AppendUInt32(shader,
                         textureOpcode | (textureLength << 24) | (textureControl << 16));
            const std::uint32_t destinationType =
                texld20OperandProbe == SyntheticTexld20OperandProbe::InputDestination
                    ? regInput
                : texld20OperandProbe == SyntheticTexld20OperandProbe::TextureDestination
                    ? regTexture
                : texld20OperandProbe == SyntheticTexld20OperandProbe::OutputDestination
                    ? regColorOut
                : texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xOutputDestination ||
                      texlddOperandProbe ==
                          SyntheticTexlddOperandProbe::Pixel30OutputDestination
                    ? regColorOut
                    : regTemp;
            const std::uint32_t destinationMask =
                texld20OperandProbe == SyntheticTexld20OperandProbe::PartialDestination ||
                        texlddOperandProbe ==
                            SyntheticTexlddOperandProbe::Pixel2xPartialDestination ||
                        texlddOperandProbe ==
                            SyntheticTexlddOperandProbe::Pixel30PartialDestination
                    ? 0x3u
                    : 0xFu;
            const std::uint32_t destinationModifier =
                texld20OperandProbe == SyntheticTexld20OperandProbe::SaturateDestination
                    ? (1u << 20)
                : texld20OperandProbe ==
                      SyntheticTexld20OperandProbe::PartialPrecisionDestination
                    ? (2u << 20)
                : texldDestinationModifierProbe ==
                          SyntheticTexldDestinationModifierProbe::Pixel30TexldSaturate ||
                      texldDestinationModifierProbe ==
                          SyntheticTexldDestinationModifierProbe::Pixel30TexldpSaturate ||
                      texldDestinationModifierProbe ==
                          SyntheticTexldDestinationModifierProbe::Pixel30TexldbSaturate
                    ? (1u << 20)
                : texldDestinationModifierProbe ==
                          SyntheticTexldDestinationModifierProbe::Pixel20TexldpPartialPrecision ||
                      texldDestinationModifierProbe ==
                          SyntheticTexldDestinationModifierProbe::Pixel30TexldpPartialPrecision ||
                      texldDestinationModifierProbe ==
                          SyntheticTexldDestinationModifierProbe::Pixel30TexldbPartialPrecision
                    ? (2u << 20)
                : texlddOperandProbe ==
                          SyntheticTexlddOperandProbe::Pixel2xSaturateDestination ||
                      texlddOperandProbe ==
                          SyntheticTexlddOperandProbe::Pixel30SaturateDestination
                    ? (1u << 20)
                : texlddOperandProbe ==
                      SyntheticTexlddOperandProbe::Pixel2xPartialPrecisionDestination
                    ? (2u << 20)
                : texldlDestinationModifierProbe ==
                      SyntheticTexldlDestinationModifierProbe::PixelSaturate
                    ? (1u << 20)
                : texldlDestinationModifierProbe ==
                      SyntheticTexldlDestinationModifierProbe::PixelPartialPrecision
                    ? (2u << 20)
                    : 0u;
            AppendUInt32(shader,
                         destination(destinationType, 0, destinationMask) |
                             destinationModifier);
            constexpr std::uint32_t swizzleBgra =
                2u | (1u << 2) | (0u << 4) | (3u << 6);
            const bool negateCoordinate =
                textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexlddCoordinate ||
                textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexldlCoordinate;
            const std::uint32_t texldCoordinateType =
                texld20OperandProbe == SyntheticTexld20OperandProbe::ColorCoordinate
                    ? regInput
                : texld20OperandProbe == SyntheticTexld20OperandProbe::ConstantCoordinate
                    ? regConst
                : texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xColorCoordinate
                    ? regInput
                : texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xConstantCoordinate ||
                      texlddOperandProbe ==
                          SyntheticTexlddOperandProbe::Pixel30ConstantCoordinate
                    ? regConst
                : texlddOperandProbe ==
                      SyntheticTexlddOperandProbe::Pixel2xTemporaryCoordinate
                    ? regTemp
                    : coordinateRegister;
            const std::uint32_t texldCoordinateIndex =
                texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xTemporaryCoordinate
                    ? 1u
                    : 0u;
            const bool swizzleTexlddCoordinate =
                texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xCoordinateSwizzle ||
                texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel30SourceSwizzles;
            AppendUInt32(shader, source(texldCoordinateType, texldCoordinateIndex,
                                        swizzleTexlddCoordinate ? swizzleBgra : swizzleIdentity,
                                        negateCoordinate ? 1u : 0u));
            const bool negateSampler =
                textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexlddSampler ||
                textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexldlSampler;
            const bool swizzleSampler =
                texld20OperandProbe == SyntheticTexld20OperandProbe::SamplerSwizzle ||
                texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xSamplerSwizzle ||
                texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel30SourceSwizzles;
            AppendUInt32(shader, source(regSampler, samplerRegister,
                                        swizzlesSampleResult || swizzleSampler ? swizzleBgra
                                                            : swizzleIdentity,
                                        negateSampler ? 1u : 0u));
            if (textureUsesGradients)
            {
                const bool negateGradient =
                    textureSourceModifierProbe ==
                    SyntheticTextureSourceModifierProbe::PixelTexlddGradient;
                const std::uint32_t gradientType =
                    texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xConstantGradients
                        ? regConst
                        : coordinateRegister;
                const bool swizzleGradient =
                    texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel2xGradientSwizzle ||
                    texlddOperandProbe == SyntheticTexlddOperandProbe::Pixel30SourceSwizzles;
                AppendUInt32(shader, source(gradientType, 0,
                                            swizzleGradient ? swizzleBgra : swizzleIdentity,
                                            negateGradient ? 1u : 0u));
                AppendUInt32(shader, source(gradientType, 0,
                                            swizzleGradient ? swizzleBgra : swizzleIdentity));
            }
            if (probesTexld20Operand || probesTexlddOperand)
            {
                // Keep every destination probe independent from a later read of that destination.
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oC0, c0
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regConst, 0));
            }
            else
            {
                // mul oC0, r0, c0 -- the sampled texel modulated by Tint, so a test can read the
                // raw texel back with Tint at (1,1,1,1) and still prove the compiled shader is what
                // ran by changing Tint.
                AppendUInt32(shader, 0x00000005u | (3u << 24));
                AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
                AppendUInt32(shader, source(regTemp, 0));
                AppendUInt32(shader, source(regConst, 0));
            }
        }
        else if (shaderModel14PhaseProbe != SyntheticShaderModel14PhaseProbe::None ||
                 pixel1OutputLivenessProbe != SyntheticPixel1OutputLivenessProbe::None)
        {
            // The dedicated phase/output-liveness program already completed or intentionally
            // omitted the required r0 components.
        }
        else if (shaderModel1InvalidOpcode != SyntheticInvalidPixelShaderModel1Opcode::None)
        {
            // The deliberately invalid instruction already writes ps_1_x's r0 colour output.
        }
        else if (probesPixel11ColorInput || probesPixel11TextureInput ||
                 probesPixel14TextureInput || probesPixel20ColorInput ||
                 probesPixel20TexCoordInput || probesPixel30Input)
        {
            // The dedicated input-range instruction already writes oC0.
        }
        else if (probesPixel20Centroid)
        {
            // The centroid discriminator already writes oC0.
        }
        else if (probesPixelOutput)
        {
            // The dedicated output-range instruction already writes its selected output.
        }
        else if (probesPixel11FloatControl || probesPixel20FloatControl ||
                 probesPixel30FloatControl)
        {
            // The dedicated constant-range instruction already writes the profile output.
        }
        else if (probesPixel11Temporary || probesPixel14Temporary)
        {
            AppendUInt32(shader, 0x00000001u); // mov r0, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (texkillOperandProbe ==
                 SyntheticTexkillOperandProbe::Pixel14TemporaryXyz)
        {
            // The profile-specific probe already completed the r0 output.
        }
        else if (probesPixel14ComponentwiseInitialization)
        {
            AppendUInt32(shader, 0x00000001u); // mov r0.yzw, c0
            AppendUInt32(shader, destination(regTemp, 0, 0xEu));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (writesMrt)
        {
            constexpr std::uint32_t swizzles[4] = {
                swizzleIdentity,
                1u | (0u << 2) | (2u << 4) | (3u << 6), // .yxzw
                2u | (0u << 2) | (1u << 4) | (3u << 6), // .zxyw
                2u | (1u << 2) | (0u << 4) | (3u << 6), // .zyxw
            };
            for (std::uint32_t slot = 0; slot < 4; ++slot)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24));
                AppendUInt32(shader, destination(regColorOut, slot, 0xFu));
                AppendUInt32(shader, source(regConst, 0, swizzles[slot]));
            }
        }
        else
        {
            // mov oC0, c0 -- or c0.yzxw for the alternate program that makes one pass
            // GPU-observably different from another (plans/plan_fx.md FX-094).
            AppendUInt32(shader, 0x00000001u | (2u << 24));
            AppendUInt32(shader, destination(regColorOut, 0, 0xFu));
            AppendUInt32(shader, source(regConst, 0,
                                        swizzleTint ? swizzleYzxw : swizzleIdentity,
                                        invalidMixedConstantAbsolute ==
                                                SyntheticInvalidShaderModel3MixedConstantAbsolute::
                                                    PixelAllAbsolute
                                            ? 11u
                                            : 0u));
        }
        AppendUInt32(shader, 0x0000FFFFu);
        return shader;
    }

    /**
     * Assembles a Shader Model 2.0 vertex-shader program and its Direct3D 9 constant table.
     *
     * plans/plan_fx.md FX-084. The program is
     * `oPos = mul(POSITION0 + TEXCOORD0 * StreamMix, Transform)` (the TEXCOORD0 term only when
     * @p readsSecondStream), written straight in Direct3D 9 shader tokens so the shared
     * conformance suite stays free of any compiler dependency, exactly like its pixel-shader
     * sibling above.
     *
     * @param readsSecondStream Whether the shader declares and consumes a TEXCOORD0 input.
     * @param forwardsTexCoord plans/plan_fx.md FX-093: whether the shader also declares TEXCOORD0 and
     *        writes it to `oT0`, which is what a sampling pixel shader reads.
     * @return The complete vertex-shader token buffer.
     */
    inline std::vector<std::uint8_t> BuildSyntheticVertexShader(bool readsSecondStream,
                                                                bool forwardsTexCoord = false,
                                                                bool forwardsThreeComponents = false,
                                                                bool usesPredication = false,
                                                                bool forwardsFourComponents = false,
                                                                bool forwardsLegacyTextureMatrix = false,
                                                                bool forwardsLegacyTextureMatrix2 = false,
                                                                bool forwardsLegacyTextureMatrix3Sample = false,
                                                                bool forwardsLegacyTextureMatrix3Specular = false,
                                                                bool forwardsLegacyTextureMatrix3VertexSpecular = false,
                                                                bool legacyTextureMatrix2ZeroDivisor = false,
                                                                bool forwardsLegacyDependentTexture = false,
                                                                bool forwardsLegacyBumpTexture = false,
                                                                bool samplesTexture = false,
                                                                std::uint32_t samplerRegister = 0,
                                                                SyntheticSamplerKind samplerKind =
                                                                    SyntheticSamplerKind::Sampler2D,
                                                                bool swizzlesSampleResult = false,
                                                                bool usesShaderModel11Input = false,
                                                                bool usesShaderModel11ExtendedInputs = false,
                                                                bool usesLegacyExpp = false,
                                                                SyntheticSgnScratchOperands sgnScratchOperands =
                                                                    SyntheticSgnScratchOperands::None,
                                                                bool usesInvalidExppSwizzle = false,
                                                                bool usesInvalidExpSwizzle = false,
                                                                SyntheticTemporaryInitializationProbe
                                                                    temporaryInitializationProbe =
                                                                        SyntheticTemporaryInitializationProbe::None,
                                                                SyntheticInvalidVertexShaderModel1Opcode
                                                                    shaderModel1InvalidOpcode =
                                                                        SyntheticInvalidVertexShaderModel1Opcode::None,
                                                                SyntheticInvalidShaderModel20DynamicFeature
                                                                    shaderModel20InvalidDynamicFeature =
                                                                        SyntheticInvalidShaderModel20DynamicFeature::None,
                                                                SyntheticInvalidPreShaderModel3AbsoluteSource
                                                                    invalidAbsoluteSource =
                                                                        SyntheticInvalidPreShaderModel3AbsoluteSource::None,
                                                                SyntheticInvalidShaderModel3MixedConstantAbsolute
                                                                    invalidMixedConstantAbsolute =
                                                                        SyntheticInvalidShaderModel3MixedConstantAbsolute::None,
                                                                SyntheticTemporaryRegisterProbe temporaryRegisterProbe =
                                                                    SyntheticTemporaryRegisterProbe::None,
                                                                SyntheticInputRegisterProbe inputRegisterProbe =
                                                                    SyntheticInputRegisterProbe::None,
                                                                SyntheticOutputRegisterProbe outputRegisterProbe =
                                                                    SyntheticOutputRegisterProbe::None,
                                                                SyntheticConstantControlRegisterProbe
                                                                    constantControlRegisterProbe =
                                                                        SyntheticConstantControlRegisterProbe::None,
                                                                SyntheticDestinationRegisterAccessProbe
                                                                    destinationRegisterAccessProbe =
                                                                        SyntheticDestinationRegisterAccessProbe::None,
                                                                SyntheticOutputRegisterSourceProbe
                                                                    outputRegisterSourceProbe =
                                                                        SyntheticOutputRegisterSourceProbe::None,
                                                                SyntheticAddressRegisterAccessProbe
                                                                    addressRegisterAccessProbe =
                                                                        SyntheticAddressRegisterAccessProbe::None,
                                                                SyntheticSamplerRegisterSourceProbe
                                                                    samplerRegisterSourceProbe =
                                                                        SyntheticSamplerRegisterSourceProbe::None,
                                                                SyntheticMissingSamplerDeclarationProbe
                                                                    missingSamplerDeclarationProbe =
                                                                        SyntheticMissingSamplerDeclarationProbe::None,
                                                                SyntheticImmediateConstantDefinitionProbe
                                                                    immediateConstantDefinitionProbe =
                                                                        SyntheticImmediateConstantDefinitionProbe::None,
                                                                SyntheticDuplicateDeclarationProbe
                                                                    duplicateDeclarationProbe =
                                                                        SyntheticDuplicateDeclarationProbe::None,
                                                                SyntheticTypedControlSourceProbe
                                                                    typedControlSourceProbe =
                                                                        SyntheticTypedControlSourceProbe::None,
                                                                SyntheticSpecialControlSourceProbe
                                                                    specialControlSourceProbe =
                                                                        SyntheticSpecialControlSourceProbe::None,
                                                                SyntheticVertexInstructionSlotProbe
                                                                    instructionSlotProbe =
                                                                        SyntheticVertexInstructionSlotProbe::None,
                                                                SyntheticRelativeAddressingProbe
                                                                    relativeAddressingProbe =
                                                                        SyntheticRelativeAddressingProbe::None,
                                                                SyntheticSemanticDeclarationProbe
                                                                    semanticDeclarationProbe =
                                                                        SyntheticSemanticDeclarationProbe::None,
                                                                SyntheticCompositeWriteMaskProbe
                                                                    compositeWriteMaskProbe =
                                                                        SyntheticCompositeWriteMaskProbe::None,
                                                                SyntheticFlowControlProbe flowControlProbe =
                                                                    SyntheticFlowControlProbe::None,
                                                                SyntheticCallGraphProbe callGraphProbe =
                                                                    SyntheticCallGraphProbe::None,
                                                                SyntheticTextureSourceModifierProbe
                                                                    textureSourceModifierProbe =
                                                                        SyntheticTextureSourceModifierProbe::None,
                                                                SyntheticTextureInstructionProfileProbe
                                                                    textureInstructionProfileProbe =
                                                                        SyntheticTextureInstructionProfileProbe::None,
                                                                SyntheticTexldlDestinationModifierProbe
                                                                    texldlDestinationModifierProbe =
                                                                        SyntheticTexldlDestinationModifierProbe::None,
                                                                SyntheticComponentwiseInitializationProbe
                                                                    componentwiseInitializationProbe =
                                                                        SyntheticComponentwiseInitializationProbe::None,
                                                                SyntheticScalarInitializationProbe
                                                                    scalarInitializationProbe =
                                                                        SyntheticScalarInitializationProbe::None,
                                                                SyntheticFixedVectorInitializationProbe
                                                                    fixedVectorInitializationProbe =
                                                                        SyntheticFixedVectorInitializationProbe::None,
                                                                SyntheticMatrixInitializationProbe
                                                                    matrixInitializationProbe =
                                                                        SyntheticMatrixInitializationProbe::None,
                                                                SyntheticSpecialVectorInitializationProbe
                                                                    specialVectorInitializationProbe =
                                                                        SyntheticSpecialVectorInitializationProbe::None)
    {
        const bool probesVertex11SpecialInitialization =
            (specialVectorInitializationProbe >=
                    SyntheticSpecialVectorInitializationProbe::Vertex11LitUnwrittenW &&
                specialVectorInitializationProbe <=
                    SyntheticSpecialVectorInitializationProbe::Vertex11DstSource1UnwrittenW) ||
            (specialVectorInitializationProbe >=
                    SyntheticSpecialVectorInitializationProbe::Vertex11LitWrittenXyw &&
                specialVectorInitializationProbe <=
                    SyntheticSpecialVectorInitializationProbe::Vertex11DstSource1WrittenYw);
        const bool usesShaderModel11 = usesShaderModel11Input ||
                                       usesShaderModel11ExtendedInputs || usesLegacyExpp ||
                                       temporaryInitializationProbe ==
                                           SyntheticTemporaryInitializationProbe::Vertex11 ||
                                       temporaryInitializationProbe ==
                                           SyntheticTemporaryInitializationProbe::Vertex11MoveWrittenX ||
                                       temporaryInitializationProbe ==
                                           SyntheticTemporaryInitializationProbe::Vertex11MoveUnwrittenY ||
                                       componentwiseInitializationProbe ==
                                           SyntheticComponentwiseInitializationProbe::
                                               Vertex11AddUnwrittenY ||
                                       componentwiseInitializationProbe ==
                                           SyntheticComponentwiseInitializationProbe::
                                               Vertex11AddWrittenX ||
                                       scalarInitializationProbe ==
                                           SyntheticScalarInitializationProbe::
                                               Vertex11RcpUnwrittenY ||
                                       scalarInitializationProbe ==
                                           SyntheticScalarInitializationProbe::
                                               Vertex11ExppUnwrittenY ||
                                       scalarInitializationProbe ==
                                           SyntheticScalarInitializationProbe::Vertex11RcpWrittenX ||
                                       fixedVectorInitializationProbe >=
                                           SyntheticFixedVectorInitializationProbe::
                                               Vertex11Dp3UnwrittenYz ||
                                       matrixInitializationProbe >=
                                           SyntheticMatrixInitializationProbe::
                                               Vertex11M4x4VectorUnwrittenW ||
                                       probesVertex11SpecialInitialization ||
                                       shaderModel1InvalidOpcode !=
                                           SyntheticInvalidVertexShaderModel1Opcode::None ||
                                       temporaryRegisterProbe ==
                                           SyntheticTemporaryRegisterProbe::Vertex11Maximum ||
                                       temporaryRegisterProbe ==
                                           SyntheticTemporaryRegisterProbe::Vertex11OutOfRange ||
                                       instructionSlotProbe ==
                                           SyntheticVertexInstructionSlotProbe::Vertex11Maximum ||
                                       instructionSlotProbe ==
                                           SyntheticVertexInstructionSlotProbe::Vertex11OutOfRange ||
                                       addressRegisterAccessProbe ==
                                           SyntheticAddressRegisterAccessProbe::Vertex11MovDestination ||
                                       addressRegisterAccessProbe ==
                                           SyntheticAddressRegisterAccessProbe::Vertex11Source ||
                                       addressRegisterAccessProbe ==
                                           SyntheticAddressRegisterAccessProbe::Vertex11AddDestination;
        const bool probesVertex11InstructionSlots =
            instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex11Maximum ||
            instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex11OutOfRange;
        const bool probesVertex30Input =
            inputRegisterProbe == SyntheticInputRegisterProbe::Vertex30Maximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Vertex30OutOfRange;
        const bool probesVertex20Input =
            inputRegisterProbe == SyntheticInputRegisterProbe::Vertex20Maximum ||
            inputRegisterProbe == SyntheticInputRegisterProbe::Vertex20OutOfRange;
        const bool probesVertex30InputDestination =
            inputRegisterProbe == SyntheticInputRegisterProbe::Vertex30Destination;
        const bool probesVertex20InputDestination =
            inputRegisterProbe == SyntheticInputRegisterProbe::Vertex20Destination;
        const bool probesVertex30Output =
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex30Maximum ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex30OutOfRange;
        const bool probesVertex20Output =
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20ColorMaximum ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20ColorOutOfRange ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20TexCoordMaximum ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20TexCoordOutOfRange ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20RasterMaximum ||
            outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20RasterOutOfRange;
        const bool probesVertex30ConstantControl =
            constantControlRegisterProbe >=
                SyntheticConstantControlRegisterProbe::Vertex30IntegerMaximum &&
            constantControlRegisterProbe <=
                SyntheticConstantControlRegisterProbe::Vertex30PredicateOutOfRange;
        const bool probesVertex20Control =
            constantControlRegisterProbe >=
                SyntheticConstantControlRegisterProbe::Vertex20AddressMaximum &&
            constantControlRegisterProbe <=
                SyntheticConstantControlRegisterProbe::Vertex20LoopOutOfRange;
        const bool probesVertex20DestinationAccess =
            destinationRegisterAccessProbe ==
            SyntheticDestinationRegisterAccessProbe::Vertex20FloatConstant;
        const bool probesVertex30DestinationAccess =
            destinationRegisterAccessProbe >=
                SyntheticDestinationRegisterAccessProbe::Vertex30IntegerConstant &&
            destinationRegisterAccessProbe <=
                SyntheticDestinationRegisterAccessProbe::Vertex30Predicate;
        const bool probesVertex20OutputSource =
            outputRegisterSourceProbe >= SyntheticOutputRegisterSourceProbe::Vertex20Raster &&
            outputRegisterSourceProbe <= SyntheticOutputRegisterSourceProbe::Vertex20TexCoord;
        const bool probesVertex30OutputSource =
            outputRegisterSourceProbe == SyntheticOutputRegisterSourceProbe::Vertex30Generic;
        const bool probesVertex30Address =
            addressRegisterAccessProbe ==
                SyntheticAddressRegisterAccessProbe::Vertex30MovaDestination ||
            addressRegisterAccessProbe == SyntheticAddressRegisterAccessProbe::Vertex30Source ||
            addressRegisterAccessProbe ==
                SyntheticAddressRegisterAccessProbe::Vertex30MovDestination;
        const bool probesVertex30SamplerSource =
            samplerRegisterSourceProbe == SyntheticSamplerRegisterSourceProbe::Vertex30;
        const bool probesMissingVertexSamplerDeclaration =
            missingSamplerDeclarationProbe ==
            SyntheticMissingSamplerDeclarationProbe::Vertex30;
        const bool probesVertexImmediateConstantDefinition =
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexFloatDuplicate ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexIntegerDuplicate ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexBooleanDuplicate ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexIntegerMinimum ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexIntegerMaximum ||
            immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexIntegerReservedW;
        const bool probesVertexSamplerDuplicate =
            duplicateDeclarationProbe == SyntheticDuplicateDeclarationProbe::VertexSamplerSame ||
            duplicateDeclarationProbe ==
                SyntheticDuplicateDeclarationProbe::VertexSamplerConflict;
        const bool probesVertex20DeclarationDuplicate =
            duplicateDeclarationProbe ==
                SyntheticDuplicateDeclarationProbe::Vertex20InputSameRegister ||
            duplicateDeclarationProbe ==
                SyntheticDuplicateDeclarationProbe::Vertex20SemanticSameDifferentRegister;
        const bool probesVertex30TypedControlSource =
            typedControlSourceProbe == SyntheticTypedControlSourceProbe::VertexInteger ||
            typedControlSourceProbe == SyntheticTypedControlSourceProbe::VertexBoolean;
        const bool probesVertex30SpecialControlSource =
            specialControlSourceProbe == SyntheticSpecialControlSourceProbe::VertexPredicate ||
            specialControlSourceProbe == SyntheticSpecialControlSourceProbe::VertexLabel ||
            specialControlSourceProbe == SyntheticSpecialControlSourceProbe::VertexLoop;
        const bool probesVertexRelativeAddressing =
            relativeAddressingProbe >=
                SyntheticRelativeAddressingProbe::VertexConstantOutsideLoop &&
            relativeAddressingProbe <=
                SyntheticRelativeAddressingProbe::VertexInputInsideLoop;
        const bool probesCentroid20 =
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidControl ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidExplicit ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::Pixel20CentroidImplicitColor;
        const bool probesVertexSemanticDeclarations =
            semanticDeclarationProbe != SyntheticSemanticDeclarationProbe::None &&
            !probesCentroid20;
        const bool probesCentroid =
            semanticDeclarationProbe == SyntheticSemanticDeclarationProbe::PixelCentroidControl ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidExplicit ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelCentroidImplicitColor ||
            probesCentroid20;
        const bool probesVertex20FlowControl =
            flowControlProbe >= SyntheticFlowControlProbe::Vertex20LoopRepDepth1 &&
            flowControlProbe <= SyntheticFlowControlProbe::Vertex2xStaticFlowCount17;
        const bool probesVertex2xFlowControl =
            flowControlProbe == SyntheticFlowControlProbe::Vertex2xStaticFlowCount16 ||
            flowControlProbe == SyntheticFlowControlProbe::Vertex2xStaticFlowCount17;
        const bool probesVertexCallGraph =
            callGraphProbe >= SyntheticCallGraphProbe::Vertex20Depth1 &&
            callGraphProbe <= SyntheticCallGraphProbe::Vertex30Label2047;
        const bool probesVertex2xCallGraph =
            callGraphProbe == SyntheticCallGraphProbe::Vertex2xDepth4 ||
            callGraphProbe == SyntheticCallGraphProbe::Vertex2xDepth5;
        const bool probesVertexTextureSourceModifier =
            textureSourceModifierProbe ==
                SyntheticTextureSourceModifierProbe::VertexTexldlCoordinate ||
            textureSourceModifierProbe ==
                SyntheticTextureSourceModifierProbe::VertexTexldlSampler;
        const bool probesVertexTextureInstructionProfile =
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Vertex20Texldl ||
            textureInstructionProfileProbe ==
                SyntheticTextureInstructionProfileProbe::Vertex2xTexldl;
        const bool probesVertex2xTextureInstructionProfile =
            textureInstructionProfileProbe ==
            SyntheticTextureInstructionProfileProbe::Vertex2xTexldl;
        const bool probesVertexTexldlDestinationModifier =
            texldlDestinationModifierProbe ==
            SyntheticTexldlDestinationModifierProbe::VertexSaturate;
        const bool probesVertex30CallGraph =
            callGraphProbe >= SyntheticCallGraphProbe::Vertex30Depth4 &&
            callGraphProbe <= SyntheticCallGraphProbe::Vertex30Label2047;
        const bool usesShaderModel3 =
            usesPredication || samplesTexture || probesVertexTextureSourceModifier ||
            probesVertexTexldlDestinationModifier ||
            temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Vertex30 ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexPlainThenAbsolute ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAbsoluteThenPlain ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAllAbsolute ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex30Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex30OutOfRange ||
            probesVertex30Input || probesVertex30InputDestination || probesVertex30Output ||
            probesVertex30ConstantControl || probesVertex30DestinationAccess ||
            probesVertex30OutputSource || probesVertex30Address || probesVertex30SamplerSource ||
            probesVertex30TypedControlSource || probesVertex30SpecialControlSource ||
            probesVertexRelativeAddressing || probesVertexSemanticDeclarations ||
            probesVertex30CallGraph || probesMissingVertexSamplerDeclaration ||
            probesVertexImmediateConstantDefinition || probesVertexSamplerDuplicate;
        const bool probesVertex2xTemporary =
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex2xMaximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex2xOutOfRange;
        const bool probesVertex2xInstructionSlots =
            instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex2xMaximum ||
            instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex2xOutOfRange;
        const std::uint32_t versionToken = usesShaderModel11
                                               ? 0xFFFE0101u
                                           : probesVertex2xTemporary ||
                                                     probesVertex2xInstructionSlots ||
                                                     probesVertex2xFlowControl ||
                                                     probesVertex2xCallGraph ||
                                                     probesVertex2xTextureInstructionProfile
                                               ? 0xFFFE02FFu
                                           : usesShaderModel3
                                               ? 0xFFFE0300u
                                               : 0xFFFE0200u;
        const std::uint32_t constantCount =
            1u + (readsSecondStream ? 1u : 0u) +
            (samplesTexture || probesVertexTextureSourceModifier ||
                     probesVertex30SamplerSource || probesVertexTextureInstructionProfile ||
                     probesVertexTexldlDestinationModifier
                 ? 1u
                 : 0u);

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

        const auto samplerType = static_cast<std::uint32_t>(ctab.size());
        AppendUInt16(ctab, EffectFormat::ClassObject);
        AppendUInt16(ctab, samplerKind == SyntheticSamplerKind::SamplerCube
                                   ? EffectFormat::TypeSamplerCube
                                   : samplerKind == SyntheticSamplerKind::Sampler3D
                                         ? EffectFormat::TypeSampler3D
                                         : EffectFormat::TypeSampler2D);
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
        const std::uint32_t transformName = appendCtabString("Transform");
        const std::uint32_t streamMixName = appendCtabString("StreamMix");
        const std::uint32_t samplerName = appendCtabString("FxSampler");
        const std::uint32_t target = appendCtabString(
            usesShaderModel11 ? "vs_1_1"
                              : probesVertex2xTemporary || probesVertex2xInstructionSlots ||
                                        probesVertex2xFlowControl ||
                                        probesVertex2xTextureInstructionProfile
                                    ? "vs_2_x"
                                    : usesShaderModel3
                                          ? "vs_3_0"
                                          : "vs_2_0");
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
        if (samplesTexture || probesVertexTextureSourceModifier ||
            probesVertex30SamplerSource || probesVertexTextureInstructionProfile ||
            probesVertexTexldlDestinationModifier)
        {
            const std::uint32_t samplerInfo = constantInfo +
                (readsSecondStream ? 40u : 20u);
            PatchUInt32(ctab, samplerInfo, samplerName);
            ctab[samplerInfo + 4] = 3; // RegisterSet: sampler
            ctab[samplerInfo + 6] = static_cast<std::uint8_t>(samplerRegister);
            PatchUInt32(ctab, samplerInfo + 12, samplerType);
        }

        // Direct3D 9 shader-token register types, and the two token shapes every instruction
        // below is built from. Identical encoding to BuildSyntheticPixelShader's own.
        constexpr std::uint32_t regTemp = 0;
        constexpr std::uint32_t regInput = 1;
        constexpr std::uint32_t regConst = 2;
        constexpr std::uint32_t regAddress = 3;
        constexpr std::uint32_t regRastOut = 4;
        constexpr std::uint32_t regAttrOut = 5;      // vs_2_0 oD#
        constexpr std::uint32_t regTexCoordOut = 6;  // vs_2_0 oT#
        constexpr std::uint32_t regPredicate = 19;
        constexpr std::uint32_t regSampler = 10;
        constexpr std::uint32_t regConstInt = 7;
        constexpr std::uint32_t regConstBool = 14;
        constexpr std::uint32_t regLoop = 15;
        constexpr std::uint32_t regLabel = 18;
        const auto registerBits = [](std::uint32_t type) {
            return ((type & 0x7u) << 28) | ((type >> 3) << 11);
        };
        const auto destination = [&registerBits](std::uint32_t type, std::uint32_t number,
                                                 std::uint32_t writeMask = 0xFu) {
            return 0x80000000u | registerBits(type) | number | (writeMask << 16);
        };
        const auto source = [&registerBits](std::uint32_t type, std::uint32_t number,
                                            std::uint32_t swizzle = 0xE4u,
                                            std::uint32_t modifier = 0u) {
            return 0x80000000u | registerBits(type) | number | (swizzle << 16) |
                   (modifier << 24);
        };

        std::vector<std::uint8_t> shader;
        AppendUInt32(shader, versionToken);
        AppendUInt32(shader, 0x0000FFFEu |
                                 ((1u + static_cast<std::uint32_t>(ctab.size() / 4)) << 16));
        AppendUInt32(shader, 0x42415443u); // 'CTAB'
        shader.insert(shader.end(), ctab.begin(), ctab.end());

        if (probesVertexImmediateConstantDefinition)
        {
            const auto appendFloatDefinition = [&](float red, float green) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, 250, 0xFu));
                AppendUInt32(shader, FloatBits(red));
                AppendUInt32(shader, FloatBits(green));
                AppendUInt32(shader, FloatBits(0.0f));
                AppendUInt32(shader, FloatBits(1.0f));
            };
            const auto appendIntegerDefinition =
                [&](std::int32_t count, std::int32_t initial, std::int32_t step,
                    std::int32_t reserved) {
                    AppendUInt32(shader, 0x00000030u | (5u << 24));
                    AppendUInt32(shader, destination(regConstInt, 15, 0xFu));
                    AppendUInt32(shader, static_cast<std::uint32_t>(count));
                    AppendUInt32(shader, static_cast<std::uint32_t>(initial));
                    AppendUInt32(shader, static_cast<std::uint32_t>(step));
                    AppendUInt32(shader, static_cast<std::uint32_t>(reserved));
                };
            const auto appendBooleanDefinition = [&](bool value) {
                AppendUInt32(shader, 0x0000002Fu | (2u << 24));
                AppendUInt32(shader, destination(regConstBool, 15, 0xFu));
                AppendUInt32(shader, value ? 1u : 0u);
            };

            switch (immediateConstantDefinitionProbe)
            {
                case SyntheticImmediateConstantDefinitionProbe::VertexFloatDuplicate:
                    appendFloatDefinition(1.0f, 0.0f);
                    appendFloatDefinition(0.0f, 1.0f);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::VertexIntegerDuplicate:
                    appendIntegerDefinition(1, 0, 1, 0);
                    appendIntegerDefinition(2, 0, 2, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::VertexBooleanDuplicate:
                    appendBooleanDefinition(true);
                    appendBooleanDefinition(false);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::VertexIntegerMinimum:
                    appendIntegerDefinition(0, 0, -128, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::VertexIntegerMaximum:
                    appendIntegerDefinition(255, 255, 127, 0);
                    break;
                case SyntheticImmediateConstantDefinitionProbe::VertexIntegerReservedW:
                    appendIntegerDefinition(1, 0, 1, 1);
                    break;
                default: break;
            }
        }

        if (usesPredication)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(240u, 1.0f, 0.0f, 1.0f, 0.0f);
            appendDef(241u, 0.5f, 0.5f, 0.5f, 0.5f);
            appendDef(242u, 0.125f, 0.25f, 0.375f, 1.0f);
            appendDef(243u, 0.75f, 0.625f, 0.5f, 0.25f);
        }
        if (forwardsLegacyTextureMatrix)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(240u, 1.0f, 0.0f, 0.0f, 1.0f);  // t0 vector
            appendDef(241u, 0.25f, 0.0f, 0.0f, 1.0f); // t1 row
            appendDef(242u, 0.5f, 0.0f, 0.0f, 1.0f);  // t2 row
            appendDef(243u, 0.25f, 0.0f, 0.0f, 1.0f); // t3 row
        }
        if (forwardsLegacyTextureMatrix2)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(241u, 0.0f, 0.0f, 0.25f, 1.0f); // t1 row
            appendDef(242u, 0.0f, 0.0f,
                      legacyTextureMatrix2ZeroDivisor ? 0.0f : 0.75f, 1.0f); // t2 row
        }
        if (forwardsLegacyTextureMatrix3Sample)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(241u, 0.0f, 0.0f, 2.0f, 1.0f); // t1 row
            appendDef(242u, 0.0f, 0.0f, 6.0f, 1.0f); // t2 row
            appendDef(243u, 0.0f, 0.0f, 4.0f, 1.0f); // t3 row
        }
        if (forwardsLegacyTextureMatrix3Specular ||
            forwardsLegacyTextureMatrix3VertexSpecular)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(241u, 1.0f, 0.0f, 0.0f, 0.1f); // first matrix row / eye x
            appendDef(242u, 0.0f, 1.0f, 0.0f, 0.2f); // second matrix row / eye y
            appendDef(243u, 0.0f, 0.0f, 1.0f, 1.0f); // third matrix row / eye z
        }
        if (forwardsLegacyDependentTexture)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(241u, 1.0f, 0.0f, 0.0f, 1.0f); // t1 dot-product row
        }
        if (forwardsLegacyBumpTexture)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(241u, 0.25f, 0.25f, 0.0f, 1.0f); // destination-stage base UV
        }
        if (usesLegacyExpp)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u);
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            appendDef(4u, 3.25f, 0.0f, 0.0f, 0.0f);
            appendDef(5u, 9.0f, 0.5f, 10.0f, 2.0f);
        }
        if (semanticDeclarationProbe == SyntheticSemanticDeclarationProbe::PackedDisjoint ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs)
        {
            const bool separate = semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs;
            AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c240
            AppendUInt32(shader, destination(regConst, 240u));
            AppendUInt32(shader, FloatBits(0.25f));
            AppendUInt32(shader, FloatBits(0.5f));
            AppendUInt32(shader, FloatBits(separate ? 0.0625f : 0.75f));
            AppendUInt32(shader, FloatBits(separate ? 0.125f : 1.0f));
            if (separate)
            {
                AppendUInt32(shader, 0x00000051u | (5u << 24)); // def c241
                AppendUInt32(shader, destination(regConst, 241u));
                AppendUInt32(shader, FloatBits(0.875f));
                AppendUInt32(shader, FloatBits(0.625f));
                AppendUInt32(shader, FloatBits(0.75f));
                AppendUInt32(shader, FloatBits(1.0f));
            }
        }
        if (compositeWriteMaskProbe != SyntheticCompositeWriteMaskProbe::None)
        {
            const auto appendDef = [&](std::uint32_t number, float x, float y, float z, float w) {
                AppendUInt32(shader, 0x00000051u | (5u << 24));
                AppendUInt32(shader, destination(regConst, number));
                AppendUInt32(shader, FloatBits(x));
                AppendUInt32(shader, FloatBits(y));
                AppendUInt32(shader, FloatBits(z));
                AppendUInt32(shader, FloatBits(w));
            };
            if (compositeWriteMaskProbe == SyntheticCompositeWriteMaskProbe::VertexDst)
            {
                appendDef(240u, 0.0f, 1.0f, 1.0f, 1.0f);
                appendDef(241u, 0.0f, 1.0f, 0.0f, 1.0f);
            }
            else
            {
                appendDef(240u, 0.0f, 1.0f, -1.0f, 0.0f);
                appendDef(241u, -1.0f, 0.0f, 1.0f, 0.0f);
            }
            appendDef(242u, 1.0f, 1.0f, 1.0f, 1.0f);
        }

        // dcl_position v0
        if (!usesShaderModel11)
        {
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | 0u);        // D3DDECLUSAGE_POSITION, index 0
            AppendUInt32(
                shader,
                destination(regInput, 0,
                            semanticDeclarationProbe ==
                                    SyntheticSemanticDeclarationProbe::VertexInputPartialMask
                                ? 0x3u
                                : 0xFu));
        }
        if (probesVertex20DeclarationDuplicate)
        {
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | 0u); // dcl_position0 v0/v1
            AppendUInt32(
                shader,
                destination(
                    regInput,
                    duplicateDeclarationProbe ==
                            SyntheticDuplicateDeclarationProbe::Vertex20InputSameRegister
                        ? 0u
                        : 1u));
        }
        if (probesVertex20Input || probesVertex30Input)
        {
            const bool maximum =
                inputRegisterProbe == SyntheticInputRegisterProbe::Vertex20Maximum ||
                inputRegisterProbe == SyntheticInputRegisterProbe::Vertex30Maximum;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord7 v#
            AppendUInt32(shader, 0x80070005u);
            AppendUInt32(shader, destination(regInput, maximum ? 15u : 16u));
        }
        if (usesShaderModel3)
        {
            // Shader Model 3 uses generic o# outputs, each with an explicit semantic declaration.
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_position0 o0
            AppendUInt32(shader, 0x80000000u | 0u);
            AppendUInt32(
                shader,
                destination(regTexCoordOut, 0,
                            semanticDeclarationProbe ==
                                    SyntheticSemanticDeclarationProbe::VertexPositionPartialMask
                                ? 0x3u
                                : 0xFu));
            if (semanticDeclarationProbe == SyntheticSemanticDeclarationProbe::PackedDisjoint ||
                semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs ||
                semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::VertexOutputOverlappingMasks ||
                semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::VertexOutputDuplicateSemantic)
            {
                const bool duplicate = semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::VertexOutputDuplicateSemantic;
                const bool overlap = semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::VertexOutputOverlappingMasks;
                const bool separate = semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs;
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord0 o1.xy
                AppendUInt32(shader, 0x80000000u | 5u);
                AppendUInt32(shader, destination(regTexCoordOut, 1, 0x3u));
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u | (duplicate ? 5u : 10u));
                AppendUInt32(shader,
                             destination(regTexCoordOut, duplicate || separate ? 2u : 1u,
                                         overlap ? 0x6u : 0xCu));
            }
            if (probesCentroid && !probesCentroid20)
            {
                const bool color = semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::PixelCentroidImplicitColor;
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u | (color ? 10u : 5u));
                AppendUInt32(shader, destination(regTexCoordOut, 1));
            }
            if (semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::VertexPointSizePartialMask)
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_psize0 o1.x
                AppendUInt32(shader, 0x80000000u | 4u);
                AppendUInt32(shader, destination(regTexCoordOut, 1, 0x1u));
            }
            if (probesVertex30Output)
            {
                const bool maximum =
                    outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex30Maximum;
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_texcoord7 o#
                AppendUInt32(shader, 0x80070005u);
                AppendUInt32(shader, destination(regTexCoordOut, maximum ? 11u : 12u));
            }
            if (usesPredication)
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_color0 o1
                AppendUInt32(shader, 0x80000000u | 10u);
                AppendUInt32(shader, destination(regTexCoordOut, 1));
            }
            if (immediateConstantDefinitionProbe ==
                SyntheticImmediateConstantDefinitionProbe::VertexFloatDuplicate)
            {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_color0 o1
                AppendUInt32(shader, 0x80000000u | 10u);
                AppendUInt32(shader, destination(regTexCoordOut, 1));
            }
        }
        if (probesVertexSamplerDuplicate)
        {
            const auto appendSamplerDeclaration = [&](std::uint32_t textureType) {
                AppendUInt32(shader, 0x0000001Fu | (2u << 24));
                AppendUInt32(shader, 0x80000000u | (textureType << 27));
                AppendUInt32(shader, destination(regSampler, 0));
            };
            appendSamplerDeclaration(EffectFormat::SamplerType2D);
            appendSamplerDeclaration(
                duplicateDeclarationProbe ==
                        SyntheticDuplicateDeclarationProbe::VertexSamplerConflict
                    ? EffectFormat::SamplerTypeCube
                    : EffectFormat::SamplerType2D);
        }
        // One declaration serves both consumers: the multi-stream fixture scales POSITION0 by it,
        // the sampling fixture forwards it, and a fixture that does both declares it once.
        if (readsSecondStream || forwardsTexCoord || probesCentroid ||
            relativeAddressingProbe == SyntheticRelativeAddressingProbe::VertexInputInsideLoop)
        {
            // dcl_texcoord v1
            AppendUInt32(shader, 0x0000001Fu | (2u << 24));
            AppendUInt32(shader, 0x80000000u | 5u);        // D3DDECLUSAGE_TEXCOORD, index 0
            AppendUInt32(shader, destination(regInput, 1));
        }
        if (samplesTexture || probesVertex30SamplerSource ||
            probesVertexTextureInstructionProfile || probesVertexTexldlDestinationModifier)
        {
            const std::uint32_t samplerTextureType =
                samplerKind == SyntheticSamplerKind::SamplerCube
                    ? EffectFormat::SamplerTypeCube
                    : samplerKind == SyntheticSamplerKind::Sampler3D
                          ? EffectFormat::SamplerTypeVolume
                          : EffectFormat::SamplerType2D;
            AppendUInt32(shader, 0x0000001Fu | (2u << 24)); // dcl_<kind> s#
            AppendUInt32(shader, 0x80000000u | (samplerTextureType << 27));
            AppendUInt32(shader, destination(regSampler, samplerRegister));
        }
        if (probesVertexRelativeAddressing)
        {
            const bool outsideLoop = relativeAddressingProbe ==
                SyntheticRelativeAddressingProbe::VertexConstantOutsideLoop;
            const bool subroutine = relativeAddressingProbe ==
                SyntheticRelativeAddressingProbe::VertexConstantSubroutineInsideLoop;
            const bool inputRegister = relativeAddressingProbe ==
                SyntheticRelativeAddressingProbe::VertexInputInsideLoop;
            if (!outsideLoop)
            {
                AppendUInt32(shader, 0x00000030u | (5u << 24)); // defi i0, 1, start, 1, 0
                AppendUInt32(shader, destination(regConstInt, 0));
                AppendUInt32(shader, 1u);
                AppendUInt32(shader, inputRegister ? 1u : 0u);
                AppendUInt32(shader, 1u);
                AppendUInt32(shader, 0u);
                AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL, i0
                AppendUInt32(shader, source(regLoop, 0));
                AppendUInt32(shader, source(regConstInt, 0));
            }
            if (subroutine)
            {
                AppendUInt32(shader, 0x00000019u | (1u << 24)); // call l0
                AppendUInt32(shader, source(regLabel, 0));
            }
            else
            {
                AppendUInt32(shader, 0x00000001u | (3u << 24)); // mov r10, c0/v0[aL]
                AppendUInt32(shader, destination(regTemp, 10));
                AppendUInt32(shader, source(inputRegister ? regInput : regConst, 0) |
                                         (1u << 13u));
                AppendUInt32(shader, source(regLoop, 0));
            }
            if (!outsideLoop) AppendUInt32(shader, 0x0000001Du); // endloop
        }
        if (instructionSlotProbe != SyntheticVertexInstructionSlotProbe::None)
        {
            const bool maximum =
                instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex11Maximum ||
                instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex20Maximum ||
                instructionSlotProbe == SyntheticVertexInstructionSlotProbe::Vertex2xMaximum;
            const std::uint32_t profileLimit = probesVertex11InstructionSlots ? 128u : 256u;
            // The ordinary terminal M4X4 below consumes four slots. Fill the remainder with NOPs
            // so these programs land exactly on the profile limit or one slot beyond it.
            const std::uint32_t nopCount = profileLimit - 4u + (maximum ? 0u : 1u);
            for (std::uint32_t index = 0; index < nopCount; ++index)
                AppendUInt32(shader, 0x00000000u);
        }
        if (probesVertex20Input || probesVertex30Input)
        {
            const bool maximum =
                inputRegisterProbe == SyntheticInputRegisterProbe::Vertex20Maximum ||
                inputRegisterProbe == SyntheticInputRegisterProbe::Vertex30Maximum;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, v#
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regInput, maximum ? 15u : 16u));
        }
        if (probesVertex20InputDestination || probesVertex30InputDestination)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov v0, c0
            AppendUInt32(shader, destination(regInput, 0));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (probesVertex20DestinationAccess || probesVertex30DestinationAccess)
        {
            std::uint32_t registerType = regConst;
            std::uint32_t writeMask = 0xFu;
            switch (destinationRegisterAccessProbe)
            {
                case SyntheticDestinationRegisterAccessProbe::Vertex20FloatConstant:
                    break;
                case SyntheticDestinationRegisterAccessProbe::Vertex30IntegerConstant:
                    registerType = regConstInt;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Vertex30BooleanConstant:
                    registerType = regConstBool;
                    writeMask = 0x1u;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Vertex30Sampler:
                    registerType = regSampler;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Vertex30Loop:
                    registerType = regLoop;
                    writeMask = 0x1u;
                    break;
                case SyntheticDestinationRegisterAccessProbe::Vertex30Predicate:
                    registerType = regPredicate;
                    break;
                default: break;
            }
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov restricted destination, c0
            AppendUInt32(shader, destination(registerType, 0, writeMask));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (probesVertex20OutputSource || probesVertex30OutputSource)
        {
            const std::uint32_t registerType = probesVertex30OutputSource
                                                   ? regTexCoordOut
                                               : outputRegisterSourceProbe ==
                                                     SyntheticOutputRegisterSourceProbe::Vertex20Raster
                                                   ? regRastOut
                                               : outputRegisterSourceProbe ==
                                                     SyntheticOutputRegisterSourceProbe::Vertex20Color
                                                   ? regAttrOut
                                                   : regTexCoordOut;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, write-only output
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(registerType, 0));
        }
        if (addressRegisterAccessProbe != SyntheticAddressRegisterAccessProbe::None)
        {
            const bool legalMov = addressRegisterAccessProbe ==
                                  SyntheticAddressRegisterAccessProbe::Vertex11MovDestination;
            const bool legalMova =
                addressRegisterAccessProbe ==
                    SyntheticAddressRegisterAccessProbe::Vertex20MovaDestination ||
                addressRegisterAccessProbe ==
                    SyntheticAddressRegisterAccessProbe::Vertex30MovaDestination;
            const bool readsAddress =
                addressRegisterAccessProbe == SyntheticAddressRegisterAccessProbe::Vertex11Source ||
                addressRegisterAccessProbe == SyntheticAddressRegisterAccessProbe::Vertex20Source ||
                addressRegisterAccessProbe == SyntheticAddressRegisterAccessProbe::Vertex30Source;
            const bool invalidAdd = addressRegisterAccessProbe ==
                                    SyntheticAddressRegisterAccessProbe::Vertex11AddDestination;
            if (legalMov || (readsAddress && usesShaderModel11))
            {
                AppendUInt32(shader, 0x00000001u); // mov a0.x, c0.w
                AppendUInt32(shader, destination(regAddress, 0, 0x1u));
                AppendUInt32(shader, source(regConst, 0, 0xFFu));
            }
            else if (legalMova || readsAddress)
            {
                AppendUInt32(shader, 0x0000002Eu | (2u << 24)); // mova a0, c0
                AppendUInt32(shader, destination(regAddress, 0,
                                                 probesVertex30Address ? 0xFu : 0x1u));
                AppendUInt32(shader, source(regConst, 0));
            }
            else if (invalidAdd)
            {
                AppendUInt32(shader, 0x00000002u); // add a0.x, c0, c0
                AppendUInt32(shader, destination(regAddress, 0, 0x1u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regConst, 0));
            }
            else
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov a0, c0
                AppendUInt32(shader, destination(regAddress, 0));
                AppendUInt32(shader, source(regConst, 0));
            }
            if (readsAddress)
            {
                AppendUInt32(shader,
                             0x00000001u | (usesShaderModel11 ? 0u : (2u << 24))); // mov r1, a0.x
                AppendUInt32(shader, destination(regTemp, 1));
                AppendUInt32(shader, source(regAddress, 0, 0x00u));
            }
        }
        if (probesVertex30SamplerSource)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, s0
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regSampler, 0));
        }
        if (probesVertex30TypedControlSource)
        {
            const bool boolean =
                typedControlSourceProbe == SyntheticTypedControlSourceProbe::VertexBoolean;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, i0/b0.x
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(boolean ? regConstBool : regConstInt, 0,
                                        boolean ? 0x00u : 0xE4u));
        }
        if (probesVertex30SpecialControlSource)
        {
            const std::uint32_t registerType =
                specialControlSourceProbe == SyntheticSpecialControlSourceProbe::VertexPredicate
                    ? regPredicate
                : specialControlSourceProbe == SyntheticSpecialControlSourceProbe::VertexLabel
                    ? regLabel
                    : regLoop;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, p0/l0/aL
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(registerType, 0, 0x00u));
        }
        if (probesVertex20Output || probesVertex30Output)
        {
            const bool maximum =
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20ColorMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20TexCoordMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20RasterMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex30Maximum;
            const bool color =
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20ColorMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20ColorOutOfRange;
            const bool texture =
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20TexCoordMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20TexCoordOutOfRange;
            const bool raster =
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20RasterMaximum ||
                outputRegisterProbe == SyntheticOutputRegisterProbe::Vertex20RasterOutOfRange;
            const std::uint32_t registerType = color ? regAttrOut
                                                       : texture || probesVertex30Output
                                                             ? regTexCoordOut
                                                             : regRastOut;
            const std::uint32_t registerNumber = color
                                                     ? (maximum ? 1u : 2u)
                                                 : texture
                                                     ? (maximum ? 7u : 8u)
                                                 : raster
                                                     ? (maximum ? 2u : 3u)
                                                     : (maximum ? 11u : 12u);
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov output, c0
            AppendUInt32(shader, destination(registerType, registerNumber,
                                             raster ? 0x1u : 0xFu));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (probesVertex30ConstantControl)
        {
            const bool maximum =
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex30IntegerMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex30BooleanMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex30PredicateMaximum;
            if (constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex30IntegerMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex30IntegerOutOfRange)
            {
                AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i#
                AppendUInt32(shader, source(regConstInt, maximum ? 15u : 16u, 0x00u));
                AppendUInt32(shader, 0x00000027u); // endrep
            }
            else if (constantControlRegisterProbe ==
                         SyntheticConstantControlRegisterProbe::Vertex30BooleanMaximum ||
                     constantControlRegisterProbe ==
                         SyntheticConstantControlRegisterProbe::Vertex30BooleanOutOfRange)
            {
                AppendUInt32(shader, 0x00000028u | (1u << 24)); // if b#
                AppendUInt32(shader, source(regConstBool, maximum ? 15u : 16u, 0x00u));
                AppendUInt32(shader, 0x0000002Bu); // endif
            }
            else
            {
                AppendUInt32(shader,
                             0x0000005Eu | (1u << 16) |
                                 (3u << 24)); // setp_gt p#, c0.x, c0.x
                AppendUInt32(shader, destination(regPredicate, maximum ? 0u : 1u, 0x1u));
                AppendUInt32(shader, source(regConst, 0, 0x00u));
                AppendUInt32(shader, source(regConst, 0, 0x00u));
            }
        }
        if (probesVertex20Control)
        {
            const bool maximum =
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex20AddressMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex20LoopMaximum;
            if (constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex20AddressMaximum ||
                constantControlRegisterProbe ==
                    SyntheticConstantControlRegisterProbe::Vertex20AddressOutOfRange)
            {
                AppendUInt32(shader, 0x0000002Eu | (2u << 24)); // mova a#, c0.x
                AppendUInt32(shader, destination(regAddress, maximum ? 0u : 1u, 0x1u));
                AppendUInt32(shader, source(regConst, 0, 0x00u));
            }
            else
            {
                AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL#, i0
                AppendUInt32(shader, source(regLoop, maximum ? 0u : 1u, 0x00u));
                AppendUInt32(shader, source(regConstInt, 0, 0x00u));
                AppendUInt32(shader, 0x0000001Du); // endloop
            }
        }
        if (sgnScratchOperands != SyntheticSgnScratchOperands::None)
        {
            const bool nonTemporary =
                sgnScratchOperands == SyntheticSgnScratchOperands::NonTemporary;
            const std::uint32_t scratchType = nonTemporary ? regConst : regTemp;
            const std::uint32_t secondScratch =
                sgnScratchOperands == SyntheticSgnScratchOperands::Aliased ? 1u : 2u;
            AppendUInt32(shader, 0x00000022u | (4u << 24)); // sgn r0, v0, scratch1, scratch2
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(scratchType, nonTemporary ? 0u : 1u));
            AppendUInt32(shader, source(scratchType, nonTemporary ? 1u : secondScratch));
        }
        if (usesInvalidExppSwizzle)
        {
            AppendUInt32(shader, 0x0000004Eu | (2u << 24)); // expp r1, c0.xyzw
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (usesInvalidExpSwizzle)
        {
            AppendUInt32(shader, 0x0000000Eu | (2u << 24)); // exp r1, c0.xyzw
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (shaderModel1InvalidOpcode != SyntheticInvalidVertexShaderModel1Opcode::None)
        {
            const auto appendUnary = [&](std::uint32_t opcode, std::uint32_t mask = 0xFu) {
                AppendUInt32(shader, opcode);
                AppendUInt32(shader, destination(regTemp, 1, mask));
                AppendUInt32(shader, source(regConst, 0));
            };
            switch (shaderModel1InvalidOpcode)
            {
                case SyntheticInvalidVertexShaderModel1Opcode::Abs:
                    appendUnary(0x00000023u);
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Crs:
                    AppendUInt32(shader, 0x00000021u);
                    AppendUInt32(shader, destination(regTemp, 1, 0x7u));
                    AppendUInt32(shader, source(regConst, 0));
                    AppendUInt32(shader, source(regConst, 1));
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Nrm:
                    appendUnary(0x00000024u);
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Pow:
                    AppendUInt32(shader, 0x00000020u);
                    AppendUInt32(shader, destination(regTemp, 1));
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    AppendUInt32(shader, source(regConst, 1, 0x00u));
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::SinCos:
                    AppendUInt32(shader, 0x00000025u);
                    AppendUInt32(shader, destination(regTemp, 1, 0x3u));
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    AppendUInt32(shader, source(regConst, 1));
                    AppendUInt32(shader, source(regConst, 2));
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Sgn:
                    AppendUInt32(shader, 0x00000022u);
                    AppendUInt32(shader, destination(regTemp, 1));
                    AppendUInt32(shader, source(regConst, 0));
                    AppendUInt32(shader, source(regTemp, 2));
                    AppendUInt32(shader, source(regTemp, 3));
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Mova:
                    AppendUInt32(shader, 0x0000002Eu);
                    AppendUInt32(shader, destination(regAddress, 0, 0x1u));
                    AppendUInt32(shader, source(regConst, 0));
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Defb:
                    AppendUInt32(shader, 0x0000002Fu);
                    AppendUInt32(shader, destination(regConstBool, 0));
                    AppendUInt32(shader, 1u);
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::Defi:
                    AppendUInt32(shader, 0x00000030u);
                    AppendUInt32(shader, destination(regConstInt, 0));
                    AppendUInt32(shader, 1u);
                    AppendUInt32(shader, 2u);
                    AppendUInt32(shader, 3u);
                    AppendUInt32(shader, 4u);
                    break;
                case SyntheticInvalidVertexShaderModel1Opcode::None: break;
            }
        }
        if (shaderModel20InvalidDynamicFeature !=
                SyntheticInvalidShaderModel20DynamicFeature::None &&
            shaderModel20InvalidDynamicFeature !=
                SyntheticInvalidShaderModel20DynamicFeature::PixelPredicatedMov)
        {
            switch (shaderModel20InvalidDynamicFeature)
            {
                case SyntheticInvalidShaderModel20DynamicFeature::VertexIfc:
                    AppendUInt32(shader, 0x00000029u | (1u << 16) | (2u << 24)); // if_gt c0.x, c1.x
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    AppendUInt32(shader, source(regConst, 1, 0x00u));
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticInvalidShaderModel20DynamicFeature::VertexBreak:
                case SyntheticInvalidShaderModel20DynamicFeature::VertexBreakc:
                    AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i0
                    AppendUInt32(shader, source(regConstInt, 0, 0x00u));
                    if (shaderModel20InvalidDynamicFeature ==
                        SyntheticInvalidShaderModel20DynamicFeature::VertexBreak)
                    {
                        AppendUInt32(shader, 0x0000002Cu); // break
                    }
                    else
                    {
                        AppendUInt32(shader,
                                     0x0000002Du | (1u << 16) |
                                         (2u << 24)); // break_gt c0.x, c1.x
                        AppendUInt32(shader, source(regConst, 0, 0x00u));
                        AppendUInt32(shader, source(regConst, 1, 0x00u));
                    }
                    AppendUInt32(shader, 0x00000027u); // endrep
                    break;
                case SyntheticInvalidShaderModel20DynamicFeature::VertexSetp:
                    AppendUInt32(shader,
                                 0x0000005Eu | (1u << 16) | (3u << 24)); // setp_gt p0, c0.x, c1.x
                    AppendUInt32(shader, destination(regPredicate, 0));
                    AppendUInt32(shader, source(regConst, 0, 0x00u));
                    AppendUInt32(shader, source(regConst, 1, 0x00u));
                    break;
                case SyntheticInvalidShaderModel20DynamicFeature::VertexIfPredicate:
                    AppendUInt32(shader, 0x00000028u | (1u << 24)); // if p0.x
                    AppendUInt32(shader, source(regPredicate, 0, 0x00u));
                    AppendUInt32(shader, 0x0000002Bu); // endif
                    break;
                case SyntheticInvalidShaderModel20DynamicFeature::VertexCallnzPredicate:
                    AppendUInt32(shader, 0x0000001Au | (2u << 24)); // callnz l0, p0.x
                    AppendUInt32(shader, source(regLabel, 0));
                    AppendUInt32(shader, source(regPredicate, 0, 0x00u));
                    break;
                case SyntheticInvalidShaderModel20DynamicFeature::VertexPredicatedMov:
                    AppendUInt32(shader, 0x10000001u | (3u << 24)); // (p0.x) mov r1, c0
                    AppendUInt32(shader, destination(regTemp, 1));
                    AppendUInt32(shader, source(regConst, 0));
                    AppendUInt32(shader, source(regPredicate, 0, 0x00u));
                    break;
                case SyntheticInvalidShaderModel20DynamicFeature::None:
                case SyntheticInvalidShaderModel20DynamicFeature::PixelPredicatedMov: break;
            }
        }
        if (invalidAbsoluteSource ==
                SyntheticInvalidPreShaderModel3AbsoluteSource::VertexAbsolute ||
            invalidAbsoluteSource ==
                SyntheticInvalidPreShaderModel3AbsoluteSource::VertexAbsoluteNegate)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, abs(c0)
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(
                shader,
                source(regConst, 0, 0xE4u,
                       invalidAbsoluteSource ==
                               SyntheticInvalidPreShaderModel3AbsoluteSource::VertexAbsolute
                           ? 11u
                           : 12u));
        }
        if (temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex11Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex11OutOfRange ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex20Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex20OutOfRange ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex2xMaximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex2xOutOfRange ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex30Maximum ||
            temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex30OutOfRange)
        {
            const std::uint32_t registerNumber =
                temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex11Maximum ||
                        temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex20Maximum
                    ? 11u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex30Maximum
                    ? 31u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex2xMaximum
                    ? 31u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex30OutOfRange
                    ? 32u
                : temporaryRegisterProbe == SyntheticTemporaryRegisterProbe::Vertex2xOutOfRange
                    ? 32u
                    : 12u;
            AppendUInt32(shader, 0x00000001u |
                                     (usesShaderModel11 ? 0u : (2u << 24))); // mov r#, c0
            AppendUInt32(shader, destination(regTemp, registerNumber));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexPlainThenAbsolute ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAbsoluteThenPlain ||
            invalidMixedConstantAbsolute ==
                SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAllAbsolute)
        {
            const bool absoluteFirst =
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAbsoluteThenPlain ||
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAllAbsolute;
            const bool absoluteSecond =
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexPlainThenAbsolute ||
                invalidMixedConstantAbsolute ==
                    SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAllAbsolute;
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, c0/abs(c0)
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regConst, 0, 0xE4u, absoluteFirst ? 11u : 0u));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, abs(c1)/c1
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regConst, 1, 0xE4u, absoluteSecond ? 11u : 0u));
        }
        if (temporaryInitializationProbe != SyntheticTemporaryInitializationProbe::None &&
            temporaryInitializationProbe != SyntheticTemporaryInitializationProbe::Pixel30 &&
            temporaryInitializationProbe !=
                SyntheticTemporaryInitializationProbe::Pixel20MoveWrittenX &&
            temporaryInitializationProbe !=
                SyntheticTemporaryInitializationProbe::Pixel20MoveUnwrittenY &&
            temporaryInitializationProbe !=
                SyntheticTemporaryInitializationProbe::Vertex11MoveWrittenX &&
            temporaryInitializationProbe !=
                SyntheticTemporaryInitializationProbe::Vertex11MoveUnwrittenY)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r1, uninitialized r0
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regTemp, 0));
        }
        if (temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Vertex11MoveWrittenX ||
            temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Vertex11MoveUnwrittenY)
        {
            AppendUInt32(shader, 0x00000001u); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, 0x00000001u); // mov r1, r0.x/y
            AppendUInt32(shader, destination(regTemp, 1));
            const bool readsY = temporaryInitializationProbe ==
                SyntheticTemporaryInitializationProbe::Vertex11MoveUnwrittenY;
            AppendUInt32(shader, source(regTemp, 0, readsY ? 0x55u : 0x00u));
        }
        if (componentwiseInitializationProbe ==
                SyntheticComponentwiseInitializationProbe::Vertex11AddUnwrittenY ||
            componentwiseInitializationProbe ==
                SyntheticComponentwiseInitializationProbe::Vertex11AddWrittenX)
        {
            AppendUInt32(shader, 0x00000001u); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            AppendUInt32(shader, 0x00000002u); // add r1.x, r0.x/y, c0.x
            AppendUInt32(shader, destination(regTemp, 1, 0x1u));
            AppendUInt32(
                shader,
                source(regTemp, 0,
                       componentwiseInitializationProbe ==
                               SyntheticComponentwiseInitializationProbe::Vertex11AddWrittenX
                           ? 0x00u
                           : 0x55u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
        }
        if (scalarInitializationProbe ==
                SyntheticScalarInitializationProbe::Vertex11RcpUnwrittenY ||
            scalarInitializationProbe ==
                SyntheticScalarInitializationProbe::Vertex11ExppUnwrittenY ||
            scalarInitializationProbe == SyntheticScalarInitializationProbe::Vertex11RcpWrittenX)
        {
            AppendUInt32(shader, 0x00000001u); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            const bool expp = scalarInitializationProbe ==
                SyntheticScalarInitializationProbe::Vertex11ExppUnwrittenY;
            AppendUInt32(shader, expp ? 0x0000004Eu : 0x00000006u); // expp/rcp r1.x, r0.x/y
            AppendUInt32(shader, destination(regTemp, 1, 0x1u));
            AppendUInt32(
                shader,
                source(regTemp, 0,
                       scalarInitializationProbe ==
                               SyntheticScalarInitializationProbe::Vertex11RcpWrittenX
                           ? 0x00u
                           : 0x55u));
        }
        if (fixedVectorInitializationProbe >=
            SyntheticFixedVectorInitializationProbe::Vertex11Dp3UnwrittenYz)
        {
            using Probe = SyntheticFixedVectorInitializationProbe;
            AppendUInt32(shader, 0x00000001u); // mov r0.x, c0.x
            AppendUInt32(shader, destination(regTemp, 0, 0x1u));
            AppendUInt32(shader, source(regConst, 0, 0x00u));
            const bool dp4 =
                fixedVectorInitializationProbe == Probe::Vertex11Dp4UnwrittenYzw ||
                fixedVectorInitializationProbe == Probe::Vertex11Dp4ReplicatedX;
            const bool initialized =
                fixedVectorInitializationProbe == Probe::Vertex11Dp3ReplicatedX ||
                fixedVectorInitializationProbe == Probe::Vertex11Dp4ReplicatedX;
            AppendUInt32(shader, dp4 ? 0x00000009u : 0x00000008u); // dp4/dp3 r1.x, ...
            AppendUInt32(shader, destination(regTemp, 1, 0x1u));
            AppendUInt32(shader,
                         source(regTemp, 0, initialized ? 0x00u : 0xE4u));
            AppendUInt32(shader, source(regConst, 0));
        }
        if (matrixInitializationProbe >=
            SyntheticMatrixInitializationProbe::Vertex11M4x4VectorUnwrittenW)
        {
            using Probe = SyntheticMatrixInitializationProbe;
            const bool matrixRows =
                matrixInitializationProbe == Probe::Vertex11M3x2MatrixRowUnwrittenZ ||
                matrixInitializationProbe == Probe::Vertex11M3x2MatrixRowsWrittenXyz;
            const bool initialized =
                matrixInitializationProbe >= Probe::Vertex11M4x4VectorWritten;
            if (matrixRows)
            {
                AppendUInt32(shader, 0x00000001u); // mov r0.xyz, c0
                AppendUInt32(shader, destination(regTemp, 0, 0x7u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, 0x00000001u); // mov r1.xy[z], c0
                AppendUInt32(shader, destination(regTemp, 1, initialized ? 0x7u : 0x3u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, 0x00000018u); // m3x2 r4.xy, c0, r0
                AppendUInt32(shader, destination(regTemp, 4, 0x3u));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, source(regTemp, 0));
            }
            else
            {
                std::uint32_t opcode = 0x00000014u;
                std::uint32_t destinationMask = 0xFu;
                bool vectorHasFourComponents = true;
                switch (matrixInitializationProbe)
                {
                    case Probe::Vertex11M4x3VectorUnwrittenW:
                    case Probe::Vertex11M4x3VectorWritten:
                        opcode = 0x00000015u;
                        destinationMask = 0x7u;
                        break;
                    case Probe::Vertex11M3x4VectorUnwrittenZ:
                    case Probe::Vertex11M3x4VectorWrittenXyz:
                        opcode = 0x00000016u;
                        vectorHasFourComponents = false;
                        break;
                    case Probe::Vertex11M3x3VectorUnwrittenZ:
                    case Probe::Vertex11M3x3VectorWrittenXyz:
                        opcode = 0x00000017u;
                        destinationMask = 0x7u;
                        vectorHasFourComponents = false;
                        break;
                    case Probe::Vertex11M3x2VectorUnwrittenZ:
                    case Probe::Vertex11M3x2VectorWrittenXyz:
                        opcode = 0x00000018u;
                        destinationMask = 0x3u;
                        vectorHasFourComponents = false;
                        break;
                    default:
                        break;
                }
                const std::uint32_t vectorMask = initialized
                    ? (vectorHasFourComponents ? 0xFu : 0x7u)
                    : (vectorHasFourComponents ? 0x7u : 0x3u);
                AppendUInt32(shader, 0x00000001u); // mov r0.mask, c0
                AppendUInt32(shader, destination(regTemp, 0, vectorMask));
                AppendUInt32(shader, source(regConst, 0));
                AppendUInt32(shader, opcode);
                AppendUInt32(shader, destination(regTemp, 4, destinationMask));
                AppendUInt32(shader, source(regTemp, 0));
                AppendUInt32(shader, source(regConst, 4));
            }
        }
        if (probesVertex11SpecialInitialization)
        {
            using Probe = SyntheticSpecialVectorInitializationProbe;
            const bool lit =
                specialVectorInitializationProbe == Probe::Vertex11LitUnwrittenW ||
                specialVectorInitializationProbe == Probe::Vertex11LitWrittenXyw;
            const bool sourceOne =
                specialVectorInitializationProbe == Probe::Vertex11DstSource1UnwrittenW ||
                specialVectorInitializationProbe == Probe::Vertex11DstSource1WrittenYw;
            const bool initialized =
                specialVectorInitializationProbe >= Probe::Vertex11LitWrittenXyw;
            const std::uint32_t temporaryMask = lit
                ? (initialized ? 0xBu : 0x7u)
                : sourceOne ? (initialized ? 0xAu : 0x2u)
                            : (initialized ? 0x6u : 0x2u);
            AppendUInt32(shader, 0x00000001u); // mov r0.mask, c0
            AppendUInt32(shader, destination(regTemp, 0, temporaryMask));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, lit ? 0x00000010u : 0x00000011u); // lit/dst r1, ...
            AppendUInt32(shader, destination(regTemp, 1));
            if (lit)
            {
                AppendUInt32(shader, source(regTemp, 0));
            }
            else
            {
                AppendUInt32(shader, source(sourceOne ? regConst : regTemp, 0));
                AppendUInt32(shader, source(sourceOne ? regTemp : regConst, 0));
            }
        }
        if (readsSecondStream)
        {
            // mad r0, v1, c4, v0
            AppendUInt32(shader, 0x00000004u | (4u << 24));
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regInput, 1));
            AppendUInt32(shader, source(regConst, 4));
            AppendUInt32(shader, source(regInput, 0));
        }
        if (compositeWriteMaskProbe != SyntheticCompositeWriteMaskProbe::None)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c242
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regConst, 242u));
            AppendUInt32(shader,
                         (compositeWriteMaskProbe == SyntheticCompositeWriteMaskProbe::VertexDst
                              ? 0x00000011u
                              : 0x00000021u) |
                             (3u << 24)); // dst/crs r0.xz, c240, c241
            AppendUInt32(shader, destination(regTemp, 0, 0x5u));
            AppendUInt32(shader, source(regConst, 240u));
            AppendUInt32(shader, source(regConst, 241u));
            AppendUInt32(shader, 0x00000005u | (3u << 24)); // mul r0, v0, r0
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regTemp, 0));
        }
        if (probesMissingVertexSamplerDeclaration)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x0000005Fu | (3u << 24)); // texldl r1, r0, s0
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regSampler, 0));
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov o0, v0
            AppendUInt32(shader, destination(regTexCoordOut, 0));
            AppendUInt32(shader, source(regInput, 0));
        }
        else if (samplesTexture || probesVertexTextureSourceModifier ||
                 probesVertexTextureInstructionProfile ||
                 probesVertexTexldlDestinationModifier)
        {
            // texldl r0, v1, s#; mov o0, r0. The texture therefore owns the complete clip-space
            // position and makes the vertex-stage lookup directly observable in target coverage.
            AppendUInt32(shader, 0x0000005Fu | (3u << 24));
            AppendUInt32(shader,
                         destination(regTemp, 0) |
                             (probesVertexTexldlDestinationModifier ? (1u << 20) : 0u));
            const bool negateCoordinate =
                textureSourceModifierProbe ==
                SyntheticTextureSourceModifierProbe::VertexTexldlCoordinate;
            AppendUInt32(shader, source(regInput, 1, 0xE4u,
                                        negateCoordinate ? 1u : 0u));
            constexpr std::uint32_t swizzleBgra =
                2u | (1u << 2u) | (0u << 4u) | (3u << 6u);
            const bool negateSampler =
                textureSourceModifierProbe ==
                SyntheticTextureSourceModifierProbe::VertexTexldlSampler;
            AppendUInt32(shader, source(regSampler, samplerRegister,
                                        swizzlesSampleResult ? swizzleBgra : 0xE4u,
                                        negateSampler ? 1u : 0u));
            AppendUInt32(shader, 0x00000001u | (2u << 24));
            AppendUInt32(shader, destination(regTexCoordOut, 0));
            AppendUInt32(shader, source(regTemp, 0));
        }
        else if (usesLegacyExpp)
        {
            // A conforming vs_1_1 EXPP gives (8, .25, 2^3.25, 1), whose four components are all
            // below c5. Replicating 2^3.25 instead makes r1.x zero and collapses the whole quad.
            AppendUInt32(shader, 0x00000014u); // m4x4 r2, v0, c0
            AppendUInt32(shader, destination(regTemp, 2));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x0000004Eu); // expp r0, c4.x
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regConst, 4, 0x00u));
            AppendUInt32(shader, 0x0000000Cu); // slt r1, r0, c5
            AppendUInt32(shader, destination(regTemp, 1));
            AppendUInt32(shader, source(regTemp, 0));
            AppendUInt32(shader, source(regConst, 5));
            AppendUInt32(shader, 0x00000005u); // mul oPos, r2, r1.x
            AppendUInt32(shader, destination(regRastOut, 0));
            AppendUInt32(shader, source(regTemp, 2));
            AppendUInt32(shader, source(regTemp, 1, 0x00u));
        }
        else if (usesShaderModel11ExtendedInputs)
        {
            // Shader Model 1 has a fixed declaration-free map: v5 is COLOR0 and v14 is
            // TEXCOORD7. Both values must be one for the transformed quad to retain coverage.
            AppendUInt32(shader, 0x00000014u); // m4x4 r2, v0, c0
            AppendUInt32(shader, destination(regTemp, 2));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000005u); // mul r0, v5.x, v14.x
            AppendUInt32(shader, destination(regTemp, 0));
            AppendUInt32(shader, source(regInput, 5, 0x00u));
            AppendUInt32(shader, source(regInput, 14, 0x00u));
            AppendUInt32(shader, 0x00000005u); // mul oPos, r2, r0.x
            AppendUInt32(shader, destination(regRastOut, 0));
            AppendUInt32(shader, source(regTemp, 2));
            AppendUInt32(shader, source(regTemp, 0, 0x00u));
        }
        else if (usesShaderModel11)
        {
            AppendUInt32(shader, 0x00000014u); // m4x4 oPos, v0, c0
            AppendUInt32(shader, destination(regRastOut, 0));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regConst, 0));
        }
        else if (probesVertexCallGraph)
        {
            const bool backward =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30BackwardCall ||
                callGraphProbe == SyntheticCallGraphProbe::Vertex30BackwardCallNz;
            const bool conditionalBackward =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30BackwardCallNz;
            const bool undefinedLabel =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30UndefinedLabel;
            const bool duplicateLabel =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30DuplicateLabel;
            const bool missingReturn =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30MissingReturn;
            const bool probesLabelRange =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30Label16 ||
                callGraphProbe == SyntheticCallGraphProbe::Vertex30Label2047;
            const std::uint32_t boundaryLabel =
                callGraphProbe == SyntheticCallGraphProbe::Vertex30Label2047 ? 2047u : 16u;
            const int depth =
                callGraphProbe == SyntheticCallGraphProbe::Vertex20Depth1
                    ? 1
                : callGraphProbe == SyntheticCallGraphProbe::Vertex20Depth2
                    ? 2
                : callGraphProbe == SyntheticCallGraphProbe::Vertex2xDepth5 ||
                        callGraphProbe == SyntheticCallGraphProbe::Vertex30Depth5
                    ? 5
                    : 4;
            if (conditionalBackward)
            {
                AppendUInt32(shader, 0x0000002Fu | (2u << 24)); // defb b0, true
                AppendUInt32(shader, destination(regConstBool, 0, 0x1u));
                AppendUInt32(shader, 1u);
            }
            AppendUInt32(shader, 0x00000014u | (3u << 24)); // m4x4 output, v0, c0
            AppendUInt32(shader,
                         destination(usesShaderModel3 ? regTexCoordOut : regRastOut, 0));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regConst, 0));
            AppendUInt32(shader, 0x00000019u | (1u << 24)); // call label
            AppendUInt32(shader,
                         source(regLabel,
                                probesLabelRange ? boundaryLabel : backward ? 1u : 0u));
            AppendUInt32(shader, 0x0000001Cu); // ret from main

            int labelCount = depth;
            if (probesLabelRange || missingReturn)
                labelCount = 1;
            else if (undefinedLabel)
                labelCount = 0;
            else if (backward || duplicateLabel)
                labelCount = 2;
            for (int label = 0; label < labelCount; ++label)
            {
                AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l#
                AppendUInt32(shader,
                             source(regLabel,
                                    probesLabelRange
                                        ? boundaryLabel
                                    : duplicateLabel
                                        ? 0u
                                        : static_cast<std::uint32_t>(label)));
                if (backward && label == 1)
                {
                    AppendUInt32(shader,
                                 (conditionalBackward ? 0x0000001Au | (2u << 24)
                                                      : 0x00000019u | (1u << 24)));
                    AppendUInt32(shader, source(regLabel, 0));
                    if (conditionalBackward)
                        AppendUInt32(shader, source(regConstBool, 0, 0x00u));
                }
                else if (!backward && label + 1 < labelCount)
                {
                    if (!duplicateLabel)
                    {
                        AppendUInt32(shader, 0x00000019u | (1u << 24)); // call next label
                        AppendUInt32(shader,
                                     source(regLabel, static_cast<std::uint32_t>(label + 1)));
                    }
                }
                if (missingReturn)
                {
                    AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov r0, c0
                    AppendUInt32(shader, destination(regTemp, 0, 0xFu));
                    AppendUInt32(shader, source(regConst, 0));
                }
                else
                    AppendUInt32(shader, 0x0000001Cu); // ret from subroutine
            }
        }
        else if (probesVertex20FlowControl)
        {
            AppendUInt32(shader, 0x00000030u | (5u << 24)); // defi i0, 1, 0, 1, 0
            AppendUInt32(shader, destination(regConstInt, 0, 0xFu));
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0u);
            AppendUInt32(shader, 1u);
            AppendUInt32(shader, 0u);
            AppendUInt32(shader, 0x0000002Fu | (2u << 24)); // defb b0, true
            AppendUInt32(shader, destination(regConstBool, 0, 0x1u));
            AppendUInt32(shader, 1u);
            if (flowControlProbe == SyntheticFlowControlProbe::Vertex20LoopRepDepth1 ||
                flowControlProbe == SyntheticFlowControlProbe::Vertex20LoopRepDepth2)
            {
                AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL, i0
                AppendUInt32(shader, source(regLoop, 0, 0x00u));
                AppendUInt32(shader, source(regConstInt, 0));
                if (flowControlProbe == SyntheticFlowControlProbe::Vertex20LoopRepDepth2)
                {
                    AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i0
                    AppendUInt32(shader, source(regConstInt, 0, 0x00u));
                    AppendUInt32(shader, 0x00000027u); // endrep
                }
                AppendUInt32(shader, 0x0000001Du); // endloop
            }
            else
            {
                const auto appendIf = [&](bool includeElse)
                {
                    AppendUInt32(shader, 0x00000028u | (1u << 24)); // if b0
                    AppendUInt32(shader, source(regConstBool, 0, 0x00u));
                    if (includeElse) AppendUInt32(shader, 0x0000002Au); // else
                    AppendUInt32(shader, 0x0000002Bu); // endif
                };
                const auto appendLoop = [&]()
                {
                    AppendUInt32(shader, 0x0000001Bu | (2u << 24)); // loop aL, i0
                    AppendUInt32(shader, source(regLoop, 0, 0x00u));
                    AppendUInt32(shader, source(regConstInt, 0));
                    AppendUInt32(shader, 0x0000001Du); // endloop
                };
                const auto appendRep = [&]()
                {
                    AppendUInt32(shader, 0x00000026u | (1u << 24)); // rep i0
                    AppendUInt32(shader, source(regConstInt, 0, 0x00u));
                    AppendUInt32(shader, 0x00000027u); // endrep
                };
                const auto appendCall = [&](bool conditional)
                {
                    AppendUInt32(shader,
                                 (conditional ? 0x0000001Au | (2u << 24)
                                              : 0x00000019u | (1u << 24)));
                    AppendUInt32(shader, source(regLabel, 0));
                    if (conditional)
                        AppendUInt32(shader, source(regConstBool, 0, 0x00u));
                };

                // This mixed prefix consumes 16 static-flow counts: IF+ELSE, LOOP, REP,
                // six CALLs and six Boolean CALLNZs. For the ELSE overflow probe the prefix
                // stops at 15, then IF reaches 16 and ELSE is the first excess operation.
                appendIf(true);
                appendLoop();
                appendRep();
                for (int index = 0; index < 6; ++index) appendCall(false);
                const int conditionalCalls =
                    flowControlProbe == SyntheticFlowControlProbe::Vertex20StaticFlowCount17Else
                        ? 5
                        : 6;
                for (int index = 0; index < conditionalCalls; ++index) appendCall(true);

                switch (flowControlProbe)
                {
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount16:
                    case SyntheticFlowControlProbe::Vertex2xStaticFlowCount16: break;
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount17If:
                        appendIf(false);
                        break;
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount17Else:
                        appendIf(true);
                        break;
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount17Loop:
                        appendLoop();
                        break;
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount17Rep:
                        appendRep();
                        break;
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount17Call:
                        appendCall(false);
                        break;
                    case SyntheticFlowControlProbe::Vertex20StaticFlowCount17CallNz:
                        appendCall(true);
                        break;
                    case SyntheticFlowControlProbe::Vertex2xStaticFlowCount17:
                        appendIf(false);
                        break;
                    default: break;
                }
            }
            AppendUInt32(shader, 0x00000014u | (3u << 24)); // m4x4 oPos, v0, c0
            AppendUInt32(shader, destination(regRastOut, 0));
            AppendUInt32(shader, source(regInput, 0));
            AppendUInt32(shader, source(regConst, 0));
            if (flowControlProbe >= SyntheticFlowControlProbe::Vertex20StaticFlowCount16)
            {
                AppendUInt32(shader, 0x0000001Cu); // ret from main
                AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l0
                AppendUInt32(shader, source(regLabel, 0));
                AppendUInt32(shader, 0x0000001Cu); // ret from subroutine
            }
        }
        else if (invalidMixedConstantAbsolute ==
                 SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAllAbsolute)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov o0, abs(c0)
            AppendUInt32(shader, destination(regTexCoordOut, 0));
            AppendUInt32(shader, source(regConst, 0, 0xE4u, 11u));
        }
        else
        {
            // m4x4 oPos, <r0|v0>, c0
            AppendUInt32(shader, 0x00000014u | (3u << 24));
            AppendUInt32(shader, destination(usesShaderModel3 ? regTexCoordOut : regRastOut, 0));
            AppendUInt32(shader,
                         readsSecondStream ||
                                 compositeWriteMaskProbe != SyntheticCompositeWriteMaskProbe::None
                             ? source(regTemp, 0)
                             : source(regInput, 0));
            AppendUInt32(
                shader,
                source(regConst, 0, 0xE4u,
                       invalidMixedConstantAbsolute ==
                               SyntheticInvalidShaderModel3MixedConstantAbsolute::VertexAllAbsolute
                           ? 11u
                           : 0u));
        }
        if (semanticDeclarationProbe == SyntheticSemanticDeclarationProbe::PackedDisjoint ||
            semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov o1, c240
            AppendUInt32(shader, destination(regTexCoordOut, 1));
            AppendUInt32(shader, source(regConst, 240u));
            if (semanticDeclarationProbe ==
                SyntheticSemanticDeclarationProbe::PixelPackedFromSeparateOutputs)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov o2, c241
                AppendUInt32(shader, destination(regTexCoordOut, 2));
                AppendUInt32(shader, source(regConst, 241u));
            }
        }
        if (probesCentroid)
        {
            const bool color = semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::PixelCentroidImplicitColor ||
                semanticDeclarationProbe ==
                    SyntheticSemanticDeclarationProbe::Pixel20CentroidImplicitColor;
            AppendUInt32(shader, 0x00000001u | (2u << 24));
            AppendUInt32(shader, destination(
                probesCentroid20 && color ? regAttrOut : regTexCoordOut,
                probesCentroid20 ? 0u : 1u));
            AppendUInt32(shader, source(regInput, 1));
        }
        if (usesPredication)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov o1, c242
            AppendUInt32(shader, destination(regTexCoordOut, 1));
            AppendUInt32(shader, source(regConst, 242u));
            AppendUInt32(shader, 0x0000005Eu | (1u << 16) | (3u << 24)); // setp_gt p0, c240, c241
            AppendUInt32(shader, destination(regPredicate, 0));
            AppendUInt32(shader, source(regConst, 240u));
            AppendUInt32(shader, source(regConst, 241u));
            AppendUInt32(shader, 0x10000001u | (3u << 24)); // (p0) mov o1, c243
            AppendUInt32(shader, destination(regTexCoordOut, 1));
            AppendUInt32(shader, source(regConst, 243u));
            AppendUInt32(shader, source(regPredicate, 0));
            AppendUInt32(shader, 0x10000001u | (3u << 24)); // (!p0.y) mov o1.y, c243.y
            AppendUInt32(shader, destination(regTexCoordOut, 1, 0x2u));
            AppendUInt32(shader, source(regConst, 243u, 0x55u));
            AppendUInt32(shader, source(regPredicate, 0, 0x55u, 13u));
        }
        if (forwardsTexCoord && !samplesTexture)
        {
            // mov oT0.xy(z), v1 -- the interpolated coordinate the sampling pixel shader reads.
            // plans/plan_fx.md FX-110: a cube or volume sampler needs three components, so the mask
            // follows the sampler the pixel shader declares rather than being fixed at .xy.
            AppendUInt32(shader, 0x00000001u | (2u << 24));
            AppendUInt32(shader, destination(regTexCoordOut, 0,
                                             forwardsFourComponents ? 0xFu
                                                                    : forwardsThreeComponents
                                                                          ? 0x7u
                                                                          : 0x3u));
            AppendUInt32(shader, source(regInput, 1));
        }
        if (forwardsLegacyTextureMatrix)
        {
            for (std::uint32_t index = 0; index < 4; ++index)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT#, c240+#
                AppendUInt32(shader, destination(regTexCoordOut, index));
                AppendUInt32(shader, source(regConst, 240u + index));
            }
        }
        if (forwardsLegacyTextureMatrix2)
        {
            for (std::uint32_t index = 1; index <= 2; ++index)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT#, c240+#
                AppendUInt32(shader, destination(regTexCoordOut, index));
                AppendUInt32(shader, source(regConst, 240u + index));
            }
        }
        if (forwardsLegacyTextureMatrix3Sample)
        {
            for (std::uint32_t index = 1; index <= 3; ++index)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT#, c240+#
                AppendUInt32(shader, destination(regTexCoordOut, index));
                AppendUInt32(shader, source(regConst, 240u + index));
            }
        }
        if (forwardsLegacyTextureMatrix3Specular ||
            forwardsLegacyTextureMatrix3VertexSpecular)
        {
            for (std::uint32_t index = 1; index <= 3; ++index)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT#, c240+#
                AppendUInt32(shader, destination(regTexCoordOut, index));
                AppendUInt32(shader, source(regConst, 240u + index));
            }
            if (forwardsLegacyTextureMatrix3VertexSpecular)
            {
                AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT3.w, v1.w
                AppendUInt32(shader, destination(regTexCoordOut, 3, 0x8u));
                AppendUInt32(shader, source(regInput, 1));
            }
        }
        if (forwardsLegacyDependentTexture)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT1, c241
            AppendUInt32(shader, destination(regTexCoordOut, 1));
            AppendUInt32(shader, source(regConst, 241u));
        }
        if (forwardsLegacyBumpTexture)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov oT1, c241
            AppendUInt32(shader, destination(regTexCoordOut, 1));
            AppendUInt32(shader, source(regConst, 241u));
        }
        if (relativeAddressingProbe ==
            SyntheticRelativeAddressingProbe::VertexConstantSubroutineInsideLoop)
        {
            AppendUInt32(shader, 0x0000001Cu); // ret from main
            AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l0
            AppendUInt32(shader, source(regLabel, 0));
            AppendUInt32(shader, 0x00000001u | (3u << 24)); // mov r10, c0[aL]
            AppendUInt32(shader, destination(regTemp, 10));
            AppendUInt32(shader, source(regConst, 0) | (1u << 13u));
            AppendUInt32(shader, source(regLoop, 0));
            AppendUInt32(shader, 0x0000001Cu); // ret from subroutine
        }
        if (shaderModel20InvalidDynamicFeature ==
            SyntheticInvalidShaderModel20DynamicFeature::VertexCallnzPredicate)
        {
            AppendUInt32(shader, 0x0000001Cu); // ret from main
            AppendUInt32(shader, 0x0000001Eu | (1u << 24)); // label l0
            AppendUInt32(shader, source(regLabel, 0));
            AppendUInt32(shader, 0x0000001Cu); // ret from subroutine
        }
        if (immediateConstantDefinitionProbe ==
            SyntheticImmediateConstantDefinitionProbe::VertexFloatDuplicate)
        {
            AppendUInt32(shader, 0x00000001u | (2u << 24)); // mov o1, c250
            AppendUInt32(shader, destination(regTexCoordOut, 1));
            AppendUInt32(shader, source(regConst, 250));
        }
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
        const std::uint32_t captionName = AppendEffectString(bytes, "Caption");
        // The Effect Framework stores a string object's own characters in the large-object table;
        // this is the initial value the reflected parameter reports before a game assigns one.
        const std::string captionInitial = "initial caption";

        // plans/plan_fx.md FX-084: a drawable fixture needs a shader pair on StatePass; the multi-stream
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
        // plans/plan_fx.md FX-110: the reflected object types follow the sampler dimension the shader
        // declares, so a cube fixture reports TextureCube/SamplerCube through the public API too.
        const std::uint32_t reflectedTextureType =
            options.samplerKind == SyntheticSamplerKind::SamplerCube
                ? EffectFormat::TypeTextureCube
                : options.samplerKind == SyntheticSamplerKind::Sampler3D
                      ? EffectFormat::TypeTexture3D
                      : EffectFormat::TypeTexture2D;
        const std::uint32_t reflectedSamplerType =
            options.samplerKind == SyntheticSamplerKind::SamplerCube
                ? EffectFormat::TypeSamplerCube
                : options.samplerKind == SyntheticSamplerKind::Sampler3D
                      ? EffectFormat::TypeSampler3D
                      : EffectFormat::TypeSampler2D;
        const std::uint32_t textureType =
            AppendObjectType(bytes, reflectedTextureType, textureName, empty);
        const std::uint32_t samplerType =
            AppendObjectType(bytes, reflectedSamplerType, samplerName, empty);
        const std::uint32_t stateTextureType =
            AppendObjectType(bytes, reflectedTextureType, empty, empty);
        const std::uint32_t pixelShaderType =
            AppendObjectType(bytes, EffectFormat::TypePixelShader, empty, empty);
        const std::uint32_t vertexShaderType =
            AppendObjectType(bytes, EffectFormat::TypeVertexShader, empty, empty);
        const std::uint32_t streamMixType = AppendNumericType(
            bytes, EffectFormat::TypeFloat, EffectFormat::ClassVector,
            streamMixName, empty, 0, 4, 1);
        const std::uint32_t captionType =
            AppendObjectType(bytes, EffectFormat::TypeString, captionName, empty);

        // Object indices: 0 stays unused (the Effect Framework reserves it); the rest are packed
        // in emission order so no index is ever declared without a record behind it.
        constexpr std::uint32_t textureObjectIndex = 1;
        const std::uint32_t pixelShaderObjectIndex = options.includeSampler ? 2u : 1u;
        const std::uint32_t vertexShaderObjectIndex = pixelShaderObjectIndex + 1u;
        // plans/plan_fx.md FX-094: a drawable fixture carries a SECOND pixel shader, `oC0 = Tint.yzxw`,
        // and gives it to pass P0 alone. Before this every pass of a drawable fixture bound the
        // identical program pair, so a backend that applied pass 0 where the contract asked for
        // pass 1 -- or fell back to "the first pass" when it could not resolve one -- rendered
        // exactly the same pixels and passed. One channel rotation is enough to tell them apart
        // and costs one extra object.
        const std::uint32_t altPixelShaderObjectIndex = vertexShaderObjectIndex + 1u;
        std::uint32_t objectCount = 0u;
        if (includeProgram)
        {
            objectCount = pixelShaderObjectIndex + 1u;
            if (includeVertexShader) objectCount = vertexShaderObjectIndex + 1u;
            if (options.includeDrawableProgram) objectCount = altPixelShaderObjectIndex + 1u;
        }
        else if (options.includeStringParameter)
        {
            objectCount = 1u;  // index 0 stays reserved; the string takes index 1.
        }
        const std::uint32_t captionObjectIndex = options.includeStringParameter ? objectCount : 0u;
        if (options.includeStringParameter) objectCount += 1u;

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
        const std::uint32_t captionValue =
            options.includeStringParameter ? AppendValueBits(bytes, captionObjectIndex) : 0u;
        std::vector<std::uint32_t> stateValueOffsets;
        stateValueOffsets.reserve(renderStates.size());
        for (const auto& state : renderStates)
            stateValueOffsets.push_back(AppendValueBits(bytes, state.valueBits));

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
            // Pass ordinal 0 is technique 0's "P0"; the drawable fixture points it at the
            // alternate program instead, leaving StatePass and P1 on the primary one so every
            // existing draw expectation is untouched.
            if (options.includeDrawableProgram)
                pixelShaderValues[0] = AppendValueBits(bytes, altPixelShaderObjectIndex);
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
                                (includeStreamMix ? 1u : 0u) +
                                (options.includeStringParameter ? 1u : 0u)); // parameters
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

        if (options.includeStringParameter)
        {
            AppendUInt32(bytes, captionType);
            AppendUInt32(bytes, captionValue);
            AppendUInt32(bytes, 0); // flags
            AppendUInt32(bytes, 0); // annotations
        }

        AppendUInt32(bytes, firstTechnique);
        AppendUInt32(bytes, 1); // annotations
        AppendUInt32(bytes, 2); // passes
        AppendUInt32(bytes, qualityType);
        AppendUInt32(bytes, qualityValue);

        // plans/plan_fx.md FX-084: a drawable fixture binds the same program in every pass, so any pass a
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
            AppendUInt32(bytes, renderStates[i].target);
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

        // A string object carries its characters inline in the small-object table, exactly like the
        // sampler's mapped-texture name below: index, byte length, bytes, padded to a dword.
        const auto appendStringObject = [&bytes](std::uint32_t index, const std::string& value) {
            AppendUInt32(bytes, index);
            AppendUInt32(bytes, static_cast<std::uint32_t>(value.size() + 1));
            bytes.insert(bytes.end(), value.begin(), value.end());
            bytes.push_back(0);
            while ((bytes.size() & 3u) != 0) bytes.push_back(0);
        };

        if (!includeProgram)
        {
            AppendUInt32(bytes, options.includeStringParameter ? 1u : 0u); // small objects
            AppendUInt32(bytes, 0); // large objects
            if (options.includeStringParameter)
                appendStringObject(captionObjectIndex, captionInitial);
            return bytes;
        }

        AppendUInt32(bytes, 1u + (options.includeSampler ? 1u : 0u) +
                                (includeVertexShader ? 1u : 0u) +
                                (options.includeDrawableProgram ? 1u : 0u) +
                                (options.includeStringParameter ? 1u : 0u)); // small objects
        AppendUInt32(bytes, 0); // large objects

        if (options.includeStringParameter)
            appendStringObject(captionObjectIndex, captionInitial);

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
            options.samplerRegister, options.breakShaderSymbolBinding,
            options.includeSampler && !options.vertexShaderSamplesTexture &&
                options.textureInstructionProfileProbe !=
                    SyntheticTextureInstructionProfileProbe::Vertex20Texldl &&
                options.textureInstructionProfileProbe !=
                    SyntheticTextureInstructionProfileProbe::Vertex2xTexldl &&
                options.texldlDestinationModifierProbe !=
                    SyntheticTexldlDestinationModifierProbe::VertexSaturate,
            options.pixelShaderSamplesTexture, /*swizzleTint=*/false, options.samplerKind,
            options.pixelShaderWritesMrt, options.pixelShaderUsesLoop,
            options.pixelLoopCount, options.pixelLoopInitial, options.pixelLoopStep,
            options.pixelShaderUsesSubroutine, options.pixelShaderUsesDerivatives,
            options.pixelShaderUsesTextureGradients,
            options.pixelShaderUsesRasterInputs, options.shadersUsePredication,
            options.pixelShaderUsesPredicatedTexkill,
            options.pixelShaderUsesShaderModel14TexldDz,
            options.pixelShaderUsesShaderModel14TexldDw,
            options.pixelShaderUsesShaderModel14TexcrdDw,
            options.shaderModel14TextureOperandProbe,
            options.pixelShaderUsesShaderModel14TextureLoad,
            options.pixelShaderUsesShaderModel14Phase,
            options.shaderModel14PhaseProbe,
            options.pixel1OutputLivenessProbe,
            options.pixelShaderUsesLegacyTextureMatrix,
            options.pixelShaderLegacyTextureRemap,
            options.pixelShaderUsesLegacyTextureMatrix2,
            options.pixelShaderUsesLegacyTextureMatrix3Sample,
            options.pixelShaderUsesLegacyTextureMatrix3Specular,
            options.pixelShaderUsesLegacyTextureMatrix3VertexSpecular,
            options.pixelShaderLegacyDepthOutput,
            options.pixelShaderLegacyDepthZeroDivisor,
            options.pixelShaderLegacyDependentTexture,
            options.pixelShaderLegacyBumpEnvironment,
            options.pixelShaderBemOperandProbe,
            options.pixelShaderTextureCoordinateSelectorProbe,
            options.pixelShaderTemporaryTextureSelectorProbe,
            options.pixelShaderDuplicatesShaderModel14Phase,
            options.pixelShaderUsesRelativeTextureCoordinate,
            options.pixelShaderUsesDependentTemporaryTextureCoordinate,
            options.pixelShaderSwizzlesSampleResult,
            options.pixelShaderUsesSignedLog,
            options.pixelShaderUsesNrmWriteMask,
            options.pixelShaderReadsUninitializedDestination,
            options.temporaryInitializationProbe,
            options.pixelShaderTexkillReadsPartialTemporary,
            options.pixelShaderTexkillReadsSplitTemporary,
            options.pixelShaderTexkillOperandProbe,
            options.pixelShaderUsesInvalidSgn,
            options.pixelShaderUsesInvalidExpp,
            options.pixelShaderUsesInvalidLogp,
            options.pixelShaderUsesInvalidLit,
            options.pixelShaderUsesInvalidSlt,
            options.pixelShaderUsesInvalidSge,
            options.pixelShaderUsesInvalidExpSwizzle,
            options.pixelShaderModel1InvalidOpcode,
            options.pixelShaderModel20InvalidOpcode,
            options.shaderModel20InvalidDynamicFeature,
            options.pixelShaderModel2xUsesInvalidLoop,
            options.pixelShaderInvalidMatrixOperands,
            options.invalidPreShaderModel3AbsoluteSource,
            options.invalidShaderModel3MixedConstantAbsolute,
            options.temporaryRegisterProbe,
            options.inputRegisterProbe,
            options.outputRegisterProbe,
            options.constantControlRegisterProbe,
            options.pixel20InstructionSlotProbe,
            options.pixel1InstructionSlotProbe,
            options.pixel1DestinationMaskProbe,
            options.pixel1CoissueProbe,
            options.destinationRegisterAccessProbe,
            options.outputRegisterSourceProbe,
            options.samplerRegisterSourceProbe,
            options.missingSamplerDeclarationProbe,
            options.immediateConstantDefinitionProbe,
            options.duplicateDeclarationProbe,
            options.typedControlSourceProbe,
            options.specialControlSourceProbe,
            options.relativeAddressingProbe,
            options.miscellaneousInputProbe,
            options.semanticDeclarationProbe,
            options.flowControlProbe,
            options.callGraphProbe,
            options.textureSourceModifierProbe,
            options.texld20OperandProbe,
            options.texldDestinationModifierProbe,
            options.textureInstructionProfileProbe,
            options.texlddOperandProbe,
            options.texldlDestinationModifierProbe,
            options.componentwiseInitializationProbe,
            options.scalarInitializationProbe,
            options.fixedVectorInitializationProbe,
            options.matrixInitializationProbe,
            options.specialVectorInitializationProbe);
        AppendUInt32(bytes, pixelShaderObjectIndex);
        AppendUInt32(bytes, static_cast<std::uint32_t>(shader.size()));
        bytes.insert(bytes.end(), shader.begin(), shader.end());

        if (includeVertexShader)
        {
            const std::vector<std::uint8_t> vertexShader = BuildSyntheticVertexShader(
                options.vertexShaderReadsSecondStream,
                options.vertexShaderSamplesTexture || options.pixelShaderSamplesTexture ||
                    options.pixelShaderUsesTextureGradients ||
                    (options.textureInstructionProfileProbe >=
                         SyntheticTextureInstructionProfileProbe::Pixel20Texldd &&
                     options.textureInstructionProfileProbe <=
                         SyntheticTextureInstructionProfileProbe::Pixel2xTexldl) ||
                    options.textureInstructionProfileProbe ==
                        SyntheticTextureInstructionProfileProbe::Vertex20Texldl ||
                    options.textureInstructionProfileProbe ==
                        SyntheticTextureInstructionProfileProbe::Vertex2xTexldl ||
                    (options.textureSourceModifierProbe >=
                         SyntheticTextureSourceModifierProbe::PixelTexlddCoordinate &&
                     options.textureSourceModifierProbe <=
                         SyntheticTextureSourceModifierProbe::PixelTexldlSampler) ||
                    options.texld20OperandProbe != SyntheticTexld20OperandProbe::None ||
                    options.texldDestinationModifierProbe !=
                        SyntheticTexldDestinationModifierProbe::None ||
                    options.texlddOperandProbe != SyntheticTexlddOperandProbe::None ||
                    options.texldlDestinationModifierProbe !=
                        SyntheticTexldlDestinationModifierProbe::None ||
                    options.pixelShaderUsesDerivatives ||
                    options.pixelShaderUsesRelativeTextureCoordinate ||
                    options.pixelShaderUsesDependentTemporaryTextureCoordinate ||
                    options.pixelShaderUsesShaderModel14TexldDz ||
                    options.pixelShaderUsesShaderModel14TexldDw ||
                    options.pixelShaderUsesShaderModel14TexcrdDw ||
                    options.pixelShaderUsesShaderModel14TextureLoad ||
                    options.pixelShaderUsesShaderModel14Phase ||
                    options.pixelShaderDuplicatesShaderModel14Phase ||
                    options.shaderModel14PhaseProbe != SyntheticShaderModel14PhaseProbe::None ||
                    options.pixelShaderLegacyTextureRemap !=
                        SyntheticLegacyTextureRemap::None ||
                    options.pixelShaderLegacyDependentTexture !=
                        SyntheticLegacyDependentTexture::None ||
                    (options.pixelShaderLegacyBumpEnvironment !=
                         SyntheticLegacyBumpEnvironment::None &&
                     options.pixelShaderLegacyBumpEnvironment !=
                         SyntheticLegacyBumpEnvironment::Arithmetic) ||
                    options.pixelShaderBemOperandProbe != SyntheticBemOperandProbe::None ||
                    options.pixelShaderTextureCoordinateSelectorProbe !=
                        SyntheticTextureCoordinateSelectorProbe::None ||
                    options.pixelShaderTemporaryTextureSelectorProbe !=
                        SyntheticTemporaryTextureSelectorProbe::None ||
                    options.pixelShaderUsesLegacyTextureMatrix2 ||
                    options.pixelShaderUsesLegacyTextureMatrix3Sample ||
                    options.pixelShaderUsesLegacyTextureMatrix3Specular ||
                    options.pixelShaderUsesLegacyTextureMatrix3VertexSpecular ||
                    options.pixelShaderLegacyDepthOutput ==
                        SyntheticLegacyDepthOutput::TextureMatrix2,
                options.samplerKind != SyntheticSamplerKind::Sampler2D ||
                    options.pixelShaderLegacyDependentTexture !=
                        SyntheticLegacyDependentTexture::None ||
                    (options.pixelShaderLegacyBumpEnvironment !=
                         SyntheticLegacyBumpEnvironment::None &&
                     options.pixelShaderLegacyBumpEnvironment !=
                         SyntheticLegacyBumpEnvironment::Arithmetic) ||
                    options.pixelShaderUsesLegacyTextureMatrix2 ||
                    options.pixelShaderUsesLegacyTextureMatrix3Sample ||
                    options.pixelShaderUsesLegacyTextureMatrix3Specular ||
                    options.pixelShaderUsesLegacyTextureMatrix3VertexSpecular ||
                    options.pixelShaderLegacyDepthOutput ==
                        SyntheticLegacyDepthOutput::TextureMatrix2,
                options.shadersUsePredication,
                options.vertexShaderSamplesTexture ||
                    options.pixelShaderUsesTextureGradients ||
                    (options.textureInstructionProfileProbe >=
                         SyntheticTextureInstructionProfileProbe::Pixel20Texldd &&
                     options.textureInstructionProfileProbe <=
                         SyntheticTextureInstructionProfileProbe::Pixel2xTexldl) ||
                    (options.textureSourceModifierProbe >=
                         SyntheticTextureSourceModifierProbe::PixelTexlddCoordinate &&
                     options.textureSourceModifierProbe <=
                         SyntheticTextureSourceModifierProbe::PixelTexldlSampler) ||
                    options.texlddOperandProbe != SyntheticTexlddOperandProbe::None ||
                    options.texldDestinationModifierProbe !=
                        SyntheticTexldDestinationModifierProbe::None ||
                    options.texldlDestinationModifierProbe !=
                        SyntheticTexldlDestinationModifierProbe::None ||
                    options.pixelShaderUsesShaderModel14TexldDz ||
                    options.pixelShaderUsesShaderModel14TexldDw ||
                    options.pixelShaderUsesShaderModel14TexcrdDw ||
                    options.pixelShaderUsesShaderModel14TextureLoad ||
                    options.pixelShaderUsesShaderModel14Phase ||
                    options.pixelShaderDuplicatesShaderModel14Phase ||
                    options.shaderModel14PhaseProbe != SyntheticShaderModel14PhaseProbe::None ||
                    options.pixelShaderLegacyTextureRemap !=
                        SyntheticLegacyTextureRemap::None,
                options.pixelShaderUsesLegacyTextureMatrix,
                options.pixelShaderUsesLegacyTextureMatrix2 ||
                    options.pixelShaderLegacyDepthOutput ==
                        SyntheticLegacyDepthOutput::TextureMatrix2,
                options.pixelShaderUsesLegacyTextureMatrix3Sample,
                options.pixelShaderUsesLegacyTextureMatrix3Specular,
                options.pixelShaderUsesLegacyTextureMatrix3VertexSpecular,
                options.pixelShaderLegacyDepthZeroDivisor,
                options.pixelShaderLegacyDependentTexture !=
                    SyntheticLegacyDependentTexture::None,
                options.pixelShaderLegacyBumpEnvironment !=
                        SyntheticLegacyBumpEnvironment::None &&
                    options.pixelShaderLegacyBumpEnvironment !=
                        SyntheticLegacyBumpEnvironment::Arithmetic,
                options.vertexShaderSamplesTexture, options.samplerRegister,
                options.samplerKind, options.vertexShaderSwizzlesSampleResult,
                options.vertexShaderUsesShaderModel11Input,
                options.vertexShaderUsesShaderModel11ExtendedInputs,
                options.vertexShaderUsesLegacyExpp,
                options.vertexShaderSgnScratchOperands,
                options.vertexShaderUsesInvalidExppSwizzle,
                options.vertexShaderUsesInvalidExpSwizzle,
                options.temporaryInitializationProbe,
                options.vertexShaderModel1InvalidOpcode,
                options.shaderModel20InvalidDynamicFeature,
                options.invalidPreShaderModel3AbsoluteSource,
                options.invalidShaderModel3MixedConstantAbsolute,
                options.temporaryRegisterProbe,
                options.inputRegisterProbe,
                options.outputRegisterProbe,
                options.constantControlRegisterProbe,
                options.destinationRegisterAccessProbe,
                options.outputRegisterSourceProbe,
                options.addressRegisterAccessProbe,
                options.samplerRegisterSourceProbe,
                options.missingSamplerDeclarationProbe,
                options.immediateConstantDefinitionProbe,
                options.duplicateDeclarationProbe,
                options.typedControlSourceProbe,
                options.specialControlSourceProbe,
                options.vertexInstructionSlotProbe,
                options.relativeAddressingProbe,
                options.semanticDeclarationProbe,
                options.vertexShaderCompositeWriteMaskProbe,
                options.flowControlProbe,
                options.callGraphProbe,
                options.textureSourceModifierProbe,
                options.textureInstructionProfileProbe,
                options.texldlDestinationModifierProbe,
                options.componentwiseInitializationProbe,
                options.scalarInitializationProbe,
                options.fixedVectorInitializationProbe,
                options.matrixInitializationProbe,
                options.specialVectorInitializationProbe);
            AppendUInt32(bytes, vertexShaderObjectIndex);
            AppendUInt32(bytes, static_cast<std::uint32_t>(vertexShader.size()));
            bytes.insert(bytes.end(), vertexShader.begin(), vertexShader.end());
        }

        if (options.includeDrawableProgram)
        {
            // The alternate program declares NO sampler even in a sampling fixture: pass P0 must
            // stay drawable on a backend that requires every reflected sampler to have a texture
            // bound, and its whole job is to differ from the primary program's output colour.
            const std::vector<std::uint8_t> alternate = BuildSyntheticPixelShader(
                options.samplerRegister, /*breakSymbolBinding=*/false, /*includeSampler=*/false,
                /*samplesTexture=*/false, /*swizzleTint=*/true);
            AppendUInt32(bytes, altPixelShaderObjectIndex);
            AppendUInt32(bytes, static_cast<std::uint32_t>(alternate.size()));
            bytes.insert(bytes.end(), alternate.begin(), alternate.end());
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
     * @brief plans/plan_fx.md FX-084: the conformance fixture with a real, drawable program pair.
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

    /**
     * @brief Builds a drawable fixture that emits four distinct COLOR outputs.
     *
     * COLOR0 is Tint, while COLOR1-3 use different RGB swizzles. This makes attachment routing,
     * per-target channel masks, blending and multisample storage independently observable.
     *
     * @return Complete Effect Framework bytecode with one vertex program and four pixel outputs.
     */
    inline std::vector<std::uint8_t> BuildSyntheticMrtEffect()
    {
        SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.pixelShaderWritesMrt = true;
        return BuildSyntheticEffect(options);
    }

    /**
     * @brief plans/plan_fx.md FX-093: a drawable fixture whose pixel shader actually SAMPLES a texture.
     *
     * `StatePass` (technique 0, pass 1) binds `oPos = mul(POSITION0, Transform)` with TEXCOORD0
     * forwarded, and `oC0 = tex2D(FxSampler, TEXCOORD0) * Tint`. With `Tint` at (1,1,1,1) the
     * read-back pixel IS the texel the sampler selected, so the texture binding, addressing mode,
     * filter and LOD clamp a backend applied are all visible in the result rather than only on
     * `GraphicsDevice.SamplerStates`.
     *
     * Pass `P0` (technique 0, pass 0) keeps the non-sampling alternate program, so a fixture built
     * here is still pass-discriminating.
     *
     * @param samplerStates The `sampler_state` assignments the pass declares, in order.
     * @param samplerRegister The sampler register the shader declares.
     * @param samplerKind Which sampler dimension the shader declares (plans/plan_fx.md FX-110).
     * @return The complete effect bytecode.
     */
    /**
     * @brief plans/plan_fx.md FX-104: the conformance fixture plus a reflected String parameter.
     *
     * `Caption` is an Effect Framework string object with an initial value, which is what
     * `EffectParameter.SetValue(string)` and `GetValueString()` are actually specified against.
     * Opt-in, so the reflection contract's parameter counts are untouched.
     *
     * @return The complete effect bytecode.
     */
    inline std::vector<std::uint8_t> BuildSyntheticStringParameterEffect()
    {
        SyntheticEffectOptions options;
        options.includeStringParameter = true;
        return BuildSyntheticEffect(options);
    }

    inline std::vector<std::uint8_t> BuildSyntheticSamplingEffect(
        const std::vector<SyntheticSamplerState>& samplerStates,
        std::uint32_t samplerRegister = 0,
        SyntheticSamplerKind samplerKind = SyntheticSamplerKind::Sampler2D)
    {
        SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.pixelShaderSamplesTexture = true;
        options.samplerStates = samplerStates;
        options.samplerRegister = samplerRegister;
        options.samplerKind = samplerKind;
        return BuildSyntheticEffect(options);
    }

    /**
     * @brief Builds a drawable Effect whose vertex shader samples an XNA HiDef texture slot.
     *
     * The Shader Model 3 vertex program executes `texldl r0, v1, s#` and publishes that sampled
     * Vector4 directly as clip-space POSITION0. The pixel shader writes Tint unchanged. A four-
     * texel Vector4 texture can therefore describe a full quad and makes stage/slot selection,
     * sampler addressing and explicit mip selection visible as target coverage.
     *
     * @param samplerStates Sampler assignments applied by the vertex-shader pass.
     * @param samplerRegister Vertex sampler register in XNA's zero-through-three range.
     * @param samplerKind Which vertex sampler dimension the shader declares.
     * @param swizzlesSampleResult Whether TEXLDL applies a BGRA source swizzle after sampling.
     * @return Complete Effect Framework bytecode.
     */
    inline std::vector<std::uint8_t> BuildSyntheticVertexSamplingEffect(
        const std::vector<SyntheticSamplerState>& samplerStates,
        std::uint32_t samplerRegister = 0,
        SyntheticSamplerKind samplerKind = SyntheticSamplerKind::Sampler2D,
        bool swizzlesSampleResult = false)
    {
        SyntheticEffectOptions options;
        options.includeDrawableProgram = true;
        options.includeSampler = true;
        options.vertexShaderSamplesTexture = true;
        options.samplerStates = samplerStates;
        options.samplerRegister = samplerRegister;
        options.samplerKind = samplerKind;
        options.vertexShaderSwizzlesSampleResult = swizzlesSampleResult;
        return BuildSyntheticEffect(options);
    }

    /**
     * @brief Builds a sampling Effect whose passes never assign a vertex shader.
     *
     * This is the classic SpriteBatch custom-effect shape: XNA applies its internal SpriteEffect
     * first, then a pixel-only user pass inherits the sprite vertex stage and MatrixTransform.
     *
     * @param samplerStates Sampler assignments applied by the pixel-shader pass.
     * @return Complete Effect Framework bytecode with a sampling pixel shader and no vertex shader.
     */
    inline std::vector<std::uint8_t> BuildSyntheticPixelOnlySamplingEffect(
        const std::vector<SyntheticSamplerState>& samplerStates)
    {
        SyntheticEffectOptions options;
        options.includeSampler = true;
        options.pixelShaderSamplesTexture = true;
        options.samplerStates = samplerStates;
        return BuildSyntheticEffect(options);
    }

}
