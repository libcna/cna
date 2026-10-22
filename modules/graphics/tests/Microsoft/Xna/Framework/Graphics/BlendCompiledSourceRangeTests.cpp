// SPDX-License-Identifier: MS-PL
// ISSUE-027: compiled MRTs clamp each attachment according to its own format.
#include <array>
#include <string>
#include <gtest/gtest.h>
#include "CNA/GraphicsCapability.hpp"
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

namespace
{
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
struct TargetPair { SurfaceFormat normalized; bool normalizedFirst; };
class BlendCompiledSourceRangeTest : public testing::TestWithParam<TargetPair>
{
protected:
    GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(),
                          GraphicsProfile::HiDef, PresentationParameters{}};
    template<class Pixel>
    Pixel Read(RenderTarget2D& target)
    {
        Pixel pixel;
        const Rectangle centre(4, 4, 1, 1);
        target.GetData(0, &centre, &pixel, 0, 1);
        return pixel;
    }
};

TEST_P(BlendCompiledSourceRangeTest, AttachmentsUseTheirOwnFormatRange)
{
    RecordProperty("renderer", std::string(device.GetGraphicsRendererName()));
    if (!device.SupportsCapability(CNA::GraphicsCapability::CompiledEffects))
        GTEST_SKIP() << "Configured renderer has no compiled effects";
    if (!device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        GTEST_SKIP() << "Renderer has no MRT support";
    const auto [format, normalizedFirst] = GetParam();
    // XNA requires equal bits per pixel and forbids blending Single/HalfVector2.
    // The 64-bit pair can mix UNORM and HDR; 32-bit pairs use a Color sibling.
    const auto floating = format == SurfaceFormat::Rgba64
        ? SurfaceFormat::HdrBlendable : SurfaceFormat::Color;
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(format) ||
        !device.SupportsSurfaceFormatAsRenderTargetEXT(floating))
        GTEST_SKIP() << "Renderer cannot construct this exact mixed target pair";
    RenderTarget2D normalized(device, 8, 8, false, format, DepthFormat::None);
    RenderTarget2D hdr(device, 8, 8, false, floating, DepthFormat::None);
    ASSERT_EQ(normalized.getFormatProperty(), format);
    ASSERT_EQ(hdr.getFormatProperty(), floating);
    Effect effect(device, CNA::TestSupport::BuildSyntheticMrtDrawableEffect());
    effect.getParametersProperty()["Transform"]->SetValue(Matrix::getIdentityProperty());
    effect.getParametersProperty()["Tint"]->SetValue(Vector4(0.75f, 0.5f, 0.25f, 2.0f));
    const VertexDeclaration declaration(3 * sizeof(float), {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0)});
    const float quad[6][3] = {{-1,1,0},{-1,-1,0},{1,-1,0},
                              {-1,1,0},{1,-1,0},{1,1,0}};
    device.SetRenderTargets({RenderTargetBinding(normalizedFirst ? &normalized : &hdr),
                             RenderTargetBinding(normalizedFirst ? &hdr : &normalized)});
    device.Clear(0.25f, 0.25f, 0.25f, 0.25f);
    effect.getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
    device.setBlendStateProperty(BlendState::AlphaBlend);
    device.setDepthStencilStateProperty(DepthStencilState::None);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.DrawUserPrimitives(PrimitiveType::TriangleList, static_cast<const void*>(quad),
                              0, 2, declaration);
    device.SetRenderTarget(nullptr);
    // Fixture oC1 swizzles .yxzw; normalized alpha2 becomes1 before blending.
    const Vector4 expected = normalizedFirst ? Vector4(0.75f,0.5f,0.25f,1)
                                             : Vector4(0.5f,0.75f,0.25f,1);
    Vector4 got;
    float step;
    switch (format)
    {
        case SurfaceFormat::Color:
            got = Read<Color>(normalized).ToVector4(); step = 1.0f / 255; break;
        case SurfaceFormat::Rgba64:
            got = Read<PackedVector::Rgba64>(normalized).ToVector4();
            step = 1.0f / 65535; break;
        case SurfaceFormat::Rg32:
            got = Read<PackedVector::Rg32>(normalized).ToVector4();
            step = 1.0f / 65535; break;
        case SurfaceFormat::Rgba1010102:
            got = Read<PackedVector::Rgba1010102>(normalized).ToVector4();
            step = 1.0f / 1023; break;
        default: FAIL() << "Unexpected normalized format";
    }
    // A single stored UNORM step covers rounding, not a relaxed blend oracle.
    EXPECT_NEAR(got.X, expected.X, step);
    EXPECT_NEAR(got.Y, expected.Y, step);
    if (format != SurfaceFormat::Rg32)
    {
        EXPECT_NEAR(got.Z, expected.Z, step);
        EXPECT_FLOAT_EQ(got.W, 1.0f);
    }
    // Float target retains alpha2 and a negative destination factor. These are exact.
    const Vector4 expectedFloat = normalizedFirst ? Vector4(0.25f,0.5f,0,1.75f)
                                                  : Vector4(0.5f,0.25f,0,1.75f);
    if (floating == SurfaceFormat::Color)
    {
        const auto value = Read<Color>(hdr).ToVector4();
        const Vector4 other = normalizedFirst ? Vector4(0.5f,0.75f,0.25f,1)
                                              : Vector4(0.75f,0.5f,0.25f,1);
        EXPECT_NEAR(value.X, other.X, 1.0f / 255);
        EXPECT_NEAR(value.Y, other.Y, 1.0f / 255);
        EXPECT_NEAR(value.Z, other.Z, 1.0f / 255);
        EXPECT_FLOAT_EQ(value.W, other.W);
    }
    else
    {
        const auto value = Read<PackedVector::HalfVector4>(hdr).ToVector4();
        EXPECT_FLOAT_EQ(value.X, expectedFloat.X);
        EXPECT_FLOAT_EQ(value.Y, expectedFloat.Y);
        EXPECT_FLOAT_EQ(value.Z, expectedFloat.Z);
        EXPECT_FLOAT_EQ(value.W, expectedFloat.W);
    }
}

INSTANTIATE_TEST_SUITE_P(MixedTargets, BlendCompiledSourceRangeTest, testing::Values(
    TargetPair{SurfaceFormat::Color,true}, TargetPair{SurfaceFormat::Color,false},
    TargetPair{SurfaceFormat::Rgba64,true}, TargetPair{SurfaceFormat::Rgba64,false},
    TargetPair{SurfaceFormat::Rg32,true}, TargetPair{SurfaceFormat::Rg32,false},
    TargetPair{SurfaceFormat::Rgba1010102,true}, TargetPair{SurfaceFormat::Rgba1010102,false}));
}
