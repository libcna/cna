// SPDX-License-Identifier: MS-PL
//
// FNA3D-17: GTest coverage for the XNA-ordinal to FNA3D-enumerator bridge and for
// the stride-keyed vertex layout table.
//
// Most FNA3D ordinals match Microsoft XNA. BlendFunction::Min and Max are reversed in FNA3D,
// so the bridge translates those two values. These tests also pin that an ordinal outside the contract is
// REJECTED rather than cast into an undefined enumerator, and that the layout table a
// declaration-less internal route falls back on describes the bytes CNA actually stages.
#include <gtest/gtest.h>

// RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_FNA3D is defined project-wide.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dVertexLayouts.hpp"

#include <array>
#include <limits>
#include <utility>
#include <vector>
#include <stdexcept>
#include <string>

namespace
{
    using namespace CNA::Internal::Renderers::Fna3d;
    namespace Xna = Microsoft::Xna::Framework::Graphics;
}

TEST(Fna3dEnumMappingTests, BlendOrdinalsMapOntoTheMatchingFna3dValues)
{
    EXPECT_EQ(ToFna3dBlend(static_cast<int>(Xna::Blend::One)), FNA3D_BLEND_ONE);
    EXPECT_EQ(ToFna3dBlend(static_cast<int>(Xna::Blend::InverseSourceAlpha)),
              FNA3D_BLEND_INVERSESOURCEALPHA);
    EXPECT_EQ(ToFna3dBlend(static_cast<int>(Xna::Blend::SourceAlphaSaturation)),
              FNA3D_BLEND_SOURCEALPHASATURATION);
}

TEST(Fna3dEnumMappingTests, MicrosoftMinAndMaxOrdinalsMapToFna3dOperations)
{
    EXPECT_EQ(ToFna3dBlendFunction(static_cast<int>(Xna::BlendFunction::Min)),
              FNA3D_BLENDFUNCTION_MIN);
    EXPECT_EQ(ToFna3dBlendFunction(static_cast<int>(Xna::BlendFunction::Max)),
              FNA3D_BLENDFUNCTION_MAX);
}

TEST(Fna3dEnumMappingTests, OutOfRangeOrdinalsAreRejectedWithADiagnostic)
{
    EXPECT_THROW((void) ToFna3dBlend(-1), std::runtime_error);
    EXPECT_THROW((void) ToFna3dBlend(13), std::runtime_error);
    EXPECT_THROW((void) ToFna3dBlendFunction(5), std::runtime_error);
    EXPECT_THROW((void) ToFna3dCompareFunction(8), std::runtime_error);
    EXPECT_THROW((void) ToFna3dStencilOperation(8), std::runtime_error);
    EXPECT_THROW((void) ToFna3dCullMode(3), std::runtime_error);
    EXPECT_THROW((void) ToFna3dFillMode(2), std::runtime_error);
    EXPECT_THROW((void) ToFna3dTextureAddressMode(3), std::runtime_error);
    EXPECT_THROW((void) ToFna3dTextureFilter(9), std::runtime_error);
    EXPECT_THROW((void) ToFna3dDepthFormat(4), std::runtime_error);
    EXPECT_THROW((void) ToFna3dSurfaceFormat(27), std::runtime_error);
    EXPECT_THROW((void) ToFna3dColorWriteChannels(16), std::runtime_error);
}

TEST(Fna3dEnumMappingTests, TheRejectionMessageNamesTheStateAndTheOrdinal)
{
    try
    {
        (void) ToFna3dCullMode(42);
        FAIL() << "an out-of-range CullMode ordinal must be rejected";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("CullMode"), std::string::npos);
        EXPECT_NE(message.find("42"), std::string::npos);
    }
}

TEST(Fna3dEnumMappingTests, EveryBlendMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::Blend::One, FNA3D_BLEND_ONE},
        std::pair{Xna::Blend::Zero, FNA3D_BLEND_ZERO},
        std::pair{Xna::Blend::SourceColor, FNA3D_BLEND_SOURCECOLOR},
        std::pair{Xna::Blend::InverseSourceColor, FNA3D_BLEND_INVERSESOURCECOLOR},
        std::pair{Xna::Blend::SourceAlpha, FNA3D_BLEND_SOURCEALPHA},
        std::pair{Xna::Blend::InverseSourceAlpha, FNA3D_BLEND_INVERSESOURCEALPHA},
        std::pair{Xna::Blend::DestinationColor, FNA3D_BLEND_DESTINATIONCOLOR},
        std::pair{Xna::Blend::InverseDestinationColor, FNA3D_BLEND_INVERSEDESTINATIONCOLOR},
        std::pair{Xna::Blend::DestinationAlpha, FNA3D_BLEND_DESTINATIONALPHA},
        std::pair{Xna::Blend::InverseDestinationAlpha, FNA3D_BLEND_INVERSEDESTINATIONALPHA},
        std::pair{Xna::Blend::BlendFactor, FNA3D_BLEND_BLENDFACTOR},
        std::pair{Xna::Blend::InverseBlendFactor, FNA3D_BLEND_INVERSEBLENDFACTOR},
        std::pair{Xna::Blend::SourceAlphaSaturation, FNA3D_BLEND_SOURCEALPHASATURATION},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dBlend(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryTextureFilterMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::TextureFilter::Linear, FNA3D_TEXTUREFILTER_LINEAR},
        std::pair{Xna::TextureFilter::Point, FNA3D_TEXTUREFILTER_POINT},
        std::pair{Xna::TextureFilter::Anisotropic, FNA3D_TEXTUREFILTER_ANISOTROPIC},
        std::pair{Xna::TextureFilter::LinearMipPoint, FNA3D_TEXTUREFILTER_LINEAR_MIPPOINT},
        std::pair{Xna::TextureFilter::PointMipLinear, FNA3D_TEXTUREFILTER_POINT_MIPLINEAR},
        std::pair{Xna::TextureFilter::MinLinearMagPointMipLinear, FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPLINEAR},
        std::pair{Xna::TextureFilter::MinLinearMagPointMipPoint, FNA3D_TEXTUREFILTER_MINLINEAR_MAGPOINT_MIPPOINT},
        std::pair{Xna::TextureFilter::MinPointMagLinearMipLinear, FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPLINEAR},
        std::pair{Xna::TextureFilter::MinPointMagLinearMipPoint, FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dTextureFilter(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryCompareFunctionMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::CompareFunction::Always, FNA3D_COMPAREFUNCTION_ALWAYS},
        std::pair{Xna::CompareFunction::Never, FNA3D_COMPAREFUNCTION_NEVER},
        std::pair{Xna::CompareFunction::Less, FNA3D_COMPAREFUNCTION_LESS},
        std::pair{Xna::CompareFunction::LessEqual, FNA3D_COMPAREFUNCTION_LESSEQUAL},
        std::pair{Xna::CompareFunction::Equal, FNA3D_COMPAREFUNCTION_EQUAL},
        std::pair{Xna::CompareFunction::GreaterEqual, FNA3D_COMPAREFUNCTION_GREATEREQUAL},
        std::pair{Xna::CompareFunction::Greater, FNA3D_COMPAREFUNCTION_GREATER},
        std::pair{Xna::CompareFunction::NotEqual, FNA3D_COMPAREFUNCTION_NOTEQUAL},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dCompareFunction(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryStencilOperationMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::StencilOperation::Keep, FNA3D_STENCILOPERATION_KEEP},
        std::pair{Xna::StencilOperation::Zero, FNA3D_STENCILOPERATION_ZERO},
        std::pair{Xna::StencilOperation::Replace, FNA3D_STENCILOPERATION_REPLACE},
        std::pair{Xna::StencilOperation::Increment, FNA3D_STENCILOPERATION_INCREMENT},
        std::pair{Xna::StencilOperation::Decrement, FNA3D_STENCILOPERATION_DECREMENT},
        std::pair{Xna::StencilOperation::IncrementSaturation, FNA3D_STENCILOPERATION_INCREMENTSATURATION},
        std::pair{Xna::StencilOperation::DecrementSaturation, FNA3D_STENCILOPERATION_DECREMENTSATURATION},
        std::pair{Xna::StencilOperation::Invert, FNA3D_STENCILOPERATION_INVERT},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dStencilOperation(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryCullModeMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::CullMode::None, FNA3D_CULLMODE_NONE},
        std::pair{Xna::CullMode::CullClockwiseFace, FNA3D_CULLMODE_CULLCLOCKWISEFACE},
        std::pair{Xna::CullMode::CullCounterClockwiseFace, FNA3D_CULLMODE_CULLCOUNTERCLOCKWISEFACE},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dCullMode(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryFillModeMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::FillMode::Solid, FNA3D_FILLMODE_SOLID},
        std::pair{Xna::FillMode::WireFrame, FNA3D_FILLMODE_WIREFRAME},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dFillMode(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryTextureAddressModeMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::TextureAddressMode::Wrap, FNA3D_TEXTUREADDRESSMODE_WRAP},
        std::pair{Xna::TextureAddressMode::Clamp, FNA3D_TEXTUREADDRESSMODE_CLAMP},
        std::pair{Xna::TextureAddressMode::Mirror, FNA3D_TEXTUREADDRESSMODE_MIRROR},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dTextureAddressMode(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryDepthFormatMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::DepthFormat::None, FNA3D_DEPTHFORMAT_NONE},
        std::pair{Xna::DepthFormat::Depth16, FNA3D_DEPTHFORMAT_D16},
        std::pair{Xna::DepthFormat::Depth24, FNA3D_DEPTHFORMAT_D24},
        std::pair{Xna::DepthFormat::Depth24Stencil8, FNA3D_DEPTHFORMAT_D24S8},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dDepthFormat(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EverySurfaceFormatMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::SurfaceFormat::Color, FNA3D_SURFACEFORMAT_COLOR},
        std::pair{Xna::SurfaceFormat::Bgr565, FNA3D_SURFACEFORMAT_BGR565},
        std::pair{Xna::SurfaceFormat::Bgra5551, FNA3D_SURFACEFORMAT_BGRA5551},
        std::pair{Xna::SurfaceFormat::Bgra4444, FNA3D_SURFACEFORMAT_BGRA4444},
        std::pair{Xna::SurfaceFormat::Dxt1, FNA3D_SURFACEFORMAT_DXT1},
        std::pair{Xna::SurfaceFormat::Dxt3, FNA3D_SURFACEFORMAT_DXT3},
        std::pair{Xna::SurfaceFormat::Dxt5, FNA3D_SURFACEFORMAT_DXT5},
        std::pair{Xna::SurfaceFormat::NormalizedByte2, FNA3D_SURFACEFORMAT_NORMALIZEDBYTE2},
        std::pair{Xna::SurfaceFormat::NormalizedByte4, FNA3D_SURFACEFORMAT_NORMALIZEDBYTE4},
        std::pair{Xna::SurfaceFormat::Rgba1010102, FNA3D_SURFACEFORMAT_RGBA1010102},
        std::pair{Xna::SurfaceFormat::Rg32, FNA3D_SURFACEFORMAT_RG32},
        std::pair{Xna::SurfaceFormat::Rgba64, FNA3D_SURFACEFORMAT_RGBA64},
        std::pair{Xna::SurfaceFormat::Alpha8, FNA3D_SURFACEFORMAT_ALPHA8},
        std::pair{Xna::SurfaceFormat::Single, FNA3D_SURFACEFORMAT_SINGLE},
        std::pair{Xna::SurfaceFormat::Vector2, FNA3D_SURFACEFORMAT_VECTOR2},
        std::pair{Xna::SurfaceFormat::Vector4, FNA3D_SURFACEFORMAT_VECTOR4},
        std::pair{Xna::SurfaceFormat::HalfSingle, FNA3D_SURFACEFORMAT_HALFSINGLE},
        std::pair{Xna::SurfaceFormat::HalfVector2, FNA3D_SURFACEFORMAT_HALFVECTOR2},
        std::pair{Xna::SurfaceFormat::HalfVector4, FNA3D_SURFACEFORMAT_HALFVECTOR4},
        std::pair{Xna::SurfaceFormat::HdrBlendable, FNA3D_SURFACEFORMAT_HDRBLENDABLE},
        std::pair{Xna::SurfaceFormat::ColorBgraEXT, FNA3D_SURFACEFORMAT_COLORBGRA_EXT},
        std::pair{Xna::SurfaceFormat::ColorSrgbEXT, FNA3D_SURFACEFORMAT_COLORSRGB_EXT},
        std::pair{Xna::SurfaceFormat::Dxt5SrgbEXT, FNA3D_SURFACEFORMAT_DXT5SRGB_EXT},
        std::pair{Xna::SurfaceFormat::Bc7EXT, FNA3D_SURFACEFORMAT_BC7_EXT},
        std::pair{Xna::SurfaceFormat::Bc7SrgbEXT, FNA3D_SURFACEFORMAT_BC7SRGB_EXT},
        std::pair{Xna::SurfaceFormat::ByteEXT, FNA3D_SURFACEFORMAT_BYTE_EXT},
        std::pair{Xna::SurfaceFormat::UShortEXT, FNA3D_SURFACEFORMAT_USHORT_EXT},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dSurfaceFormat(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryVertexElementFormatMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::VertexElementFormat::Single, FNA3D_VERTEXELEMENTFORMAT_SINGLE},
        std::pair{Xna::VertexElementFormat::Vector2, FNA3D_VERTEXELEMENTFORMAT_VECTOR2},
        std::pair{Xna::VertexElementFormat::Vector3, FNA3D_VERTEXELEMENTFORMAT_VECTOR3},
        std::pair{Xna::VertexElementFormat::Vector4, FNA3D_VERTEXELEMENTFORMAT_VECTOR4},
        std::pair{Xna::VertexElementFormat::Color, FNA3D_VERTEXELEMENTFORMAT_COLOR},
        std::pair{Xna::VertexElementFormat::Byte4, FNA3D_VERTEXELEMENTFORMAT_BYTE4},
        std::pair{Xna::VertexElementFormat::Short2, FNA3D_VERTEXELEMENTFORMAT_SHORT2},
        std::pair{Xna::VertexElementFormat::Short4, FNA3D_VERTEXELEMENTFORMAT_SHORT4},
        std::pair{Xna::VertexElementFormat::NormalizedShort2, FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT2},
        std::pair{Xna::VertexElementFormat::NormalizedShort4, FNA3D_VERTEXELEMENTFORMAT_NORMALIZEDSHORT4},
        std::pair{Xna::VertexElementFormat::HalfVector2, FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR2},
        std::pair{Xna::VertexElementFormat::HalfVector4, FNA3D_VERTEXELEMENTFORMAT_HALFVECTOR4},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dVertexElementFormat(input), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryVertexElementUsageMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::VertexElementUsage::Position, FNA3D_VERTEXELEMENTUSAGE_POSITION},
        std::pair{Xna::VertexElementUsage::Color, FNA3D_VERTEXELEMENTUSAGE_COLOR},
        std::pair{Xna::VertexElementUsage::TextureCoordinate, FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE},
        std::pair{Xna::VertexElementUsage::Normal, FNA3D_VERTEXELEMENTUSAGE_NORMAL},
        std::pair{Xna::VertexElementUsage::Binormal, FNA3D_VERTEXELEMENTUSAGE_BINORMAL},
        std::pair{Xna::VertexElementUsage::Tangent, FNA3D_VERTEXELEMENTUSAGE_TANGENT},
        std::pair{Xna::VertexElementUsage::BlendIndices, FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES},
        std::pair{Xna::VertexElementUsage::BlendWeight, FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT},
        std::pair{Xna::VertexElementUsage::Depth, FNA3D_VERTEXELEMENTUSAGE_DEPTH},
        std::pair{Xna::VertexElementUsage::Fog, FNA3D_VERTEXELEMENTUSAGE_FOG},
        std::pair{Xna::VertexElementUsage::PointSize, FNA3D_VERTEXELEMENTUSAGE_POINTSIZE},
        std::pair{Xna::VertexElementUsage::Sample, FNA3D_VERTEXELEMENTUSAGE_SAMPLE},
        std::pair{Xna::VertexElementUsage::TessellateFactor, FNA3D_VERTEXELEMENTUSAGE_TESSELATEFACTOR},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dVertexElementUsage(input), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryPrimitiveTypeMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::PrimitiveType::TriangleList, FNA3D_PRIMITIVETYPE_TRIANGLELIST},
        std::pair{Xna::PrimitiveType::TriangleStrip, FNA3D_PRIMITIVETYPE_TRIANGLESTRIP},
        std::pair{Xna::PrimitiveType::LineList, FNA3D_PRIMITIVETYPE_LINELIST},
        std::pair{Xna::PrimitiveType::LineStrip, FNA3D_PRIMITIVETYPE_LINESTRIP},
        std::pair{Xna::PrimitiveType::PointListEXT, FNA3D_PRIMITIVETYPE_POINTLIST_EXT},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dPrimitiveType(input), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EverySetDataOptionsMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    const std::array cases{
        std::pair{Xna::SetDataOptions::None, FNA3D_SETDATAOPTIONS_NONE},
        std::pair{Xna::SetDataOptions::Discard, FNA3D_SETDATAOPTIONS_DISCARD},
        std::pair{Xna::SetDataOptions::NoOverwrite, FNA3D_SETDATAOPTIONS_NOOVERWRITE},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dSetDataOptions(input), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryBlendFunctionMapsByMeaning)
{
    // Oracle: FNA src/Graphics state/vertex enums and pinned FNA3D.h named values.
    // Microsoft XNA Min=3, Max=4; FNA reverses these (XNA enum metadata).
    const std::array cases{
        std::pair{Xna::BlendFunction::Add, FNA3D_BLENDFUNCTION_ADD},
        std::pair{Xna::BlendFunction::Subtract, FNA3D_BLENDFUNCTION_SUBTRACT},
        std::pair{Xna::BlendFunction::ReverseSubtract, FNA3D_BLENDFUNCTION_REVERSESUBTRACT},
        std::pair{Xna::BlendFunction::Min, FNA3D_BLENDFUNCTION_MIN},
        std::pair{Xna::BlendFunction::Max, FNA3D_BLENDFUNCTION_MAX},
    };
    for (const auto& [input, expected] : cases)
    {
        EXPECT_EQ(ToFna3dBlendFunction(static_cast<int>(input)), expected) << static_cast<int>(input);
    }
}

TEST(Fna3dEnumMappingTests, EveryColorWriteMaskPreservesEachChannelBit)
{
    // Oracle: XNA ColorWriteChannels and FNA3D.h: R=1, G=2, B=4, A=8.
    for (int mask = 0; mask < 16; ++mask)
    {
        const auto expected = static_cast<FNA3D_ColorWriteChannels>(
            ((mask & 1) ? FNA3D_COLORWRITECHANNELS_RED : 0) |
            ((mask & 2) ? FNA3D_COLORWRITECHANNELS_GREEN : 0) |
            ((mask & 4) ? FNA3D_COLORWRITECHANNELS_BLUE : 0) |
            ((mask & 8) ? FNA3D_COLORWRITECHANNELS_ALPHA : 0));
        EXPECT_EQ(ToFna3dColorWriteChannels(mask), expected) << mask;
    }
}

TEST(Fna3dEnumMappingTests, EveryBridgeRejectsBothSidesOfItsOrdinalDomain)
{
    // Oracle: FNA3D.h enum domains; bridge contract rejects rather than defaults.
    const auto check = [](auto convert, int maximum) {
        for (const int invalid : {-1, maximum + 1, std::numeric_limits<int>::min(),
                                   std::numeric_limits<int>::max()})
            EXPECT_THROW((void) convert(invalid), std::runtime_error) << invalid;
    };
    check(ToFna3dBlend, 12);
    check(ToFna3dTextureFilter, 8);
    check(ToFna3dCompareFunction, 7);
    check(ToFna3dStencilOperation, 7);
    check(ToFna3dCullMode, 2);
    check(ToFna3dFillMode, 1);
    check(ToFna3dTextureAddressMode, 2);
    check(ToFna3dDepthFormat, 3);
    check(ToFna3dSurfaceFormat, 26);
    check([](int value) { return ToFna3dVertexElementFormat(static_cast<Xna::VertexElementFormat>(value)); }, 11);
    check([](int value) { return ToFna3dVertexElementUsage(static_cast<Xna::VertexElementUsage>(value)); }, 12);
    check([](int value) { return ToFna3dPrimitiveType(static_cast<Xna::PrimitiveType>(value)); }, 4);
    check([](int value) { return ToFna3dSetDataOptions(static_cast<Xna::SetDataOptions>(value)); }, 2);
    check(ToFna3dBlendFunction, 4);
    check(ToFna3dColorWriteChannels, 15);
}

TEST(Fna3dEnumMappingTests, PrimitiveAndVertexEnumsMapThrough)
{
    EXPECT_EQ(ToFna3dPrimitiveType(Xna::PrimitiveType::TriangleStrip),
              FNA3D_PRIMITIVETYPE_TRIANGLESTRIP);
    EXPECT_EQ(ToFna3dPrimitiveType(Xna::PrimitiveType::PointListEXT),
              FNA3D_PRIMITIVETYPE_POINTLIST_EXT);
    EXPECT_EQ(ToFna3dSetDataOptions(Xna::SetDataOptions::NoOverwrite),
              FNA3D_SETDATAOPTIONS_NOOVERWRITE);
    EXPECT_EQ(ToFna3dVertexElementFormat(Xna::VertexElementFormat::Byte4),
              FNA3D_VERTEXELEMENTFORMAT_BYTE4);
    EXPECT_EQ(ToFna3dVertexElementUsage(Xna::VertexElementUsage::BlendIndices),
              FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES);
}

TEST(Fna3dEnumMappingTests, VertexCountForPrimitivesFollowsTheTopology)
{
    EXPECT_EQ(VertexCountForPrimitives(Xna::PrimitiveType::TriangleList, 2), 6);
    EXPECT_EQ(VertexCountForPrimitives(Xna::PrimitiveType::TriangleStrip, 2), 4);
    EXPECT_EQ(VertexCountForPrimitives(Xna::PrimitiveType::LineList, 3), 6);
    EXPECT_EQ(VertexCountForPrimitives(Xna::PrimitiveType::LineStrip, 3), 4);
    EXPECT_EQ(VertexCountForPrimitives(Xna::PrimitiveType::PointListEXT, 5), 5);
}

TEST(Fna3dVertexLayoutTests, KnownStridesDescribeTheBytesCnaStages)
{
    const auto positionColor = ElementsForStride(16);
    ASSERT_EQ(positionColor.size(), 2u);
    EXPECT_EQ(positionColor[0].offset, 0);
    EXPECT_EQ(positionColor[0].vertexElementFormat, FNA3D_VERTEXELEMENTFORMAT_VECTOR3);
    EXPECT_EQ(positionColor[0].vertexElementUsage, FNA3D_VERTEXELEMENTUSAGE_POSITION);
    EXPECT_EQ(positionColor[1].offset, 12);
    EXPECT_EQ(positionColor[1].vertexElementFormat, FNA3D_VERTEXELEMENTFORMAT_COLOR);

    const auto spriteVertex = ElementsForStride(24);
    ASSERT_EQ(spriteVertex.size(), 3u);
    EXPECT_EQ(spriteVertex[1].offset, 12);
    EXPECT_EQ(spriteVertex[2].offset, 16);
    EXPECT_EQ(spriteVertex[2].vertexElementUsage, FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE);

    const auto skinned = ElementsForStride(52);
    ASSERT_EQ(skinned.size(), 5u);
    EXPECT_EQ(skinned[3].vertexElementUsage, FNA3D_VERTEXELEMENTUSAGE_BLENDWEIGHT);
    EXPECT_EQ(skinned[3].offset, 32);
    EXPECT_EQ(skinned[4].vertexElementUsage, FNA3D_VERTEXELEMENTUSAGE_BLENDINDICES);
    EXPECT_EQ(skinned[4].vertexElementFormat, FNA3D_VERTEXELEMENTFORMAT_BYTE4);
    EXPECT_EQ(skinned[4].offset, 48);
}

TEST(Fna3dVertexLayoutTests, EveryKnownStrideIsFullyDescribedAndInBounds)
{
    for (const int stride : { 16, 20, 24, 32, 48, 52, 56, 68 })
    {
        const auto elements = ElementsForStride(stride);
        ASSERT_FALSE(elements.empty()) << "stride " << stride;
        EXPECT_EQ(elements[0].offset, 0) << "stride " << stride;
        for (const FNA3D_VertexElement& element : elements)
        {
            EXPECT_GE(element.offset, 0) << "stride " << stride;
            EXPECT_LT(element.offset, stride) << "stride " << stride;
        }
    }
}

TEST(Fna3dVertexLayoutTests, AnUnknownStrideIsRefusedRatherThanApproximated)
{
    EXPECT_THROW((void) ElementsForStride(0), std::runtime_error);
    EXPECT_THROW((void) ElementsForStride(17), std::runtime_error);
    EXPECT_THROW((void) ElementsForStride(28), std::runtime_error);

    try
    {
        (void) ElementsForStride(17);
        FAIL() << "an unknown stride must be rejected";
    }
    catch (const std::runtime_error& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("stride 17"), std::string::npos);
        EXPECT_NE(message.find("VertexDeclaration"), std::string::npos);
    }
}


TEST(Fna3dVertexLayoutTests, StandardLayoutsMatchXnaVertexDeclarations)
{
    // Oracle: FNA src/Graphics/Vertices/VertexPosition{Color,Texture,ColorTexture,NormalTexture}.cs.
    // POSITION is 3*4 bytes; COLOR is 4 bytes; NORMAL is 3*4; TEXCOORD is 2*4.
    struct Element { int offset; FNA3D_VertexElementFormat format; FNA3D_VertexElementUsage usage; };
    struct Layout { int stride; std::vector<Element> elements; };
    const std::array cases{
        Layout{16, {{0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3, FNA3D_VERTEXELEMENTUSAGE_POSITION},
                    {12, FNA3D_VERTEXELEMENTFORMAT_COLOR, FNA3D_VERTEXELEMENTUSAGE_COLOR}}},
        Layout{20, {{0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3, FNA3D_VERTEXELEMENTUSAGE_POSITION},
                    {12, FNA3D_VERTEXELEMENTFORMAT_VECTOR2, FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE}}},
        Layout{24, {{0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3, FNA3D_VERTEXELEMENTUSAGE_POSITION},
                    {12, FNA3D_VERTEXELEMENTFORMAT_COLOR, FNA3D_VERTEXELEMENTUSAGE_COLOR},
                    {16, FNA3D_VERTEXELEMENTFORMAT_VECTOR2, FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE}}},
        Layout{32, {{0, FNA3D_VERTEXELEMENTFORMAT_VECTOR3, FNA3D_VERTEXELEMENTUSAGE_POSITION},
                    {12, FNA3D_VERTEXELEMENTFORMAT_VECTOR3, FNA3D_VERTEXELEMENTUSAGE_NORMAL},
                    {24, FNA3D_VERTEXELEMENTFORMAT_VECTOR2, FNA3D_VERTEXELEMENTUSAGE_TEXTURECOORDINATE}}},
    };
    for (const auto& layout : cases)
    {
        SCOPED_TRACE(layout.stride);
        const auto actual = ElementsForStride(layout.stride);
        ASSERT_EQ(actual.size(), layout.elements.size());
        for (std::size_t index = 0; index < actual.size(); ++index)
        {
            SCOPED_TRACE(index);
            EXPECT_EQ(actual[index].offset, layout.elements[index].offset);
            EXPECT_EQ(actual[index].vertexElementFormat, layout.elements[index].format);
            EXPECT_EQ(actual[index].vertexElementUsage, layout.elements[index].usage);
            EXPECT_EQ(actual[index].usageIndex, 0);
        }
    }
}

#endif // CNA_RENDERER_FNA3D / CNA_RENDERER_PRESENT_FNA3D
