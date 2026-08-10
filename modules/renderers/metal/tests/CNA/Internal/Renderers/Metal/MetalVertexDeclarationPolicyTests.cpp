// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Metal/MetalVertexDeclarationPolicy.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"
#include "System/NotSupportedException.hpp"

using CNA::Internal::Renderers::Metal::RequireFaithfulDeclarationEXT;
using CNA::Internal::Graphics::DeclaredVertexLayout;
using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
using Microsoft::Xna::Framework::Graphics::VertexElement;
using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
using Microsoft::Xna::Framework::Graphics::VertexElementUsage;

namespace
{
    void Require(const VertexDeclaration& declaration, int uploadedStride,
                 const char* route = "ordinary-nonindexed")
    {
        DeclaredVertexLayout remembered;
        remembered.Remember(declaration);
        RequireFaithfulDeclarationEXT(remembered, uploadedStride, route);
    }

    VertexElement Element(int offset, VertexElementFormat format,
                          VertexElementUsage usage, int usageIndex = 0)
    {
        return VertexElement(offset, format, usage, usageIndex);
    }
}

TEST(MetalVertexDeclarationPolicy, AcceptsEveryCanonicalMetalLayout)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    const VertexDeclaration* declarations[] = {
        &VertexPositionColor::getVertexDeclarationStatic(),
        &VertexPositionTexture::getVertexDeclarationStatic(),
        &VertexPositionColorTexture::getVertexDeclarationStatic(),
        &VertexPositionNormalTexture::getVertexDeclarationStatic(),
        &VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
        &VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(),
        &VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(),
    };
    for (const VertexDeclaration* declaration : declarations)
    {
        EXPECT_NO_THROW(Require(*declaration, declaration->getVertexStrideProperty()));
        EXPECT_NO_THROW(Require(*declaration, declaration->getVertexStrideProperty(),
                                "ordinary-indexed"));
    }
}

TEST(MetalVertexDeclarationPolicy, RejectsSameStrideWithWrongSemantic)
{
    const VertexDeclaration declaration(16, {
        Element(0, VertexElementFormat::Color, VertexElementUsage::Color),
        Element(4, VertexElementFormat::Vector3, VertexElementUsage::Position),
    });
    EXPECT_THROW(Require(declaration, 16), System::NotSupportedException);
}

TEST(MetalVertexDeclarationPolicy, RejectsSameStrideWithWrongOffset)
{
    const VertexDeclaration declaration(24, {
        Element(0, VertexElementFormat::Vector3, VertexElementUsage::Position),
        Element(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate),
        Element(20, VertexElementFormat::Color, VertexElementUsage::Color),
    });
    EXPECT_THROW(Require(declaration, 24), System::NotSupportedException);
}

TEST(MetalVertexDeclarationPolicy, RejectsSameStrideWithWrongFormat)
{
    const VertexDeclaration declaration(16, {
        Element(0, VertexElementFormat::Vector3, VertexElementUsage::Position),
        Element(12, VertexElementFormat::Byte4, VertexElementUsage::Color),
    });
    EXPECT_THROW(Require(declaration, 16), System::NotSupportedException);
}

TEST(MetalVertexDeclarationPolicy, RejectsDuplicateSemanticOwnership)
{
    const VertexDeclaration declaration(24, {
        Element(0, VertexElementFormat::Vector3, VertexElementUsage::Position),
        Element(12, VertexElementFormat::Color, VertexElementUsage::Color),
        Element(16, VertexElementFormat::Color, VertexElementUsage::Color),
    });
    EXPECT_THROW(Require(declaration, 24), System::NotSupportedException);
}

TEST(MetalVertexDeclarationPolicy, RejectsDeclarationStrideDifferentFromUpload)
{
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;
    EXPECT_THROW(Require(VertexPositionColor::getVertexDeclarationStatic(), 24),
                 System::NotSupportedException);
}
