// SPDX-License-Identifier: MS-PL
#include <array>
#include <memory>
#include <typeinfo>
#include <gtest/gtest.h>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "System/NotSupportedException.hpp"

using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Testing::Renderers;

namespace
{
    struct FormatCase
    {
        SurfaceFormat format;
        bool reach2D;
        bool cube;
        bool volume;
        bool vertexTexture;
        bool softwareTarget;
    };
    // Oracle: XNA ProfileCapabilities::.cctor IL_00d4-0193, IL_030e-0605.
    // docs/software-renderer.md, Targets: SOFTWARE has no packed 16-bit or signed-normalized targets (documented
    // renderer boundary); XNA's preferred-format negotiation substitutes Color there.
    constexpr FormatCase formats[] = {
        {SurfaceFormat::Color,           true,  true,  true,  false, true},
        {SurfaceFormat::Bgr565,          true,  true,  true,  false, false},
        {SurfaceFormat::Bgra5551,        true,  true,  true,  false, false},
        {SurfaceFormat::Bgra4444,        true,  true,  true,  false, false},
        {SurfaceFormat::Dxt1,            true,  true,  false, false, false},
        {SurfaceFormat::Dxt3,            true,  true,  false, false, false},
        {SurfaceFormat::Dxt5,            true,  true,  false, false, false},
        {SurfaceFormat::NormalizedByte2, true,  false, false, false, false},
        {SurfaceFormat::NormalizedByte4, true,  false, false, false, false},
        {SurfaceFormat::Rgba1010102,     false, true,  true,  false, true},
        {SurfaceFormat::Rg32,            false, true,  true,  false, true},
        {SurfaceFormat::Rgba64,          false, true,  true,  false, true},
        {SurfaceFormat::Alpha8,          false, true,  true,  false, true},
        {SurfaceFormat::Single,          false, true,  true,  true,  true},
        {SurfaceFormat::Vector2,         false, true,  true,  true,  true},
        {SurfaceFormat::Vector4,         false, true,  true,  true,  true},
        {SurfaceFormat::HalfSingle,      false, true,  true,  true,  true},
        {SurfaceFormat::HalfVector2,     false, true,  true,  true,  true},
        {SurfaceFormat::HalfVector4,     false, true,  true,  true,  true},
        {SurfaceFormat::HdrBlendable,    false, true,  true,  true,  true},
    };

    class GraphicsProfileFormatConstructionTest : public testing::TestWithParam<GraphicsProfile>
    {
    protected:
        void SetUp() override
        {
            CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software);
            device = std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
                GetParam(), PresentationParameters::CreateWindowedEXT(8, 8));
        }
        bool Reach() const { return GetParam() == GraphicsProfile::Reach; }
        std::unique_ptr<GraphicsDevice> device;
    };
    INSTANTIATE_TEST_SUITE_P(Xna, GraphicsProfileFormatConstructionTest,
        testing::Values(GraphicsProfile::Reach, GraphicsProfile::HiDef),
        [](const testing::TestParamInfo<GraphicsProfile>& info) {
            return info.param == GraphicsProfile::Reach ? "Reach" : "HiDef";
        });
}

TEST_P(GraphicsProfileFormatConstructionTest, Texture2DConstructorsEnforceTheEntireProfileTable)
{
    // Unlike the existing predicate tests, these requests pass through the public constructor.
    // Texture2D.ValidateCreationParameters IL_0034-005d refuses before native allocation.
    for (const auto& entry : formats)
    {
        SCOPED_TRACE(static_cast<int>(entry.format));
        if (Reach() && !entry.reach2D)
            EXPECT_THROW((void)Texture2D(*device, 4, 4, false, entry.format), System::NotSupportedException);
        else
        {
            Texture2D texture(*device, 4, 4, false, entry.format);
            EXPECT_EQ(texture.getFormatProperty(), entry.format);
            EXPECT_EQ(texture.getWidthProperty(), 4);
            EXPECT_EQ(texture.getHeightProperty(), 4);
        }
    }
}

TEST_P(GraphicsProfileFormatConstructionTest, CubeConstructorsEnforceResourceKindFormats)
{
    // XNA TextureCube.ValidateCreationParameters IL_0020-0049. MOD-107 intentionally
    // allows the eleven HiDef formats on Reach cubes; preserve the owner-approved exception.
    for (const auto& entry : formats)
    {
        SCOPED_TRACE(static_cast<int>(entry.format));
        if (!entry.cube)
            EXPECT_THROW((void)TextureCube(*device, 4, false, entry.format), System::NotSupportedException);
        else
        {
            TextureCube texture(*device, 4, false, entry.format);
            EXPECT_EQ(texture.getFormatProperty(), entry.format);
            EXPECT_EQ(texture.getSizeProperty(), 4);
        }
    }
}

TEST_P(GraphicsProfileFormatConstructionTest, VolumeConstructorsEnforceTheEntireProfileTable)
{
    // XNA Texture3D.CreateTexture IL_0057-00a4; .cctor IL_04e8-05aa.
    for (const auto& entry : formats)
    {
        SCOPED_TRACE(static_cast<int>(entry.format));
        if (Reach() || !entry.volume)
            EXPECT_THROW((void)Texture3D(*device, 2, 2, 2, false, entry.format), System::NotSupportedException);
        else
        {
            Texture3D texture(*device, 2, 2, 2, false, entry.format);
            EXPECT_EQ(texture.getFormatProperty(), entry.format);
            EXPECT_EQ(texture.getDepthProperty(), 2);
        }
    }
}

TEST_P(GraphicsProfileFormatConstructionTest, TargetConstructorsNegotiateProfileAndRendererFormats)
{
    // XNA QueryFormat + RenderTarget2D.CreateRenderTarget IL_0019-0043: invalid preferred
    // formats are substituted, rather than rejecting construction. Cube uses the same query.
    for (const auto& entry : formats)
    {
        SCOPED_TRACE(static_cast<int>(entry.format));
        const auto expected = entry.softwareTarget && (!Reach() || entry.reach2D)
                            ? entry.format : SurfaceFormat::Color;
        SurfaceFormat selected = SurfaceFormat::HdrBlendable;
        DepthFormat depth = DepthFormat::Depth24Stencil8;
        int samples = -1;
        const bool exact = GraphicsAdapter::getDefaultAdapterProperty().QueryRenderTargetFormat(
            GetParam(), entry.format, DepthFormat::None, 0, selected, depth, samples);
        EXPECT_EQ(selected, expected);
        EXPECT_EQ(exact, expected == entry.format);
        EXPECT_EQ(depth, DepthFormat::None);
        EXPECT_EQ(samples, 0);
        RenderTarget2D target(*device, 4, 4, false, entry.format, DepthFormat::None);
        RenderTargetCube cube(*device, 4, false, entry.format, DepthFormat::None);
        EXPECT_EQ(target.getFormatProperty(), expected);
        EXPECT_EQ(cube.getFormatProperty(), expected);
    }
}

TEST_P(GraphicsProfileFormatConstructionTest, VertexFormatTableIsAppliedToStaticAndDynamicBuffers)
{
    // XNA .cctor IL_01b8-0230/06d3-0765 and VertexElementValidator.Validate(profile)
    // IL_004e-008d: Reach accepts ordinals 0..9, HiDef 0..11.
    constexpr std::array sizes{4, 8, 12, 16, 4, 4, 4, 8, 4, 8, 4, 8};
    for (int ordinal = 0; ordinal != 12; ++ordinal)
    {
        SCOPED_TRACE(ordinal);
        const auto format = static_cast<VertexElementFormat>(ordinal);
        VertexDeclaration declaration(sizes[ordinal], {
            VertexElement(0, format, VertexElementUsage::Position, 0)});
        if (Reach() && ordinal >= 10)
        {
            EXPECT_THROW((void)VertexBuffer(*device, declaration, 1, BufferUsage::None), System::NotSupportedException);
            EXPECT_THROW((void)DynamicVertexBuffer(*device, declaration, 1, BufferUsage::None), System::NotSupportedException);
        }
        else
        {
            VertexBuffer buffer(*device, declaration, 1, BufferUsage::None);
            DynamicVertexBuffer dynamic(*device, declaration, 1, BufferUsage::None);
            EXPECT_EQ(buffer.getVertexDeclarationProperty().GetVertexElements()[0].getVertexElementFormatProperty(), format);
            EXPECT_EQ(dynamic.getVertexDeclarationProperty().getVertexStrideProperty(), sizes[ordinal]);
        }
    }
}

TEST_P(GraphicsProfileFormatConstructionTest, HiDefVertexTextureFormatsRefuseWithoutReplacingTheSlot)
{
    // XNA TextureCollection.set_Item IL_0051-0081 checks ValidVertexTextureFormats at
    // binding, before storing. Reach has zero vertex texture slots (tested in boundary suite).
    if (Reach()) return;
    Texture2D sentinel(*device, 4, 4, false, SurfaceFormat::Single);
    auto& textures = device->getVertexTexturesProperty();
    for (const auto& entry : formats)
    {
        SCOPED_TRACE(static_cast<int>(entry.format));
        Texture2D texture(*device, 4, 4, false, entry.format);
        textures(3, &sentinel);
        if (entry.vertexTexture)
        {
            textures(3, &texture);
            EXPECT_EQ(textures[3], &texture);
        }
        else
        {
            constexpr const char* names[] = {
                "Color", "Bgr565", "Bgra5551", "Bgra4444", "Dxt1", "Dxt3", "Dxt5",
                "NormalizedByte2", "NormalizedByte4", "Rgba1010102", "Rg32", "Rgba64", "Alpha8"};
            try { textures(3, &texture); ADD_FAILURE() << "Invalid vertex texture accepted"; }
            catch (const System::NotSupportedException& error)
            {
                EXPECT_EQ(error.getMessageProperty(), std::string("XNA Framework HiDef profile does not support vertex texture format ") +
                    names[static_cast<int>(entry.format)] + ".");
            }
            EXPECT_EQ(textures[3], &sentinel);
        }
    }
}
