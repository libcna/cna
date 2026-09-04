// SPDX-License-Identifier: MS-PL
//
// plans/plan_magnum.md MAGNUM-40: structural GTest coverage for everything on the MAGNUM renderer that
// does not need a live OpenGL context -- the XNA-ordinal -> Magnum-enum mappings, the vertex
// layout resolution both the stock and the declaration-driven routes share, and the generated
// stock GLSL. Everything that needs a real context (resource creation, draws, readback) is left to
// the on-device smoke checks, since a headless CI container has no GL driver to create one on.
#include <gtest/gtest.h>

// plans/plan_runtimerenderer.md RTR-P9-9: PRESENT_, not the identity macro. This suite is
// device-free policy coverage for its own renderer, so it is worth compiling and running
// whenever that renderer is COMPILED IN -- in a multi-renderer build it need not be the
// selected one. Only the default renderer's CNA_RENDERER_MAGNUM is defined project-wide.
#if defined(CNA_RENDERER_MAGNUM) || defined(CNA_RENDERER_PRESENT_MAGNUM)
#include "CNA/Internal/Renderers/Magnum/MagnumBuffers.hpp"
#include "CNA/Internal/Renderers/Magnum/MagnumProgram.hpp"
#include "CNA/Internal/Renderers/Magnum/MagnumStateMapping.hpp"
#include "CNA/Internal/Renderers/Magnum/MagnumStockShaders.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/InvalidOperationException.hpp"

#include <string>
#include <vector>

using namespace CNA::Internal::Renderers::Magnum;
using Renderer = ::Magnum::GL::Renderer;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

// ---- Blend factors ----

TEST(MagnumStateMappingTest, BlendOrdinalsMapToTheirGlFactors)
{
    EXPECT_EQ(ToBlendFunction(0), Renderer::BlendFunction::One);
    EXPECT_EQ(ToBlendFunction(1), Renderer::BlendFunction::Zero);
    EXPECT_EQ(ToBlendFunction(2), Renderer::BlendFunction::SourceColor);
    EXPECT_EQ(ToBlendFunction(3), Renderer::BlendFunction::OneMinusSourceColor);
    EXPECT_EQ(ToBlendFunction(4), Renderer::BlendFunction::SourceAlpha);
    EXPECT_EQ(ToBlendFunction(5), Renderer::BlendFunction::OneMinusSourceAlpha);
    EXPECT_EQ(ToBlendFunction(6), Renderer::BlendFunction::DestinationColor);
    EXPECT_EQ(ToBlendFunction(7), Renderer::BlendFunction::OneMinusDestinationColor);
    EXPECT_EQ(ToBlendFunction(8), Renderer::BlendFunction::DestinationAlpha);
    EXPECT_EQ(ToBlendFunction(9), Renderer::BlendFunction::OneMinusDestinationAlpha);
    EXPECT_EQ(ToBlendFunction(10), Renderer::BlendFunction::ConstantColor);
    EXPECT_EQ(ToBlendFunction(11), Renderer::BlendFunction::OneMinusConstantColor);
    EXPECT_EQ(ToBlendFunction(12), Renderer::BlendFunction::SourceAlphaSaturate);
}

TEST(MagnumStateMappingTest, UnknownBlendOrdinalFallsBackToOne)
{
    EXPECT_EQ(ToBlendFunction(999), Renderer::BlendFunction::One);
    EXPECT_EQ(ToBlendFunction(-1), Renderer::BlendFunction::One);
}

// ---- Blend equations ----

TEST(MagnumStateMappingTest, BlendFunctionOrdinalsMapToTheirGlEquations)
{
    EXPECT_EQ(ToBlendEquation(0), Renderer::BlendEquation::Add);
    EXPECT_EQ(ToBlendEquation(1), Renderer::BlendEquation::Subtract);
    EXPECT_EQ(ToBlendEquation(2), Renderer::BlendEquation::ReverseSubtract);
    EXPECT_EQ(ToBlendEquation(3), Renderer::BlendEquation::Max);
    EXPECT_EQ(ToBlendEquation(4), Renderer::BlendEquation::Min);
    EXPECT_EQ(ToBlendEquation(77), Renderer::BlendEquation::Add);
}

// ---- Compare functions ----

TEST(MagnumStateMappingTest, CompareFunctionOrdinalsMapToTheirDepthFunctions)
{
    EXPECT_EQ(ToDepthFunction(0), Renderer::DepthFunction::Always);
    EXPECT_EQ(ToDepthFunction(1), Renderer::DepthFunction::Never);
    EXPECT_EQ(ToDepthFunction(2), Renderer::DepthFunction::Less);
    EXPECT_EQ(ToDepthFunction(3), Renderer::DepthFunction::LessOrEqual);
    EXPECT_EQ(ToDepthFunction(4), Renderer::DepthFunction::Equal);
    EXPECT_EQ(ToDepthFunction(5), Renderer::DepthFunction::GreaterOrEqual);
    EXPECT_EQ(ToDepthFunction(6), Renderer::DepthFunction::Greater);
    EXPECT_EQ(ToDepthFunction(7), Renderer::DepthFunction::NotEqual);
    EXPECT_EQ(ToDepthFunction(42), Renderer::DepthFunction::Always);
}

TEST(MagnumStateMappingTest, CompareFunctionOrdinalsMapToTheirStencilFunctions)
{
    EXPECT_EQ(ToStencilFunction(0), Renderer::StencilFunction::Always);
    EXPECT_EQ(ToStencilFunction(1), Renderer::StencilFunction::Never);
    EXPECT_EQ(ToStencilFunction(2), Renderer::StencilFunction::Less);
    EXPECT_EQ(ToStencilFunction(3), Renderer::StencilFunction::LessOrEqual);
    EXPECT_EQ(ToStencilFunction(4), Renderer::StencilFunction::Equal);
    EXPECT_EQ(ToStencilFunction(5), Renderer::StencilFunction::GreaterOrEqual);
    EXPECT_EQ(ToStencilFunction(6), Renderer::StencilFunction::Greater);
    EXPECT_EQ(ToStencilFunction(7), Renderer::StencilFunction::NotEqual);
    EXPECT_EQ(ToStencilFunction(42), Renderer::StencilFunction::Always);
}

// ---- Stencil operations ----

TEST(MagnumStateMappingTest, StencilOperationOrdinalsMapToTheirGlOperations)
{
    EXPECT_EQ(ToStencilOperation(0), Renderer::StencilOperation::Keep);
    EXPECT_EQ(ToStencilOperation(1), Renderer::StencilOperation::Zero);
    EXPECT_EQ(ToStencilOperation(2), Renderer::StencilOperation::Replace);
    EXPECT_EQ(ToStencilOperation(7), Renderer::StencilOperation::Invert);
    EXPECT_EQ(ToStencilOperation(99), Renderer::StencilOperation::Keep);
}

TEST(MagnumStateMappingTest, XnaIncrementWrapsAndIncrementSaturationClamps)
{
    // XNA names these the opposite way round from GL: Increment/Decrement wrap, and only the
    // *Saturation pair clamps. Getting the pairing backwards is invisible until a stencil counter
    // actually reaches its limit, which is exactly what this pins down.
    EXPECT_EQ(ToStencilOperation(3), Renderer::StencilOperation::IncrementWrap);
    EXPECT_EQ(ToStencilOperation(4), Renderer::StencilOperation::DecrementWrap);
    EXPECT_EQ(ToStencilOperation(5), Renderer::StencilOperation::Increment);
    EXPECT_EQ(ToStencilOperation(6), Renderer::StencilOperation::Decrement);
}

// ---- Primitives ----

TEST(MagnumStateMappingTest, PrimitiveTypesMapToTheirMeshPrimitives)
{
    EXPECT_EQ(ToMeshPrimitive(PrimitiveType::TriangleList), ::Magnum::GL::MeshPrimitive::Triangles);
    EXPECT_EQ(ToMeshPrimitive(PrimitiveType::TriangleStrip),
              ::Magnum::GL::MeshPrimitive::TriangleStrip);
    EXPECT_EQ(ToMeshPrimitive(PrimitiveType::LineList), ::Magnum::GL::MeshPrimitive::Lines);
    EXPECT_EQ(ToMeshPrimitive(PrimitiveType::LineStrip), ::Magnum::GL::MeshPrimitive::LineStrip);
    EXPECT_EQ(ToMeshPrimitive(PrimitiveType::PointListEXT), ::Magnum::GL::MeshPrimitive::Points);
}

TEST(MagnumStateMappingTest, UnrecognizedPrimitiveTypeThrows)
{
    EXPECT_THROW((void)ToMeshPrimitive(static_cast<PrimitiveType>(77)),
                 System::InvalidOperationException);
    EXPECT_THROW((void)VertexCountForPrimitives(static_cast<PrimitiveType>(77), 1),
                 System::InvalidOperationException);
}

TEST(MagnumStateMappingTest, PrimitiveCountsConvertToVertexCounts)
{
    EXPECT_EQ(VertexCountForPrimitives(PrimitiveType::TriangleList, 4), 12);
    EXPECT_EQ(VertexCountForPrimitives(PrimitiveType::TriangleStrip, 4), 6);
    EXPECT_EQ(VertexCountForPrimitives(PrimitiveType::LineList, 4), 8);
    EXPECT_EQ(VertexCountForPrimitives(PrimitiveType::LineStrip, 4), 5);
    EXPECT_EQ(VertexCountForPrimitives(PrimitiveType::PointListEXT, 4), 4);
}

// ---- Sampler state ----

TEST(MagnumStateMappingTest, EveryTextureFilterOrdinalCarriesAMipmapTerm)
{
    // A filter that drops its mipmap component makes a texture owning a real chain never
    // mip-filter -- including under Linear, the default every game gets unless it says otherwise.
    for (int ordinal = 0; ordinal <= 8; ++ordinal)
    {
        ::Magnum::GL::SamplerFilter minification{};
        ::Magnum::GL::SamplerMipmap mipmap{};
        ::Magnum::GL::SamplerFilter magnification{};
        DecomposeTextureFilter(ordinal, minification, mipmap, magnification);
        EXPECT_TRUE(mipmap == ::Magnum::GL::SamplerMipmap::Nearest
                    || mipmap == ::Magnum::GL::SamplerMipmap::Linear)
            << "filter ordinal " << ordinal << " lost its mipmap term";
    }
}

TEST(MagnumStateMappingTest, LinearAndPointDecomposeIntoAllThreeComponents)
{
    ::Magnum::GL::SamplerFilter minification{};
    ::Magnum::GL::SamplerMipmap mipmap{};
    ::Magnum::GL::SamplerFilter magnification{};

    DecomposeTextureFilter(0, minification, mipmap, magnification);
    EXPECT_EQ(minification, ::Magnum::GL::SamplerFilter::Linear);
    EXPECT_EQ(mipmap, ::Magnum::GL::SamplerMipmap::Linear);
    EXPECT_EQ(magnification, ::Magnum::GL::SamplerFilter::Linear);

    DecomposeTextureFilter(1, minification, mipmap, magnification);
    EXPECT_EQ(minification, ::Magnum::GL::SamplerFilter::Nearest);
    EXPECT_EQ(mipmap, ::Magnum::GL::SamplerMipmap::Nearest);
    EXPECT_EQ(magnification, ::Magnum::GL::SamplerFilter::Nearest);
}

TEST(MagnumStateMappingTest, MixedMinMagFiltersKeepTheirTwoHalvesDistinct)
{
    ::Magnum::GL::SamplerFilter minification{};
    ::Magnum::GL::SamplerMipmap mipmap{};
    ::Magnum::GL::SamplerFilter magnification{};

    DecomposeTextureFilter(5, minification, mipmap, magnification);  // MinLinearMagPointMipLinear
    EXPECT_EQ(minification, ::Magnum::GL::SamplerFilter::Linear);
    EXPECT_EQ(mipmap, ::Magnum::GL::SamplerMipmap::Linear);
    EXPECT_EQ(magnification, ::Magnum::GL::SamplerFilter::Nearest);

    DecomposeTextureFilter(8, minification, mipmap, magnification);  // MinPointMagLinearMipPoint
    EXPECT_EQ(minification, ::Magnum::GL::SamplerFilter::Nearest);
    EXPECT_EQ(mipmap, ::Magnum::GL::SamplerMipmap::Nearest);
    EXPECT_EQ(magnification, ::Magnum::GL::SamplerFilter::Linear);
}

TEST(MagnumStateMappingTest, AddressModesMapToTheirWrappings)
{
    EXPECT_EQ(ToSamplerWrapping(0), ::Magnum::GL::SamplerWrapping::Repeat);
    EXPECT_EQ(ToSamplerWrapping(1), ::Magnum::GL::SamplerWrapping::ClampToEdge);
    EXPECT_EQ(ToSamplerWrapping(2), ::Magnum::GL::SamplerWrapping::MirroredRepeat);
    EXPECT_EQ(ToSamplerWrapping(9), ::Magnum::GL::SamplerWrapping::ClampToEdge);
}

// ---- Vertex layouts ----

TEST(MagnumVertexLayoutTest, EachBuiltInStrideResolvesToItsOwnAttributeSet)
{
    EXPECT_EQ(StockAttributesForStride(16).size(), 2u);
    EXPECT_EQ(StockAttributesForStride(20).size(), 2u);
    EXPECT_EQ(StockAttributesForStride(24).size(), 3u);
    EXPECT_EQ(StockAttributesForStride(32).size(), 3u);
}

TEST(MagnumVertexLayoutTest, UnknownStrideResolvesToNoAttributes)
{
    EXPECT_TRUE(StockAttributesForStride(13).empty());
    EXPECT_TRUE(StockAttributesForStride(0).empty());
}

TEST(MagnumVertexLayoutTest, SkinnedStridesResolveToTheirBonePaletteAttributes)
{
    EXPECT_EQ(StockAttributesForStride(52).size(), 5u);
    EXPECT_EQ(StockAttributesForStride(56).size(), 6u);
}

TEST(MagnumVertexLayoutTest, TheTwoColourCarryingPbrStridesBindCOLOR_0AtLocationSix)
{
    // plans/plan_gltf.md GLTF-462/GLTF-463/GLTF-465. Stride 60 is the rigid PBR record with a packed
    // COLOR_0 at offset 56 and stride 80 the skinned one with the same colour at 76. glTF 2.0 §3.9.2
    // makes that colour a multiplier on base colour, so it has to reach the shader -- and it has to
    // reach it at the location the generated PBR source declares (6), past the four rigid PBR
    // attributes and the two skinning ones, so that one program can serve both strides of its family.
    const std::vector<MagnumVertexAttribute> rigid = StockAttributesForStride(60);
    ASSERT_EQ(5u, rigid.size());
    EXPECT_EQ(6, rigid.back().location);
    EXPECT_EQ(56, rigid.back().offsetInStream);
    EXPECT_EQ(4, rigid.back().components);
    EXPECT_TRUE(rigid.back().normalized) << "a packed Color is UNORM; unnormalized it would arrive "
                                            "as 0..255 and blow the base-colour product out";

    const std::vector<MagnumVertexAttribute> skinned = StockAttributesForStride(80);
    ASSERT_EQ(7u, skinned.size());
    EXPECT_EQ(6, skinned.back().location);
    EXPECT_EQ(76, skinned.back().offsetInStream);
    EXPECT_EQ(4, skinned.back().components);
    EXPECT_TRUE(skinned.back().normalized);

    // Stride 60's prefix must stay byte-identical to stride 48's, and stride 80's to stride 68's --
    // the same "append, never insert" rule the skinned colour stride already relies on. If the colour
    // shifted anything before it, both programs would read every earlier attribute from the wrong
    // offset and the failure would look like corrupt geometry rather than a wrong colour.
    const std::vector<MagnumVertexAttribute> rigidBase = StockAttributesForStride(48);
    ASSERT_EQ(4u, rigidBase.size());
    for (std::size_t i = 0; i < rigidBase.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(rigidBase[i].location, rigid[i].location);
        EXPECT_EQ(rigidBase[i].offsetInStream, rigid[i].offsetInStream);
        EXPECT_EQ(rigidBase[i].components, rigid[i].components);
    }
    const std::vector<MagnumVertexAttribute> skinnedBase = StockAttributesForStride(68);
    ASSERT_EQ(6u, skinnedBase.size());
    for (std::size_t i = 0; i < skinnedBase.size(); ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_EQ(skinnedBase[i].location, skinned[i].location);
        EXPECT_EQ(skinnedBase[i].offsetInStream, skinned[i].offsetInStream);
        EXPECT_EQ(skinnedBase[i].components, skinned[i].components);
    }
}

TEST(MagnumStockProgramTest, TheColourCarryingPbrStridesSelectTheSamePbrProgramsAsTheirBareTwins)
{
    // plans/plan_gltf.md GLTF-465: a vertex colour is a term in the metallic-roughness product, not a
    // different material model, so stride 60 must select the same program as 48 and stride 80 the
    // same as 68. Selecting nothing (which is what this renderer did before) refuses the draw --
    // acceptable while the product was unimplemented, and wrong now that it is.
    MagnumStockSelector selector;
    selector.pbr = true;

    MagnumStockProgram program{};
    for (const std::size_t stride : {std::size_t{48}, std::size_t{60}})
    {
        SCOPED_TRACE(stride);
        selector.skinned = false;
        selector.strideInBytes = stride;
        ASSERT_TRUE(SelectStockProgram(selector, program));
        EXPECT_EQ(MagnumStockProgram::Pbr, program);
    }
    for (const std::size_t stride : {std::size_t{68}, std::size_t{80}})
    {
        SCOPED_TRACE(stride);
        selector.skinned = true;
        selector.strideInBytes = stride;
        ASSERT_TRUE(SelectStockProgram(selector, program));
        EXPECT_EQ(MagnumStockProgram::PbrSkinned, program);
    }

    // Stride 76 -- skinned PBR with a second UV set and NO colour -- is still refused, because this
    // renderer samples one UV set and nothing about GLTF-465 changed that. Refusing it is the
    // acceptable state; drawing it as if the second set did not exist would not be.
    selector.skinned = true;
    selector.strideInBytes = 76;
    EXPECT_FALSE(SelectStockProgram(selector, program));
}

TEST(MagnumStockShaderTest, ThePbrProgramsMultiplyCOLOR_0IntoBaseColourAndItsAlphaUnderTheEffectsGate)
{
    // The generated source IS the shader here -- there is no offline blob to inspect -- so this
    // asserts §3.9.2's product in the text that gets compiled: the attribute is declared, carried to
    // the fragment stage, gated on the effect's own switch, and multiplied into BOTH the albedo and
    // the alpha. The alpha half is what a BLEND-mode vertex-coloured primitive's transparency is.
    for (const MagnumStockProgram program : {MagnumStockProgram::Pbr, MagnumStockProgram::PbrSkinned})
    {
        const std::string vertex = StockVertexShaderSource(program);
        const std::string fragment = StockFragmentShaderSource(program);
        EXPECT_NE(std::string::npos, vertex.find("layout(location=6) in vec4 aColor;"));
        EXPECT_NE(std::string::npos, vertex.find("out vec4 vColor;"));
        EXPECT_NE(std::string::npos, vertex.find("vColor = aColor;"));
        EXPECT_NE(std::string::npos, fragment.find("uniform float uVertexColorEnabled;"));
        EXPECT_NE(std::string::npos, fragment.find(
            "vec4 cnaVertexColor = (uVertexColorEnabled > 0.5) ? vColor : vec4(1.0);"));
        EXPECT_NE(std::string::npos, fragment.find(
            "vec3 albedo = baseLinear * uDiffuseColor.rgb * cnaVertexColor.rgb;"));
        EXPECT_NE(std::string::npos, fragment.find(
            "float alpha = baseColor.a * uDiffuseColor.a * cnaVertexColor.a;"));
    }
}

TEST(MagnumVertexLayoutTest, SkinnedStridesShareLocationsZeroToFourByteForByte)
{
    // Stride 56 appends its colour rather than inserting it, which is the whole reason one skinned
    // program can serve both -- an inserted colour would shift every later element's location.
    const std::vector<MagnumVertexAttribute> narrow = StockAttributesForStride(52);
    const std::vector<MagnumVertexAttribute> wide = StockAttributesForStride(56);
    ASSERT_EQ(narrow.size(), 5u);
    ASSERT_EQ(wide.size(), 6u);
    for (std::size_t i = 0; i < narrow.size(); ++i)
    {
        EXPECT_EQ(narrow[i].location, wide[i].location) << "element " << i;
        EXPECT_EQ(narrow[i].offsetInStream, wide[i].offsetInStream) << "element " << i;
        EXPECT_EQ(narrow[i].components, wide[i].components) << "element " << i;
    }
    EXPECT_EQ(wide[5].location, 5);
    EXPECT_EQ(wide[5].offsetInStream, 52);
    EXPECT_TRUE(wide[5].normalized);
}

TEST(MagnumVertexLayoutTest, PbrStridesResolveToTheirTangentCarryingAttributes)
{
    const std::vector<MagnumVertexAttribute> plain = StockAttributesForStride(48);
    ASSERT_EQ(plain.size(), 4u);
    // The tangent is a float4: its w carries the bitangent handedness, and dropping it flips the
    // bitangent on every mirrored UV shell.
    EXPECT_EQ(plain[2].components, 4);
    EXPECT_EQ(plain[2].offsetInStream, 24);
    EXPECT_EQ(plain[3].components, 2);
    EXPECT_EQ(plain[3].offsetInStream, 40);

    const std::vector<MagnumVertexAttribute> skinned = StockAttributesForStride(68);
    ASSERT_EQ(skinned.size(), 6u);
    for (std::size_t i = 0; i < plain.size(); ++i)
    {
        EXPECT_EQ(plain[i].location, skinned[i].location) << "element " << i;
        EXPECT_EQ(plain[i].offsetInStream, skinned[i].offsetInStream) << "element " << i;
    }
    EXPECT_EQ(skinned[4].offsetInStream, 48);
    EXPECT_EQ(skinned[5].offsetInStream, 64);
    EXPECT_TRUE(skinned[5].integral);
}

TEST(MagnumVertexLayoutTest, OnlyBoneIndicesArriveAsIntegers)
{
    // Byte4 is a float-converted format everywhere else it appears; only the bone indices, which
    // index a uniform array, need real integers.
    const std::vector<MagnumVertexAttribute> skinned = StockAttributesForStride(52);
    ASSERT_EQ(skinned.size(), 5u);
    for (std::size_t i = 0; i < 4u; ++i)
        EXPECT_FALSE(skinned[i].integral) << "element " << i;
    EXPECT_TRUE(skinned[4].integral);
    EXPECT_EQ(skinned[4].offsetInStream, 48);

    const std::vector<VertexElement> byte4Declaration{
        VertexElement(0, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
    };
    EXPECT_FALSE(AttributesForDeclaration(byte4Declaration, 0, 0)[0].integral);
}

TEST(MagnumVertexLayoutTest, PositionColorTextureOffsetsMatchThePackedLayout)
{
    const std::vector<MagnumVertexAttribute> attributes = StockAttributesForStride(24);
    ASSERT_EQ(attributes.size(), 3u);
    EXPECT_EQ(attributes[0].location, 0);
    EXPECT_EQ(attributes[0].offsetInStream, 0);
    EXPECT_EQ(attributes[0].components, 3);
    EXPECT_FALSE(attributes[0].normalized);

    EXPECT_EQ(attributes[1].location, 1);
    EXPECT_EQ(attributes[1].offsetInStream, 12);
    EXPECT_EQ(attributes[1].components, 4);
    EXPECT_TRUE(attributes[1].normalized) << "packed Color must arrive normalized into 0..1";

    EXPECT_EQ(attributes[2].location, 2);
    EXPECT_EQ(attributes[2].offsetInStream, 16);
    EXPECT_EQ(attributes[2].components, 2);
}

TEST(MagnumVertexLayoutTest, DeclarationElementsTakeConsecutiveLocationsInOrder)
{
    const std::vector<VertexElement> elements{
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    };
    const std::vector<MagnumVertexAttribute> attributes = AttributesForDeclaration(elements, 0, 0);

    ASSERT_EQ(attributes.size(), 3u);
    EXPECT_EQ(attributes[0].location, 0);
    EXPECT_EQ(attributes[1].location, 1);
    EXPECT_EQ(attributes[2].location, 2);
    EXPECT_EQ(attributes[0].components, 3);
    EXPECT_EQ(attributes[1].components, 3);
    EXPECT_EQ(attributes[2].components, 2);
}

TEST(MagnumVertexLayoutTest, DeclarationOffsetsBecomeStreamLocalAndLocationsAreRebased)
{
    // A second stream's declaration offsets are combined-layout offsets, so the stream's own byte
    // base is subtracted before the element can address its own buffer.
    const std::vector<VertexElement> elements{
        VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
        VertexElement(48, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
    };
    const std::vector<MagnumVertexAttribute> attributes = AttributesForDeclaration(elements, 4, 32);

    ASSERT_EQ(attributes.size(), 2u);
    EXPECT_EQ(attributes[0].location, 4);
    EXPECT_EQ(attributes[0].offsetInStream, 0);
    EXPECT_EQ(attributes[1].location, 5);
    EXPECT_EQ(attributes[1].offsetInStream, 16);
}

TEST(MagnumVertexLayoutTest, PackedColorAndByte4DifferOnlyInNormalization)
{
    const std::vector<VertexElement> elements{
        VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        VertexElement(4, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
    };
    const std::vector<MagnumVertexAttribute> attributes = AttributesForDeclaration(elements, 0, 0);

    ASSERT_EQ(attributes.size(), 2u);
    EXPECT_EQ(attributes[0].components, 4);
    EXPECT_TRUE(attributes[0].normalized);
    EXPECT_EQ(attributes[1].components, 4);
    EXPECT_FALSE(attributes[1].normalized);
}

TEST(MagnumVertexLayoutTest, NormalizedShortFormatsAreMarkedNormalized)
{
    const std::vector<VertexElement> elements{
        VertexElement(0, VertexElementFormat::Short2, VertexElementUsage::TextureCoordinate, 0),
        VertexElement(4, VertexElementFormat::NormalizedShort2,
                      VertexElementUsage::TextureCoordinate, 1),
        VertexElement(8, VertexElementFormat::HalfVector4, VertexElementUsage::Position, 1),
    };
    const std::vector<MagnumVertexAttribute> attributes = AttributesForDeclaration(elements, 0, 0);

    ASSERT_EQ(attributes.size(), 3u);
    EXPECT_FALSE(attributes[0].normalized);
    EXPECT_TRUE(attributes[1].normalized);
    EXPECT_EQ(attributes[2].components, 4);
    EXPECT_FALSE(attributes[2].normalized);
}

// ---- Stock shader generation ----

namespace
{
    MagnumStockProgram SelectOrDie(std::size_t stride, bool dualTexture = false,
                                   bool envMapping = false, bool skinned = false,
                                   bool pbr = false)
    {
        MagnumStockSelector selector;
        selector.strideInBytes = stride;
        selector.dualTexture = dualTexture;
        selector.envMapping = envMapping;
        selector.skinned = skinned;
        selector.pbr = pbr;
        MagnumStockProgram program{};
        EXPECT_TRUE(SelectStockProgram(selector, program));
        return program;
    }

    constexpr MagnumStockProgram kAllPrograms[] = {
        MagnumStockProgram::PositionColor,
        MagnumStockProgram::PositionTexture,
        MagnumStockProgram::PositionColorTexture,
        MagnumStockProgram::PositionNormalTexture,
        MagnumStockProgram::DualTexture,
        MagnumStockProgram::DualTextureColored,
        MagnumStockProgram::EnvironmentMap,
        MagnumStockProgram::PositionNormalTextureVertexLit,
        MagnumStockProgram::Skinned,
        MagnumStockProgram::SkinnedVertexLit,
        MagnumStockProgram::Pbr,
        MagnumStockProgram::PbrSkinned,
    };
}

TEST(MagnumStockShaderTest, BuiltInStridesSelectTheirStockProgram)
{
    EXPECT_EQ(SelectOrDie(16), MagnumStockProgram::PositionColor);
    EXPECT_EQ(SelectOrDie(20), MagnumStockProgram::PositionTexture);
    EXPECT_EQ(SelectOrDie(24), MagnumStockProgram::PositionColorTexture);
    EXPECT_EQ(SelectOrDie(32), MagnumStockProgram::PositionNormalTexture);
}

TEST(MagnumStockShaderTest, UnknownStrideSelectsNoStockProgram)
{
    MagnumStockSelector selector;
    MagnumStockProgram program{};
    selector.strideInBytes = 52;
    EXPECT_FALSE(SelectStockProgram(selector, program));
    selector.strideInBytes = 0;
    EXPECT_FALSE(SelectStockProgram(selector, program));
}

TEST(MagnumStockShaderTest, DualTextureSelectsTheVertexColourAwareProgramOnlyForStride24)
{
    // Stride 20 carries no colour at all, so tinting the two-layer result by one would read an
    // input the layout does not have; stride 24 does, and DualTextureEffect must honour it.
    EXPECT_EQ(SelectOrDie(20, true), MagnumStockProgram::DualTexture);
    EXPECT_EQ(SelectOrDie(24, true), MagnumStockProgram::DualTextureColored);
}

TEST(MagnumStockShaderTest, DualTextureOnALayoutWithoutTextureCoordinatesSelectsNothing)
{
    MagnumStockSelector selector;
    selector.dualTexture = true;
    MagnumStockProgram program{};
    selector.strideInBytes = 16;
    EXPECT_FALSE(SelectStockProgram(selector, program));
    selector.strideInBytes = 32;
    EXPECT_FALSE(SelectStockProgram(selector, program));
}

TEST(MagnumStockShaderTest, EnvironmentMappingSelectsItsOwnProgramOnTheNormalCarryingLayout)
{
    EXPECT_EQ(SelectOrDie(32, false, true), MagnumStockProgram::EnvironmentMap);
}

TEST(MagnumStockShaderTest, EnvironmentMappingOnALayoutWithoutNormalsSelectsNothing)
{
    // The reflection basis needs a per-vertex normal; a layout without one has nothing to reflect
    // about, so the draw is refused rather than rendered through some other program.
    MagnumStockSelector selector;
    selector.envMapping = true;
    MagnumStockProgram program{};
    for (const std::size_t stride : {std::size_t{16}, std::size_t{20}, std::size_t{24}})
    {
        selector.strideInBytes = stride;
        EXPECT_FALSE(SelectStockProgram(selector, program)) << "stride " << stride;
    }
}

TEST(MagnumStockShaderTest, EnvironmentMappingWinsOverDualTexturingOnItsOwnLayout)
{
    // Both flags name programs over different layouts, so a draw carrying both can only mean the
    // one whose layout it actually supplies.
    EXPECT_EQ(SelectOrDie(32, true, true), MagnumStockProgram::EnvironmentMap);
    EXPECT_EQ(SelectOrDie(24, true, false), MagnumStockProgram::DualTextureColored);
}

TEST(MagnumStockShaderTest, EnvironmentMapBlendsByLerpAndAddsSpecularOnTop)
{
    // The reflection REPLACES the lit base as the amount rises -- an additive blend would answer
    // base+cube at amount 1 instead. Only the lerp is FNA's own formula.
    const std::string source = StockFragmentShaderSource(MagnumStockProgram::EnvironmentMap);
    EXPECT_NE(source.find("mix(litColor * diffuse.rgb, reflection.rgb * alpha, vEnvMapBlend)"),
              std::string::npos);
    EXPECT_NE(source.find("uEnvMapSpecular * reflection.a * alpha"), std::string::npos);
    EXPECT_NE(source.find("uniform samplerCube uEnvMap"), std::string::npos);
    EXPECT_NE(source.find("reflect(-eye, normal)"), std::string::npos);
}

TEST(MagnumStockShaderTest, EnvironmentMapWeightsItsBlendByFresnelOnlyWhenAsked)
{
    const std::string source = StockVertexShaderSource(MagnumStockProgram::EnvironmentMap);
    EXPECT_NE(source.find("uFresnelEnabled > 0.5"), std::string::npos);
    EXPECT_NE(source.find("pow(max(1.0 - abs(viewAngle), 0.0), uFresnelFactor) * uEnvMapAmount"),
              std::string::npos);
}

TEST(MagnumStockShaderTest, EnvironmentMapCarriesNoAmbientOrSpecularLightTerms)
{
    // EnvironmentMapEffect has neither in XNA -- the reflection stands in for them. Reusing
    // BasicEffect's fragment stage would silently add an ambient term the effect does not have.
    const std::string source = StockFragmentShaderSource(MagnumStockProgram::EnvironmentMap);
    EXPECT_EQ(source.find("uAmbientColor"), std::string::npos);
    EXPECT_EQ(source.find("uSpecularPower"), std::string::npos);
    EXPECT_NE(source.find("uEmissiveColor"), std::string::npos);
}

TEST(MagnumStockShaderTest, SkinningSelectsItsOwnProgramOnBothBonePaletteLayouts)
{
    // 52 and 56 differ only by an APPENDED per-vertex colour, so one program serves both.
    EXPECT_EQ(SelectOrDie(52, false, false, true), MagnumStockProgram::Skinned);
    EXPECT_EQ(SelectOrDie(56, false, false, true), MagnumStockProgram::Skinned);
}

TEST(MagnumStockShaderTest, SkinningOnALayoutWithoutBoneDataSelectsNothing)
{
    MagnumStockSelector selector;
    selector.skinned = true;
    MagnumStockProgram program{};
    for (const std::size_t stride : {std::size_t{16}, std::size_t{20},
                                     std::size_t{24}, std::size_t{32}})
    {
        selector.strideInBytes = stride;
        EXPECT_FALSE(SelectStockProgram(selector, program)) << "stride " << stride;
    }
}

TEST(MagnumStockShaderTest, SkinningIsResolvedBeforeEveryOtherEffectFlag)
{
    // Only the skinned layouts carry a bone palette, so a draw naming skinning cannot mean any
    // program that would share its stride.
    EXPECT_EQ(SelectOrDie(52, true, true, true), MagnumStockProgram::Skinned);
}

TEST(MagnumStockShaderTest, SkinnedShaderSumsOnlyTheDeclaredWeightPairs)
{
    // A mesh authored for one or two bones per vertex leaves the remaining weights undefined
    // rather than zeroed, so summing all four unconditionally corrupts it.
    const std::string source = StockVertexShaderSource(MagnumStockProgram::Skinned);
    EXPECT_NE(source.find("mat4 skin = uBones[aBoneIndices.x] * aBoneWeights.x"), std::string::npos);
    EXPECT_NE(source.find("if (uWeightsPerVertex >= 2)"), std::string::npos);
    EXPECT_NE(source.find("if (uWeightsPerVertex >= 4)"), std::string::npos);
}

TEST(MagnumStockShaderTest, SkinnedShaderDeclaresABonePaletteOfTheXnaLimit)
{
    const std::string source = StockVertexShaderSource(MagnumStockProgram::Skinned);
    EXPECT_NE(source.find("uniform mat4 uBones[" + std::to_string(kMagnumMaxBones) + "]"),
              std::string::npos);
    EXPECT_EQ(kMagnumMaxBones, 72);
}

TEST(MagnumStockShaderTest, SkinnedShaderReadsBoneIndicesAsIntegers)
{
    // They index a uniform array; a float round trip loses exactness for the higher bone numbers.
    const std::string source = StockVertexShaderSource(MagnumStockProgram::Skinned);
    EXPECT_NE(source.find("in uvec4 aBoneIndices"), std::string::npos);
}

TEST(MagnumStockShaderTest, SkinnedShaderFallsBackToTheUnskinnedNormalWhenThePaletteDegenerates)
{
    // An all-zero palette collapses the skinned normal to nothing, which would light the surface
    // black rather than leave it as authored.
    const std::string source = StockVertexShaderSource(MagnumStockProgram::Skinned);
    EXPECT_NE(source.find("skinnedNormalLength > 1e-6"), std::string::npos);
    EXPECT_NE(source.find("(skinnedNormal / skinnedNormalLength) : aNormal"), std::string::npos);
}

TEST(MagnumStockShaderTest, SkinnedShaderCarriesNoAmbientTermOfItsOwn)
{
    // SkinnedEffect folds ambient into EmissiveColor before the draw, so a separate ambient term
    // here would double-count it.
    const std::string source = StockFragmentShaderSource(MagnumStockProgram::Skinned);
    EXPECT_EQ(source.find("uAmbientColor"), std::string::npos);
    EXPECT_NE(source.find("uEmissiveColor"), std::string::npos);
    EXPECT_NE(source.find("uSpecularPower"), std::string::npos);
}

TEST(MagnumStockShaderTest, PbrSelectsItsPlainOrSkinnedProgramByLayout)
{
    EXPECT_EQ(SelectOrDie(48, false, false, false, true), MagnumStockProgram::Pbr);
    EXPECT_EQ(SelectOrDie(68, false, false, true, true), MagnumStockProgram::PbrSkinned);
}

TEST(MagnumStockShaderTest, PbrOnTheWrongLayoutSelectsNothing)
{
    // Each PBR program has exactly one layout: the plain one has no bone palette to read and the
    // skinned one has no unskinned stride to fall back to.
    MagnumStockSelector selector;
    selector.pbr = true;
    MagnumStockProgram program{};
    selector.strideInBytes = 68;
    EXPECT_FALSE(SelectStockProgram(selector, program)) << "plain PBR over the skinned stride";
    selector.skinned = true;
    selector.strideInBytes = 48;
    EXPECT_FALSE(SelectStockProgram(selector, program)) << "skinned PBR over the plain stride";
}

TEST(MagnumStockShaderTest, PbrIsResolvedBeforeSkinning)
{
    // `pbr && skinned` is one program, not a choice between two: the two share a material model
    // and differ only by whether a bone palette deforms the tangent frame.
    EXPECT_EQ(SelectOrDie(68, false, false, true, true), MagnumStockProgram::PbrSkinned);
    EXPECT_EQ(SelectOrDie(52, false, false, true, false), MagnumStockProgram::Skinned);
}

TEST(MagnumStockShaderTest, PbrShaderImplementsTheGltfMetallicRoughnessBrdf)
{
    const std::string source = StockFragmentShaderSource(MagnumStockProgram::Pbr);
    // glTF's own packing: green is roughness, blue is metallic. Swapping them is invisible on any
    // texture whose two channels happen to agree.
    EXPECT_NE(source.find("metallicRoughness.g * uRoughnessFactor"), std::string::npos);
    EXPECT_NE(source.find("metallicRoughness.b * uMetallicFactor"), std::string::npos);
    // The diffuse lobe scales away as the surface becomes metallic, and F0 becomes the albedo.
    EXPECT_NE(source.find("albedo * (1.0 - metallic)"), std::string::npos);
    // This used to spell the dielectric endpoint as the literal `vec3(0.04)` and had been failing
    // silently since KHR_materials_specular/ior made it a transported value: the endpoint is now
    // `dielectricF0`, computed from uSpecularFresnelInputs and the specular colour texture. The rule
    // being asserted is unchanged -- F0 interpolates from the dielectric endpoint to the albedo with
    // metalness -- so only the name of the endpoint moves. Found by running Magnum's own tests, which
    // this environment had never done (plans/plan_gltf.md GLTF-465).
    EXPECT_NE(source.find("mix(dielectricF0, albedo, metallic)"), std::string::npos);
}

TEST(MagnumStockShaderTest, PbrShaderReorthogonalizesTheTangentBeforeBuildingItsBasis)
{
    // The interpolated tangent is no longer exactly perpendicular to the interpolated normal, so
    // a basis built from it directly is skewed across the triangle's interior.
    const std::string source = StockFragmentShaderSource(MagnumStockProgram::Pbr);
    EXPECT_NE(source.find("normalize(vTangent - normal * dot(normal, vTangent))"),
              std::string::npos);
    EXPECT_NE(source.find("cross(normal, tangent) * vBitangentSign"), std::string::npos);
}

TEST(MagnumStockShaderTest, PbrShaderReadsTheOcclusionMapThroughItsOwnFlipFlag)
{
    // Five sampled units do not fit in uRtFlipV's four components, so the fifth needs its own.
    const std::string source = StockFragmentShaderSource(MagnumStockProgram::Pbr);
    EXPECT_NE(source.find("uniform vec4 uRtFlipVHi"), std::string::npos);
    // Also stale rather than wrong: the sample now goes through the map's own KHR_texture_transform
    // row (`cnaPbrTransformUV(vTexCoord, 4)`) before the flip flag is applied. Both properties are
    // still asserted, which is the point -- the occlusion unit keeps its own flip component AND its
    // own transform slot.
    EXPECT_NE(source.find("texture(uOcclusionMap, cnaSampleUV(cnaPbrTransformUV(vTexCoord, 4), uRtFlipVHi.x))"),
              std::string::npos);
}

TEST(MagnumStockShaderTest, OnlyTheSkinnedPbrProgramDeformsItsTangentFrame)
{
    const std::string plain = StockVertexShaderSource(MagnumStockProgram::Pbr);
    EXPECT_EQ(plain.find("uBones"), std::string::npos);
    EXPECT_NE(plain.find("vec3 boneTangent = aTangent.xyz"), std::string::npos);

    // Leaving the tangent frame in bind pose would light a deformed surface with the normal map of
    // an undeformed one.
    const std::string skinned = StockVertexShaderSource(MagnumStockProgram::PbrSkinned);
    EXPECT_NE(skinned.find("uBones["), std::string::npos);
    EXPECT_NE(skinned.find("vec3 boneTangent = mat3(skin) * aTangent.xyz"), std::string::npos);
    EXPECT_NE(skinned.find("if (uWeightsPerVertex >= 2)"), std::string::npos);
}

TEST(MagnumStockShaderTest, EveryStockShaderTargetsTheDesktopCoreProfile)
{
    for (const MagnumStockProgram program : kAllPrograms)
    {
        EXPECT_EQ(StockVertexShaderSource(program).rfind("#version 330 core", 0), 0u);
        EXPECT_EQ(StockFragmentShaderSource(program).rfind("#version 330 core", 0), 0u);
    }
    EXPECT_EQ(SpriteVertexShaderSource().rfind("#version 330 core", 0), 0u);
    EXPECT_EQ(SpriteFragmentShaderSource().rfind("#version 330 core", 0), 0u);
}

TEST(MagnumStockShaderTest, EachLayoutDeclaresExactlyTheInputsItsStrideCarries)
{
    const std::string positionColor = StockVertexShaderSource(MagnumStockProgram::PositionColor);
    EXPECT_NE(positionColor.find("location=1) in vec4 aColor"), std::string::npos);
    EXPECT_EQ(positionColor.find("aTexCoord;"), std::string::npos);
    EXPECT_EQ(positionColor.find("aNormal;"), std::string::npos);

    const std::string positionTexture = StockVertexShaderSource(MagnumStockProgram::PositionTexture);
    EXPECT_NE(positionTexture.find("location=1) in vec2 aTexCoord"), std::string::npos);
    EXPECT_EQ(positionTexture.find("aColor;"), std::string::npos);

    const std::string colorTexture =
        StockVertexShaderSource(MagnumStockProgram::PositionColorTexture);
    EXPECT_NE(colorTexture.find("location=1) in vec4 aColor"), std::string::npos);
    EXPECT_NE(colorTexture.find("location=2) in vec2 aTexCoord"), std::string::npos);

    const std::string normalTexture =
        StockVertexShaderSource(MagnumStockProgram::PositionNormalTexture);
    EXPECT_NE(normalTexture.find("location=1) in vec3 aNormal"), std::string::npos);
    EXPECT_NE(normalTexture.find("location=2) in vec2 aTexCoord"), std::string::npos);

    const std::string environmentMap = StockVertexShaderSource(MagnumStockProgram::EnvironmentMap);
    EXPECT_NE(environmentMap.find("location=1) in vec3 aNormal"), std::string::npos);
    EXPECT_NE(environmentMap.find("location=2) in vec2 aTexCoord"), std::string::npos);
}

TEST(MagnumStockShaderTest, DualTextureProgramsDeclareTheirOwnLayoutsInputs)
{
    // Each dual-texture program must declare the SAME inputs as the plain program sharing its
    // stride -- reading a texture coordinate from the wrong location is exactly the failure a
    // shared-source generator invites.
    const std::string plain = StockVertexShaderSource(MagnumStockProgram::DualTexture);
    EXPECT_NE(plain.find("location=1) in vec2 aTexCoord"), std::string::npos);
    EXPECT_EQ(plain.find("aColor;"), std::string::npos);

    const std::string colored = StockVertexShaderSource(MagnumStockProgram::DualTextureColored);
    EXPECT_NE(colored.find("location=1) in vec4 aColor"), std::string::npos);
    EXPECT_NE(colored.find("location=2) in vec2 aTexCoord"), std::string::npos);
}

TEST(MagnumStockShaderTest, EveryStockVertexShaderReservesTheInstanceMatrixLocations)
{
    for (const MagnumStockProgram program : kAllPrograms)
    {
        const std::string source = StockVertexShaderSource(program);
        EXPECT_NE(source.find("location=12) in vec4 cnaInstanceCol0"), std::string::npos);
        EXPECT_NE(source.find("location=15) in vec4 cnaInstanceCol3"), std::string::npos);
        EXPECT_NE(source.find("uniform float uCnaInstanced"), std::string::npos);
    }
}

TEST(MagnumStockShaderTest, EveryStockFragmentShaderCarriesAlphaTestAndFog)
{
    for (const MagnumStockProgram program : kAllPrograms)
    {
        const std::string source = StockFragmentShaderSource(program);
        EXPECT_NE(source.find("uAlphaTest"), std::string::npos);
        EXPECT_NE(source.find("discard"), std::string::npos);
        EXPECT_NE(source.find("mix(uFogColor"), std::string::npos);
    }
}

TEST(MagnumStockShaderTest, OnlyTexturedLayoutsSampleTheDiffuseTexture)
{
    EXPECT_EQ(StockFragmentShaderSource(MagnumStockProgram::PositionColor)
                  .find("texture(uTexture"), std::string::npos);
    EXPECT_NE(StockFragmentShaderSource(MagnumStockProgram::PositionTexture)
                  .find("texture(uTexture"), std::string::npos);
    EXPECT_NE(StockFragmentShaderSource(MagnumStockProgram::PositionColorTexture)
                  .find("texture(uTexture"), std::string::npos);
    EXPECT_NE(StockFragmentShaderSource(MagnumStockProgram::PositionNormalTexture)
                  .find("texture(uTexture"), std::string::npos);
}

TEST(MagnumStockShaderTest, OnlyDualTextureProgramsSampleASecondLayer)
{
    for (const MagnumStockProgram program : kAllPrograms)
    {
        const bool isDual = program == MagnumStockProgram::DualTexture
                         || program == MagnumStockProgram::DualTextureColored;
        const std::string source = StockFragmentShaderSource(program);
        EXPECT_EQ(source.find("uTexture2") != std::string::npos, isDual);
    }
}

TEST(MagnumStockShaderTest, OnlyTheEnvironmentMapProgramSamplesACubeMap)
{
    for (const MagnumStockProgram program : kAllPrograms)
    {
        const bool isEnvironmentMap = program == MagnumStockProgram::EnvironmentMap;
        const std::string source = StockFragmentShaderSource(program);
        EXPECT_EQ(source.find("samplerCube") != std::string::npos, isEnvironmentMap);
    }
}

TEST(MagnumStockShaderTest, DualTextureOverbrightsTheBaseLayerAndTintsOnlyWhenTheLayoutCanBe)
{
    // DualTextureEffect's second layer is a modulate-2x lightmap, so the base is doubled and a 0.5
    // overlay texel is neutral. Dropping the x2 halves every dual-textured surface, which is the
    // kind of thing only an explicit assertion catches.
    const std::string plain = StockFragmentShaderSource(MagnumStockProgram::DualTexture);
    EXPECT_NE(plain.find("base.rgb *= 2.0"), std::string::npos);
    EXPECT_EQ(plain.find("uVertexColorEnabled > 0.5"), std::string::npos);

    const std::string colored = StockFragmentShaderSource(MagnumStockProgram::DualTextureColored);
    EXPECT_NE(colored.find("base.rgb *= 2.0"), std::string::npos);
    EXPECT_NE(colored.find("uVertexColorEnabled > 0.5"), std::string::npos);
}

TEST(MagnumStockShaderTest, DualTextureCorrectsRowOrderPerSampledUnit)
{
    // Either layer can be a render target, and the two flags are independent -- reusing unit 0's
    // flag for unit 1 would flip a correctly-oriented overlay.
    for (const MagnumStockProgram program : {MagnumStockProgram::DualTexture,
                                             MagnumStockProgram::DualTextureColored})
    {
        const std::string source = StockFragmentShaderSource(program);
        EXPECT_NE(source.find("cnaSampleUV(vTexCoord, uRtFlipV.x)"), std::string::npos);
        EXPECT_NE(source.find("cnaSampleUV(vTexCoord, uRtFlipV.y)"), std::string::npos);
    }
}

TEST(MagnumStockShaderTest, TexturedLayoutsCorrectRenderTargetRowOrderWhenSampling)
{
    // A render target's colour texture is stored bottom-up; a stock shader that samples one must
    // mirror V, and it does so through the per-unit flag rather than by baking the flip in.
    for (const MagnumStockProgram program : {MagnumStockProgram::PositionTexture,
                                             MagnumStockProgram::PositionColorTexture,
                                             MagnumStockProgram::PositionNormalTexture})
    {
        const std::string source = StockFragmentShaderSource(program);
        EXPECT_NE(source.find("cnaSampleUV(vTexCoord, uRtFlipV.x)"), std::string::npos);
    }
}

TEST(MagnumStockShaderTest, SpriteShaderMultipliesTheSampleByTheVertexTint)
{
    const std::string fragment = SpriteFragmentShaderSource();
    EXPECT_NE(fragment.find("texture(texture1, vTexCoord) * vColor"), std::string::npos);
    EXPECT_NE(SpriteVertexShaderSource().find("uniform mat4 projection"), std::string::npos);
}

TEST(MagnumShaderVersionTest, TheDeclaredVersionSurvivesToTheCompiler)
{
    // Every stage would otherwise be compiled as 3.30 regardless of what it asked for, so an
    // effect needing a 4.00 feature would fail for a reason nothing in its own source explains.
    EXPECT_EQ(DeclaredVersion("#version 400 core\nvoid main() {}\n"), Mg::GL::Version::GL400);
    EXPECT_EQ(DeclaredVersion("#version 410\n"), Mg::GL::Version::GL410);
    EXPECT_EQ(DeclaredVersion("#version 420\n"), Mg::GL::Version::GL420);
    EXPECT_EQ(DeclaredVersion("#version 430\n"), Mg::GL::Version::GL430);
    EXPECT_EQ(DeclaredVersion("#version 440\n"), Mg::GL::Version::GL440);
    EXPECT_EQ(DeclaredVersion("#version 450\n"), Mg::GL::Version::GL450);
    EXPECT_EQ(DeclaredVersion("#version 460\n"), Mg::GL::Version::GL460);
    EXPECT_EQ(DeclaredVersion("  \n\t#version 400 core\nvoid main() {}\n"),
              Mg::GL::Version::GL400);
}

TEST(MagnumShaderVersionTest, AnAbsentOrUnrecognizedVersionKeepsTheStockOne)
{
    // 3.30 is what CNA's own stock shaders are written against, so it is the safe fallback -- a
    // source with no directive at all must not be rejected, and a version this renderer has no
    // enumerator for must not become a guess at a later one.
    EXPECT_EQ(DeclaredVersion("void main() {}\n"), Mg::GL::Version::GL330);
    EXPECT_EQ(DeclaredVersion(""), Mg::GL::Version::GL330);
    EXPECT_EQ(DeclaredVersion("#version 330 core\n"), Mg::GL::Version::GL330);
    EXPECT_EQ(DeclaredVersion("#version 999\n"), Mg::GL::Version::GL330);
    EXPECT_EQ(DeclaredVersion("#version core\n"), Mg::GL::Version::GL330);
    // Not a declaration at the start of the source, so not this function's business.
    EXPECT_EQ(DeclaredVersion("void main() {}\n#version 400\n"), Mg::GL::Version::GL330);
}

TEST(MagnumShaderVersionTest, OnlyALeadingVersionLineIsStripped)
{
    // GL::Shader writes its own #version and then a #line, so a source keeping its own would push
    // the directive to line 2 and fail to compile at all.
    EXPECT_EQ(StripVersionDirective("#version 400 core\nvoid main() {}\n"),
              "void main() {}\n");
    EXPECT_EQ(StripVersionDirective("\n  #version 330 core\nbody\n"), "body\n");
    EXPECT_EQ(StripVersionDirective("void main() {}\n"), "void main() {}\n");
    EXPECT_EQ(StripVersionDirective("#version 400 core"), "");
    EXPECT_EQ(StripVersionDirective(""), "");
}

namespace
{
    MagnumStockProgram SelectVertexLitOrDie(std::size_t stride, bool skinned)
    {
        MagnumStockSelector selector;
        selector.strideInBytes = stride;
        selector.skinned = skinned;
        selector.vertexLighting = true;
        MagnumStockProgram program{};
        EXPECT_TRUE(SelectStockProgram(selector, program));
        return program;
    }
}

TEST(MagnumStockShaderTest, VertexLightingSelectsItsOwnProgramOnEveryNormalCarryingStride)
{
    // XNA's own BasicEffect/SkinnedEffect default is per-vertex lighting, so this is the family a
    // draw that says nothing about lighting frequency lands in.
    EXPECT_EQ(SelectVertexLitOrDie(32, false),
              MagnumStockProgram::PositionNormalTextureVertexLit);
    EXPECT_EQ(SelectVertexLitOrDie(52, true), MagnumStockProgram::SkinnedVertexLit);
    EXPECT_EQ(SelectVertexLitOrDie(56, true), MagnumStockProgram::SkinnedVertexLit);
}

TEST(MagnumStockShaderTest, VertexLightingLeavesTheNormalFreeStridesAlone)
{
    // Lighting needs a normal, and only stride 32 and the skinned layouts carry one. A draw that
    // asks for per-vertex lighting over a layout that cannot be lit must not be diverted to some
    // other program by the flag.
    for (const std::size_t stride : {std::size_t{16}, std::size_t{20}, std::size_t{24}})
    {
        MagnumStockSelector selector;
        selector.strideInBytes = stride;
        selector.vertexLighting = true;
        MagnumStockProgram program{};
        ASSERT_TRUE(SelectStockProgram(selector, program)) << "stride " << stride;
        EXPECT_EQ(program, SelectOrDie(stride)) << "stride " << stride;
    }
}

TEST(MagnumStockShaderTest, TheVertexLitFamilyEvaluatesLightingInItsVertexStage)
{
    for (const MagnumStockProgram program : {MagnumStockProgram::PositionNormalTextureVertexLit,
                                             MagnumStockProgram::SkinnedVertexLit})
    {
        const std::string vertex = StockVertexShaderSource(program);
        const std::string fragment = StockFragmentShaderSource(program);

        EXPECT_NE(vertex.find("cnaLighting(vNormal, vWorldPosition, vLightSum, vSpecular)"),
                  std::string::npos);
        EXPECT_NE(vertex.find("out vec3 vLightSum;"), std::string::npos);
        EXPECT_NE(vertex.find("uniform vec3 uLight0Dir;"), std::string::npos);

        // The fragment stage reads the result rather than recomputing it, and therefore has no
        // light uniforms of its own to recompute it from.
        EXPECT_NE(fragment.find("in vec3 vLightSum;"), std::string::npos);
        EXPECT_EQ(fragment.find("void cnaLighting"), std::string::npos);
        EXPECT_EQ(fragment.find("uniform vec3 uLight0Dir;"), std::string::npos);
    }
}

TEST(MagnumStockShaderTest, ThePerPixelFamilyKeepsLightingInItsFragmentStage)
{
    for (const MagnumStockProgram program : {MagnumStockProgram::PositionNormalTexture,
                                             MagnumStockProgram::Skinned})
    {
        const std::string vertex = StockVertexShaderSource(program);
        const std::string fragment = StockFragmentShaderSource(program);

        EXPECT_NE(fragment.find("cnaLighting(vNormal, vWorldPosition, lightSum, specular)"),
                  std::string::npos);
        EXPECT_NE(fragment.find("uniform vec3 uLight0Dir;"), std::string::npos);
        EXPECT_EQ(vertex.find("void cnaLighting"), std::string::npos);
        EXPECT_EQ(vertex.find("out vec3 vLightSum;"), std::string::npos);
    }
}

TEST(MagnumStockShaderTest, BothLightingFamiliesShareOneFormula)
{
    // The two families must differ ONLY in which stage evaluates the lighting -- if the arithmetic
    // itself drifted, PreferPerPixelLighting would be changing the picture for a second reason.
    const std::string perPixel =
        StockFragmentShaderSource(MagnumStockProgram::PositionNormalTexture);
    const std::string perVertex =
        StockVertexShaderSource(MagnumStockProgram::PositionNormalTextureVertexLit);
    const std::size_t body = perPixel.find("void cnaLighting");
    ASSERT_NE(body, std::string::npos);
    const std::size_t end = perPixel.find("\n}\n", body);
    ASSERT_NE(end, std::string::npos);
    EXPECT_NE(perVertex.find(perPixel.substr(body, end - body)), std::string::npos);
}

TEST(MagnumStockShaderTest, OnlyTheSkinnedFamilyDropsTheSeparateAmbientTerm)
{
    // SkinnedEffect folds ambient into EmissiveColor before the draw, so its light sum carries no
    // ambient of its own -- in either family.
    EXPECT_EQ(StockFragmentShaderSource(MagnumStockProgram::Skinned).find("uAmbientColor"),
              std::string::npos);
    EXPECT_EQ(StockVertexShaderSource(MagnumStockProgram::SkinnedVertexLit).find("uAmbientColor"),
              std::string::npos);
    EXPECT_NE(StockFragmentShaderSource(MagnumStockProgram::PositionNormalTexture)
                  .find("uAmbientColor"), std::string::npos);
    EXPECT_NE(StockVertexShaderSource(MagnumStockProgram::PositionNormalTextureVertexLit)
                  .find("uAmbientColor"), std::string::npos);
}

#endif  // CNA_RENDERER_MAGNUM / CNA_RENDERER_PRESENT_MAGNUM
