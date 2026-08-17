// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>

/**
 * @file
 * @brief Direct3D 9 Effect Framework format constants used to build compiled-effect fixtures.
 *
 * plan_fx.md FX-060: the shared backend conformance suite has to build its own fixtures, and a
 * fixture is a byte layout of the legacy Effect Framework format -- not of any particular
 * renderer's parser. These are therefore the format's own values (the `D3DRENDERSTATETYPE`,
 * `D3DSAMPLERSTATETYPE`, `D3DXPARAMETER_*` and related Direct3D 9 enumerations), spelled once here
 * so a backend's test file needs no parser headers to construct a fixture.
 *
 * `Fna3dCompiledEffectTests.cpp` static-asserts these against the pinned MojoShader enumerations,
 * which keeps the table honest without making every backend depend on MojoShader.
 */
namespace CNA::TestSupport::EffectFormat
{
    /** @brief `D3DXPARAMETER_CLASS` values that appear in a compiled parameter's type record. */
    enum ParameterClass : std::uint32_t
    {
        ClassScalar = 0,
        ClassVector = 1,
        ClassMatrixRows = 2,
        ClassMatrixColumns = 3,
        ClassObject = 4,
        ClassStruct = 5,
    };

    /** @brief `D3DXPARAMETER_TYPE` values that appear in a compiled parameter's type record. */
    enum ParameterType : std::uint32_t
    {
        TypeVoid = 0,
        TypeBool = 1,
        TypeInt = 2,
        TypeFloat = 3,
        TypeString = 4,
        TypeTexture = 5,
        TypeTexture2D = 7,
        TypeSampler = 10,
        TypeSampler2D = 12,
        TypePixelShader = 15,
        TypeVertexShader = 16,
    };

    /** @brief `D3DRENDERSTATETYPE` tokens a compiled pass can assign. */
    enum RenderState : std::uint32_t
    {
        RsZEnable = 0,
        RsFillMode = 1,
        RsZWriteEnable = 3,
        RsSrcBlend = 6,
        RsDestBlend = 7,
        RsCullMode = 8,
        RsZFunc = 9,
        RsAlphaBlendEnable = 13,
        RsStencilEnable = 22,
        RsStencilFail = 23,
        RsStencilZFail = 24,
        RsStencilPass = 25,
        RsStencilFunc = 26,
        RsStencilRef = 27,
        RsStencilMask = 28,
        RsStencilWriteMask = 29,
        RsMultiSampleAntiAlias = 67,
        RsMultiSampleMask = 68,
        RsColorWriteEnable = 73,
        RsBlendOp = 75,
        RsScissorTestEnable = 78,
        RsSlopeScaleDepthBias = 79,
        RsTwoSidedStencilMode = 88,
        RsCcwStencilFail = 89,
        RsCcwStencilZFail = 90,
        RsCcwStencilPass = 91,
        RsCcwStencilFunc = 92,
        RsColorWriteEnable1 = 93,
        RsColorWriteEnable2 = 94,
        RsColorWriteEnable3 = 95,
        RsBlendFactor = 96,
        RsDepthBias = 98,
        RsSeparateAlphaBlendEnable = 99,
        RsSrcBlendAlpha = 100,
        RsDestBlendAlpha = 101,
        RsBlendOpAlpha = 102,
        RsVertexShader = 146,
        RsPixelShader = 147,
        /// Undocumented token the legacy Effect compiler emits as sampler metadata; FNA ignores it.
        RsSetSampler = 178,
        /// Not a real token: used by the conformance suite to prove the unknown-token policy.
        RsUnknownForTesting = 177,
    };

    /** @brief `D3DZBUFFERTYPE` values. */
    enum ZBufferType : std::uint32_t { ZBufferFalse = 0, ZBufferTrue = 1 };

    /** @brief `D3DFILLMODE` values reachable from XNA's FillMode. */
    enum FillMode : std::uint32_t { FillWireframe = 2, FillSolid = 3 };

    /** @brief `D3DBLEND` values. */
    enum BlendMode : std::uint32_t
    {
        BlendZero = 1,
        BlendOne = 2,
        BlendSrcColor = 3,
        BlendInvSrcColor = 4,
        BlendSrcAlpha = 5,
        BlendInvSrcAlpha = 6,
        BlendDestAlpha = 7,
        BlendInvDestAlpha = 8,
        BlendDestColor = 9,
        BlendInvDestColor = 10,
        BlendSrcAlphaSat = 11,
        BlendBlendFactor = 14,
        BlendInvBlendFactor = 15,
    };

    /** @brief `D3DBLENDOP` values. */
    enum BlendOperation : std::uint32_t
    {
        BlendOpAdd = 1,
        BlendOpSubtract = 2,
        BlendOpRevSubtract = 3,
        BlendOpMin = 4,
        BlendOpMax = 5,
    };

    /** @brief `D3DCMPFUNC` values. */
    enum CompareFunc : std::uint32_t
    {
        CmpNever = 1,
        CmpLess = 2,
        CmpEqual = 3,
        CmpLessEqual = 4,
        CmpGreater = 5,
        CmpNotEqual = 6,
        CmpGreaterEqual = 7,
        CmpAlways = 8,
    };

    /** @brief `D3DSTENCILOP` values. */
    enum StencilOperation : std::uint32_t
    {
        StencilKeep = 1,
        StencilZero = 2,
        StencilReplace = 3,
        StencilIncrSat = 4,
        StencilDecrSat = 5,
        StencilInvert = 6,
        StencilIncr = 7,
        StencilDecr = 8,
    };

    /** @brief `D3DCULL` values. */
    enum CullMode : std::uint32_t { CullNone = 1, CullCw = 2, CullCcw = 3 };

    /** @brief `D3DSAMPLERSTATETYPE` tokens a compiled sampler can assign. */
    enum SamplerState : std::uint32_t
    {
        SampTexture = 4,
        SampAddressU = 5,
        SampAddressV = 6,
        SampAddressW = 7,
        SampMagFilter = 9,
        SampMinFilter = 10,
        SampMipFilter = 11,
        SampMipMapLodBias = 12,
        SampMaxMipLevel = 13,
        SampMaxAnisotropy = 14,
        /// Has no XNA 4.0 SamplerState representation; used to prove the unknown-token policy.
        SampSrgbTexture = 15,
    };

    /** @brief `D3DTEXTUREADDRESS` values. */
    enum TextureAddress : std::uint32_t
    {
        AddressWrap = 1,
        AddressMirror = 2,
        AddressClamp = 3,
        /// Not representable by XNA 4.0 TextureAddressMode.
        AddressBorder = 4,
    };

    /** @brief `D3DTEXTUREFILTERTYPE` values. */
    enum TextureFilterType : std::uint32_t
    {
        FilterNone = 0,
        FilterPoint = 1,
        FilterLinear = 2,
        FilterAnisotropic = 3,
    };
}
