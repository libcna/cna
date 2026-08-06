// SPDX-License-Identifier: MS-PL
//
// plan_magnum.md MAGNUM-40: structural GTest coverage for everything on the MAGNUM backend that
// does not need a live OpenGL context -- the XNA-ordinal -> Magnum-enum mappings, the vertex
// layout resolution both the stock and the declaration-driven routes share, and the generated
// stock GLSL. Everything that needs a real context (resource creation, draws, readback) is left to
// the on-device smoke checks, since a headless CI container has no GL driver to create one on.
#include <gtest/gtest.h>

#if defined(CNA_BACKEND_MAGNUM)
#include "CNA/Internal/Backends/Magnum/MagnumBuffers.hpp"
#include "CNA/Internal/Backends/Magnum/MagnumStateMapping.hpp"
#include "CNA/Internal/Backends/Magnum/MagnumStockShaders.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/InvalidOperationException.hpp"

#include <string>
#include <vector>

using namespace CNA::Internal::Backends::Magnum;
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
    MagnumStockProgram SelectOrDie(std::size_t stride, bool dualTexture = false)
    {
        MagnumStockSelector selector;
        selector.strideInBytes = stride;
        selector.dualTexture = dualTexture;
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

#endif  // CNA_BACKEND_MAGNUM
