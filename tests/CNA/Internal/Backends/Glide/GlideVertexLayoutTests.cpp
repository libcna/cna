#include <gtest/gtest.h>

#include "CNA/Internal/Backends/Glide/GlideVertexLayout.hpp"

using CNA::Internal::Backends::Glide::KnownGlideVertexLayout;
using CNA::Internal::Backends::Glide::ParseGlideVertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

TEST(GlideVertexLayoutTest, DecodesCompatibleAttributesAtArbitraryOffsets)
{
    const VertexDeclaration declaration(48, {
        VertexElement(16, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(0, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        VertexElement(28, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
        VertexElement(40, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
    });

    const auto layout = ParseGlideVertexDeclaration(declaration);

    EXPECT_EQ(layout.stride, 48u);
    EXPECT_EQ(layout.positionOffset, 16);
    EXPECT_EQ(layout.colorOffset, 0);
    EXPECT_EQ(layout.normalOffset, 28);
    EXPECT_EQ(layout.textureCoordinate0Offset, 40);
}

TEST(GlideVertexLayoutTest, KeepsKnownPackedStreamsWorkingWithoutDeclaration)
{
    const auto layout = KnownGlideVertexLayout(24);

    EXPECT_EQ(layout.positionOffset, 0);
    EXPECT_EQ(layout.colorOffset, 12);
    EXPECT_EQ(layout.textureCoordinate0Offset, 16);
    EXPECT_FALSE(layout.HasNormal());
}

TEST(GlideVertexLayoutTest, RejectsUnsupportedOrAmbiguousSemantics)
{
    const VertexDeclaration textureCoordinate1(20, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
    });
    const VertexDeclaration tangent(24, {
        VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
        VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
    });
    const VertexDeclaration wrongPositionFormat(16, {
        VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::Position, 0),
    });

    EXPECT_THROW(static_cast<void>(ParseGlideVertexDeclaration(textureCoordinate1)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(ParseGlideVertexDeclaration(tangent)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(ParseGlideVertexDeclaration(wrongPositionFormat)), std::runtime_error);
}
