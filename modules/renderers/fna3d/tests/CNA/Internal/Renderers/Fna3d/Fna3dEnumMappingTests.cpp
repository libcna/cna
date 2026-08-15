// SPDX-License-Identifier: MS-PL
//
// plan_fna3d.md FNA3D-17: GTest coverage for the XNA-ordinal to FNA3D-enumerator bridge and for
// the stride-keyed vertex layout table.
//
// FNA3D's header states its enumerations "should match XNA 4.0", and CNA's are ports of the same
// specification, so every mapping is numerically the identity. The production code pins that with
// static_asserts; these tests pin the other half -- that an ordinal outside the contract is
// REJECTED rather than cast into an undefined enumerator, and that the layout table a
// declaration-less internal route falls back on describes the bytes CNA actually stages.
#include <gtest/gtest.h>

// plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_FNA3D is defined project-wide.
#if defined(CNA_RENDERER_FNA3D) || defined(CNA_RENDERER_PRESENT_FNA3D)
#include "CNA/Internal/Renderers/Fna3d/Fna3dEnumMapping.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dVertexLayouts.hpp"

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

TEST(Fna3dEnumMappingTests, EveryInRangeOrdinalIsAccepted)
{
    for (int i = FNA3D_BLEND_ONE; i <= FNA3D_BLEND_SOURCEALPHASATURATION; ++i)
    {
        EXPECT_NO_THROW((void) ToFna3dBlend(i)) << "blend ordinal " << i;
    }
    for (int i = FNA3D_TEXTUREFILTER_LINEAR;
         i <= FNA3D_TEXTUREFILTER_MINPOINT_MAGLINEAR_MIPPOINT; ++i)
    {
        EXPECT_NO_THROW((void) ToFna3dTextureFilter(i)) << "filter ordinal " << i;
    }
    for (int mask = FNA3D_COLORWRITECHANNELS_NONE; mask <= FNA3D_COLORWRITECHANNELS_ALL; ++mask)
    {
        EXPECT_NO_THROW((void) ToFna3dColorWriteChannels(mask)) << "write mask " << mask;
    }
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

#endif // CNA_RENDERER_FNA3D / CNA_RENDERER_PRESENT_FNA3D
